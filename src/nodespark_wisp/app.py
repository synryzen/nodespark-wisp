from __future__ import annotations

import queue
import signal
import threading
import time
from typing import Any
from urllib.parse import quote

from .audio import AudioIO, Transcriber
from .config import AppConfig, StateStore
from .display import DeviceDisplay
from .hub import HubClient, HubError
from .system import read_status


class NodeSparkWispApp:
    def __init__(self, cfg: AppConfig, state: StateStore):
        self.cfg = cfg
        self.state = state
        self.display = DeviceDisplay(
            cfg.display.enabled,
            cfg.display.driver_path,
            cfg.display.backlight,
            cfg.display.spi_speed_hz,
        )
        self.audio = AudioIO(cfg.audio.enabled, cfg.audio.sample_rate, cfg.audio.channels, cfg.audio.format)
        self.transcriber = Transcriber(
            cfg.audio.transcription_provider,
            cfg.audio.openai_api_key,
            cfg.audio.openai_model,
            cfg.audio.vosk_model_path,
        )
        self.hub = HubClient(cfg.hub.base_url, state.device_id, cfg.device.name, state.token)
        self.events: queue.Queue[str] = queue.Queue()
        self.running = True
        self.selected_index = 0
        self._pressed_at = 0.0
        self._showcase_index = 0
        self._last_status_footer = ""
        self._last_status_read = 0.0
        self.notifications: list[dict[str, Any]] = []
        self.pending_approval: dict[str, Any] | None = None
        self.ble_bridge = None

    def pair(self, code: str) -> dict[str, Any]:
        response = self.hub.pair(code)
        token = str(response.get("deviceToken", ""))
        if not token:
            raise HubError("Pairing response did not include a device token.")
        self.state.set_pairing(str(response.get("hubId", "")), token, response.get("expiresAt"))
        self.hub.token = token
        return response

    def run_once(self, text: str = "", workflow: str | None = None) -> dict[str, Any]:
        workflow_name = workflow or self.current_workflow()
        payload = {
            "source": "wisp",
            "deviceId": self.state.device_id,
            "deviceName": self.cfg.device.name,
            "text": text,
            "input": text,
            "utterance": text,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        self._animate("Running", workflow_name, (255, 180, 50), seconds=0.45)
        response = self.hub.run_workflow(workflow_name, payload, trace=False)
        output = str(response.get("output") or response.get("status") or "Done")
        status = str(response.get("status", "success"))
        accent = (35, 190, 95) if status.lower() == "success" else (255, 80, 80)
        self.display.show("Done" if status.lower() == "success" else "Run Failed", output[:180], workflow_name, accent, self._status_footer())
        self._chime("success" if status.lower() == "success" else "error")
        if self.cfg.speech.enabled:
            self.audio.speak(output, self.cfg.speech.voice, self.cfg.speech.rate)
        return response

    def daemon(self) -> None:
        self._install_signal_handlers()
        self._install_button_handlers()
        self._start_ble_bridge()

        if self.cfg.display.startup_logo_enabled:
            self.display.show_startup_logo(
                "Starting Wisp client",
                "Hold button to talk",
                self.cfg.display.startup_logo_path,
                self._status_footer(),
            )
            self._chime("startup")
            time.sleep(1.4)
        else:
            self.display.show("NodeSparkHub", "Starting Wisp client...", "Hold button to talk", (60, 130, 255), self._status_footer())

        if not self.state.token:
            self._show_pairing_qr()

        next_checkin = 0.0
        next_command_poll = 0.0
        next_showcase = time.time() + 12
        while self.running:
            now = time.time()
            if now >= next_checkin:
                self._checkin()
                next_checkin = now + max(15, int(self.cfg.device.checkin_interval_seconds))
            if now >= next_command_poll:
                self._poll_commands()
                next_command_poll = now + max(1, int(self.cfg.device.command_poll_interval_seconds))
            if self.cfg.showcase.enabled and now >= next_showcase:
                self._showcase_idle()
                next_showcase = now + max(8, int(self.cfg.showcase.idle_interval_seconds))

            try:
                event = self.events.get(timeout=0.25)
            except queue.Empty:
                continue
            if event == "voice":
                self._voice_command()
            elif event == "cycle":
                self._cycle_workflow()

        self._stop_ble_bridge()
        self.display.cleanup()

    def current_workflow(self) -> str:
        favorites = self.cfg.hub.favorite_workflows or [self.cfg.hub.default_workflow]
        return favorites[self.selected_index % len(favorites)]

    def _checkin(self) -> None:
        try:
            health = self.hub.health()
            checkin = self.hub.checkin()
            caps = ", ".join(health.get("capabilities", [])[:3])
            self.display.show("Connected", f"{self.current_workflow()}\nDevice {checkin.get('deviceId')}", caps, (45, 160, 255), self._status_footer(force=True))
        except Exception as exc:
            self.display.show("Hub Offline", str(exc)[:180], self.cfg.hub.base_url, (255, 70, 70), self._status_footer(force=True))

    def _poll_commands(self) -> None:
        if not self.state.token:
            return
        try:
            for command in self.hub.poll_commands():
                self._execute_command(command)
        except Exception as exc:
            print(f"[commands] poll failed: {exc}")

    def _start_ble_bridge(self) -> None:
        if not self.cfg.bluetooth.enabled:
            return
        try:
            from .ble_bridge import WispBLEBridge
            self.ble_bridge = WispBLEBridge(self)
            if self.ble_bridge.start():
                print("[ble] Wisp Mobile Bridge enabled")
        except Exception as exc:
            print(f"[ble] could not start Wisp Mobile Bridge: {exc}")

    def _stop_ble_bridge(self) -> None:
        if self.ble_bridge is not None:
            try:
                self.ble_bridge.stop()
            except Exception:
                pass

    def _execute_command(self, command: dict[str, Any]) -> None:
        command_id = str(command.get("id", ""))
        kind = str(command.get("type", "display")).strip().lower()
        try:
            if kind in {"display", "displaymessage", "show", "message"}:
                title = str(command.get("title") or "NodeSparkHub")
                body = str(command.get("body") or command.get("text") or "")
                rgb = self._rgb(command, (60, 130, 255))
                self.display.show(title, body, "Sent from NodeSparkHub", rgb, self._status_footer())
                self._chime("success")
                self.hub.ack_command(command_id, "completed", "displayed")
            elif kind in {"card", "alert", "success", "warning", "error", "ai", "voice", "timer", "weather", "statuscard", "promo"}:
                title = str(command.get("title") or self._title_for_kind(kind))
                body = self._command_text(command)
                subtitle = str(command.get("subtitle") or command.get("detail") or "")
                style = str(command.get("style") or self._style_for_kind(kind))
                icon = str(command.get("icon") or self._icon_for_kind(kind))
                rgb = self._rgb(command, self._accent_for_style(style))
                progress = command.get("progress")
                self.display.show_card(title, body, subtitle, icon, style, rgb, self._status_footer(), progress if isinstance(progress, (int, float)) else None)
                self._chime("error" if style == "error" else "success")
                self.hub.ack_command(command_id, "completed", f"{style} card shown")
            elif kind in {"graphic", "graphics", "icons", "icongrid"}:
                title = str(command.get("title") or "NodeSparkHub")
                items = self._items(command) or ["workflow", "ai", "phone", "webhook", "alert", "done"]
                self.display.show_icon_grid(title, items, self._rgb(command, (60, 130, 255)), self._status_footer())
                self._chime("success")
                self.hub.ack_command(command_id, "completed", "graphics shown")
            elif kind in {"dashboard", "metrics", "monitor"}:
                title = str(command.get("title") or "Live Workflow")
                label = str(command.get("metricLabel") or "Status")
                value = str(command.get("metricValue") or command.get("subtitle") or "Online")
                items = self._items(command)
                if not items:
                    payload = command.get("payload") if isinstance(command.get("payload"), dict) else {}
                    items = [f"{key}: {value}" for key, value in list(payload.items())[:5]]
                self.display.show_dashboard(title, label, value, items, self._rgb(command, (45, 160, 255)), self._status_footer())
                self._chime("success")
                self.hub.ack_command(command_id, "completed", "dashboard shown")
            elif kind in {"health", "status", "devicehealth"}:
                status = read_status()
                items = [
                    f"Hub: {self.cfg.hub.base_url}",
                    f"Workflow: {self.current_workflow()}",
                    f"IP: {status.ip or 'offline'}",
                    f"Battery: {status.battery or 'unknown'}",
                    f"Temp: {status.temperature or 'unknown'}",
                ]
                self.display.show_dashboard("Device Health", "Wisp", "Ready", items, self._rgb(command, (35, 190, 95)), self._status_footer(force=True))
                self._chime("success")
                self.hub.ack_command(command_id, "completed", "health shown")
            elif kind in {"notify", "notification", "inbox"}:
                note = {
                    "title": str(command.get("title") or "NodeSparkHub"),
                    "body": self._command_text(command),
                    "createdAt": time.strftime("%H:%M:%S"),
                }
                self.notifications.append(note)
                self.notifications = self.notifications[-10:]
                self.display.show_notification_stack("Notification Center", self.notifications, self._rgb(command, (45, 160, 255)), self._status_footer())
                self._chime("success")
                self.hub.ack_command(command_id, "completed", f"{len(self.notifications)} notifications saved")
            elif kind in {"approval", "approve", "approvalcard", "decision"}:
                self.pending_approval = command
                title = str(command.get("title") or "Approval Needed")
                body = self._command_text(command) or "Review this request."
                choices = self._choices(command) or ["Approve", "Reject"]
                self.display.show_approval(title, body, choices, self._rgb(command, (255, 180, 50)), self._status_footer())
                self._chime("listen")
            elif kind in {"speak", "say", "speaktext", "speech", "speaker", "tts"}:
                text = str(command.get("text") or command.get("body") or "")
                self.display.show("Speaking", text[:180], "NodeSparkHub", (120, 90, 255), self._status_footer())
                self._chime("success")
                if self.cfg.speech.enabled:
                    self.audio.speak(text, self.cfg.speech.voice, self.cfg.speech.rate)
                self.hub.ack_command(command_id, "completed", "spoken")
            elif kind in {"led", "rgb", "setled"}:
                rgb = self._rgb(command, (60, 130, 255))
                self.display.set_rgb(*rgb)
                self._chime("success")
                self.hub.ack_command(command_id, "completed", f"rgb={rgb}")
            elif kind in {"runworkflow", "run", "workflow"}:
                workflow = str(command.get("workflowName") or self.current_workflow())
                payload = command.get("payload") if isinstance(command.get("payload"), dict) else {}
                payload.setdefault("source", "whisplay-command")
                payload.setdefault("deviceId", self.state.device_id)
                self._animate("Hub Command", f"Running {workflow}", (255, 180, 50), seconds=0.6)
                response = self.hub.run_workflow(workflow, payload)
                self._chime("success")
                self.hub.ack_command(command_id, "completed", f"runId={response.get('runId', '')}")
            elif kind in {"assistant", "ask", "askai"}:
                text = self._command_text(command) or "Help me from NodeSpark Wisp."
                self._animate("Wisp Assistant", "Asking NodeSparkHub AI", self._rgb(command, (120, 90, 255)), seconds=0.7)
                try:
                    response = self.hub.ask_assistant(text)
                    reply = str(response.get("reply") or response.get("message") or response.get("error") or "No assistant reply.")
                    ok = bool(response.get("ok", True))
                    self.display.show_card("Wisp Assistant" if ok else "AI Setup Needed", reply[:280], "NodeSparkHub AI", "ai", "ai" if ok else "warning", self._rgb(command, (120, 90, 255)), self._status_footer(force=True))
                    self._chime("success" if ok else "error")
                    self.hub.ack_command(command_id, "completed" if ok else "failed", reply[:500])
                except Exception:
                    workflow = str(command.get("workflowName") or self.current_workflow())
                    payload = {
                        "source": "whisplay-assistant-fallback",
                        "deviceId": self.state.device_id,
                        "text": text,
                        "input": text,
                    }
                    response = self.hub.run_workflow_async(workflow, payload)
                    self.display.show_card("Wisp Assistant", "Direct AI endpoint was unavailable, so the request was sent to a Hub workflow.", "Fallback workflow", "ai", "warning", self._rgb(command, (255, 180, 50)), self._status_footer(force=True))
                    self.hub.ack_command(command_id, "completed", f"workflowRun={response.get('runId', '')}")
            elif kind in {"selectworkflow", "select"}:
                name = str(command.get("workflowName") or "")
                favorites = self.cfg.hub.favorite_workflows or [self.cfg.hub.default_workflow]
                if name in favorites:
                    self.selected_index = favorites.index(name)
                self.display.show("Workflow", self.current_workflow(), "Selected by Hub", (80, 210, 130), self._status_footer())
                self.hub.ack_command(command_id, "completed", self.current_workflow())
            elif kind == "ping":
                self.display.show("Ping", "NodeSparkHub is talking to this device.", self.cfg.device.name, (35, 190, 95), self._status_footer(force=True))
                self._chime("success")
                self.hub.ack_command(command_id, "completed", "pong")
            elif kind in {"splash", "logo", "startup"}:
                subtitle = str(command.get("body") or command.get("text") or "NodeSparkHub physical node")
                self.display.show_startup_logo(subtitle[:80], "Sent from NodeSparkHub", self.cfg.display.startup_logo_path, self._status_footer())
                self._chime("startup")
                self.hub.ack_command(command_id, "completed", "logo")
            elif kind in {"qr", "pairingqr"}:
                qr_data = str(command.get("qrData") or command.get("text") or command.get("body") or "")
                if qr_data:
                    self.display.show_qr(str(command.get("title") or "NodeSparkHub QR"), qr_data, str(command.get("subtitle") or "Scan to open"), self._rgb(command, (0, 190, 255)), self._status_footer(force=True))
                else:
                    self._show_pairing_qr()
                self.hub.ack_command(command_id, "completed", "qr")
            elif kind in {"demo", "showcase", "salesdemo"}:
                self._run_demo_sequence(command)
                self.hub.ack_command(command_id, "completed", "demo played")
            else:
                self.hub.ack_command(command_id, "ignored", f"unknown type {kind}")
        except Exception as exc:
            if command_id:
                try:
                    self.hub.ack_command(command_id, "failed", str(exc)[:500])
                except Exception:
                    pass
            self._chime("error")
            self.display.show("Command Error", str(exc)[:180], kind, (255, 70, 70), self._status_footer())

    def _voice_command(self) -> None:
        try:
            self.display.show_progress("Listening", f"{self.cfg.audio.record_seconds}s recording", 1, self.current_workflow(), (255, 90, 120), self._status_footer())
            self._chime("listen")
            wav = self.audio.record_wav(self.cfg.audio.record_seconds)
            self._animate("Thinking", "Transcribing command", (160, 90, 255), seconds=0.7)
            text = self.transcriber.transcribe(wav)
            if not text:
                text = ""
                self.display.show("No Transcript", "Running workflow with an empty voice payload.", self.current_workflow(), (255, 180, 50), self._status_footer())
            else:
                self.display.show("Heard", text[:160], self.current_workflow(), (60, 180, 255), self._status_footer())
            self.run_once(text=text)
        except Exception as exc:
            self._chime("error")
            self.display.show("Error", str(exc)[:180], self.current_workflow(), (255, 70, 70), self._status_footer())

    def _cycle_workflow(self) -> None:
        favorites = self.cfg.hub.favorite_workflows or [self.cfg.hub.default_workflow]
        self.selected_index = (self.selected_index + 1) % len(favorites)
        self.display.show("Workflow", self.current_workflow(), "Hold button to run", (80, 210, 130), self._status_footer())

    def _resolve_approval(self, approved: bool) -> None:
        command = self.pending_approval
        self.pending_approval = None
        if not command:
            return

        command_id = str(command.get("id", ""))
        result = "approved" if approved else "rejected"
        accent = (35, 190, 95) if approved else (255, 70, 70)
        self.display.show_card(
            "Approved" if approved else "Rejected",
            self._command_text(command)[:150],
            "Decision sent to NodeSparkHub",
            "success" if approved else "error",
            "success" if approved else "error",
            accent,
            self._status_footer(force=True),
        )
        self._chime("success" if approved else "error")

        if command_id:
            self.hub.ack_command(command_id, "completed", result)

        workflow = str(command.get("workflowName") or "")
        if workflow:
            payload = command.get("payload") if isinstance(command.get("payload"), dict) else {}
            payload["source"] = "whisplay-approval"
            payload["deviceId"] = self.state.device_id
            payload["approval"] = result
            try:
                self.hub.run_workflow_async(workflow, payload)
            except Exception as exc:
                print(f"[approval] follow-up workflow failed: {exc}")

    def _run_demo_sequence(self, command: dict[str, Any]) -> None:
        title = str(command.get("title") or "NodeSparkHub")
        body = self._command_text(command) or "Physical workflows are live."
        self.display.show_startup_logo(body[:80], "Demo Mode", self.cfg.display.startup_logo_path, self._status_footer())
        self._chime("startup")
        time.sleep(0.9)
        cards = [
            ("Webhook Received", "Hub caught an event and routed it here.", "webhook", "info", (45, 160, 255)),
            ("AI Thinking", "NodeSparkHub can summarize, decide, and act.", "ai", "ai", (120, 90, 255)),
            ("Real Output", title, "spark", "voice", (255, 90, 205)),
            ("Workflow Done", body, "success", "success", (35, 190, 95)),
        ]
        for card_title, card_body, icon, style, accent in cards:
            self.display.show_card(card_title, card_body, "Mac + iPhone + physical device", icon, style, accent, self._status_footer())
            time.sleep(0.95)

    def _showcase_idle(self) -> None:
        messages = self.cfg.showcase.idle_messages
        if not messages:
            return
        body = messages[self._showcase_index % len(messages)]
        self._showcase_index += 1
        palette = [(60, 130, 255), (255, 90, 205), (80, 210, 130), (255, 180, 50)]
        self.display.show("NodeSparkHub", body, self.current_workflow(), palette[self._showcase_index % len(palette)], self._status_footer())

    def _show_pairing_qr(self) -> None:
        name = quote(self.cfg.device.name, safe="")
        hub = quote(self.cfg.hub.base_url, safe="")
        data = f"nodesparkhub-device://pair?deviceId={self.state.device_id}&name={name}&hub={hub}"
        self.display.show_qr("Pair Device", data, "Use Hub pairing code", (0, 190, 255), self._status_footer(force=True))

    def _animate(self, title: str, body: str, accent: tuple[int, int, int], seconds: float = 0.8) -> None:
        if not self.cfg.display.animation_enabled:
            self.display.show(title, body, self.current_workflow(), accent, self._status_footer())
            return
        end = time.time() + seconds
        phase = 0
        while time.time() < end and self.running:
            self.display.show_progress(title, body, phase, self.current_workflow(), accent, self._status_footer())
            phase += 1
            time.sleep(0.12)

    def _status_footer(self, force: bool = False) -> str:
        if not self.cfg.display.show_status_bar:
            return ""
        now = time.time()
        if force or now - self._last_status_read > 20:
            status = read_status()
            self._last_status_footer = status.footer()
            self._last_status_read = now
        return self._last_status_footer

    def _chime(self, kind: str) -> None:
        if not self.cfg.sound.enabled:
            return
        if kind == "startup" and not self.cfg.sound.startup_chime:
            return
        if kind == "success" and not self.cfg.sound.command_chime:
            return
        if kind == "error" and not self.cfg.sound.error_chime:
            return
        threading.Thread(target=self.audio.chime, args=(kind,), daemon=True).start()

    @staticmethod
    def _rgb(command: dict[str, Any], fallback: tuple[int, int, int]) -> tuple[int, int, int]:
        raw = command.get("rgb")
        if isinstance(raw, list) and len(raw) >= 3:
            try:
                return tuple(max(0, min(255, int(raw[i]))) for i in range(3))  # type: ignore[return-value]
            except Exception:
                return fallback
        return fallback

    @staticmethod
    def _command_text(command: dict[str, Any]) -> str:
        return str(command.get("body") or command.get("text") or command.get("detail") or "")

    @staticmethod
    def _items(command: dict[str, Any]) -> list[str]:
        raw = command.get("items")
        if isinstance(raw, list):
            return [str(item) for item in raw if str(item).strip()]
        payload = command.get("payload") if isinstance(command.get("payload"), dict) else {}
        raw = payload.get("items") if isinstance(payload, dict) else None
        if isinstance(raw, list):
            return [str(item) for item in raw if str(item).strip()]
        return []

    @staticmethod
    def _choices(command: dict[str, Any]) -> list[str]:
        raw = command.get("choices")
        if isinstance(raw, list):
            return [str(item) for item in raw if str(item).strip()]
        return []

    @staticmethod
    def _style_for_kind(kind: str) -> str:
        if kind in {"success"}:
            return "success"
        if kind in {"error"}:
            return "error"
        if kind in {"warning", "alert", "timer"}:
            return "warning"
        if kind in {"ai"}:
            return "ai"
        if kind in {"voice"}:
            return "voice"
        return "info"

    @staticmethod
    def _icon_for_kind(kind: str) -> str:
        mapping = {
            "alert": "alert",
            "success": "success",
            "warning": "warning",
            "error": "error",
            "ai": "ai",
            "voice": "voice",
            "workflow": "spark",
            "timer": "calendar",
            "weather": "home",
        }
        return mapping.get(kind, "spark")

    @staticmethod
    def _title_for_kind(kind: str) -> str:
        mapping = {
            "alert": "Alert",
            "success": "Success",
            "warning": "Warning",
            "error": "Error",
            "ai": "AI Reply",
            "voice": "Voice",
            "workflow": "Workflow",
            "timer": "Timer",
            "weather": "Status",
        }
        return mapping.get(kind, "NodeSparkHub")

    @staticmethod
    def _accent_for_style(style: str) -> tuple[int, int, int]:
        mapping = {
            "success": (35, 190, 95),
            "error": (255, 70, 70),
            "warning": (255, 180, 50),
            "approval": (255, 180, 50),
            "ai": (120, 90, 255),
            "voice": (255, 90, 205),
        }
        return mapping.get(style.lower(), (60, 130, 255))

    def _install_button_handlers(self) -> None:
        def press() -> None:
            self._pressed_at = time.time()

        def release() -> None:
            duration = time.time() - self._pressed_at
            if self.pending_approval is not None:
                self._resolve_approval(approved=duration < 0.8)
            else:
                self.events.put("voice" if duration >= 0.8 else "cycle")

        if not self.display.set_button_handlers(on_press=press, on_release=release):
            print("[buttons] No Wisp button found. Use CLI commands or Ctrl+C to stop.")

    def _install_signal_handlers(self) -> None:
        def stop(_signum, _frame) -> None:
            self.running = False

        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)
