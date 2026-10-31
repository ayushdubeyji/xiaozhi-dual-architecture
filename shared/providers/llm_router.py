import time
import logging
import re
from dataclasses import dataclass
from typing import Optional, List, Dict, Any, Union
import httpx

try:
    from .config_loader import ConfigManager
except (ImportError, ValueError):
    from config_loader import ConfigManager

logger = logging.getLogger("xiaozhi.llm_router")

@dataclass
class LLMResponse:
    content: str
    brain: str
    model: str
    provider: str
    latency_ms: float
    success: bool
    key_rotation_occurred: bool = False
    error: Optional[str] = None

class LLMRouter:
    def __init__(self, config_manager: Optional[ConfigManager] = None):
        self.cfg_mgr = config_manager or ConfigManager.get_instance()
        self.active_brain: str = self.cfg_mgr.get("llm.default_brain", "gemini")
        self.active_gemini_key_type: str = "primary"  # "primary" or "backup"
        self._active_gemini_model: Optional[str] = None
        self._active_groq_model: Optional[str] = None
        self._pending_query: Optional[str] = None
        self._client: Optional[httpx.AsyncClient] = None

    async def __aenter__(self) -> "LLMRouter":
        self._client = httpx.AsyncClient()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.close()

    def _get_client(self) -> httpx.AsyncClient:
        if self._client is None or self._client.is_closed:
            self._client = httpx.AsyncClient()
        return self._client

    def reset_key_state(self) -> None:
        self.active_gemini_key_type = "primary"
        self._active_gemini_model = None
        self._active_groq_model = None

    def set_brain(self, brain: str) -> bool:
        known_brains = ["gemini", "groq", "grok"]
        clean_brain = brain.lower().strip()
        if clean_brain in known_brains:
            self.active_brain = clean_brain
            logger.info(f"Active brain switched to: {clean_brain}")
            return True
        return False

    def check_and_execute_brain_switch(self, text: str) -> Optional[str]:
        if not text:
            return None
        self._pending_query = None
        cleaned = re.sub(r"[^\w\s]", "", text).lower().strip()

        # Regex for imperative brain switch commands
        m = re.match(
            r"^(?:please\s+)?(?:switch|change|set|activate|use)(?:\s+(?:the|my))?(?:\s+(?:brain|model|ai))?(?:\s+to)?\s+(gemini|groq|grok|llama)\b(?:\s+(?:mode|now))?(?:[,\s]+(?:and\s+|then\s+)?(.*))?$",
            cleaned,
            re.IGNORECASE,
        )
        if not m:
            m = re.match(r"^(gemini|groq|grok|llama)\s+mode(?:[,\s]+(?:and\s+|then\s+)?(.*))?$", cleaned, re.IGNORECASE)

        if m:
            target_brain = m.group(1).lower()
            if target_brain == "llama":
                target_brain = "groq"
            remainder = (m.group(2) or "").strip()
            if remainder:
                self._pending_query = remainder
            if self.set_brain(target_brain):
                model_name = self.cfg_mgr.get(f"llm.providers.{target_brain}.model", target_brain)
                return f"Brain switched to {target_brain.capitalize()} ({model_name})."

        # Also support exact match with configured patterns if query is solely the phrase
        patterns = self.cfg_mgr.get("brain_switcher.patterns", {})
        for brain_name, phrase_list in patterns.items():
            for phrase in phrase_list:
                cleaned_phrase = re.sub(r"[^\w\s]", "", phrase).lower().strip()
                if cleaned == cleaned_phrase or cleaned == f"please {cleaned_phrase}" or cleaned == f"{cleaned_phrase} now":
                    if self.set_brain(brain_name):
                        model_name = self.cfg_mgr.get(f"llm.providers.{brain_name}.model", brain_name)
                        return f"Brain switched to {brain_name.capitalize()} ({model_name})."
        return None

    async def chat(
        self,
        prompt: Union[str, List[Dict[str, str]]],
        brain: Optional[str] = None,
        system_prompt: Optional[str] = None,
        _attempted_brains: Optional[set] = None,
    ) -> LLMResponse:
        selected_brain = (brain or self.active_brain).lower()

        # Format input messages
        if isinstance(prompt, str):
            messages = [{"role": "user", "content": prompt}]
        else:
            messages = list(prompt)

        # Check voice command switch on the latest user input
        if messages and messages[-1].get("role") == "user":
            user_text = messages[-1]["content"]
            switch_msg = self.check_and_execute_brain_switch(user_text)
            if switch_msg:
                if self._pending_query:
                    messages[-1]["content"] = self._pending_query
                    self._pending_query = None
                    selected_brain = self.active_brain
                else:
                    return LLMResponse(
                        content=switch_msg,
                        brain=self.active_brain,
                        model="system-switcher",
                        provider="internal",
                        latency_ms=1.0,
                        success=True,
                    )

        if _attempted_brains is None:
            _attempted_brains = set()
        _attempted_brains.add(selected_brain)

        cfg = self.cfg_mgr.config.get("llm", {})
        timeout_sec = float(cfg.get("timeout_seconds", 25.0))
        temperature = float(cfg.get("temperature", 0.7))
        max_tokens = int(cfg.get("max_tokens", 1024))
        providers_cfg = cfg.get("providers", {})

        if selected_brain == "gemini":
            resp = await self._call_gemini(messages, system_prompt, providers_cfg.get("gemini", {}), temperature, max_tokens, timeout_sec)
        elif selected_brain == "groq":
            resp = await self._call_groq(messages, system_prompt, providers_cfg.get("groq", {}), temperature, max_tokens, timeout_sec)
        elif selected_brain == "grok":
            resp = await self._call_grok(messages, system_prompt, providers_cfg.get("grok", {}), temperature, max_tokens, timeout_sec)
        else:
            resp = LLMResponse(
                content="",
                brain=selected_brain,
                model="unknown",
                provider="none",
                latency_ms=0.0,
                success=False,
                error=f"Unsupported brain: {selected_brain}",
            )

        # Fallback to secondary brains without infinite recursion
        if not resp.success:
            fallback_candidates = [
                cfg.get("fallback_brain", "groq"),
                "groq",
                "gemini",
                "grok",
            ]
            next_brain = next((b for b in fallback_candidates if b not in _attempted_brains), None)
            if next_brain:
                logger.warning(f"Brain '{selected_brain}' failed ({resp.error}). Falling back to '{next_brain}'...")
                return await self.chat(messages, brain=next_brain, system_prompt=system_prompt, _attempted_brains=_attempted_brains)

        return resp

    async def _call_gemini(
        self,
        messages: List[Dict[str, str]],
        system_prompt: Optional[str],
        gemini_cfg: Dict[str, Any],
        temperature: float,
        max_tokens: int,
        timeout_sec: float,
    ) -> LLMResponse:
        primary_key = gemini_cfg.get("api_key", "")
        backup_key = gemini_cfg.get("backup_api_key", "")

        models_to_try = []
        if self._active_gemini_model:
            models_to_try.append(self._active_gemini_model)
        for m in [
            gemini_cfg.get("model", "gemini-2.5-flash"),
            gemini_cfg.get("fallback_model", "gemini-3.5-flash-lite"),
            "gemini-3.8-flash",
            "gemini-flash-latest",
        ]:
            if m and m not in models_to_try:
                models_to_try.append(m)

        base_url = gemini_cfg.get("base_url", "https://generativelanguage.googleapis.com/v1beta").rstrip("/")

        # Convert OpenAI-style messages to Gemini generateContent contents
        contents = []
        extracted_sys = system_prompt or ""
        for m in messages:
            role = m.get("role", "user")
            text = m.get("content", "")
            if role == "system":
                extracted_sys += ("\n" + text if extracted_sys else text)
            elif role == "assistant":
                contents.append({"role": "model", "parts": [{"text": text}]})
            else:
                contents.append({"role": "user", "parts": [{"text": text}]})

        if not contents:
            contents = [{"role": "user", "parts": [{"text": "Hello"}]}]

        req_payload: Dict[str, Any] = {
            "contents": contents,
            "generationConfig": {
                "temperature": temperature,
                "maxOutputTokens": max_tokens,
            },
        }
        if extracted_sys:
            req_payload["systemInstruction"] = {"parts": [{"text": extracted_sys}]}

        keys_to_attempt = []
        if self.active_gemini_key_type == "primary":
            if primary_key:
                keys_to_attempt.append(("primary", primary_key))
            if backup_key and backup_key != primary_key:
                keys_to_attempt.append(("backup", backup_key))
        else:
            if backup_key:
                keys_to_attempt.append(("backup", backup_key))
            if primary_key and primary_key != backup_key:
                keys_to_attempt.append(("primary", primary_key))

        client = self._get_client()
        last_error = "No keys configured"
        rotation_occurred = False

        start_t = time.perf_counter()
        for key_type, key in keys_to_attempt:
            for model in models_to_try:
                url = f"{base_url}/models/{model}:generateContent?key={key.strip()}"
                try:
                    resp = await client.post(url, json=req_payload, timeout=timeout_sec)
                    if resp.status_code == 200:
                        data = resp.json()
                        candidates = data.get("candidates", [])
                        if candidates and "content" in candidates[0] and "parts" in candidates[0]["content"]:
                            text_reply = candidates[0]["content"]["parts"][0].get("text", "")
                            latency = (time.perf_counter() - start_t) * 1000.0
                            if key_type != self.active_gemini_key_type:
                                self.active_gemini_key_type = key_type
                                rotation_occurred = True
                            self._active_gemini_model = model
                            return LLMResponse(
                                content=text_reply.strip(),
                                brain="gemini",
                                model=model,
                                provider="google",
                                latency_ms=round(latency, 2),
                                success=True,
                                key_rotation_occurred=rotation_occurred,
                            )
                    elif resp.status_code in (401, 403, 429) or (
                        resp.status_code == 400
                        and any(kw in resp.text.lower() for kw in ("api_key", "key not valid", "invalid_argument", "api key"))
                    ):
                        last_error = f"HTTP {resp.status_code} ({key_type} key error): {resp.text}"
                        logger.warning(f"Gemini {key_type} key failed: {last_error}. Switching key...")
                        break  # Break out to next key immediately
                    elif resp.status_code == 404:
                        last_error = f"Model {model} returned 404, falling back to next candidate"
                        continue
                    else:
                        last_error = f"HTTP {resp.status_code}: {resp.text}"
                except Exception as e:
                    last_error = f"Gemini request exception with {key_type} key: {e}"
                    logger.warning(last_error)
                    break

        latency = (time.perf_counter() - start_t) * 1000.0
        return LLMResponse(
            content="",
            brain="gemini",
            model=models_to_try[0] if models_to_try else "gemini",
            provider="google",
            latency_ms=round(latency, 2),
            success=False,
            error=last_error,
        )

    async def _call_groq(
        self,
        messages: List[Dict[str, str]],
        system_prompt: Optional[str],
        groq_cfg: Dict[str, Any],
        temperature: float,
        max_tokens: int,
        timeout_sec: float,
    ) -> LLMResponse:
        api_key = (groq_cfg.get("api_key") or "").strip()
        if not api_key:
            return LLMResponse(content="", brain="groq", model="none", provider="groq", latency_ms=0.0, success=False, error="GROQ_API_KEY missing")

        base_url = (groq_cfg.get("base_url") or "https://api.groq.com/openai/v1").rstrip("/")
        models_to_try = []
        if self._active_groq_model:
            models_to_try.append(self._active_groq_model)
        for m in [
            groq_cfg.get("model", "llama-3.3-70b-versatile"),
            groq_cfg.get("fallback_model", "openai/gpt-oss-120b"),
            "openai/gpt-oss-20b",
            "qwen/qwen3.8-27b",
        ]:
            if m and m not in models_to_try:
                models_to_try.append(m)

        chat_messages = []
        if system_prompt:
            chat_messages.append({"role": "system", "content": system_prompt})
        chat_messages.extend(messages)

        client = self._get_client()
        url = f"{base_url}/chat/completions"
        headers = {"Authorization": f"Bearer {api_key}"}

        start_t = time.perf_counter()
        last_error = "Unknown"
        for model in models_to_try:
            payload = {
                "model": model,
                "messages": chat_messages,
                "temperature": temperature,
                "max_tokens": max_tokens,
            }
            try:
                resp = await client.post(url, headers=headers, json=payload, timeout=timeout_sec)
                if resp.status_code == 200:
                    data = resp.json()
                    content = data["choices"][0]["message"]["content"]
                    latency = (time.perf_counter() - start_t) * 1000.0
                    self._active_groq_model = model
                    return LLMResponse(
                        content=content.strip(),
                        brain="groq",
                        model=model,
                        provider="groq",
                        latency_ms=round(latency, 2),
                        success=True,
                    )
                elif resp.status_code in (404, 400) and any(kw in resp.text.lower() for kw in ("model_not_found", "not exist", "does not exist")):
                    last_error = f"Model {model} unavailable on Groq ({resp.text}), trying fallback"
                    continue
                else:
                    last_error = f"HTTP {resp.status_code}: {resp.text}"
                    break
            except Exception as e:
                last_error = f"Groq exception: {e}"
                break

        latency = (time.perf_counter() - start_t) * 1000.0
        return LLMResponse(
            content="",
            brain="groq",
            model=models_to_try[0] if models_to_try else "groq",
            provider="groq",
            latency_ms=round(latency, 2),
            success=False,
            error=last_error,
        )

    async def _call_grok(
        self,
        messages: List[Dict[str, str]],
        system_prompt: Optional[str],
        grok_cfg: Dict[str, Any],
        temperature: float,
        max_tokens: int,
        timeout_sec: float,
    ) -> LLMResponse:
        api_key = (grok_cfg.get("api_key") or "").strip()
        if not api_key:
            return LLMResponse(content="", brain="grok", model="grok-2-latest", provider="xai", latency_ms=0.0, success=False, error="XAI_GROK_API_KEY missing")

        base_url = (grok_cfg.get("base_url") or "https://api.x.ai/v1").rstrip("/")
        model = grok_cfg.get("model", "grok-2-latest")

        chat_messages = []
        if system_prompt:
            chat_messages.append({"role": "system", "content": system_prompt})
        chat_messages.extend(messages)

        client = self._get_client()
        url = f"{base_url}/chat/completions"
        headers = {"Authorization": f"Bearer {api_key}"}
        payload = {
            "model": model,
            "messages": chat_messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
        }

        start_t = time.perf_counter()
        try:
            resp = await client.post(url, headers=headers, json=payload, timeout=timeout_sec)
            resp.raise_for_status()
            data = resp.json()
            content = data["choices"][0]["message"]["content"]
            latency = (time.perf_counter() - start_t) * 1000.0
            return LLMResponse(
                content=content.strip(),
                brain="grok",
                model=model,
                provider="xai",
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
            return LLMResponse(
                content="",
                brain="grok",
                model=model,
                provider="xai",
                latency_ms=round(latency, 2),
                success=False,
                error=f"xAI Grok error: {err_detail}",
            )

    async def generate_text(self, prompt: str, brain: Optional[str] = None, system_prompt: Optional[str] = None) -> str:
        res = await self.chat(prompt=prompt, brain=brain, system_prompt=system_prompt)
        return res.content if res.success else ""

    async def query(self, prompt: str, brain: Optional[str] = None, system_prompt: Optional[str] = None) -> str:
        return await self.generate_text(prompt=prompt, brain=brain, system_prompt=system_prompt)

    async def close(self) -> None:
        if self._client and not self._client.is_closed:
            await self._client.aclose()
            self._client = None

async def generate_text(prompt: str, brain: Optional[str] = None, system_prompt: Optional[str] = None) -> str:
    router = LLMRouter()
    try:
        res = await router.chat(prompt=prompt, brain=brain, system_prompt=system_prompt)
        return res.content if res.success else ""
    finally:
        await router.close()

async def query(prompt: str, brain: Optional[str] = None, system_prompt: Optional[str] = None) -> str:
    return await generate_text(prompt=prompt, brain=brain, system_prompt=system_prompt)
