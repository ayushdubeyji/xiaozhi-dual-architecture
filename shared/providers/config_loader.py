import os
import re
from pathlib import Path
from typing import Any, Dict, Optional
import yaml

ENV_VAR_PATTERN = re.compile(r"\$\{([A-Za-z0-9_]+)(?::-([^}]*))?\}")

def load_env_file(env_path: Optional[Path] = None) -> None:
    if env_path is None:
        current_dir = Path(__file__).resolve().parent
        candidate_paths = [
            current_dir / ".env",
            current_dir.parent / ".env",
            current_dir.parent.parent / ".env",
        ]
        for p in candidate_paths:
            if p.exists():
                env_path = p
                break

    if env_path and env_path.exists():
        try:
            from dotenv import load_dotenv
            load_dotenv(dotenv_path=env_path, override=True)
            return
        except ImportError:
            pass

        with open(env_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                k = k.strip()
                v = v.strip()
                if (v.startswith('"') and v.endswith('"')) or (v.startswith("'") and v.endswith("'")):
                    v = v[1:-1]
                elif " #" in v:
                    v = v.split(" #", 1)[0].strip()
                os.environ[k] = v

def expand_env_vars(data: Any) -> Any:
    if isinstance(data, dict):
        return {k: expand_env_vars(v) for k, v in data.items()}
    if isinstance(data, list):
        return [expand_env_vars(item) for item in data]
    if isinstance(data, str):
        def repl(match: re.Match) -> str:
            var_name = match.group(1)
            default_val = match.group(2) if match.group(2) is not None else ""
            val = os.environ.get(var_name)
            if val is not None and val != "":
                return val
            return default_val
        return ENV_VAR_PATTERN.sub(repl, data)
    return data

class ConfigManager:
    _instance: Optional["ConfigManager"] = None

    def __init__(self, config_path: Optional[Path] = None, env_path: Optional[Path] = None):
        self.base_dir = Path(__file__).resolve().parent
        self.env_path = env_path or (self.base_dir / ".env")
        self.config_path = config_path or self._locate_config_path()
        self._raw_config: Dict[str, Any] = {}
        self._config: Dict[str, Any] = {}
        self.reload()

    @classmethod
    def get_instance(cls, config_path: Optional[Path] = None, env_path: Optional[Path] = None) -> "ConfigManager":
        if cls._instance is None:
            cls._instance = ConfigManager(config_path=config_path, env_path=env_path)
        return cls._instance

    @classmethod
    def reset_instance(cls) -> None:
        cls._instance = None

    def _locate_config_path(self) -> Path:
        candidates = [
            self.base_dir / "data" / ".config.yaml",
            self.base_dir / "config.template.yaml",
        ]
        for c in candidates:
            if c.exists():
                return c
        return candidates[0]

    def reload(self) -> Dict[str, Any]:
        load_env_file(self.env_path)
        if self.config_path and self.config_path.exists():
            with open(self.config_path, "r", encoding="utf-8") as f:
                self._raw_config = yaml.safe_load(f) or {}
        else:
            self._raw_config = {}
        self._config = expand_env_vars(self._raw_config)
        return self._config

    def get(self, key_path: str, default: Any = None) -> Any:
        parts = key_path.split(".")
        curr = self._config
        for p in parts:
            if isinstance(curr, dict) and p in curr:
                curr = curr[p]
            else:
                return default
        return curr

    def set_key(self, key_path: str, value: Any) -> None:
        parts = key_path.split(".")
        curr = self._config
        for p in parts[:-1]:
            if p not in curr or not isinstance(curr[p], dict):
                curr[p] = {}
            curr = curr[p]
        curr[parts[-1]] = value

    def rotate_key(self, env_var_name: str, new_value: str) -> None:
        os.environ[env_var_name] = new_value
        self._config = expand_env_vars(self._raw_config)

    @property
    def config(self) -> Dict[str, Any]:
        return self._config

def load_config(config_path: Optional[str] = None, env_path: Optional[str] = None) -> Dict[str, Any]:
    c_path = Path(config_path) if config_path else None
    e_path = Path(env_path) if env_path else None
    manager = ConfigManager.get_instance(config_path=c_path, env_path=e_path)
    return manager.config
