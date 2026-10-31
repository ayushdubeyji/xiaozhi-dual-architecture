"""Xiaozhi Unified Cloud ASR & Multi-LLM Provider Package.

Provides low-latency speech-to-text (Whisper on Groq/OpenAI) and hot-swappable
multi-LLM reasoning (Gemini with failover, Groq LLM, xAI Grok) with dynamic brain switching.
"""

from .config_loader import ConfigManager, load_config, load_env_file, expand_env_vars
from .asr_whisper_router import (
    WhisperASRRouter,
    ASRResult,
    pcm_to_wav,
    prepare_audio_payload,
    create_ogg_opus_stream,
    transcribe_audio_pcm,
    transcribe,
)
from .llm_router import LLMRouter, LLMResponse, generate_text, query

__all__ = [
    "ConfigManager",
    "load_config",
    "load_env_file",
    "expand_env_vars",
    "WhisperASRRouter",
    "ASRResult",
    "pcm_to_wav",
    "prepare_audio_payload",
    "create_ogg_opus_stream",
    "transcribe_audio_pcm",
    "transcribe",
    "LLMRouter",
    "LLMResponse",
    "generate_text",
    "query",
]
