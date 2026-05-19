from __future__ import annotations

from dataclasses import dataclass
from typing import Any
from urllib.parse import quote

import requests

from . import __version__


class HubError(RuntimeError):
    pass


@dataclass
class HubClient:
    base_url: str
    device_id: str
    device_name: str
    token: str = ""
    timeout: float = 20.0

    def __post_init__(self) -> None:
        self.base_url = self.base_url.rstrip("/")
        self.session = requests.Session()

    def health(self) -> dict[str, Any]:
        return self._request("GET", "/health", auth=False)

    def pair(self, code: str, platform: str = "Raspberry Pi Zero 2 W / NodeSpark Wisp") -> dict[str, Any]:
        payload = {
            "code": code.strip(),
            "deviceId": self.device_id,
            "deviceName": self.device_name,
            "platform": platform,
            "osVersion": _linux_pretty_name(),
            "appVersion": f"nodespark-wisp/{__version__}",
        }
        return self._request("POST", "/pair", json=payload, auth=False)

    def checkin(self) -> dict[str, Any]:
        payload = {
            "deviceId": self.device_id,
            "name": self.device_name,
            "platform": "Raspberry Pi Zero 2 W / NodeSpark Wisp",
            "osVersion": _linux_pretty_name(),
            "appVersion": f"nodespark-wisp/{__version__}",
            "capabilities": [
                "run",
                "workflows",
                "deviceActions",
                "deviceCommands",
                "pairing",
                "display",
                "speaker",
                "microphone",
                "button",
                "rgb",
                "qr",
                "approval",
                "dashboard",
                "assistant",
                "voice",
                "volume",
                "health",
                "icons",
                "showcase",
                "storage",
                "mobileBridge",
            ],
        }
        return self._request("POST", "/devices/checkin", json=payload)

    def list_workflows(self) -> list[str]:
        response = self._request("GET", "/workflows")
        workflows = response.get("workflows", [])
        if not isinstance(workflows, list):
            raise HubError("Hub returned an unexpected workflows response.")
        return [str(item) for item in workflows]

    def run_workflow(self, workflow: str, payload: dict[str, Any], trace: bool = False) -> dict[str, Any]:
        encoded = quote(workflow, safe="")
        query = "?trace=1" if trace else ""
        return self._request("POST", f"/workflows/{encoded}/run{query}", json=payload)

    def run_workflow_async(self, workflow: str, payload: dict[str, Any]) -> dict[str, Any]:
        encoded = quote(workflow, safe="")
        return self._request("POST", f"/workflows/{encoded}/run?async=1", json=payload)

    def ask_assistant(self, text: str) -> dict[str, Any]:
        payload = {
            "deviceId": self.device_id,
            "deviceName": self.device_name,
            "text": text,
            "source": "wisp-whisplay",
            "platform": "Raspberry Pi Zero 2 W / PiSugar Whisplay",
            "sessionId": f"whisplay:{self.device_id}",
            "voice": True,
            "capabilities": ["display", "speaker", "microphone", "button", "rgb", "assistant", "workflow"],
        }
        return self._request("POST", "/wisp/assistant", json=payload)

    def run_status(self, run_id: str) -> dict[str, Any]:
        return self._request("GET", f"/runs/{quote(run_id, safe='')}/status")

    def run_result(self, run_id: str) -> dict[str, Any]:
        return self._request("GET", f"/runs/{quote(run_id, safe='')}/result")

    def poll_commands(self, limit: int = 10) -> list[dict[str, Any]]:
        response = self._request("GET", f"/devices/{quote(self.device_id, safe='')}/commands/poll?limit={limit}")
        commands = response.get("commands", [])
        if isinstance(commands, list):
            return [item for item in commands if isinstance(item, dict)]
        return []

    def ack_command(self, command_id: str, status: str = "completed", result: str = "") -> dict[str, Any]:
        payload = {"status": status, "result": result}
        return self._request("POST", f"/devices/{quote(self.device_id, safe='')}/commands/{quote(command_id, safe='')}/ack", json=payload)

    def _request(self, method: str, path: str, json: Any | None = None, auth: bool = True) -> dict[str, Any]:
        if not self.base_url:
            raise HubError("Hub base_url is not configured.")
        headers = {
            "Accept": "application/json",
            "User-Agent": f"nodespark-wisp/{__version__}",
            "X-NodeSparkHub-Device-ID": self.device_id,
            "X-NodeSparkHub-Device-Name": self.device_name,
        }
        if json is not None:
            headers["Content-Type"] = "application/json"
        if auth and self.token:
            headers["Authorization"] = f"Bearer {self.token}"
            headers["X-NodeSparkHub-Token"] = self.token

        url = f"{self.base_url}{path}"
        try:
            resp = self.session.request(method, url, json=json, headers=headers, timeout=self.timeout)
        except requests.RequestException as exc:
            raise HubError(f"Could not reach NodeSparkHub at {url}: {exc}") from exc

        text = resp.text.strip()
        if not (200 <= resp.status_code < 300):
            raise HubError(f"NodeSparkHub HTTP {resp.status_code}: {text[:500]}")
        if not text:
            return {}
        try:
            data = resp.json()
        except ValueError as exc:
            raise HubError(f"NodeSparkHub returned non-JSON: {text[:500]}") from exc
        if isinstance(data, dict):
            return data
        return {"value": data}


def _linux_pretty_name() -> str:
    try:
        for line in open("/etc/os-release", encoding="utf-8"):
            if line.startswith("PRETTY_NAME="):
                return line.split("=", 1)[1].strip().strip('"')
    except OSError:
        pass
    return "Linux"
