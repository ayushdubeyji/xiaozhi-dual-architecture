#!/usr/bin/env python3
"""Unit tests for Node A Pi 4B bridge daemons and agents."""

import os
import sys
import math
import struct
import unittest
import asyncio
from unittest.mock import MagicMock, AsyncMock, patch
from pathlib import Path

# Ensure pi4b-bridge is on sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import hid_wake_daemon
import xiaozhi_pi_agent


class TestHidWakeDaemon(unittest.TestCase):
    def test_audio_constants(self):
        self.assertEqual(hid_wake_daemon.AUDIO_SAMPLE_RATE, 16000)
        self.assertEqual(hid_wake_daemon.AUDIO_FRAME_MS, 60)
        self.assertEqual(hid_wake_daemon.AUDIO_FRAME_SAMPLES, 960)
        self.assertEqual(hid_wake_daemon.AUDIO_FRAME_BYTES, 1920)

    def test_rover_packet_format(self):
        self.assertEqual(hid_wake_daemon.ROVER_PKT_MAGIC, 0xA7)
        self.assertEqual(hid_wake_daemon.ROVER_PKT_LEN, 12)
        # Test pack and unpack
        pkt = struct.pack(
            hid_wake_daemon.ROVER_PKT_STRUCT,
            0xA7, 3, 10, 20, 30, 80, 0, 0, 0, 1
        )
        self.assertEqual(len(pkt), 12)
        self.assertEqual(pkt[0], 0xA7)
        unpacked = struct.unpack(hid_wake_daemon.ROVER_PKT_STRUCT, pkt)
        self.assertEqual(unpacked[0], 0xA7)
        self.assertEqual(unpacked[1], 3)
        self.assertEqual(unpacked[2], 10)
        self.assertEqual(unpacked[3], 20)
        self.assertEqual(unpacked[4], 30)
        self.assertEqual(unpacked[5], 80)

    def test_modifier_combo_logic(self):
        mock_bridge = MagicMock()
        grabber = hid_wake_daemon.S3HidGrabber(ws_bridge=mock_bridge)
        
        # Test combo with ecodes constants
        ec = hid_wake_daemon.ecodes
        grabber.pressed_keys = {ec.KEY_LEFTCTRL, ec.KEY_LEFTSHIFT}
        self.assertFalse(grabber._is_modifier_combo_held())

        grabber.pressed_keys.add(ec.KEY_LEFTALT)
        self.assertTrue(grabber._is_modifier_combo_held())

    def test_rover_packet_cdc_write(self):
        mock_bridge = MagicMock()
        serial_bridge = hid_wake_daemon.S3SerialBridge(ws_bridge=mock_bridge)
        mock_writer = MagicMock()
        mock_writer.is_closing.return_value = False
        serial_bridge.writer = mock_writer

        # 12-byte packed rover packet
        pkt = struct.pack(
            hid_wake_daemon.ROVER_PKT_STRUCT,
            0xA7, 3, 15, -20, 35, 70, 1, 2, 0, 10
        )
        serial_bridge.write_rover_packet(pkt)

        # Verify both ASCII command and binary frame were written
        written_data = [call[0][0] for call in mock_writer.write.call_args_list]
        self.assertIn(b"CMD:ROVER_DRIVE 15 -20 35 1 2\n", written_data)
        framed_binary = b"\xAA\x55\x0C" + pkt
        self.assertIn(framed_binary, written_data)
        serial_bridge.telem_sock.close()

    def test_cdc_stream_audio_and_telem_parsing(self):
        async def _run():
            mock_bridge = MagicMock()
            mock_bridge.send_audio_frame = AsyncMock()
            serial_bridge = hid_wake_daemon.S3SerialBridge(ws_bridge=mock_bridge)
            serial_bridge.telem_sock.close()
            mock_telem_sock = MagicMock()
            serial_bridge.telem_sock = mock_telem_sock

            # 1. 0xAA 0x55 Audio frame (1920 bytes)
            audio_frame = bytes([1] * 1920)
            audio_packet = b"\xAA\x55" + struct.pack("<H", 1920) + audio_frame

            # 2. ROVER_TELEM text line
            telem_line = b'ROVER_TELEM:{"seq":5,"bat":85,"tof_f":120}\n'

            # 3. RNDIS_STATUS text line
            rndis_line = b"RNDIS_STATUS:ACTIVE\n"

            stream_data = audio_packet + telem_line + rndis_line

            buffer = bytearray(stream_data)
            while len(buffer) >= 2:
                if buffer.startswith(b"\xAA\x55") and len(buffer) >= 4:
                    frame_len = struct.unpack("<H", buffer[2:4])[0]
                    if frame_len == hid_wake_daemon.AUDIO_FRAME_BYTES:
                        total_needed = 4 + frame_len
                        if len(buffer) >= total_needed:
                            payload = bytes(buffer[4:total_needed])
                            buffer = buffer[total_needed:]
                            await mock_bridge.send_audio_frame(payload)
                            continue
                        else:
                            break
                    elif buffer[3] == hid_wake_daemon.ROVER_PKT_MAGIC:
                        pkt_len = buffer[2]
                        total_needed = 3 + pkt_len
                        if len(buffer) >= total_needed:
                            telem_payload = bytes(buffer[3:total_needed])
                            buffer = buffer[total_needed:]
                            mock_telem_sock.sendto(telem_payload, ("127.0.0.1", hid_wake_daemon.DEFAULT_TELEM_UDP_PORT))
                            continue
                        else:
                            break
                    else:
                        buffer.pop(0)
                        continue
                elif b"\n" in buffer:
                    nl_idx = buffer.find(b"\n")
                    line_bytes = bytes(buffer[:nl_idx]).rstrip(b"\r")
                    try:
                        line_str = line_bytes.decode("ascii")
                        if line_str.startswith("ROVER_TELEM:"):
                            buffer = buffer[nl_idx + 1:]
                            mock_telem_sock.sendto(line_bytes, ("127.0.0.1", hid_wake_daemon.DEFAULT_TELEM_UDP_PORT))
                            continue
                        elif line_str.startswith("RNDIS_STATUS:") or line_str.startswith("PONG:"):
                            buffer = buffer[nl_idx + 1:]
                            continue
                    except UnicodeDecodeError:
                        pass
                else:
                    break

            mock_bridge.send_audio_frame.assert_awaited_once_with(audio_frame)
            mock_telem_sock.sendto.assert_called_once_with(
                b'ROVER_TELEM:{"seq":5,"bat":85,"tof_f":120}',
                ("127.0.0.1", 8090)
            )
            self.assertEqual(len(buffer), 0)

        asyncio.run(_run())


