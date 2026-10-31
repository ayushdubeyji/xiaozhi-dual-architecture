"""
Dual-Xiaozhi Inter-Agent Federation Bridge (MCP over WebSocket).
Node A (xiaozhi-pi: 192.168.0.106) <--> Node B (xiaozhi-workbench: 192.168.0.111)
Complies with Model Context Protocol (MCP) JSON-RPC 2.0 schema over WebSocket.
"""

import os
import sys
import json
import time
import uuid
import struct
import base64
import asyncio
import sqlite3
import logging
from typing import Dict, Any, List, Optional, Tuple, Callable, Generator
from contextlib import asynccontextmanager, contextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Query, HTTPException, status
from fastapi.responses import JSONResponse
import uvicorn

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [%(name)s] %(message)s"
)
logger = logging.getLogger("xiaozhi_federation")

# Constants
DEFAULT_PORT = 8000
DEFAULT_WS_PATH = "/ws/xiaozhi-peer"
DEFAULT_DB_PATH = os.path.join(os.path.dirname(__file__), "federation_memory.db")
SAMPLE_RATE_HZ = 16000
AUDIO_CHANNELS = 1
AUDIO_SAMPLE_WIDTH = 2  # 16-bit PCM
FRAME_DURATION_MS = 60
SAMPLES_PER_FRAME = (SAMPLE_RATE_HZ * FRAME_DURATION_MS) // 1000  # 960 samples
BYTES_PER_FRAME = SAMPLES_PER_FRAME * AUDIO_SAMPLE_WIDTH * AUDIO_CHANNELS  # 1920 bytes


