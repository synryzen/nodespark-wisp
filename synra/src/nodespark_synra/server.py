from __future__ import annotations

import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from .app import SynraApp


class SynraHTTPServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], handler_class: type[SimpleHTTPRequestHandler], app: SynraApp, web_root: Path):
        super().__init__(server_address, handler_class)
        self.app = app
        self.web_root = web_root


class SynraRequestHandler(SimpleHTTPRequestHandler):
    server: SynraHTTPServer

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/state":
            self._json({"ok": True, "state": self.server.app.state.snapshot()})
            return
        if path == "/api/health":
            self._json({
                "ok": True,
                "deviceId": self.server.app.store.device_id,
                "hubConfigured": self.server.app.hub.configured(),
                "hubUrl": self.server.app.cfg.hub.base_url,
            })
            return
        super().do_GET()

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        payload = self._read_json()
        if path == "/api/state":
            state = self.server.app.state.set_state(payload)
            self._json({"ok": True, "state": state})
            return
        if path == "/api/command":
            self._json(self.server.app.handle_command(payload, ack=False))
            return
        if path == "/api/pair":
            code = str(payload.get("code") or "")
            try:
                response = self.server.app.pair(code)
                self._json({"ok": True, "response": response})
            except Exception as exc:
                self._json({"ok": False, "error": str(exc)}, status=400)
            return
        self._json({"ok": False, "error": f"Unknown API path: {path}"}, status=404)

    def log_message(self, fmt: str, *args) -> None:
        print(f"[http] {self.address_string()} - {fmt % args}")

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0:
            return {}
        body = self.rfile.read(length)
        try:
            data = json.loads(body.decode("utf-8"))
        except ValueError:
            return {}
        return data if isinstance(data, dict) else {}

    def _json(self, data: dict[str, Any], status: int = 200) -> None:
        encoded = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)


def serve(app: SynraApp, host: str, port: int) -> None:
    web_root = Path(__file__).resolve().parents[2] / "web"
    handler = partial(SynraRequestHandler, directory=str(web_root))
    httpd = SynraHTTPServer((host, port), handler, app, web_root)
    print(f"[synra] serving monitor UI at http://{host}:{port}")
    httpd.serve_forever()
