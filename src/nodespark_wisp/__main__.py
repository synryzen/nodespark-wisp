from __future__ import annotations

import argparse
import json
import sys
from urllib.parse import quote

from .app import NodeSparkWispApp
from .config import StateStore, load_config
from .hub import HubClient
from .system import read_status, run_update


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="nodespark-wisp")
    parser.add_argument("--config", help="Path to config.toml")
    sub = parser.add_subparsers(dest="cmd")

    sub.add_parser("daemon", help="Run the Wisp button/display service")

    pair = sub.add_parser("pair", help="Pair this device with NodeSparkHub")
    pair.add_argument("--code", required=True, help="Pairing code shown in NodeSparkHub")

    run = sub.add_parser("run", help="Run a Hub workflow once")
    run.add_argument("--workflow", help="Workflow name. Defaults to config hub.default_workflow")
    run.add_argument("--text", default="", help="Text payload sent as input/text/utterance")

    sub.add_parser("health", help="Call GET /health")
    sub.add_parser("workflows", help="List workflows from the Hub")
    sub.add_parser("status", help="Print local IP/Wi-Fi/battery/temperature")
    sub.add_parser("qr", help="Show pairing QR/device identity on the Wisp display")
    update = sub.add_parser("update", help="Refresh dependencies/package and restart the service")
    update.add_argument("--app-root", default="/opt/nodespark-wisp", help="Installed app root")

    args = parser.parse_args(argv)
    cfg = load_config(args.config)
    state = StateStore()
    app = None

    try:
        if args.cmd == "pair":
            client = HubClient(cfg.hub.base_url, state.device_id, cfg.device.name, state.token)
            response = client.pair(args.code)
            token = str(response.get("deviceToken", ""))
            if token:
                state.set_pairing(str(response.get("hubId", "")), token, response.get("expiresAt"))
                client.token = token
                try:
                    response["checkin"] = client.checkin()
                except Exception as exc:
                    response["checkinWarning"] = str(exc)
            print(json.dumps(response, indent=2, sort_keys=True))
        elif args.cmd == "run":
            app = NodeSparkWispApp(cfg, state)
            print(json.dumps(app.run_once(args.text, args.workflow), indent=2, sort_keys=True))
        elif args.cmd == "health":
            client = HubClient(cfg.hub.base_url, state.device_id, cfg.device.name, state.token)
            print(json.dumps(client.health(), indent=2, sort_keys=True))
        elif args.cmd == "workflows":
            client = HubClient(cfg.hub.base_url, state.device_id, cfg.device.name, state.token)
            for name in client.list_workflows():
                print(name)
        elif args.cmd == "status":
            print(json.dumps(read_status().__dict__, indent=2, sort_keys=True))
        elif args.cmd == "qr":
            app = NodeSparkWispApp(cfg, state)
            name = quote(cfg.device.name, safe="")
            hub = quote(cfg.hub.base_url, safe="")
            data = f"nodesparkhub-device://pair?deviceId={state.device_id}&name={name}&hub={hub}"
            app.display.show_qr("Pair Device", data, "Use Hub pairing code")
            print(data)
        elif args.cmd == "update":
            raise SystemExit(run_update(args.app_root))
        else:
            app = NodeSparkWispApp(cfg, state)
            app.daemon()
    except Exception as exc:
        print(f"nodespark-wisp: {exc}", file=sys.stderr)
        return 1
    finally:
        if app is not None:
            app.display.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