class FederationMemoryDB:
    """SQLite persistence for shared conversation memory, telemetry, and dialogue turns."""

    def __init__(self, db_path: str = DEFAULT_DB_PATH):
        self.db_path = db_path
        self._init_db()

    @contextmanager
    def _get_connection(self) -> Generator[sqlite3.Connection, None, None]:
        conn = sqlite3.connect(self.db_path, timeout=10.0)
        conn.row_factory = sqlite3.Row
        try:
            yield conn
        finally:
            conn.close()

    def _init_db(self):
        with self._get_connection() as conn:
            with conn:
                cursor = conn.cursor()
                cursor.execute("""
                    CREATE TABLE IF NOT EXISTS shared_memory (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        session_id TEXT NOT NULL,
                        timestamp REAL NOT NULL,
                        source_node TEXT NOT NULL,
                        summary TEXT NOT NULL,
                        tags TEXT,
                        metadata_json TEXT
                    )
                """)
                cursor.execute("""
                    CREATE TABLE IF NOT EXISTS dialogue_history (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        session_id TEXT NOT NULL,
                        topic TEXT NOT NULL,
                        turn_index INTEGER NOT NULL,
                        speaker_node TEXT NOT NULL,
                        text TEXT NOT NULL,
                        audio_bytes_len INTEGER DEFAULT 0,
                        timestamp REAL NOT NULL
                    )
                """)
                cursor.execute("""
                    CREATE TABLE IF NOT EXISTS telemetry_logs (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        node_id TEXT NOT NULL,
                        cpu_percent REAL,
                        ram_mb REAL,
                        net_status TEXT,
                        timestamp REAL NOT NULL
                    )
                """)
                cursor.execute("CREATE INDEX IF NOT EXISTS idx_mem_session ON shared_memory(session_id)")
                cursor.execute("CREATE INDEX IF NOT EXISTS idx_dlg_session ON dialogue_history(session_id)")

    def store_memory(
        self,
        session_id: str,
        summary: str,
        tags: List[str],
        source_node: str,
        metadata: Optional[Dict[str, Any]] = None
    ) -> int:
        with self._get_connection() as conn:
            with conn:
                cursor = conn.cursor()
                cursor.execute("""
                    INSERT INTO shared_memory (session_id, timestamp, source_node, summary, tags, metadata_json)
                    VALUES (?, ?, ?, ?, ?, ?)
                """, (
                    session_id,
                    time.time(),
                    source_node,
                    summary,
                    json.dumps(tags),
                    json.dumps(metadata or {})
                ))
                return cursor.lastrowid

    def query_memory(self, session_id: Optional[str] = None, limit: int = 10) -> List[Dict[str, Any]]:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            if session_id:
                cursor.execute("""
                    SELECT id, session_id, timestamp, source_node, summary, tags, metadata_json
                    FROM shared_memory
                    WHERE session_id = ?
                    ORDER BY timestamp DESC LIMIT ?
                """, (session_id, limit))
            else:
                cursor.execute("""
                    SELECT id, session_id, timestamp, source_node, summary, tags, metadata_json
                    FROM shared_memory
                    ORDER BY timestamp DESC LIMIT ?
                """, (limit,))
            rows = cursor.fetchall()
            return [
                {
                    "id": r["id"],
                    "session_id": r["session_id"],
                    "timestamp": r["timestamp"],
                    "source_node": r["source_node"],
                    "summary": r["summary"],
                    "tags": json.loads(r["tags"]) if r["tags"] else [],
                    "metadata": json.loads(r["metadata_json"]) if r["metadata_json"] else {}
                }
                for r in rows
            ]

    def record_dialogue_turn(
        self,
        session_id: str,
        topic: str,
        turn_index: int,
        speaker_node: str,
        text: str,
        audio_bytes_len: int = 0
    ) -> int:
        with self._get_connection() as conn:
            with conn:
                cursor = conn.cursor()
                cursor.execute("""
                    INSERT INTO dialogue_history (session_id, topic, turn_index, speaker_node, text, audio_bytes_len, timestamp)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                """, (session_id, topic, turn_index, speaker_node, text, audio_bytes_len, time.time()))
                return cursor.lastrowid

    def get_dialogue_history(self, session_id: str) -> List[Dict[str, Any]]:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT id, session_id, topic, turn_index, speaker_node, text, audio_bytes_len, timestamp
                FROM dialogue_history
                WHERE session_id = ?
                ORDER BY turn_index ASC
            """, (session_id,))
            rows = cursor.fetchall()
            return [dict(r) for r in rows]

    def record_telemetry(self, node_id: str, cpu_percent: float, ram_mb: float, net_status: str = "ok"):
        with self._get_connection() as conn:
            with conn:
                cursor = conn.cursor()
                cursor.execute("""
                    INSERT INTO telemetry_logs (node_id, cpu_percent, ram_mb, net_status, timestamp)
                    VALUES (?, ?, ?, ?, ?)
                """, (node_id, cpu_percent, ram_mb, net_status, time.time()))


def get_system_telemetry(node_id: str) -> Dict[str, Any]:
    """Lazy-imports psutil to capture host memory/CPU metrics without global overhead."""
    cpu_pct = 0.0
    ram_mb = 0.0
    try:
        import psutil  # Lazy import compliant with user rule
        cpu_pct = float(psutil.cpu_percent(interval=None))
        mem = psutil.virtual_memory()
        ram_mb = float(mem.used / (1024 * 1024))
    except Exception as e:
        logger.warning(f"Telemetry psutil read exception: {e}")
    return {
        "node_id": node_id,
        "cpu_percent": cpu_pct,
        "ram_used_mb": round(ram_mb, 2),
        "timestamp": time.time()
    }


def generate_16k_pcm_audio_frame(frequency_hz: float = 440.0, duration_ms: int = FRAME_DURATION_MS) -> bytes:
    """Generates 960-sample (60ms) 16kHz mono 16-bit PCM chunk representing synthesized audio turn."""
    import math
    num_samples = (SAMPLE_RATE_HZ * duration_ms) // 1000
    pcm_bytes = bytearray()
    for i in range(num_samples):
        # 0.25 amplitude sine wave to simulate vocal tone
        sample_val = int(32767.0 * 0.25 * math.sin(2.0 * math.pi * frequency_hz * (i / SAMPLE_RATE_HZ)))
        pcm_bytes.extend(struct.pack("<h", sample_val))
    return bytes(pcm_bytes)


class XiaozhiMCPFederationHub:
    """MCP Protocol engine and connection manager for dual-node peer synchronization."""

    def __init__(self, node_id: str, db_path: str = DEFAULT_DB_PATH):
        self.node_id = node_id
        self.db = FederationMemoryDB(db_path)
        self.active_peers: Dict[str, WebSocket] = {}
        self.pending_rpc_requests: Dict[str, asyncio.Future] = {}

    def get_supported_tools(self) -> List[Dict[str, Any]]:
        return [
            {
                "name": "ask_peer_xiaozhi",
                "description": "Consult peer Xiaozhi node for complex reasoning, query processing, or task delegation",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "target_node": {"type": "string", "description": "xiaozhi-pi or xiaozhi-workbench"},
                        "query": {"type": "string", "description": "The user query or sub-task to delegate"},
                        "context": {"type": "object", "description": "Contextual session data or sensor state"}
                    },
                    "required": ["target_node", "query"]
                }
            },
            {
                "name": "sync_shared_memory",
                "description": "Sync conversation summary, context, and telemetry to the shared SQLite database",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "session_id": {"type": "string", "description": "Unique session ID"},
                        "summary": {"type": "string", "description": "Concise summary of interaction"},
                        "tags": {"type": "array", "items": {"type": "string"}, "description": "Tags for searchability"},
                        "metadata": {"type": "object", "description": "Optional metadata payload"}
                    },
                    "required": ["session_id", "summary", "tags"]
                }
            },
            {
                "name": "agent_dialogue_mode",
                "description": "Initiate inter-agent conversation on a topic with audio broadcast turns",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "topic": {"type": "string", "description": "Topic for dual-Xiaozhi discussion"},
                        "max_turns": {"type": "integer", "description": "Maximum dialogue turns", "default": 4},
                        "target_node": {"type": "string", "description": "Target peer node ID"}
                    },
                    "required": ["topic"]
                }
            },
            {
                "name": "get_node_telemetry",
                "description": "Retrieve current CPU, RAM, and status telemetry for this node",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "node_id": {"type": "string", "description": "Target node"}
                    }
                }
            }
        ]

    async def handle_tool_call(self, tool_name: str, arguments: Dict[str, Any], sender_node: str) -> Dict[str, Any]:
        """Executes tool requests dispatched over MCP protocol."""
        logger.info(f"[{self.node_id}] Executing tool '{tool_name}' from sender '{sender_node}'")

        if tool_name == "ask_peer_xiaozhi":
            query = arguments.get("query", "")
            context = arguments.get("context", {})
            target_node = arguments.get("target_node", self.node_id)
            
            # Simulated edge-to-workbench reasoning response
            reply = (
                f"[{self.node_id}] Processed peer query '{query}'. "
                f"Context keys: {list(context.keys())}. "
                f"Status: Routed to local reasoning engine successfully."
            )
            return {
                "status": "success",
                "answering_node": self.node_id,
                "target_node": target_node,
                "reply": reply,
                "text": reply,
                "timestamp": time.time()
            }

        elif tool_name == "sync_shared_memory":
            session_id = arguments.get("session_id", str(uuid.uuid4()))
            summary = arguments.get("summary", "")
            tags = arguments.get("tags", [])
            metadata = arguments.get("metadata", {})
            
            row_id = self.db.store_memory(
                session_id=session_id,
                summary=summary,
                tags=tags,
                source_node=sender_node,
                metadata=metadata
            )
            return {
                "status": "success",
                "persisted_row_id": row_id,
                "session_id": session_id,
                "database": self.db.db_path
            }

        elif tool_name == "agent_dialogue_mode":
            topic = arguments.get("topic", "General Coordination")
            max_turns = arguments.get("max_turns", 4)
            session_id = f"dialogue_{int(time.time())}"
            
            turns_output = []
            for turn in range(1, max_turns + 1):
                speaker = self.node_id if (turn % 2 != 0) else sender_node
                text_content = f"Turn {turn} on '{topic}' articulated by {speaker}."
                # 960 samples (60ms) of 16kHz audio frame
                audio_frame = generate_16k_pcm_audio_frame(frequency_hz=300.0 + (turn * 80.0))
                audio_b64 = base64.b64encode(audio_frame).decode("ascii")
                
                self.db.record_dialogue_turn(
                    session_id=session_id,
                    topic=topic,
                    turn_index=turn,
                    speaker_node=speaker,
                    text=text_content,
                    audio_bytes_len=len(audio_frame)
                )
                turns_output.append({
                    "turn_index": turn,
                    "speaker": speaker,
                    "text": text_content,
                    "audio_frame_bytes": len(audio_frame),
                    "audio_base64": audio_b64,
                    "sample_rate": SAMPLE_RATE_HZ,
                    "samples_count": SAMPLES_PER_FRAME
                })

                # Broadcast turn event to all connected peers for room speaker feedback
                turn_event = {
                    "jsonrpc": "2.0",
                    "method": "notifications/dialogue_turn",
                    "params": {
                        "session_id": session_id,
                        "topic": topic,
                        "turn_index": turn,
                        "speaker": speaker,
                        "text": text_content,
                        "audio_base64": audio_b64,
                        "audio_frame_bytes": len(audio_frame),
                        "sample_rate": SAMPLE_RATE_HZ,
                        "samples_count": SAMPLES_PER_FRAME
                    }
                }
                await self.broadcast_message(json.dumps(turn_event))

            return {
                "status": "success",
                "session_id": session_id,
                "topic": topic,
                "turns_completed": len(turns_output),
                "turns": turns_output
            }

        elif tool_name == "get_node_telemetry":
            telem = get_system_telemetry(self.node_id)
            self.db.record_telemetry(self.node_id, telem["cpu_percent"], telem["ram_used_mb"])
            return telem

        else:
            raise ValueError(f"Unknown MCP tool: {tool_name}")

    async def broadcast_message(self, message: str):
        """Broadcasts text/JSON-RPC message to all active connected peers."""
        dead_peers = []
        for peer_id, ws in list(self.active_peers.items()):
            try:
                await ws.send_text(message)
            except Exception:
                dead_peers.append(peer_id)
        for p in dead_peers:
            self.active_peers.pop(p, None)

    async def broadcast_bytes(self, data: bytes):
        """Broadcasts binary audio frames to all active connected peers."""
        dead_peers = []
        for peer_id, ws in list(self.active_peers.items()):
            try:
                await ws.send_bytes(data)
            except Exception:
                dead_peers.append(peer_id)
        for p in dead_peers:
            self.active_peers.pop(p, None)

    async def handle_binary_audio(self, audio_bytes: bytes, sender_node: str):
        """Processes incoming 16kHz audio stream frames."""
        logger.debug(f"[{self.node_id}] Ingested {len(audio_bytes)} raw audio bytes from {sender_node}")

    async def call_peer(
        self,
        peer_node_id: str,
        method: str,
        params: Optional[Dict[str, Any]] = None,
        timeout: float = 10.0
    ) -> Dict[str, Any]:
        """Dispatches JSON-RPC request to connected client peer."""
        ws = self.active_peers.get(peer_node_id)
        if not ws:
            raise ConnectionError(f"Peer '{peer_node_id}' is not connected")
        req_id = str(uuid.uuid4())
        loop = asyncio.get_running_loop()
        future = loop.create_future()
        self.pending_rpc_requests[req_id] = future
        msg = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "sender_node": self.node_id,
            "params": params or {}
        }
        await ws.send_text(json.dumps(msg))
        try:
            return await asyncio.wait_for(future, timeout=timeout)
        finally:
            self.pending_rpc_requests.pop(req_id, None)

    async def ask_peer(
        self,
        target_node: str,
        query: str,
        context: Optional[Dict[str, Any]] = None,
        timeout: float = 10.0
    ) -> Dict[str, Any]:
        """High-level RPC from server hub to peer."""
        res = await self.call_peer(
            peer_node_id=target_node,
            method="tools/call",
            params={
                "name": "ask_peer_xiaozhi",
                "arguments": {
                    "target_node": target_node,
                    "query": query,
                    "context": context or {}
                }
            },
            timeout=timeout
        )
        content = res.get("result", {}).get("content", [{}])[0].get("text", "{}")
        return json.loads(content)

    async def process_mcp_message(
        self,
        raw_message: str,
        sender_ws: Optional[WebSocket] = None,
        sender_node: Optional[str] = None
    ) -> Optional[str]:
        """Handles incoming JSON-RPC 2.0 / MCP messages."""
        try:
            data = json.loads(raw_message)
        except Exception as e:
            return json.dumps({
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32700, "message": f"Parse error: {str(e)}"}
            })

        msg_id = data.get("id")
        method = data.get("method")

        # Response to our pending outbound RPC request
        if "result" in data or "error" in data:
            if msg_id and msg_id in self.pending_rpc_requests:
                future = self.pending_rpc_requests.pop(msg_id)
                if not future.done():
                    future.set_result(data)
            return None

        # MCP Handshake & Discovery
        if method == "initialize":
            return json.dumps({
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "serverInfo": {
                        "name": f"xiaozhi-federation-hub-{self.node_id}",
                        "version": "1.0.0"
                    },
                    "capabilities": {
                        "tools": {"listChanged": False},
                        "resources": {"subscribe": False, "listChanged": False}
                    }
                }
            })

        elif method == "tools/list":
            return json.dumps({
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "tools": self.get_supported_tools()
                }
            })

        elif method == "tools/call":
            params = data.get("params", {})
            name = params.get("name")
            arguments = params.get("arguments", {})
            sender = data.get("sender_node") or sender_node or "unknown_peer"
            try:
                res = await self.handle_tool_call(name, arguments, sender)
                return json.dumps({
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "content": [
                            {"type": "text", "text": json.dumps(res)}
                        ],
                        "isError": False
                    }
                })
            except Exception as ex:
                return json.dumps({
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "content": [
                            {"type": "text", "text": str(ex)}
                        ],
                        "isError": True
                    }
                })

        elif method == "ping":
            return json.dumps({"jsonrpc": "2.0", "id": msg_id, "result": "pong"})

        else:
            return json.dumps({
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32601, "message": f"Method '{method}' not found"}
            })


# FastAPI Application instance
app = FastAPI(title="Xiaozhi Federation Bridge", version="1.0.0")

# Global hub singleton
current_node_id = os.environ.get("XIAOZHI_NODE_ID", "xiaozhi-workbench")
hub = XiaozhiMCPFederationHub(node_id=current_node_id)


@app.get("/health")
async def health_check():
    return {
        "status": "healthy",
        "node_id": hub.node_id,
        "database": hub.db.db_path,
        "active_peers": list(hub.active_peers.keys())
    }


@app.get("/api/federation/memory")
async def read_memory(session_id: Optional[str] = None, limit: int = 10):
    records = hub.db.query_memory(session_id, limit)
    return {"status": "success", "count": len(records), "data": records}


@app.get("/api/federation/dialogue/{session_id}")
async def read_dialogue(session_id: str):
    records = hub.db.get_dialogue_history(session_id)
    return {"status": "success", "session_id": session_id, "turns": records}


@app.websocket(DEFAULT_WS_PATH)
async def websocket_federation_endpoint(
    websocket: WebSocket,
    node_id: str = Query(default="unnamed-peer")
):
    await websocket.accept()
    hub.active_peers[node_id] = websocket
    logger.info(f"Peer connected: {node_id} (Total peers: {len(hub.active_peers)})")
    try:
        while True:
            msg = await websocket.receive()
            if msg.get("type") == "websocket.disconnect":
                logger.info(f"Peer disconnected cleanly: {node_id}")
                break
            elif "text" in msg and msg["text"]:
                response_json = await hub.process_mcp_message(msg["text"], websocket, sender_node=node_id)
                if response_json:
                    await websocket.send_text(response_json)
            elif "bytes" in msg and msg["bytes"]:
                await hub.handle_binary_audio(msg["bytes"], sender_node=node_id)
    except WebSocketDisconnect:
        logger.info(f"Peer disconnected: {node_id}")
    except Exception as e:
        logger.warning(f"Error handling peer {node_id}: {e}")
    finally:
        hub.active_peers.pop(node_id, None)


class XiaozhiFederationClient:
    """Outbound client connecting to a peer Xiaozhi node over MCP-WebSocket."""

    def __init__(
        self,
        node_id: str,
        peer_host: str = "127.0.0.1",
        peer_port: int = DEFAULT_PORT,
        on_notification: Optional[Callable[[Dict[str, Any]], None]] = None,
        hub: Optional[XiaozhiMCPFederationHub] = None
    ):
        self.node_id = node_id
        self.peer_host = peer_host
        self.peer_port = peer_port
        self.ws_url = f"ws://{peer_host}:{peer_port}{DEFAULT_WS_PATH}?node_id={node_id}"
        self.ws = None
        self._listen_task = None
        self.pending_responses: Dict[str, asyncio.Future] = {}
        self.is_connected = False
        self.on_notification = on_notification
        self.hub = hub

    async def connect(self, timeout: float = 5.0):
        import websockets
        self.ws = await asyncio.wait_for(websockets.connect(self.ws_url), timeout=timeout)
        self.is_connected = True
        self._listen_task = asyncio.create_task(self._listen_loop())
        logger.info(f"[{self.node_id}] Connected to peer at {self.ws_url}")

        init_res = await self.send_rpc("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": self.node_id, "version": "1.0.0"}
        })
        logger.info(f"[{self.node_id}] MCP Handshake OK: {init_res.get('result', {}).get('serverInfo')}")

    async def _handle_incoming_rpc(self, payload: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        req_id = payload.get("id")
        method = payload.get("method")
        params = payload.get("params", {})
        if method == "ping":
            return {"jsonrpc": "2.0", "id": req_id, "result": "pong"}
        elif method == "tools/call" and params.get("name") == "ask_peer_xiaozhi":
            args = params.get("arguments", {})
            query = args.get("query", "")
            ans = f"[{self.node_id}] Client response to query '{query}'. Status: OK"
            res = {
                "status": "success",
                "answering_node": self.node_id,
                "reply": ans,
                "text": ans,
                "timestamp": time.time()
            }
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {"content": [{"type": "text", "text": json.dumps(res)}], "isError": False}
            }
        elif self.hub:
            raw_res = await self.hub.process_mcp_message(json.dumps(payload), None)
            return json.loads(raw_res) if raw_res else None
        return None

    async def _listen_loop(self):
        try:
            async for message in self.ws:
                try:
                    payload = json.loads(message)
                    req_id = payload.get("id")
                    method = payload.get("method")
                    if ("result" in payload or "error" in payload) and req_id in self.pending_responses:
                        fut = self.pending_responses.pop(req_id)
                        if not fut.done():
                            fut.set_result(payload)
                    elif method and method.startswith("notifications/"):
                        if self.on_notification:
                            try:
                                self.on_notification(payload)
                            except Exception as ex:
                                logger.error(f"Notification error: {ex}")
                    elif method:
                        resp = await self._handle_incoming_rpc(payload)
                        if resp and self.ws:
                            await self.ws.send(json.dumps(resp))
                except Exception as ex:
                    logger.error(f"Error decoding incoming message: {ex}")
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.warning(f"Client listen loop terminated: {e}")
        finally:
            self.is_connected = False
            for fut in self.pending_responses.values():
                if not fut.done():
                    fut.set_exception(ConnectionError("Federation connection closed"))
            self.pending_responses.clear()

    async def send_rpc(self, method: str, params: Optional[Dict[str, Any]] = None, timeout: float = 10.0) -> Dict[str, Any]:
        if not self.ws or not self.is_connected:
            raise ConnectionError("WebSocket client not connected")
        req_id = str(uuid.uuid4())
        loop = asyncio.get_running_loop()
        future = loop.create_future()
        self.pending_responses[req_id] = future

        msg = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "sender_node": self.node_id,
            "params": params or {}
        }
        await self.ws.send(json.dumps(msg))
        return await asyncio.wait_for(future, timeout=timeout)

    async def send_audio_frame(self, audio_bytes: bytes):
        """Sends raw 16kHz binary audio frame to connected peer."""
        if not self.ws or not self.is_connected:
            raise ConnectionError("WebSocket client not connected")
        await self.ws.send(audio_bytes)

    async def ask_peer_xiaozhi(self, target_node: str, query: str, context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Core inter-agent function 1: Delegate reasoning or task to peer."""
        response = await self.send_rpc("tools/call", {
            "name": "ask_peer_xiaozhi",
            "arguments": {
                "target_node": target_node,
                "query": query,
                "context": context or {}
            }
        })
        content = response.get("result", {}).get("content", [{}])[0].get("text", "{}")
        return json.loads(content)

    async def sync_shared_memory(
        self,
        session_id: str,
        summary: str,
        tags: List[str],
        metadata: Optional[Dict[str, Any]] = None
    ) -> Dict[str, Any]:
        """Core inter-agent function 2: Sync conversation summary and context."""
        response = await self.send_rpc("tools/call", {
            "name": "sync_shared_memory",
            "arguments": {
                "session_id": session_id,
                "summary": summary,
                "tags": tags,
                "metadata": metadata or {}
            }
        })
        content = response.get("result", {}).get("content", [{}])[0].get("text", "{}")
        return json.loads(content)

    async def agent_dialogue_mode(self, topic: str, max_turns: int = 4) -> Dict[str, Any]:
        """Core inter-agent function 3: Dual-Xiaozhi voice dialogue mode."""
        response = await self.send_rpc("tools/call", {
            "name": "agent_dialogue_mode",
            "arguments": {
                "topic": topic,
                "max_turns": max_turns,
                "target_node": self.node_id
            }
        })
        content = response.get("result", {}).get("content", [{}])[0].get("text", "{}")
        return json.loads(content)

    async def close(self):
        if self._listen_task and not self._listen_task.done():
            self._listen_task.cancel()
            try:
                await self._listen_task
            except asyncio.CancelledError:
                pass
        if self.ws:
            await self.ws.close()
        self.is_connected = False


