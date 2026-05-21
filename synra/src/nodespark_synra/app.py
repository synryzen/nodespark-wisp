from __future__ import annotations

import threading
import time
from typing import Any

from .config import AppConfig, StateStore
from .hub import HubClient
from .state import SynraStateMachine


class SynraApp:
    def __init__(self, cfg: AppConfig, store: StateStore):
        self.cfg = cfg
        self.store = store
        self.state = SynraStateMachine()
        self.hub = HubClient(cfg.hub.base_url, store.device_id, cfg.device.name, store.token)
        self.running = False
        self._thread: threading.Thread | None = None

    def start_background(self) -> None:
        if self.running:
            return
        self.running = True
        self._thread = threading.Thread(target=self._loop, name="synra-hub-loop", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self.running = False
        if self._thread:
            self._thread.join(timeout=2)

    def pair(self, code: str) -> dict[str, Any]:
        response = self.hub.pair(code)
        token = str(response.get("deviceToken", ""))
        if token:
            self.store.set_pairing(str(response.get("hubId", "")), token, response.get("expiresAt"))
            self.hub.token = token
        return response

    def handle_command(self, command: dict[str, Any], ack: bool = False) -> dict[str, Any]:
        command_id = str(command.get("id") or command.get("commandId") or "")
        kind = str(command.get("type", "showCard")).strip().lower()

        if kind in {"assistant", "ask", "askai"}:
            text = str(command.get("text") or command.get("body") or "Help me from NodeSpark Synra.")
            self.state.set_state({
                "mode": "thinking",
                "expression": "focused",
                "message": f"Thinking about: {text}",
                "subtitle": "Synra Assistant",
                "card": {
                    "title": "Voice Request",
                    "body": text,
                    "detail": "Sending to NodeSparkHub",
                    "style": "thinking",
                },
            })
            if not self.hub.configured():
                result = "NodeSparkHub URL is not configured. Set hub.base_url in config.toml."
                self.state.apply_command({"type": "error", "text": result, "id": command_id})
            else:
                try:
                    response = self.hub.ask_assistant(text)
                    reply = str(response.get("displayText") or response.get("reply") or response.get("message") or "No assistant reply.")
                    self.state.apply_command({
                        "type": "speak",
                        "title": "Synra",
                        "text": reply,
                        "subtitle": "NodeSparkHub AI",
                        "id": command_id,
                    })
                    result = reply[:240]
                except Exception as exc:
                    self.state.apply_command({"type": "error", "text": str(exc), "id": command_id})
                    result = str(exc)
        elif kind in {"runworkflow", "run", "workflow"} and self.hub.configured():
            workflow = str(command.get("workflowName") or command.get("workflow") or self.cfg.hub.default_workflow)
            self.state.apply_command({**command, "workflowName": workflow})
            payload = command.get("payload") if isinstance(command.get("payload"), dict) else {}
            payload.setdefault("source", "synra")
            payload.setdefault("deviceId", self.store.device_id)
            try:
                response = self.hub.run_workflow_async(workflow, payload)
                result = f"runId={response.get('runId', '')}"
            except Exception as exc:
                self.state.apply_command({"type": "error", "text": str(exc), "id": command_id})
                result = str(exc)
        else:
            result, _snapshot = self.state.apply_command(command)

        if ack and command_id and self.hub.configured():
            try:
                self.hub.ack_command(command_id, "completed", result)
            except Exception as exc:
                print(f"[hub] command ack failed: {exc}")

        return {"ok": True, "result": result, "state": self.state.snapshot()}

    def _loop(self) -> None:
        next_checkin = 0.0
        next_poll = 0.0
        while self.running:
            now = time.time()
            if self.hub.configured() and now >= next_checkin:
                self._checkin()
                next_checkin = now + max(15, int(self.cfg.device.checkin_interval_seconds))
            if self.hub.configured() and self.hub.token and now >= next_poll:
                self._poll_commands()
                next_poll = now + max(1, int(self.cfg.device.command_poll_interval_seconds))
            time.sleep(0.25)

    def _checkin(self) -> None:
        try:
            self.hub.checkin()
        except Exception as exc:
            print(f"[hub] checkin failed: {exc}")

    def _poll_commands(self) -> None:
        try:
            commands = self.hub.poll_commands()
        except Exception as exc:
            print(f"[hub] command poll failed: {exc}")
            return
        for command in commands:
            self.handle_command(command, ack=True)
