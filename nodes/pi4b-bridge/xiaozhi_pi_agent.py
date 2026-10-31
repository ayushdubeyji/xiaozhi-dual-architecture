#!/usr/bin/env python3
"""Low-Memory Xiaozhi Voice Agent Node A (xiaozhi-pi) on Raspberry Pi 4B."""

import os
import sys
import math
import time
import json
import struct
import socket
import asyncio
import logging
from typing import Dict, Any, List, Optional
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException, Request
from fastapi.responses import JSONResponse
import uvicorn

# Constants
NODE_ID = "xiaozhi-pi"
PORT = 8765
SAMPLE_RATE = 16000
AUDIO_CHANNELS = 1
BYTES_PER_SAMPLE = 2
FRAME_DURATION_MS = 60
SAMPLES_PER_FRAME = (SAMPLE_RATE * FRAME_DURATION_MS) // 1000  # 960 samples
BYTES_PER_FRAME = SAMPLES_PER_FRAME * BYTES_PER_SAMPLE * AUDIO_CHANNELS  # 1920 bytes
MAX_BUFFER_FRAMES = 250  # ~15 seconds max audio buffer (under 500 KB RAM)
SILENCE_ENERGY_THRESHOLD = 500.0  # RMS threshold
SILENCE_TIMEOUT_FRAMES = 25  # ~1.5s silence to trigger auto-ASR after speech

ROVER_UDP_PORT = 8085
ROVER_PKT_MAGIC = 0xA7
ROVER_PKT_STRUCT = "<BBbbbBBBhH"  # 12 bytes (magic, type, x, y, z, speed, mode, flags, arg, seq)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [xiaozhi_pi] %(message)s"
)
logger = logging.getLogger("xiaozhi_pi_agent")

# Path discovery for shared providers and federation
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROVIDERS_DIR = os.path.join(REPO_ROOT, "shared", "providers")
FEDERATION_DIR = os.path.join(REPO_ROOT, "federation", "workbench-bridge")

for p in [PROVIDERS_DIR, FEDERATION_DIR]:
    if os.path.exists(p) and p not in sys.path:
        sys.path.insert(0, p)


# Lazy provider loader
class ProviderRegistry:
    def __init__(self):
        self.asr_router = None
        self.llm_router = None
        self.federation_client = None
        self._init_providers()

    def _init_providers(self):
        try:
            from asr_whisper_router import WhisperASRRouter
            self.asr_router = WhisperASRRouter()
            logger.info("Loaded WhisperASRRouter successfully")
        except Exception as e:
            try:
                import asr_whisper_router
                self.asr_router = asr_whisper_router
                logger.info("Loaded asr_whisper_router module fallback")
            except Exception as e2:
                logger.warning(f"asr_whisper_router fallback stub: {e2}")

        try:
            from llm_router import LLMRouter
            self.llm_router = LLMRouter()
            logger.info("Loaded LLMRouter successfully")
        except Exception as e:
            try:
                import llm_router
                self.llm_router = llm_router
                logger.info("Loaded llm_router module fallback")
            except Exception as e2:
                logger.warning(f"llm_router fallback stub: {e2}")

        try:
            from xiaozhi_federation_mcp import XiaozhiFederationClient
            peer_host = os.environ.get("XIAOZHI_WORKBENCH_HOST", "192.168.0.111")
            peer_port = int(os.environ.get("XIAOZHI_WORKBENCH_PORT", "8000"))
            self.federation_client = XiaozhiFederationClient(
                node_id=NODE_ID,
                peer_host=peer_host,
                peer_port=peer_port
            )
            logger.info(f"Initialized Federation Client pointing to {peer_host}:{peer_port}")
        except Exception as e:
            logger.warning(f"xiaozhi_federation_mcp fallback stub: {e}")


providers = ProviderRegistry()


class AgentState:
    IDLE = "IDLE"
    LISTENING = "LISTENING"
    PROCESSING_ASR = "PROCESSING_ASR"
    THINKING_LLM = "THINKING_LLM"
    SPEAKING_TTS = "SPEAKING_TTS"