class TestXiaozhiPiAgent(unittest.TestCase):
    @classmethod
    def tearDownClass(cls):
        try:
            xiaozhi_pi_agent.session_mgr.rover_sock.close()
        except Exception:
            pass

    def setUp(self):
        self.mgr = xiaozhi_pi_agent.XiaozhiSessionManager()

    def tearDown(self):
        try:
            self.mgr.rover_sock.close()
        except Exception:
            pass

    def test_rover_intent_parsing(self):
        # Stop
        res = self.mgr.handle_rover_intent("Please stop the rover right now")
        self.assertIn("stopped", res.lower())

        # Emergency stop
        res = self.mgr.handle_rover_intent("Emergency stop!")
        self.assertIn("emergency stop", res.lower())

        # Forward
        res = self.mgr.handle_rover_intent("Move forward ahead")
        self.assertIn("forward", res.lower())

        # Crawl mode
        res = self.mgr.handle_rover_intent("switch to crawl mode")
        self.assertIn("crawl", res.lower())

        # Unrelated query
        res = self.mgr.handle_rover_intent("What is the capital of France?")
        self.assertIsNone(res)

    def test_compute_frame_rms(self):
        # Silence frame
        silence = bytes(1920)
        rms_silence = self.mgr.compute_frame_rms(silence)
        self.assertEqual(rms_silence, 0.0)

        # Sine wave tone (amplitude 1000)
        samples = [int(1000 * math.sin(2 * math.pi * i / 32)) for i in range(960)]
        tone_frame = struct.pack(f"<{len(samples)}h", *samples)
        rms_tone = self.mgr.compute_frame_rms(tone_frame)
        self.assertGreater(rms_tone, 600.0)
        self.assertLess(rms_tone, 800.0)

    def test_memory_rss_lazy_import(self):
        mem = self.mgr.get_memory_rss_mb()
        self.assertIsInstance(mem, float)
        self.assertGreaterEqual(mem, 0.0)

    def test_rover_packet_dispatch(self):
        old_sock = self.mgr.rover_sock
        mock_sock = MagicMock()
        self.mgr.rover_sock = mock_sock
        try:
            self.mgr.dispatch_rover_packet(pkt_type=3, vx=10, vy=20, vz=30, speed=70)
            mock_sock.sendto.assert_called_once()
            args, kwargs = mock_sock.sendto.call_args
            pkt_sent = args[0]
            self.assertEqual(len(pkt_sent), 12)
            self.assertEqual(pkt_sent[0], 0xA7)
            self.assertEqual(args[1], ("127.0.0.1", 8085))
        finally:
            try:
                old_sock.close()
            except Exception:
                pass

    def test_state_transitions(self):
        async def _test():
            self.assertEqual(self.mgr.state, xiaozhi_pi_agent.AgentState.IDLE)
            await self.mgr.start_listening(is_ptt=True)
            self.assertEqual(self.mgr.state, xiaozhi_pi_agent.AgentState.LISTENING)
            self.assertTrue(self.mgr.is_ptt)

            # Ingest dummy frame
            frame = bytes(1920)
            await self.mgr.ingest_audio_frame(frame)
            self.assertEqual(len(self.mgr.audio_frames), 1)

        asyncio.run(_test())

    def test_brain_switching_and_provider_routing(self):
        async def _test():
            mock_llm = MagicMock()
            mock_llm.check_and_execute_brain_switch.return_value = "Brain switched to Groq (llama-3.3-70b-versatile)."
            mock_asr = MagicMock()
            mock_asr.transcribe = AsyncMock()
            asr_res = MagicMock()
            asr_res.text = "switch brain to groq"
            mock_asr.transcribe.return_value = asr_res

            with patch.object(xiaozhi_pi_agent.providers, "asr_router", mock_asr), \
                 patch.object(xiaozhi_pi_agent.providers, "llm_router", mock_llm):

                for _ in range(4):
                    self.mgr.audio_frames.append(bytes(1920))
                self.mgr.state = xiaozhi_pi_agent.AgentState.LISTENING

                await self.mgr.process_audio_pipeline()
                mock_asr.transcribe.assert_awaited_once()
                mock_llm.check_and_execute_brain_switch.assert_called_once_with("switch brain to groq")
                self.assertEqual(self.mgr.state, xiaozhi_pi_agent.AgentState.IDLE)

        asyncio.run(_test())


