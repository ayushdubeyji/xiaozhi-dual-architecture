#!/usr/bin/env python3
"""Waveshare ESP32-S3-Zero USB HID Grabber, Audio Streamer & Rover UDP-CDC Bridge."""

import os
import sys
import glob
import time
import json
import struct
import socket
import asyncio
import logging
from typing import Optional, Set

try:
    import evdev
    from evdev import ecodes
    EVDEV_AVAILABLE = True
except ImportError:
    EVDEV_AVAILABLE = False
    class DummyEcodes:
        KEY_LEFTCTRL = 29
        KEY_RIGHTCTRL = 97
        KEY_LEFTSHIFT = 42
        KEY_RIGHTSHIFT = 54
        KEY_LEFTALT = 56
        KEY_RIGHTALT = 100
        KEY_W = 17
        KEY_P = 25
        EV_KEY = 1
    ecodes = DummyEcodes()

try:
    import serial_asyncio
    SERIAL_ASYNCIO_AVAILABLE = True
except ImportError:
    SERIAL_ASYNCIO_AVAILABLE = False

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [s3_daemon] %(message)s"
)
logger = logging.getLogger("s3_hid_wake_daemon")

# Constants
ESP32_VENDOR_ID = 0x303A
ROVER_PKT_MAGIC = 0xA7
ROVER_PKT_STRUCT = "<BBbbbBBBhH"  # 12 bytes (magic, type, x, y, z, speed, mode, flags, arg, seq)
ROVER_PKT_LEN = struct.calcsize(ROVER_PKT_STRUCT)
AUDIO_SAMPLE_RATE = 16000
AUDIO_CHANNELS = 1
AUDIO_SAMPLE_WIDTH = 2
AUDIO_FRAME_MS = 60
AUDIO_FRAME_SAMPLES = (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS) // 1000  # 960 samples
AUDIO_FRAME_BYTES = AUDIO_FRAME_SAMPLES * AUDIO_SAMPLE_WIDTH * AUDIO_CHANNELS  # 1920 bytes

DEFAULT_SERIAL_PORT = "/dev/ttyS3Zero"
FALLBACK_SERIAL_PORT = "/dev/ttyACM0"
DEFAULT_ROVER_UDP_PORT = 8085
DEFAULT_TELEM_UDP_PORT = 8090
DEFAULT_XIAOZHI_WS_URL = "ws://127.0.0.1:8765/ws/xiaozhi"