class XiaozhiSessionManager:
    """Session state and audio buffer manager."""

    def __init__(self):
        self.state = AgentState.IDLE
        self.audio_frames: List[bytes] = []
        self.is_ptt = False
        self.has_voiced = False
        self.silent_frame_count = 0
        self.lock = asyncio.Lock()
        self.active_websockets: List[WebSocket] = []
        self.processed_utterances = 0
        self.rover_commands_count = 0
        self.rover_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def get_memory_rss_mb(self) -> float:
        """Lazy import of psutil to conserve memory and startup time."""
        try:
            import psutil
            process = psutil.Process()
            return round(process.memory_info().rss / (1024 * 1024), 2)
        except Exception:
            return 0.0

    async def broadcast_event(self, event_data: dict):
        dead_ws = []
        for ws in self.active_websockets:
            try:
                await ws.send_json(event_data)
            except Exception:
                dead_ws.append(ws)
        for ws in dead_ws:
            if ws in self.active_websockets:
                self.active_websockets.remove(ws)

    def compute_frame_rms(self, frame_bytes: bytes) -> float:
        if len(frame_bytes) < 2:
            return 0.0
        num_samples = len(frame_bytes) // 2
        fmt = f"<{num_samples}h"
        samples = struct.unpack(fmt, frame_bytes[:num_samples * 2])
        sum_sq = sum(float(s * s) for s in samples)
        return math.sqrt(sum_sq / num_samples)

    def dispatch_rover_packet(self, pkt_type: int, vx: int, vy: int, vz: int, speed: int = 50, mode: int = 0, flags: int = 0):
        """Constructs 12-byte packed RoverPkt with magic 0xA7 and sends via UDP to 8085."""
        seq = (self.rover_commands_count + 1) & 0xFFFF
        pkt = struct.pack(
            ROVER_PKT_STRUCT,
            ROVER_PKT_MAGIC,
            pkt_type,
            int(vx),
            int(vy),
            int(vz),
            int(speed),
            int(mode),
            int(flags),
            0,
            seq
        )
        try:
            self.rover_sock.sendto(pkt, ("127.0.0.1", ROVER_UDP_PORT))
            self.rover_commands_count += 1
            logger.info(f"Dispatched RoverPkt: type={pkt_type} vx={vx} vy={vy} vz={vz} seq={seq}")
        except Exception as e:
            logger.warning(f"Error dispatching rover packet: {e}")

    def handle_rover_intent(self, transcript: str) -> Optional[str]:
        t = transcript.lower().strip()
        if "emergency stop" in t or "e-stop" in t:
            self.dispatch_rover_packet(pkt_type=4, vx=0, vy=0, vz=0, speed=0, flags=0x80)
            return "Emergency stop engaged on Mecanum Rover."
        if "stop" in t or "halt" in t:
            self.dispatch_rover_packet(pkt_type=4, vx=0, vy=0, vz=0, speed=0)
            return "Rover stopped."
        if "forward" in t or "ahead" in t:
            self.dispatch_rover_packet(pkt_type=3, vx=0, vy=60, vz=0, speed=60)
            return "Rover moving forward."
        if "backward" in t or "reverse" in t:
            self.dispatch_rover_packet(pkt_type=3, vx=0, vy=-60, vz=0, speed=60)
            return "Rover moving backward."
        if "strafe left" in t or "left" in t and "strafe" in t:
            self.dispatch_rover_packet(pkt_type=3, vx=-60, vy=0, vz=0, speed=60)
            return "Rover strafing left."
        if "strafe right" in t or "right" in t and "strafe" in t:
            self.dispatch_rover_packet(pkt_type=3, vx=60, vy=0, vz=0, speed=60)
            return "Rover strafing right."
        if "rotate left" in t or "turn left" in t:
            self.dispatch_rover_packet(pkt_type=3, vx=0, vy=0, vz=-60, speed=60)
            return "Rover turning left."
        if "rotate right" in t or "turn right" in t:
            self.dispatch_rover_packet(pkt_type=3, vx=0, vy=0, vz=60, speed=60)
            return "Rover turning right."
        if "crawl mode" in t:
            self.dispatch_rover_packet(pkt_type=5, vx=0, vy=0, vz=0, speed=30, mode=1)
            return "Rover switched to Crawl mode."
        return None

    async def start_listening(self, is_ptt: bool = False):
        async with self.lock:
            self.state = AgentState.LISTENING
            self.is_ptt = is_ptt
            self.has_voiced = False
            self.silent_frame_count = 0
            self.audio_frames.clear()
            logger.info(f"Agent state changed to LISTENING (is_ptt={is_ptt})")
        await self.broadcast_event({"event": "state_change", "state": AgentState.LISTENING, "is_ptt": is_ptt})

    async def ingest_audio_frame(self, frame_bytes: bytes):
        if self.state != AgentState.LISTENING:
            return

        rms = self.compute_frame_rms(frame_bytes)
        async with self.lock:
            if len(self.audio_frames) < MAX_BUFFER_FRAMES:
                self.audio_frames.append(frame_bytes)

            if rms > SILENCE_ENERGY_THRESHOLD:
                self.has_voiced = True
                self.silent_frame_count = 0
            elif self.has_voiced:
                self.silent_frame_count += 1

            should_trigger_asr = (
                not self.is_ptt
                and self.has_voiced
                and (self.silent_frame_count >= SILENCE_TIMEOUT_FRAMES)
            )

        if should_trigger_asr:
            logger.info("VAD detected end of speech; triggering processing pipeline")
            asyncio.create_task(self.process_audio_pipeline())

    async def stop_listening_and_process(self):
        async with self.lock:
            if self.state != AgentState.LISTENING:
                return
            self.state = AgentState.PROCESSING_ASR
        logger.info("PTT released; processing speech buffer")
        await self.broadcast_event({"event": "state_change", "state": AgentState.PROCESSING_ASR})
        await self.process_audio_pipeline()

    async def process_audio_pipeline(self):
        async with self.lock:
            self.state = AgentState.PROCESSING_ASR
            raw_audio = b"".join(self.audio_frames)
            self.audio_frames.clear()  # Immediately free RAM

        if len(raw_audio) < BYTES_PER_FRAME * 3:  # under 180ms
            logger.debug("Audio buffer too short, resetting to IDLE")
            self.state = AgentState.IDLE
            await self.broadcast_event({"event": "state_change", "state": AgentState.IDLE})
            return

        # Step 1: Speech to Text (ASR)
        transcript = ""
        try:
            if providers.asr_router:
                if hasattr(providers.asr_router, "transcribe"):
                    res = await providers.asr_router.transcribe(audio_data=raw_audio)
                    transcript = getattr(res, "text", "") or ""
                elif hasattr(providers.asr_router, "transcribe_audio_pcm"):
                    transcript = await providers.asr_router.transcribe_audio_pcm(
                        pcm_bytes=raw_audio,
                        sample_rate=SAMPLE_RATE
                    )
                else:
                    transcript = f"Mock transcription ({len(raw_audio)} bytes)"
            else:
                transcript = f"Mock transcription ({len(raw_audio)} bytes)"
        except Exception as e:
            logger.error(f"ASR transcription error: {e}")
            transcript = ""

        if not transcript.strip():
            self.state = AgentState.IDLE
            await self.broadcast_event({"event": "state_change", "state": AgentState.IDLE})
            return

        logger.info(f"User Utterance: '{transcript}'")
        await self.broadcast_event({"event": "transcript", "text": transcript})

        # Step 2a: Check brain switch voice command (e.g. "switch to gemini", "use groq")
        if providers.llm_router and hasattr(providers.llm_router, "check_and_execute_brain_switch"):
            switch_msg = providers.llm_router.check_and_execute_brain_switch(transcript)
            if switch_msg:
                self.state = AgentState.SPEAKING_TTS
                await self.broadcast_event({"event": "llm_response", "text": switch_msg})
                await self.play_tts_feedback(switch_msg)
                self.state = AgentState.IDLE
                await self.broadcast_event({"event": "state_change", "state": AgentState.IDLE})
                return

        # Step 2b: Intent check (Local Rover commands)
        rover_res = self.handle_rover_intent(transcript)
        if rover_res:
            self.state = AgentState.SPEAKING_TTS
            await self.broadcast_event({"event": "llm_response", "text": rover_res})
            await self.play_tts_feedback(rover_res)
            self.state = AgentState.IDLE
            await self.broadcast_event({"event": "state_change", "state": AgentState.IDLE})
            return

        # Step 3: LLM Reasoning / Workbench Delegation
        self.state = AgentState.THINKING_LLM
        await self.broadcast_event({"event": "state_change", "state": AgentState.THINKING_LLM})

        answer = ""
        try:
            # Check if peer consultation requested or workbench is preferred
            use_workbench = ("workbench" in transcript.lower()) or ("hub" in transcript.lower())
            if use_workbench and providers.federation_client and providers.federation_client.is_connected:
                peer_res = await providers.federation_client.ask_peer_xiaozhi(
                    target_node="xiaozhi-workbench",
                    query=transcript,
                    context={"source": "xiaozhi-pi-voice"}
                )
                answer = peer_res.get("text", "")

            if not answer:
                if providers.llm_router:
                    if hasattr(providers.llm_router, "chat"):
                        res = await providers.llm_router.chat(prompt=transcript)
                        answer = getattr(res, "content", "") or ""
                    elif hasattr(providers.llm_router, "generate_text"):
                        answer = await providers.llm_router.generate_text(prompt=transcript)
                    elif hasattr(providers.llm_router, "query"):
                        answer = await providers.llm_router.query(transcript)
                if not answer:
                    answer = f"I heard: {transcript}. Ready on Pi 4B."
        except Exception as e:
            logger.error(f"LLM generation error: {e}")
            answer = "Sorry, I had trouble processing that request."

        # Step 4: TTS & Sync
        self.state = AgentState.SPEAKING_TTS
        await self.broadcast_event({"event": "llm_response", "text": answer})
        await self.play_tts_feedback(answer)

        # Sync memory if federation client available
        if providers.federation_client and providers.federation_client.is_connected:
            try:
                session_id = f"pi_session_{int(time.time())}"
                await providers.federation_client.sync_shared_memory(
                    session_id=session_id,
                    summary=f"User: {transcript} | Pi: {answer}",
                    tags=["voice_turn", "node_a"]
                )
            except Exception as ex:
                logger.debug(f"Federation sync skipped: {ex}")

        self.processed_utterances += 1
        self.state = AgentState.IDLE
        await self.broadcast_event({"event": "state_change", "state": AgentState.IDLE})

    async def play_tts_feedback(self, text: str):
        logger.info(f"Speaking: '{text}'")
        # Non-blocking aplay or pipewire audio feedback if sound system available
        try:
            # Short notification or espeak-ng / pico2wave if installed
            proc = await asyncio.create_subprocess_exec(
                "espeak-ng", "-v", "en-us", "-s", "160", text,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL
            )
            await asyncio.wait_for(proc.wait(), timeout=5.0)
        except Exception:
            pass


