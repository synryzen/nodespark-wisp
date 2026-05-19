#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


@dataclass
class HTTPResult:
    status: int
    body: str
    data: Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run NodeSparkHub / NodeSpark Wisp connectivity smoke checks."
    )
    parser.add_argument("--hub-url", default="http://127.0.0.1:8787", help="NodeSparkHub base URL.")
    parser.add_argument("--token", default="", help="Optional Hub bearer token for remote/non-local Hub URLs.")
    parser.add_argument("--device-id", default="", help="Specific Wisp device UUID to command.")
    parser.add_argument("--all-wisp", action="store_true", help="Send command checks to every fresh Wisp device.")
    parser.add_argument("--send", action="store_true", help="Queue Wisp device commands. Without this, only read checks run.")
    parser.add_argument("--workflow", default="", help="Workflow name to run for the workflow command.")
    parser.add_argument("--fresh-minutes", type=float, default=5.0, help="Device freshness window for --all-wisp.")
    parser.add_argument("--timeout", type=float, default=20.0, help="Per-request timeout in seconds.")
    parser.add_argument("--skip-assistant", action="store_true", help="Skip the /wisp/assistant check.")
    return parser.parse_args()


def request_json(
    base_url: str,
    method: str,
    path: str,
    token: str = "",
    payload: Any | None = None,
    timeout: float = 20.0,
) -> HTTPResult:
    url = base_url.rstrip("/") + path
    body = None
    headers = {"Accept": "application/json", "User-Agent": "nodespark-wisp-smoke/1.0"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
        headers["X-NodeSparkHub-Token"] = token
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = Request(url, data=body, headers=headers, method=method)
    try:
        with urlopen(req, timeout=timeout) as response:
            text = response.read().decode("utf-8", errors="replace")
            return HTTPResult(response.status, text, decode_json(text))
    except HTTPError as exc:
        text = exc.read().decode("utf-8", errors="replace")
        return HTTPResult(exc.code, text, decode_json(text))
    except URLError as exc:
        raise RuntimeError(f"Could not reach {url}: {exc.reason}") from exc
    except TimeoutError:
        return HTTPResult(599, f"Timed out after {timeout:.0f}s", {"error": "timeout"})


def decode_json(text: str) -> Any:
    try:
        return json.loads(text) if text.strip() else {}
    except json.JSONDecodeError:
        return {"raw": text}


def devices_from(data: Any) -> list[dict[str, Any]]:
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]
    if isinstance(data, dict):
        raw = data.get("devices") or data.get("clients") or []
        if isinstance(raw, list):
            return [item for item in raw if isinstance(item, dict)]
    return []


def workflows_from(data: Any) -> list[str]:
    if isinstance(data, dict) and isinstance(data.get("workflows"), list):
        return [str(item) for item in data["workflows"]]
    if isinstance(data, list):
        return [str(item) for item in data]
    return []


def is_wisp(device: dict[str, Any]) -> bool:
    haystack = " ".join(str(device.get(key, "")) for key in ("name", "platform", "appVersion")).lower()
    return any(marker in haystack for marker in ("wisp", "whisplay", "esp32", "core2"))