class S3HidGrabber:
    """Exclusive evdev HID grabber for ESP32-S3 keyboard events."""

    def __init__(self, ws_bridge: "XiaozhiWsBridge"):
        self.ws_bridge = ws_bridge
        self.device: Optional[evdev.InputDevice] = None
        self.pressed_keys: Set[int] = set()
        self.ptt_active = False

    def find_device_path(self) -> Optional[str]:
        if not EVDEV_AVAILABLE:
            return None

        # Check symlinks in /dev/input/by-id
        by_id_matches = glob.glob("/dev/input/by-id/*ESP32*") + glob.glob("/dev/input/by-id/*303a*")
        for p in by_id_matches:
            if os.path.exists(p):
                return p

        # Iterate all event devices
        devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
        for dev in devices:
            if dev.info.vendor == ESP32_VENDOR_ID:
                return dev.path
            name_lower = dev.name.lower()
            if "esp32" in name_lower or "waveshare" in name_lower:
                return dev.path
        return None

    def _is_modifier_combo_held(self) -> bool:
        ctrl = (ecodes.KEY_LEFTCTRL in self.pressed_keys) or (ecodes.KEY_RIGHTCTRL in self.pressed_keys)
        shift = (ecodes.KEY_LEFTSHIFT in self.pressed_keys) or (ecodes.KEY_RIGHTSHIFT in self.pressed_keys)
        alt = (ecodes.KEY_LEFTALT in self.pressed_keys) or (ecodes.KEY_RIGHTALT in self.pressed_keys)
        return ctrl and shift and alt

    async def run(self):
        if not EVDEV_AVAILABLE:
            logger.warning("evdev not available on this platform; HID grabber operating in stub mode")
            while True:
                await asyncio.sleep(3600)

        while True:
            dev_path = self.find_device_path()
            if not dev_path:
                logger.debug("Waiting for ESP32-S3 USB HID device...")
                await asyncio.sleep(2.0)
                continue

            try:
                self.device = evdev.InputDevice(dev_path)
                self.device.grab()
                logger.info(f"Grabbed ESP32-S3 HID exclusively at {dev_path} ({self.device.name})")

                async for event in self.device.async_read_loop():
                    if event.type != ecodes.EV_KEY:
                        continue

                    key_code = event.code
                    key_val = event.value  # 0=release, 1=press, 2=repeat

                    if key_val == 1:
                        self.pressed_keys.add(key_code)
                        if self._is_modifier_combo_held():
                            if key_code == ecodes.KEY_W:
                                logger.info("Detected WakeNet trigger: Ctrl+Shift+Alt+W")
                                await self.ws_bridge.send_event({"event": "wake_word", "source": "s3_hid"})
                            elif key_code == ecodes.KEY_P and not self.ptt_active:
                                self.ptt_active = True
                                logger.info("Detected PTT Start: Ctrl+Shift+Alt+P")
                                await self.ws_bridge.send_event({"event": "ptt_start", "source": "s3_hid"})
                    elif key_val == 0:
                        self.pressed_keys.discard(key_code)
                        if self.ptt_active and (key_code == ecodes.KEY_P or not self._is_modifier_combo_held()):
                            self.ptt_active = False
                            logger.info("Detected PTT Stop: Release P or Modifiers")
                            await self.ws_bridge.send_event({"event": "ptt_stop", "source": "s3_hid"})

            except (OSError, IOError) as ex:
                logger.warning(f"ESP32-S3 HID disconnected or error: {ex}")
            finally:
                if self.device:
                    try:
                        self.device.ungrab()
                    except Exception:
                        pass
                    self.device = None
                self.pressed_keys.clear()
                self.ptt_active = False
            await asyncio.sleep(1.0)


class XiaozhiWsBridge:
    """Client bridge to xiaozhi-pi agent WebSocket."""

    def __init__(self, ws_url: str = DEFAULT_XIAOZHI_WS_URL):
        self.ws_url = ws_url
        self.ws = None
        self.is_connected = False
        self._outbound_queue: asyncio.Queue = asyncio.Queue(maxsize=100)

    async def connect_loop(self):
        import websockets
        while True:
            try:
                async with websockets.connect(self.ws_url, max_size=10_000_000) as ws:
                    self.ws = ws
                    self.is_connected = True
                    logger.info(f"Connected to xiaozhi-pi agent at {self.ws_url}")
                    
                    # Drain queue
                    while True:
                        item = await self._outbound_queue.get()
                        if isinstance(item, (bytes, bytearray)):
                            await ws.send(item)
                        elif isinstance(item, dict):
                            await ws.send(json.dumps(item))
                        self._outbound_queue.task_done()
            except asyncio.CancelledError:
                break
            except Exception as e:
                self.is_connected = False
                self.ws = None
                logger.debug(f"xiaozhi-pi connection waiting: {e}")
                await asyncio.sleep(2.0)

    async def send_event(self, data: dict):
        if self._outbound_queue.full():
            try:
                self._outbound_queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
        await self._outbound_queue.put(data)

    async def send_audio_frame(self, frame_bytes: bytes):
        if self._outbound_queue.full():
            try:
                self._outbound_queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
        await self._outbound_queue.put(frame_bytes)


class RoverUdpBridge(asyncio.DatagramProtocol):
    """UDP listener bridging Mecanum Rover packets (magic 0xA7) from Dashboard to S3 CDC."""

    def __init__(self, serial_bridge: "S3SerialBridge"):
        self.serial_bridge = serial_bridge
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport
        logger.info(f"Rover UDP listener active on port {DEFAULT_ROVER_UDP_PORT}")

    def datagram_received(self, data: bytes, addr):
        if len(data) >= ROVER_PKT_LEN and data[0] == ROVER_PKT_MAGIC:
            # Valid Rover packet: dispatch to ESP32-S3 CDC for ESP-NOW broadcast
            self.serial_bridge.write_rover_packet(data)
        elif len(data) > 0:
            logger.debug(f"Received non-A7 UDP datagram len={len(data)} from {addr}")