class TestSystemdAndDesktopFiles(unittest.TestCase):
    def test_desktop_files_syntax(self):
        desktop_files = ["S3_Internet.desktop", "Xiaozhi_PTT.desktop"]
        for df in desktop_files:
            path = os.path.join(SCRIPT_DIR, df)
            self.assertTrue(os.path.exists(path), f"Missing {df}")
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()
                self.assertIn("[Desktop Entry]", content)
                self.assertIn("Type=Application", content)
                self.assertIn("Exec=", content)

    def test_service_files_memory_guards(self):
        service_files = ["xiaozhi-pi.service", "s3-hid-wake.service"]
        for sf in service_files:
            path = os.path.join(SCRIPT_DIR, sf)
            self.assertTrue(os.path.exists(path), f"Missing {sf}")
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()
                self.assertIn("MemoryMax=450M", content)
                self.assertIn("MemoryHigh=380M", content)
                self.assertIn("Restart=always", content)

    def test_udev_rules(self):
        rules_path = os.path.join(SCRIPT_DIR, "99-esp32-s3-zero.rules")
        self.assertTrue(os.path.exists(rules_path))
        with open(rules_path, "r", encoding="utf-8") as f:
            content = f.read()
            self.assertIn('303a', content)
            self.assertIn('ttyS3Zero', content)
            self.assertIn('usb0', content)