def parse_date(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    text = value.replace("Z", "+00:00")
    try:
        return datetime.fromisoformat(text).astimezone(timezone.utc)
    except ValueError:
        return None


def age_seconds(device: dict[str, Any]) -> float | None:
    seen = parse_date(device.get("lastSeen") or device.get("lastSeenAt") or device.get("last_seen"))
    if seen is None:
        return None
    return (datetime.now(timezone.utc) - seen).total_seconds()


def age_label(device: dict[str, Any]) -> str:
    age = age_seconds(device)
    if age is None:
        return "unknown"
    if age < 90:
        return f"{int(age)}s ago"
    if age < 3600:
        return f"{age / 60:.1f}m ago"
    if age < 86400:
        return f"{age / 3600:.1f}h ago"
    return f"{age / 86400:.1f}d ago"


def command_payloads(workflow: str) -> list[dict[str, Any]]:
    payloads: list[dict[str, Any]] = [
        {"type": "ping"},
        {
            "type": "card",
            "style": "ai",
            "icon": "spark",
            "title": "Smoke Test",
            "subtitle": "NodeSparkHub connected",
            "body": "The Hub can command this NodeSpark Wisp device.",
            "progress": 0.84,
            "rgb": [0, 190, 255],
        },
        {
            "type": "dashboard",
            "title": "Wisp Check",
            "metricLabel": "Hub",
            "metricValue": "Online",
            "items": ["Commands queued", "Display path ready", "Workflow bridge ready"],
            "rgb": [45, 160, 255],
        },
        {"type": "workflows"},
        {"type": "volume", "percent": 65},
        {"type": "speak", "text": "NodeSpark Wisp speaker smoke test."},
        {"type": "mic"},
        {"type": "sdcheck"},
    ]
    if workflow:
        payloads.append({
            "type": "runWorkflow",
            "workflowName": workflow,
            "payload": {
                "source": "wisp-smoke-test",
                "text": "NodeSpark Wisp workflow smoke test.",
            },
        })
    return payloads


def print_result(label: str, result: HTTPResult) -> None:
    ok = 200 <= result.status < 300
    prefix = "PASS" if ok else "WARN"
    print(f"[{prefix}] {label}: HTTP {result.status}")
    if not ok and result.body.strip():
        print(f"       {result.body.strip()[:220]}")


def main() -> int:
    args = parse_args()
    hub_url = args.hub_url.rstrip("/")

    try:
        health = request_json(hub_url, "GET", "/health", args.token, timeout=args.timeout)
    except RuntimeError as exc:
        print(f"[FAIL] Hub health: {exc}")
        return 2
    print_result("Hub health", health)
    if not (200 <= health.status < 300):
        return 1

    workflows_result = request_json(hub_url, "GET", "/workflows", args.token, timeout=args.timeout)
    print_result("Workflow list", workflows_result)
    workflows = workflows_from(workflows_result.data)
    print(f"       workflows: {', '.join(workflows) if workflows else 'none reported'}")

    devices_result = request_json(hub_url, "GET", "/devices", args.token, timeout=args.timeout)
    print_result("Device list", devices_result)
    devices = devices_from(devices_result.data)
    wisp_devices = [device for device in devices if is_wisp(device)]

    if not wisp_devices:
        print("[WARN] No Wisp-like devices are registered yet.")
    else:
        print("       Wisp candidates:")
        for device in wisp_devices:
            print(
                "       - "
                f"{device.get('name', 'Unknown')} "
                f"({device.get('platform', 'Unknown platform')}) "
                f"id={device.get('id', '')} lastSeen={age_label(device)}"
            )

    if not args.skip_assistant:
        assistant_payload = {
            "text": "Reply with one short sentence confirming the NodeSpark Wisp assistant smoke test.",
            "deviceName": "Smoke Test",
            "source": "wisp-smoke-test",
            "platform": "Mac smoke test",
            "voice": False,
            "capabilities": ["assistant", "display", "workflow"],
        }
        assistant = request_json(hub_url, "POST", "/wisp/assistant", args.token, assistant_payload, timeout=args.timeout)
        print_result("Wisp assistant", assistant)
        if isinstance(assistant.data, dict):
            reply = str(assistant.data.get("displayText") or assistant.data.get("reply") or "").strip()
            if reply:
                print(f"       reply: {reply[:180]}")

    targets: list[str] = []
    if args.device_id:
        targets = [args.device_id]
    elif args.all_wisp:
        max_age = max(1.0, args.fresh_minutes * 60)
        targets = [
            str(device["id"])
            for device in wisp_devices
            if device.get("id") and (age_seconds(device) is not None and age_seconds(device) <= max_age)
        ]

    if args.send and not targets:
        print("[WARN] No fresh target device selected. Use --device-id <uuid> to queue commands to a known device.")
        return 0

    if args.send:
        workflow = args.workflow or (workflows[0] if workflows else "")
        print(f"[INFO] Queuing command smoke tests to {len(targets)} device(s).")
        for device_id in targets:
            for payload in command_payloads(workflow):
                result = request_json(
                    hub_url,
                    "POST",
                    f"/devices/{quote(device_id, safe='')}/commands",
                    args.token,
                    payload,
                    timeout=args.timeout,
                )
                print_result(f"{device_id} command {payload['type']}", result)

    return 0


if __name__ == "__main__":
    sys.exit(main())