session_mgr = XiaozhiSessionManager()


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Connect federation client in background task
    federation_task = None
    if providers.federation_client:
        async def _connect_federation():
            try:
                await providers.federation_client.connect(timeout=4.0)
            except Exception as e:
                logger.debug(f"Federation background connect pending: {e}")
        federation_task = asyncio.create_task(_connect_federation())

    logger.info(f"Xiaozhi Pi 4B Agent running on port {PORT} (PID {os.getpid()})")
    yield

    if federation_task and not federation_task.done():
        federation_task.cancel()
    if providers.federation_client:
        await providers.federation_client.close()
    if providers.asr_router and hasattr(providers.asr_router, "close"):
        try:
            await providers.asr_router.close()
        except Exception:
            pass
    if providers.llm_router and hasattr(providers.llm_router, "close"):
        try:
            await providers.llm_router.close()
        except Exception:
            pass
    logger.info("Xiaozhi Pi 4B Agent shut down cleanly.")


app = FastAPI(title="Xiaozhi Pi 4B Voice Agent", lifespan=lifespan)


@app.get("/health")
@app.get("/api/status")
async def get_status():
    mem_mb = session_mgr.get_memory_rss_mb()
    fed_status = bool(providers.federation_client and providers.federation_client.is_connected)
    return {
        "status": "healthy",
        "node_id": NODE_ID,
        "state": session_mgr.state,
        "memory_rss_mb": mem_mb,
        "memory_guard_limit_mb": 450,
        "is_under_budget": (mem_mb < 120.0),
        "buffered_frames": len(session_mgr.audio_frames),
        "federation_connected": fed_status,
        "processed_utterances": session_mgr.processed_utterances,
        "rover_commands_count": session_mgr.rover_commands_count
    }