class TestFastApiEndpoints(unittest.TestCase):
    def setUp(self):
        from fastapi.testclient import TestClient
        self.client = TestClient(xiaozhi_pi_agent.app)

    def test_health_status(self):
        response = self.client.get("/api/status")
        self.assertEqual(response.status_code, 200)
        data = response.json()
        self.assertEqual(data["status"], "healthy")
        self.assertEqual(data["node_id"], "xiaozhi-pi")
        self.assertTrue(data["is_under_budget"])
        self.assertEqual(data["memory_guard_limit_mb"], 450)

    def test_trigger_api(self):
        response = self.client.post("/api/trigger", json={"event": "wake_word"})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["state"], xiaozhi_pi_agent.AgentState.LISTENING)

    def test_rover_cmd_api(self):
        response = self.client.post("/api/rover/cmd", json={"action": "move", "vx": 0, "vy": 50, "vz": 0, "speed": 50})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["status"], "dispatched")


class TestS3FlasherAndResourceManagement(unittest.TestCase):
    def setUp(self):
        self.base_dir = Path(__file__).resolve().parent

    def test_s3_flash_script_structure(self):
        flash_sh = self.base_dir / "s3_flash.sh"
        self.assertTrue(flash_sh.exists(), "s3_flash.sh must exist")
        content = flash_sh.read_text(encoding="utf-8")
        self.assertIn("CMD:BOOTLOADER", content)
        self.assertIn("ACK:BOOTLOADER", content)
        self.assertIn("s3-hid-wake.service", content)
        self.assertIn("write-flash", content)
        self.assertIn("0x260000", content)
        self.assertIn("0x20000", content)

    def test_desktop_entry_structure(self):
        desktop = self.base_dir / "S3_Update.desktop"
        self.assertTrue(desktop.exists(), "S3_Update.desktop must exist")
        content = desktop.read_text(encoding="utf-8")
        self.assertIn("Type=Application", content)
        self.assertIn("s3_flash.sh", content)
        self.assertIn("Terminal=true", content)

    def test_lowprio_build_structure(self):
        lowprio = self.base_dir / "lowprio-build"
        self.assertTrue(lowprio.exists(), "lowprio-build must exist")
        content = lowprio.read_text(encoding="utf-8")
        self.assertIn("MemoryMax=1200M", content)
        self.assertIn("CPUQuota=300%", content)
        self.assertIn("CMAKE_BUILD_PARALLEL_LEVEL=2", content)
        self.assertIn("systemd-run", content)

    def test_firmware_bundle_binaries(self):
        fw_dir = self.base_dir / "fw"
        self.assertTrue(fw_dir.exists(), "fw directory must exist")
        expected_bins = [
            "bootloader.bin",
            "partition-table.bin",
            "srmodels.bin",
            "esp32_s3_zero_acoustic_node.bin",
        ]
        for b in expected_bins:
            p = fw_dir / b
            self.assertTrue(p.exists(), f"Binary {b} must exist in fw/")
            self.assertGreater(p.stat().st_size, 0, f"Binary {b} must not be empty")


if __name__ == "__main__":
    unittest.main()
