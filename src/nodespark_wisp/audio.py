from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import wave

import requests


class AudioIO:
    def __init__(self, enabled: bool = True, sample_rate: int = 16000, channels: int = 1, sample_format: str = "S16_LE"):
        self.enabled = enabled
        self.sample_rate = sample_rate
        self.channels = channels
        self.sample_format = sample_format
        self.card = self._find_wm8960_card()

    def record_wav(self, seconds: int) -> Path | None:
        if not self.enabled or not shutil.which("arecord"):
            return None
        out = Path(tempfile.gettempdir()) / "nodespark-wisp-command.wav"
        device = self._alsa_device()
        cmd = [
            "arecord",
            "-q",
            "-D",
            device,
            "-f",
            self.sample_format,
            "-r",
            str(self.sample_rate),
            "-c",
            str(self.channels),
            "-d",
            str(seconds),
            str(out),
        ]
        subprocess.run(cmd, check=True)
        return out

    def speak(self, text: str, voice: str = "en-us", rate: int = 165) -> None:
        text = (text or "").strip()
        if not text or not shutil.which("espeak-ng"):
            return
        if self.card and shutil.which("aplay"):
            try:
                espeak = subprocess.Popen(
                    ["espeak-ng", "--stdout", "-v", voice, "-s", str(rate), text[:900]],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                )
                aplay = subprocess.Popen(
                    ["aplay", "-q", "-D", self._alsa_device(), "-"],
                    stdin=espeak.stdout,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                if espeak.stdout:
                    espeak.stdout.close()
                aplay.wait(timeout=30)
                espeak.wait(timeout=30)
                return
            except (OSError, subprocess.TimeoutExpired):
                pass
        cmd = ["espeak-ng", "-v", voice, "-s", str(rate), text[:900]]
        subprocess.run(cmd, check=False)

    def chime(self, kind: str = "success") -> None:
        if not self.enabled:
            return
        notes = {
            "startup": [(660, 0.08), (880, 0.10), (1320, 0.12)],
            "success": [(880, 0.08), (1175, 0.12)],
            "error": [(220, 0.12), (165, 0.18)],
            "listen": [(660, 0.08)],
        }.get(kind, [(880, 0.08)])
        if shutil.which("speaker-test"):
            device_args = ["-D", self._alsa_device()] if self.card else []
            for freq, duration in notes:
                try:
                    subprocess.run(
                        ["speaker-test", "-q", *device_args, "-t", "sine", "-f", str(freq), "-l", "1"],
                        timeout=max(1.0, duration + 0.4),
                        check=False,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                except subprocess.TimeoutExpired:
                    pass
            return
        if shutil.which("espeak-ng"):
            token = {"startup": "doo dee", "success": "ding", "error": "error", "listen": "go"}.get(kind, "ding")
            subprocess.run(["espeak-ng", "-s", "190", token], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def _alsa_device(self) -> str:
        return f"plughw:{self.card},0" if self.card else "default"

    @staticmethod
    def _find_wm8960_card() -> str:
        try:
            cards = Path("/proc/asound/cards").read_text()
        except OSError:
            return ""
        for line in cards.splitlines():
            if "wm8960soundcard" in line.lower():
                return line.split("[", 1)[0].strip()
        return ""


class Transcriber:
    def __init__(self, provider: str = "none", openai_api_key: str = "", openai_model: str = "gpt-4o-mini-transcribe", vosk_model_path: str = ""):
        self.provider = provider.lower().strip()
        self.openai_api_key = openai_api_key or os.environ.get("OPENAI_API_KEY", "")
        self.openai_model = openai_model
        self.vosk_model_path = vosk_model_path

    def transcribe(self, wav_path: Path | None) -> str:
        if not wav_path:
            return ""
        if self.provider == "openai":
            return self._openai(wav_path)
        if self.provider == "vosk":
            return self._vosk(wav_path)
        return ""

    def _openai(self, wav_path: Path) -> str:
        if not self.openai_api_key:
            raise RuntimeError("OPENAI_API_KEY is required for OpenAI transcription.")
        with wav_path.open("rb") as fh:
            resp = requests.post(
                "https://api.openai.com/v1/audio/transcriptions",
                headers={"Authorization": f"Bearer {self.openai_api_key}"},
                data={"model": self.openai_model},
                files={"file": (wav_path.name, fh, "audio/wav")},
                timeout=90,
            )
        if not (200 <= resp.status_code < 300):
            raise RuntimeError(f"OpenAI transcription HTTP {resp.status_code}: {resp.text[:500]}")
        return str(resp.json().get("text", "")).strip()

    def _vosk(self, wav_path: Path) -> str:
        from vosk import KaldiRecognizer, Model  # type: ignore

        model = Model(self.vosk_model_path)
        with wave.open(str(wav_path), "rb") as wf:
            if wf.getnchannels() != 1 or wf.getsampwidth() != 2:
                raise RuntimeError("Vosk requires mono S16_LE WAV. Set audio.channels=1 and audio.format='S16_LE'.")
            recognizer = KaldiRecognizer(model, wf.getframerate())
            chunks: list[str] = []
            while True:
                data = wf.readframes(4000)
                if not data:
                    break
                if recognizer.AcceptWaveform(data):
                    chunks.append(json.loads(recognizer.Result()).get("text", ""))
            chunks.append(json.loads(recognizer.FinalResult()).get("text", ""))
        return " ".join(part for part in chunks if part).strip()