@app.post("/api/trigger")
async def trigger_event(payload: Dict[str, Any]):
    event = payload.get("event")
    if event == "wake_word":
        await session_mgr.start_listening(is_ptt=False)
        return {"status": "ok", "state": session_mgr.state}
    elif event == "ptt_start":
        await session_mgr.start_listening(is_ptt=True)
        return {"status": "ok", "state": session_mgr.state}
    elif event == "ptt_stop":
        asyncio.create_task(session_mgr.stop_listening_and_process())
        return {"status": "ok", "state": session_mgr.state}
    raise HTTPException(status_code=400, detail=f"Unknown trigger event: {event}")


@app.post("/api/rover/cmd")
async def post_rover_cmd(payload: Dict[str, Any]):
    action = payload.get("action", "stop")
    vx = int(payload.get("vx", 0))
    vy = int(payload.get("vy", 0))
    vz = int(payload.get("vz", 0))
    speed = int(payload.get("speed", 50))
    mode = int(payload.get("mode", 0))

    if action == "stop":
        session_mgr.dispatch_rover_packet(pkt_type=4, vx=0, vy=0, vz=0, speed=0)
    elif action == "move":
        session_mgr.dispatch_rover_packet(pkt_type=3, vx=vx, vy=vy, vz=vz, speed=speed, mode=mode)
    elif action == "estop":
        session_mgr.dispatch_rover_packet(pkt_type=4, vx=0, vy=0, vz=0, speed=0, flags=0x80)
    return {"status": "dispatched", "action": action}


