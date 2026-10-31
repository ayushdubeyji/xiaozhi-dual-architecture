import asyncio
import io
import os
import struct
import wave
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from config_loader import ConfigManager, load_config, expand_env_vars, load_env_file
from asr_whisper_router import WhisperASRRouter, pcm_to_wav, prepare_audio_payload, create_ogg_opus_stream, transcribe_audio_pcm, transcribe
from llm_router import LLMRouter, LLMResponse, generate_text, query
import asr_whisper_router
import llm_router

class TestProviders(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.base_dir = Path(__file__).resolve().parent
        self.cfg_mgr = ConfigManager(
            config_path=self.base_dir / "data" / ".config.yaml",
            env_path=self.base_dir / ".env",
        )

    def test_01_config_loading_and_expansion(self):
        cfg = self.cfg_mgr.config
        self.assertIn("asr", cfg)
        self.assertIn("llm", cfg)
        self.assertEqual(cfg["asr"]["primary_provider"], "groq")

        groq_key = cfg["asr"]["providers"]["groq"]["api_key"]
        self.assertTrue(groq_key.startswith("gsk_"), f"Expected Groq key starting with gsk_, got: {groq_key[:10]}")

        gemini_primary = cfg["llm"]["providers"]["gemini"]["api_key"]
        gemini_backup = cfg["llm"]["providers"]["gemini"]["backup_api_key"]
        self.assertTrue(gemini_primary.startswith("AQ."), f"Expected Gemini key, got: {gemini_primary[:10]}")
        self.assertTrue(gemini_backup.startswith("AQ."), f"Expected Gemini backup, got: {gemini_backup[:10]}")

        # Test env expansion with default values
        expanded = expand_env_vars("${NONEXISTENT_VAR:-default_val}")
        self.assertEqual(expanded, "default_val")

    def test_02_runtime_key_rotation(self):
        new_test_key = "YOUR_GROQ_API_KEY_HERE"
        self.cfg_mgr.rotate_key("GROQ_API_KEY", new_test_key)
        self.assertEqual(self.cfg_mgr.get("llm.providers.groq.api_key"), new_test_key)

        # Restore original
        self.cfg_mgr.reload()
        restored = self.cfg_mgr.get("llm.providers.groq.api_key")
        self.assertTrue(restored.startswith("gsk_BpkB"))

    def test_03_wav_framing_and_opus(self):
        # 16000Hz * 0.1s = 1600 samples = 3200 bytes of 16-bit PCM
        num_samples = 1600
        raw_pcm = struct.pack("<" + ("h" * num_samples), *([100] * num_samples))
        self.assertEqual(len(raw_pcm), 3200)

        wav_bytes = pcm_to_wav(raw_pcm, sample_rate=16000, channels=1, sample_width=2)
        # RIFF header is 44 bytes, so total size must be 3200 + 44 = 3244
        self.assertEqual(len(wav_bytes), 3244)
        self.assertTrue(wav_bytes.startswith(b"RIFF"))

        # Verify WAV header structure using wave module
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            self.assertEqual(wf.getnchannels(), 1)
            self.assertEqual(wf.getsampwidth(), 2)
            self.assertEqual(wf.getframerate(), 16000)
            self.assertEqual(wf.getnframes(), num_samples)

        # Test odd byte alignment handling (e.g. 3201 bytes should be aligned down to 3200)
        raw_pcm_odd = raw_pcm + b"\x55"
        wav_odd = pcm_to_wav(raw_pcm_odd, sample_rate=16000, channels=1, sample_width=2)
        self.assertEqual(len(wav_odd), 3244)

        # Test prepare_audio_payload
        fname, payload, mime = prepare_audio_payload(raw_pcm)
        self.assertEqual(fname, "audio.wav")
        self.assertEqual(mime, "audio/wav")
        self.assertTrue(payload.startswith(b"RIFF"))

        fname_w, payload_w, mime_w = prepare_audio_payload(wav_bytes)
        self.assertEqual(fname_w, "audio.wav")
        self.assertEqual(payload_w, wav_bytes)

        # Test Ogg Opus encapsulation
        dummy_opus_packet = b"\xf8" + b"\x00" * 40
        ogg_stream = create_ogg_opus_stream([dummy_opus_packet], sample_rate=16000, channels=1)
        self.assertTrue(ogg_stream.startswith(b"OggS"))
        self.assertIn(b"OpusHead", ogg_stream)
        self.assertIn(b"OpusTags", ogg_stream)

        fname_ogg, payload_ogg, mime_ogg = prepare_audio_payload([dummy_opus_packet])
        self.assertEqual(fname_ogg, "audio.ogg")
        self.assertEqual(mime_ogg, "audio/ogg")
        self.assertTrue(payload_ogg.startswith(b"OggS"))

    def test_04_brain_switching_logic(self):
        router = LLMRouter(config_manager=self.cfg_mgr)
        self.assertEqual(router.active_brain, "gemini")

        # Test voice commands
        msg = router.check_and_execute_brain_switch("Please switch to groq now")
        self.assertIsNotNone(msg)
        self.assertEqual(router.active_brain, "groq")
        self.assertIn("Groq", msg)

        msg2 = router.check_and_execute_brain_switch("switch brain to gemini")
        self.assertIsNotNone(msg2)
        self.assertEqual(router.active_brain, "gemini")

        msg3 = router.check_and_execute_brain_switch("use grok")
        self.assertIsNotNone(msg3)
        self.assertEqual(router.active_brain, "grok")

        # Negative tests: informational questions mentioning keywords must NOT switch brain!
        msg_none = router.check_and_execute_brain_switch("What is the weather today?")
        self.assertIsNone(msg_none)

        msg_false_pos = router.check_and_execute_brain_switch("Can you tell me why I should use groq in my app?")
        self.assertIsNone(msg_false_pos, "Informational query should not trigger brain switch")

        msg_false_pos2 = router.check_and_execute_brain_switch("How do I activate groq on my server?")
        self.assertIsNone(msg_false_pos2, "Informational query should not trigger brain switch")

        # Compound command: switch and query
        msg_compound = router.check_and_execute_brain_switch("Switch to Groq and tell me a joke")
        self.assertIsNotNone(msg_compound)
        self.assertEqual(router.active_brain, "groq")
        self.assertEqual(router._pending_query, "tell me a joke")

    def test_05_module_level_helpers_for_pi_agent(self):
        # Verify xiaozhi_pi_agent integration entrypoints exist and are callable
        self.assertTrue(hasattr(asr_whisper_router, "transcribe_audio_pcm"))
        self.assertTrue(callable(asr_whisper_router.transcribe_audio_pcm))

        self.assertTrue(hasattr(asr_whisper_router, "transcribe"))
        self.assertTrue(callable(asr_whisper_router.transcribe))

        self.assertTrue(hasattr(llm_router, "generate_text"))
        self.assertTrue(callable(llm_router.generate_text))

        self.assertTrue(hasattr(llm_router, "query"))
        self.assertTrue(callable(llm_router.query))

    async def test_06_live_groq_whisper_asr(self):
        # Generate 0.5s of 16kHz audio
        num_samples = 8000
        raw_pcm = struct.pack("<" + ("h" * num_samples), *([0] * num_samples))

        async with WhisperASRRouter(config_manager=self.cfg_mgr) as asr:
            result = await asr.transcribe(raw_pcm)
            self.assertTrue(result.success, f"ASR failed: {result.error}")
            self.assertEqual(result.provider, "groq")
            self.assertGreater(result.latency_ms, 0)
            print(f"\n[Live ASR] Provider: {result.provider}, Model: {result.model}, Latency: {result.latency_ms}ms, Text: '{result.text}'")

    async def test_07_live_gemini_llm_and_failover(self):
        async with LLMRouter(config_manager=self.cfg_mgr) as router:
            router.set_brain("gemini")
            response = await router.chat("Say 'Antigravity verified' in 2 words")
            self.assertTrue(response.success, f"Gemini LLM failed: {response.error}")
            self.assertEqual(response.brain, "gemini")
            self.assertGreater(len(response.content), 0)
            print(f"\n[Live Gemini] Model: {response.model}, Latency: {response.latency_ms}ms, Content: '{response.content}'")

            # Verify backup key fallback mechanism
            router.active_gemini_key_type = "primary"
            # Intentionally corrupt primary key in memory
            self.cfg_mgr.set_key("llm.providers.gemini.api_key", "INVALID_PRIMARY_KEY")
            failover_resp = await router.chat("Reply with 'Backup key works'")
            self.assertTrue(failover_resp.success, f"Failover failed: {failover_resp.error}")
            self.assertTrue(failover_resp.key_rotation_occurred or router.active_gemini_key_type == "backup")
            print(f"\n[Gemini Failover] Active Key: {router.active_gemini_key_type}, Content: '{failover_resp.content}'")

    async def test_08_live_groq_llm(self):
        async with LLMRouter(config_manager=self.cfg_mgr) as router:
            router.set_brain("groq")
            response = await router.chat("Say 'Groq online' in 2 words")
            self.assertTrue(response.success, f"Groq LLM failed: {response.error}")
            self.assertEqual(response.brain, "groq")
            self.assertGreater(len(response.content), 0)
            print(f"\n[Live Groq LLM] Model: {response.model}, Latency: {response.latency_ms}ms, Content: '{response.content}'")

if __name__ == "__main__":
    unittest.main()
