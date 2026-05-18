from __future__ import annotations

import asyncio
import json
import threading
import time
from typing import Any


class WispBLEBridge:
    """Optional Bluetooth LE peripheral bridge for NodeSpark on iPhone.

    The normal Wi-Fi/Hub path remains the primary transport. This bridge is an
    optional local control channel that lets the iPhone send JSON commands to
    Wisp while mobile.
    """

    def __init__(self, app):
        self.app = app
        self.cfg = app.cfg.bluetooth
        self.thread: threading.Thread | None = None
        self.loop: asyncio.AbstractEventLoop | None = None
        self.server = None
        self.running = False

    def start(self) -> bool:
        if not self.cfg.enabled:
            return False
        if self.thread and self.thread.is_alive():
            return True

        self.running = True
        self.thread = threading.Thread(target=self._thread_main, name="nodespark-wisp-ble", daemon=True)
        self.thread.start()
        return True

    def stop(self) -> None:
        self.running = False
        if self.loop and self.loop.is_running():
            self.loop.call_soon_threadsafe(lambda: None)

    def notify_event(self, payload: dict[str, Any]) -> None:
        if not self.loop or not self.loop.is_running() or not self.server:
            return
        self.loop.call_soon_threadsafe(lambda: asyncio.create_task(self._notify_event(payload)))

    def _thread_main(self) -> None:
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        try:
            self.loop.run_until_complete(self._run())
        except Exception as exc:
            print(f"[ble] Wisp Mobile Bridge unavailable: {exc}")
        finally:
            try:
                self.loop.close()
            except Exception:
                pass

    async def _run(self) -> None:
        try:
            from bless import BlessServer  # type: ignore
            from bless.backends.characteristic import (  # type: ignore
                GATTAttributePermissions,
                GATTCharacteristicProperties,
            )
        except Exception as exc:
            print("[ble] Install optional BLE support with: pip install 'nodespark-wisp[ble]'")
            print(f"[ble] Import failed: {exc}")
            return

        self.server = BlessServer(name=self.cfg.device_name or self.app.cfg.device.name)
        self.server.read_request_func = self._read_request
        self.server.write_request_func = self._write_request

        await self.server.add_new_service(self.cfg.service_uuid)
        await self.server.add_new_characteristic(
            self.cfg.service_uuid,
            self.cfg.command_characteristic_uuid,
            GATTCharacteristicProperties.write | GATTCharacteristicProperties.write_without_response,
            None,
            GATTAttributePermissions.writeable,
        )
        await self.server.add_new_characteristic(
            self.cfg.service_uuid,
            self.cfg.event_characteristic_uuid,
            GATTCharacteristicProperties.notify | GATTCharacteristicProperties.read,
            bytearray(self._state_json()),
            GATTAttributePermissions.readable,
        )
        await self.server.add_new_characteristic(
            self.cfg.service_uuid,
            self.cfg.state_characteristic_uuid,
            GATTCharacteristicProperties.notify | GATTCharacteristicProperties.read,
            bytearray(self._state_json()),
            GATTAttributePermissions.readable,
        )

        await self.server.start()
        print(f"[ble] Advertising {self.cfg.device_name} for Wisp Mobile Bridge")
        self.app.display.show("Mobile Bridge", "Bluetooth is advertising for iPhone.", self.cfg.device_name, (0, 190, 255), self.app._status_footer(force=True))

        while self.running and self.app.running:
            await asyncio.sleep(2.0)
            await self._notify_state()

        await self.server.stop()
        print("[ble] Wisp Mobile Bridge stopped")

    def _read_request(self, characteristic, **_kwargs):
        uuid = str(getattr(characteristic, "uuid", "")).lower()
        if uuid == self.cfg.state_characteristic_uuid.lower():
            return bytearray(self._state_json())
        return bytearray(self._event_json({"type": "state", "status": "ready"}))

    def _write_request(self, characteristic, value, **_kwargs) -> None:
        raw = bytes(value or b"").decode("utf-8", errors="replace").strip()
        if not raw:
            return
        try:
            message = json.loads(raw)
            if not isinstance(message, dict):
                raise ValueError("BLE command must be a JSON object")
        except Exception as exc:
            print(f"[ble] Bad command: {exc}: {raw[:120]}")
            return

        command = dict(message)
        command.setdefault("id", f"ble-{int(time.time() * 1000)}")
        command.setdefault("source", "ios-ble-bridge")
        print(f"[ble] command {command.get('type', 'display')}: {command.get('title') or command.get('body') or command.get('text') or ''}")
        self.app._execute_command(command)

    async def _notify_state(self) -> None:
        if not self.server:
            return
        payload = bytearray(self._state_json())
        try:
            self.server.get_characteristic(self.cfg.state_characteristic_uuid).value = payload
            self.server.update_value(self.cfg.service_uuid, self.cfg.state_characteristic_uuid)
        except Exception as exc:
            print(f"[ble] notify failed: {exc}")

    async def _notify_event(self, payload: dict[str, Any]) -> None:
        if not self.server:
            return
        event = dict(payload)
        event.setdefault("deviceId", self.app.state.device_id)
        event.setdefault("deviceName", self.app.cfg.device.name)
        event.setdefault("workflowName", self.app.current_workflow())
        event.setdefault("bridge", "wisp-ble")
        data = bytearray(self._event_json(event))
        try:
            self.server.get_characteristic(self.cfg.event_characteristic_uuid).value = data
            self.server.update_value(self.cfg.service_uuid, self.cfg.event_characteristic_uuid)
        except Exception as exc:
            print(f"[ble] event notify failed: {exc}")

    def _state_json(self) -> bytes:
        return self._event_json({
            "type": "state",
            "deviceId": self.app.state.device_id,
            "deviceName": self.app.cfg.device.name,
            "workflowName": self.app.current_workflow(),
            "pairedToHub": bool(self.app.state.token),
            "bridge": "wisp-ble",
        })

    @staticmethod
    def _event_json(payload: dict[str, Any]) -> bytes:
        return json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