@app.websocket("/ws/xiaozhi")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    session_mgr.active_websockets.append(websocket)
    logger.info(f"WebSocket client connected from {websocket.client}")

    try:
        while True:
            message = await websocket.receive()
            if "bytes" in message and message["bytes"]:
                # Ingest raw 16kHz 16-bit mono audio frame
                await session_mgr.ingest_audio_frame(message["bytes"])
            elif "text" in message and message["text"]:
                try:
                    data = json.loads(message["text"])
                    event_type = data.get("event") or data.get("type")

                    if event_type == "wake_word" or event_type == "wake":
                        await session_mgr.start_listening(is_ptt=False)
                    elif event_type == "ptt_start":
                        await session_mgr.start_listening(is_ptt=True)
                    elif event_type == "ptt_stop":
                        asyncio.create_task(session_mgr.stop_listening_and_process())
                    elif event_type == "text_query":
                        text = data.get("text", "")
                        if text:
                            asyncio.create_task(session_mgr.broadcast_event({"event": "transcript", "text": text}))
                            # Check brain switch
                            if providers.llm_router and hasattr(providers.llm_router, "check_and_execute_brain_switch"):
                                switch_msg = providers.llm_router.check_and_execute_brain_switch(text)
                                if switch_msg:
                                    await session_mgr.broadcast_event({"event": "llm_response", "text": switch_msg})
                                    continue
                            # Process as text
                            session_mgr.state = AgentState.THINKING_LLM
                            await session_mgr.broadcast_event({"event": "state_change", "state": AgentState.THINKING_LLM})
                            ans = ""
                            if providers.llm_router:
                                if hasattr(providers.llm_router, "chat"):
                                    res = await providers.llm_router.chat(prompt=text)
                                    ans = getattr(res, "content", "") or ""
                                elif hasattr(providers.llm_router, "generate_text"):
                                    ans = await providers.llm_router.generate_text(prompt=text)
                            if not ans:
                                ans = f"Echo response for: {text}"
                            session_mgr.state = AgentState.IDLE
                            await session_mgr.broadcast_event({"event": "llm_response", "text": ans})
                    elif event_type == "ping":
                        await websocket.send_json({"event": "pong", "time": time.time()})
                except json.JSONDecodeError:
                    pass
    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")
    except Exception as ex:
        logger.warning(f"WebSocket session exception: {ex}")
    finally:
        if websocket in session_mgr.active_websockets:
            session_mgr.active_websockets.remove(websocket)


def run_agent():
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="info")


if __name__ == "__main__":
    run_agent()