class S3SerialBridge:
    """Manages USB CDC serial link (/dev/ttyACM0 or /dev/ttyS3Zero) to ESP32-S3."""

    def __init__(self, ws_bridge: XiaozhiWsBridge):
        self.ws_bridge = ws_bridge
        self.port = self.find_serial_port() or FALLBACK_SERIAL_PORT
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.is_open = False
        self.telem_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def find_serial_port(self) -> Optional[str]:
        for p in [DEFAULT_SERIAL_PORT, FALLBACK_SERIAL_PORT, "/dev/ttyACM1"]:
            if os.path.exists(p):
                return p
        by_id = glob.glob("/dev/serial/by-id/*ESP32*") + glob.glob("/dev/serial/by-id/*303a*")
        for p in by_id:
            if os.path.exists(p):
                return p
        return None

    def write_rover_packet(self, packet_bytes: bytes):
        if self.writer and not self.writer.is_closing():
            try:
                # 1. Dispatch text command for ESP-NOW firmware CDC task: CMD:ROVER_DRIVE <vx> <vy> <omega> <btn> <flags>
                if len(packet_bytes) >= ROVER_PKT_LEN and packet_bytes[0] == ROVER_PKT_MAGIC:
                    magic, pkt_type, vx, vy, vz, speed, mode, flags, arg, seq = struct.unpack(
                        ROVER_PKT_STRUCT, packet_bytes[:ROVER_PKT_LEN]
                    )
                    cmd_str = f"CMD:ROVER_DRIVE {vx} {vy} {vz} {mode} {flags}\n"
                    self.writer.write(cmd_str.encode("ascii"))
                # 2. Also send framed binary packet for binary-compatible firmware
                framed = b"\xAA\x55" + struct.pack("<B", len(packet_bytes)) + packet_bytes
                self.writer.write(framed)
            except Exception as e:
                logger.warning(f"Error writing rover packet to S3 CDC: {e}")

    async def run(self):
        while True:
            active_port = self.find_serial_port()
            if not active_port:
                await asyncio.sleep(2.0)
                continue

            try:
                if not SERIAL_ASYNCIO_AVAILABLE:
                    logger.warning("pyserial-asyncio not available; CDC bridge waiting")
                    await asyncio.sleep(10.0)
                    continue

                self.reader, self.writer = await serial_asyncio.open_serial_connection(
                    url=active_port,
                    baudrate=921600
                )
                self.is_open = True
                logger.info(f"Opened S3 CDC serial connection at {active_port} @ 921600 baud")

                buffer = bytearray()
                while True:
                    chunk = await self.reader.read(1024)
                    if not chunk:
                        break
                    buffer.extend(chunk)

                    # Parse stream: look for audio frames, telemetry packets, or text lines
                    while len(buffer) >= 2:
                        # Case 1: Audio Frame or Telemetry with \xAA\x55 header
                        if buffer.startswith(b"\xAA\x55") and len(buffer) >= 4:
                            frame_len = struct.unpack("<H", buffer[2:4])[0]
                            # Check if 16kHz mono audio frame (1920 bytes for 960 samples)
                            if frame_len == AUDIO_FRAME_BYTES:
                                total_needed = 4 + frame_len
                                if len(buffer) >= total_needed:
                                    audio_payload = bytes(buffer[4:total_needed])
                                    buffer = buffer[total_needed:]
                                    await self.ws_bridge.send_audio_frame(audio_payload)
                                    continue
                                else:
                                    break
                            elif buffer[3] == ROVER_PKT_MAGIC:
                                # Binary Telemetry packet: [0xAA, 0x55, pkt_len, 0xA7, ...]
                                pkt_len = buffer[2]
                                total_needed = 3 + pkt_len
                                if len(buffer) >= total_needed:
                                    telem_payload = bytes(buffer[3:total_needed])
                                    buffer = buffer[total_needed:]
                                    try:
                                        self.telem_sock.sendto(telem_payload, ("127.0.0.1", DEFAULT_TELEM_UDP_PORT))
                                    except Exception:
                                        pass
                                    continue
                                else:
                                    break
                            else:
                                buffer.pop(0)
                                continue

                        # Case 2: Audio Frame prefix b'S3\x01' + 2 bytes big-endian length
                        elif buffer.startswith(b"S3\x01") and len(buffer) >= 5:
                            frame_len = struct.unpack(">H", buffer[3:5])[0]
                            total_needed = 5 + frame_len
                            if len(buffer) >= total_needed:
                                audio_payload = bytes(buffer[5:total_needed])
                                buffer = buffer[total_needed:]
                                if len(audio_payload) == AUDIO_FRAME_BYTES:
                                    await self.ws_bridge.send_audio_frame(audio_payload)
                                continue
                            else:
                                break

                        # Case 3: CDC Text lines (ROVER_TELEM:..., RNDIS_STATUS:..., PONG:...)
                        elif b"\n" in buffer:
                            nl_idx = buffer.find(b"\n")
                            line_bytes = bytes(buffer[:nl_idx]).rstrip(b"\r")
                            try:
                                line_str = line_bytes.decode("ascii")
                                if line_str.startswith("ROVER_TELEM:"):
                                    buffer = buffer[nl_idx + 1:]
                                    try:
                                        self.telem_sock.sendto(line_bytes, ("127.0.0.1", DEFAULT_TELEM_UDP_PORT))
                                    except Exception:
                                        pass
                                    continue
                                elif line_str.startswith("RNDIS_STATUS:") or line_str.startswith("PONG:"):
                                    buffer = buffer[nl_idx + 1:]
                                    logger.info(f"S3 CDC: {line_str}")
                                    continue
                            except UnicodeDecodeError:
                                pass

                        # Case 4: Raw audio frame (1920 bytes) fallback
                        if len(buffer) >= AUDIO_FRAME_BYTES and not buffer.startswith(b"\xAA\x55") and not buffer.startswith(b"S3\x01"):
                            raw_frame = bytes(buffer[:AUDIO_FRAME_BYTES])
                            buffer = buffer[AUDIO_FRAME_BYTES:]
                            await self.ws_bridge.send_audio_frame(raw_frame)
                        else:
                            # Advance 1 byte if desynced
                            if not buffer.startswith(b"S3") and not buffer.startswith(b"\xAA"):
                                buffer.pop(0)
                            else:
                                break

            except asyncio.CancelledError:
                break
            except Exception as ex:
                logger.warning(f"S3 CDC serial exception: {ex}")
            finally:
                self.is_open = False
                if self.writer:
                    try:
                        self.writer.close()
                        await self.writer.wait_closed()
                    except Exception:
                        pass
                    self.writer = None
                    self.reader = None
            await asyncio.sleep(2.0)


async def main():
    logger.info("Initializing ESP32-S3 Host Daemon on Raspberry Pi 4B...")
    ws_bridge = XiaozhiWsBridge()
    s3_serial = S3SerialBridge(ws_bridge=ws_bridge)
    s3_hid = S3HidGrabber(ws_bridge=ws_bridge)

    loop = asyncio.get_running_loop()

    # Start UDP listener for Rover Dashboard
    transport, protocol = await loop.create_datagram_endpoint(
        lambda: RoverUdpBridge(serial_bridge=s3_serial),
        local_addr=("0.0.0.0", DEFAULT_ROVER_UDP_PORT)
    )

    tasks = [
        asyncio.create_task(ws_bridge.connect_loop()),
        asyncio.create_task(s3_serial.run()),
        asyncio.create_task(s3_hid.run())
    ]

    try:
        await asyncio.gather(*tasks)
    except asyncio.CancelledError:
        pass
    finally:
        for t in tasks:
            t.cancel()
        transport.close()
        logger.info("ESP32-S3 Host Daemon terminated cleanly.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