def run_standalone_server(port: int = DEFAULT_PORT, host: str = "0.0.0.0", node_id: str = "xiaozhi-workbench"):
    """Starts Uvicorn ASGI server hosting the federation endpoint."""
    os.environ["XIAOZHI_NODE_ID"] = node_id
    global hub
    hub = XiaozhiMCPFederationHub(node_id=node_id)
    uvicorn.run(app, host=host, port=port, log_level="info")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Xiaozhi MCP-over-WebSocket Federation Bridge")
    parser.add_argument("--role", choices=["server", "client"], default="server", help="Execution role")
    parser.add_argument("--node-id", default=os.environ.get("XIAOZHI_NODE_ID", "xiaozhi-workbench"), help="Node identifier")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host for server")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Port number")
    parser.add_argument("--peer-host", default="192.168.0.111", help="Peer host address for client")
    args = parser.parse_args()

    if args.role == "server":
        logger.info(f"Starting Federation Server as '{args.node_id}' on {args.host}:{args.port}")
        run_standalone_server(port=args.port, host=args.host, node_id=args.node_id)
    else:
        logger.info(f"Running client test connecting to {args.peer_host}:{args.port} as '{args.node_id}'")
        async def _client_demo():
            client = XiaozhiFederationClient(node_id=args.node_id, peer_host=args.peer_host, peer_port=args.port)
            await client.connect()
            res = await client.ask_peer_xiaozhi("xiaozhi-workbench", "Status report", {"source": "cli"})
            print("Response:", res)
            await client.close()
        asyncio.run(_client_demo())
