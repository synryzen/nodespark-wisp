from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import socket
import subprocess


@dataclass
class SystemStatus:
    ip: str = ""
    wifi: str = ""
    battery: str = ""
    temperature: str = ""

    def footer(self) -> str:
        parts = [part for part in [self.ip, self.wifi, self.battery] if part]
        return "  ".join(parts[:3])


def read_status() -> SystemStatus:
    return SystemStatus(
        ip=_ip_address(),
        wifi=_wifi_name(),
        battery=_battery_status(),
        temperature=_temperature(),
    )


def _ip_address() -> str:
    if shutil.which("hostname"):
        try:
            out = subprocess.check_output(["hostname", "-I"], text=True, timeout=2).strip()
            for value in out.split():
                if "." in value and not value.startswith("127."):
                    return value
        except Exception:
            pass

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            return sock.getsockname()[0]
    except Exception:
        return ""


def _wifi_name() -> str:
    if shutil.which("iwgetid"):
        try:
            out = subprocess.check_output(["iwgetid", "-r"], text=True, timeout=2).strip()
            if out:
                return out
        except Exception:
            pass
    return ""


def _battery_status() -> str:
    for cmd in (
        ["pisugar-power-manager", "-c", "get battery"],
        ["pisugar-server", "get", "battery"],
        ["pisugar", "get", "battery"],
    ):
        if not shutil.which(cmd[0]):
            continue
        try:
            out = subprocess.check_output(cmd, text=True, timeout=3).strip()
            percent = _first_percent(out)
            if percent:
                return percent
        except Exception:
            continue

    for path in Path("/sys/class/power_supply").glob("*/capacity"):
        try:
            value = path.read_text().strip()
            if value.isdigit():
                return f"{value}%"
        except Exception:
            pass
    return ""


def _temperature() -> str:
    thermal = Path("/sys/class/thermal/thermal_zone0/temp")
    try:
        raw = thermal.read_text().strip()
        if raw.isdigit():
            return f"{int(raw) / 1000:.0f}C"
    except Exception:
        pass

    if shutil.which("vcgencmd"):
        try:
            out = subprocess.check_output(["vcgencmd", "measure_temp"], text=True, timeout=2).strip()
            return out.replace("temp=", "").replace("'C", "C")
        except Exception:
            pass
    return ""


def _first_percent(text: str) -> str:
    token = ""
    for char in text:
        if char.isdigit() or char == ".":
            token += char
        elif char == "%" and token:
            return f"{float(token):.0f}%"
        elif token:
            token = ""
    if token:
        try:
            value = float(token)
            if 0 <= value <= 100:
                return f"{value:.0f}%"
        except ValueError:
            return ""
    return ""


def run_update(app_root: str = "/opt/nodespark-wisp") -> int:
    root = Path(app_root)
    if not root.exists():
        raise RuntimeError(f"App root does not exist: {root}")

    if (root / ".git").exists() and shutil.which("git"):
        subprocess.run(["git", "-C", str(root), "pull", "--ff-only"], check=True)

    venv_python = root / ".venv/bin/python"
    venv_pip = root / ".venv/bin/pip"
    if not venv_python.exists():
        subprocess.run(["python3", "-m", "venv", "--system-site-packages", str(root / ".venv")], check=True)
    subprocess.run([str(venv_pip), "install", "--upgrade", "pip", "wheel"], check=True)
    subprocess.run([str(venv_pip), "install", "-r", str(root / "requirements.txt")], check=True)
    subprocess.run([str(venv_pip), "install", "-e", str(root)], check=True)

    if os.geteuid() == 0 and shutil.which("systemctl"):
        subprocess.run(["systemctl", "restart", "nodespark-wisp"], check=False)
    elif shutil.which("sudo") and shutil.which("systemctl"):
        subprocess.run(["sudo", "systemctl", "restart", "nodespark-wisp"], check=False)
    return 0
