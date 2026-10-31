import io
import time
import wave
import struct
import logging
from dataclasses import dataclass
from typing import Optional, Dict, Any, Union, List
import httpx

try:
    from .config_loader import ConfigManager
except (ImportError, ValueError):
    from config_loader import ConfigManager

logger = logging.getLogger("xiaozhi.asr_router")

@dataclass
class ASRResult:
    text: str
    provider: str
    model: str
    latency_ms: float
    success: bool
    error: Optional[str] = None

def pcm_to_wav(pcm_bytes: bytes, sample_rate: int = 16000, channels: int = 1, sample_width: int = 2) -> bytes:
    frame_size = channels * sample_width
    rem = len(pcm_bytes) % frame_size
    if rem != 0:
        pcm_bytes = pcm_bytes[:-rem]
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sample_width)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)
    return buffer.getvalue()

def create_ogg_opus_stream(opus_packets: list, sample_rate: int = 16000, channels: int = 1) -> bytes:
    crc_table = []
    for i in range(256):
        curr = i << 24
        for _ in range(8):
            if curr & 0x80000000:
                curr = ((curr << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                curr = (curr << 1) & 0xFFFFFFFF
        crc_table.append(curr)

    def calc_crc(data: bytes) -> int:
        crc = 0
        for byte in data:
            crc = ((crc << 8) ^ crc_table[((crc >> 24) ^ byte) & 0xFF]) & 0xFFFFFFFF
        return crc

    serial = 0x5849414F  # 'XIAO' in hex
    pages = []

    # Page 1: OpusHead
    head_payload = b"OpusHead" + struct.pack("<BBHIHB", 1, channels, 0, sample_rate, 0, 0)
    page_hdr = b"OggS\x00\x02" + struct.pack("<QII", 0, serial, 0) + b"\x00\x00\x00\x00" + bytes([1, len(head_payload)])
    crc = calc_crc(page_hdr + head_payload)
    pages.append(page_hdr[:22] + struct.pack("<I", crc) + page_hdr[26:] + head_payload)

    # Page 2: OpusTags
    tags_payload = b"OpusTags\x00\x00\x00\x00\x00\x00\x00\x00"
    page_hdr2 = b"OggS\x00\x00" + struct.pack("<QII", 0, serial, 1) + b"\x00\x00\x00\x00" + bytes([1, len(tags_payload)])
    crc2 = calc_crc(page_hdr2 + tags_payload)
    pages.append(page_hdr2[:22] + struct.pack("<I", crc2) + page_hdr2[26:] + tags_payload)

    # Page 3+: Audio packets (if any provided)
    seq = 2
    granule = 0
    samples_per_frame = int(sample_rate * 0.06)  # 60ms standard Opus frame (960 samples @ 16kHz)
    for pkt in opus_packets:
        if not pkt:
            continue
        granule += samples_per_frame
        seg_len = min(len(pkt), 255)
        pkt_data = pkt[:seg_len]
        page_hdr_pkt = b"OggS\x00\x00" + struct.pack("<QII", granule, serial, seq) + b"\x00\x00\x00\x00" + bytes([1, len(pkt_data)])
        crc_pkt = calc_crc(page_hdr_pkt + pkt_data)
        pages.append(page_hdr_pkt[:22] + struct.pack("<I", crc_pkt) + page_hdr_pkt[26:] + pkt_data)
        seq += 1

    return b"".join(pages)

def prepare_audio_payload(audio_data: Any, sample_rate: int = 16000, channels: int = 1, sample_width: int = 2) -> tuple:
    if isinstance(audio_data, (list, tuple)):
        ogg_bytes = create_ogg_opus_stream(list(audio_data), sample_rate=sample_rate, channels=channels)
        return "audio.ogg", ogg_bytes, "audio/ogg"
    if not isinstance(audio_data, (bytes, bytearray)):
        audio_data = bytes(audio_data)

    if audio_data.startswith(b"RIFF"):
        return "audio.wav", bytes(audio_data), "audio/wav"
    elif audio_data.startswith(b"OggS"):
        return "audio.ogg", bytes(audio_data), "audio/ogg"
    elif audio_data.startswith(b"fLaC"):
        return "audio.flac", bytes(audio_data), "audio/flac"
    elif audio_data.startswith(b"\x1a\x45\xdf\xa3"):
        return "audio.webm", bytes(audio_data), "audio/webm"
    elif audio_data.startswith(b"ID3") or (len(audio_data) >= 2 and audio_data[:2] in (b"\xff\xfb", b"\xff\xf3", b"\xff\xf2")):
        return "audio.mp3", bytes(audio_data), "audio/mpeg"
    else:
        wav_bytes = pcm_to_wav(bytes(audio_data), sample_rate=sample_rate, channels=channels, sample_width=sample_width)
        return "audio.wav", wav_bytes, "audio/wav"

class WhisperASRRouter:
    def __init__(self, config_manager: Optional[ConfigManager] = None):
        self.cfg_mgr = config_manager or ConfigManager.get_instance()
        self._client: Optional[httpx.AsyncClient] = None

    async def __aenter__(self) -> "WhisperASRRouter":
        self._client = httpx.AsyncClient()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.close()

    def _get_client(self) -> httpx.AsyncClient:
        if self._client is None or self._client.is_closed:
            self._client = httpx.AsyncClient()
        return self._client

    async def transcribe(self, audio_data: Any, language: str = "en") -> ASRResult:
        if not audio_data:
            return ASRResult(text="", provider="none", model="none", latency_ms=0.0, success=False, error="Empty audio data")

        cfg = self.cfg_mgr.config.get("asr", {})
        timeout_sec = float(cfg.get("timeout_seconds", 15.0))
        audio_spec = cfg.get("audio_spec", {})
        sample_rate = int(audio_spec.get("sample_rate", 16000))
        channels = int(audio_spec.get("channels", 1))
        sample_width = int(audio_spec.get("sample_width", 2))

        filename, payload_bytes, mime_type = prepare_audio_payload(
            audio_data, sample_rate=sample_rate, channels=channels, sample_width=sample_width
        )

        providers = cfg.get("providers", {})
        primary_name = cfg.get("primary_provider", "groq")
        fallback_name = cfg.get("fallback_provider", "openai")

        providers_to_try = [primary_name]
        if fallback_name and fallback_name != primary_name:
            providers_to_try.append(fallback_name)

        last_error: Optional[str] = None
        for prov_name in providers_to_try:
            prov_cfg = providers.get(prov_name, {})
            api_key = (prov_cfg.get("api_key") or "").strip()
            if not api_key:
                last_error = f"API key for provider '{prov_name}' is not configured"
                continue

            base_url = (prov_cfg.get("base_url") or ("https://api.groq.com/openai/v1" if prov_name == "groq" else "https://api.openai.com/v1")).rstrip("/")
            model = prov_cfg.get("model") or ("whisper-large-v3-turbo" if prov_name == "groq" else "whisper-1")

            start_t = time.perf_counter()
            try:
                result = await self._send_transcription_request(
                    base_url=base_url,
                    api_key=api_key,
                    model=model,
                    filename=filename,
                    payload_bytes=payload_bytes,
                    mime_type=mime_type,
                    language=language,
                    timeout_sec=timeout_sec,
                )
                latency = (time.perf_counter() - start_t) * 1000.0
                return ASRResult(
                    text=result.get("text", "").strip(),
                    provider=prov_name,
                    model=model,
                    latency_ms=round(latency, 2),
                    success=True,
                )
            except Exception as e:
                latency = (time.perf_counter() - start_t) * 1000.0
                err_detail = str(e)
                if hasattr(e, "response") and getattr(e, "response", None) is not None:
                    try:
                        err_detail = f"{e} - {e.response.text}"
                    except Exception:
                        pass
                last_error = f"{prov_name} ASR failed ({err_detail})"
                logger.warning(f"{last_error}. Trying next provider...")

        return ASRResult(
            text="",
            provider="none",
            model="none",
            latency_ms=0.0,
            success=False,
            error=last_error or "No valid ASR provider configured or all providers failed",
        )

    async def _send_transcription_request(
        self,
        base_url: str,
        api_key: str,
        model: str,
        filename: str,
        payload_bytes: bytes,
        mime_type: str,
        language: str,
        timeout_sec: float,
    ) -> Dict[str, Any]:
        client = self._get_client()
        url = f"{base_url}/audio/transcriptions"
        headers = {"Authorization": f"Bearer {api_key.strip()}"}
        files = {"file": (filename, payload_bytes, mime_type)}
        data = {
            "model": model,
            "response_format": "json",
            "temperature": "0.0",
        }
        if language:
            data["language"] = language

        resp = await client.post(url, headers=headers, files=files, data=data, timeout=timeout_sec)
        resp.raise_for_status()
        return resp.json()

    async def transcribe_audio_pcm(self, pcm_bytes: bytes, sample_rate: int = 16000, language: str = "en") -> str:
        res = await self.transcribe(pcm_bytes, language=language)
        return res.text if res.success else ""

    async def close(self) -> None:
        if self._client and not self._client.is_closed:
            await self._client.aclose()
            self._client = None

async def transcribe_audio_pcm(pcm_bytes: bytes, sample_rate: int = 16000, language: str = "en") -> str:
    router = WhisperASRRouter()
    try:
        res = await router.transcribe(pcm_bytes, language=language)
        return res.text if res.success else ""
    finally:
        await router.close()

async def transcribe(audio_data: Any, language: str = "en") -> ASRResult:
    router = WhisperASRRouter()
    try:
        return await router.transcribe(audio_data, language=language)
    finally:
        await router.close()
