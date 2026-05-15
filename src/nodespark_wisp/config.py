from __future__ import annotations

from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import uuid

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    import tomli as tomllib


APP_DIR = "nodespark-wisp"
LEGACY_APP_DIR = "nodespark-whisplay"


@dataclass
class HubConfig:
    base_url: str = ""
    default_workflow: str = "Wisp Assistant"
    favorite_workflows: list[str] = field(default_factory=list)


@dataclass
class DeviceConfig:
    name: str = "NodeSpark Wisp"
    checkin_interval_seconds: int = 60
    command_poll_interval_seconds: int = 2


@dataclass
class DisplayConfig:
    enabled: bool = True
    backlight: int = 65
    driver_path: str = "/opt/Whisplay/Driver"
    spi_speed_hz: int = 48000000
    startup_logo_enabled: bool = True
    startup_logo_path: str = ""
    show_status_bar: bool = True
    animation_enabled: bool = True


@dataclass
class ShowcaseConfig:
    enabled: bool = True
    idle_interval_seconds: int = 18
    idle_messages: list[str] = field(default_factory=lambda: [
        "Ask NodeSparkHub to run automations from a physical device.",
        "Use workflows to update this screen, speak, and trigger actions.",
        "Short press changes workflow. Hold to talk.",
    ])


@dataclass
class AudioConfig:
    enabled: bool = True
    record_seconds: int = 5
    sample_rate: int = 16000
    channels: int = 1
    format: str = "S16_LE"
    transcription_provider: str = "none"
    openai_model: str = "gpt-4o-mini-transcribe"
    openai_api_key: str = ""
    vosk_model_path: str = "/opt/nodespark-wisp/models/vosk"


@dataclass
class SpeechConfig:
    enabled: bool = True
    voice: str = "en-us"
    rate: int = 165


@dataclass
class SoundConfig:
    enabled: bool = True
    startup_chime: bool = True
    command_chime: bool = True
    error_chime: bool = True


@dataclass
class BluetoothConfig:
    enabled: bool = False
    device_name: str = "NodeSpark Wisp"
    service_uuid: str = "4E530001-4E53-5749-5350-000000000001"
    command_characteristic_uuid: str = "4E530002-4E53-5749-5350-000000000001"
    event_characteristic_uuid: str = "4E530003-4E53-5749-5350-000000000001"
    state_characteristic_uuid: str = "4E530004-4E53-5749-5350-000000000001"


@dataclass
class AppConfig:
    hub: HubConfig = field(default_factory=HubConfig)
    device: DeviceConfig = field(default_factory=DeviceConfig)
    display: DisplayConfig = field(default_factory=DisplayConfig)
    showcase: ShowcaseConfig = field(default_factory=ShowcaseConfig)
    audio: AudioConfig = field(default_factory=AudioConfig)
    speech: SpeechConfig = field(default_factory=SpeechConfig)
    sound: SoundConfig = field(default_factory=SoundConfig)
    bluetooth: BluetoothConfig = field(default_factory=BluetoothConfig)


def default_config_paths() -> list[Path]:
    return [
        Path.cwd() / "config.toml",
        Path.home() / ".config" / APP_DIR / "config.toml",
        Path("/etc") / APP_DIR / "config.toml",
        Path.home() / ".config" / LEGACY_APP_DIR / "config.toml",
        Path("/etc") / LEGACY_APP_DIR / "config.toml",
    ]


def state_path() -> Path:
    override = os.environ.get("NODESPARK_WISP_STATE") or os.environ.get("NODESPARK_WHISPLAY_STATE")
    if override:
        return Path(override)
    return Path.home() / ".local" / "share" / APP_DIR / "state.json"


def load_config(path: str | None = None) -> AppConfig:
    raw: dict = {}
    candidates = [Path(path)] if path else default_config_paths()
    for candidate in candidates:
        if candidate.exists():
            with candidate.open("rb") as fh:
                raw = tomllib.load(fh)
            break

    cfg = AppConfig()
    _merge_dataclass(cfg.hub, raw.get("hub", {}))
    _merge_dataclass(cfg.device, raw.get("device", {}))
    _merge_dataclass(cfg.display, raw.get("display", {}))
    _merge_dataclass(cfg.showcase, raw.get("showcase", {}))
    _merge_dataclass(cfg.audio, raw.get("audio", {}))
    _merge_dataclass(cfg.speech, raw.get("speech", {}))
    _merge_dataclass(cfg.sound, raw.get("sound", {}))
    _merge_dataclass(cfg.bluetooth, raw.get("bluetooth", {}))

    cfg.hub.base_url = os.environ.get("NODESPARK_HUB_URL", cfg.hub.base_url).rstrip("/")
    cfg.audio.openai_api_key = os.environ.get("OPENAI_API_KEY", cfg.audio.openai_api_key)
    return cfg


def _merge_dataclass(obj: object, values: dict) -> None:
    for key, value in values.items():
        if hasattr(obj, key):
            setattr(obj, key, value)


class StateStore:
    def __init__(self, path: Path | None = None):
        self.path = path or state_path()
        self.data = self._load()
        if not self.data.get("device_id"):
            self.data["device_id"] = str(uuid.uuid4())
            self.save()

    @property
    def device_id(self) -> str:
        return self.data["device_id"]

    @property
    def token(self) -> str:
        return self.data.get("device_token", "")

    @token.setter
    def token(self, value: str) -> None:
        self.data["device_token"] = value
        self.save()

    def set_pairing(self, hub_id: str, token: str, expires_at: str | None = None) -> None:
        self.data["hub_id"] = hub_id
        self.data["device_token"] = token
        self.data["token_expires_at"] = expires_at
        self.save()

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.data, indent=2, sort_keys=True) + "\n")
        try:
            self.path.chmod(0o600)
        except OSError:
            pass

    def _load(self) -> dict:
        if not self.path.exists():
            return {}
        try:
            return json.loads(self.path.read_text())
        except Exception:
            return {}
