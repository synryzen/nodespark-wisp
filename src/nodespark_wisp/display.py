from __future__ import annotations

import os
import math
from pathlib import Path
import sys
import textwrap
import threading
from importlib import resources


class DeviceDisplay:
    width = 240
    height = 280

    def __init__(self, enabled: bool = True, driver_path: str = "/opt/Whisplay/Driver", backlight: int = 65):
        self.enabled = enabled
        self.driver_path = driver_path
        self.backlight = backlight
        self.board = None
        self._lock = threading.Lock()
        if enabled:
            self._init_board()

    def _init_board(self) -> None:
        try:
            path = Path(self.driver_path)
            if path.exists():
                sys.path.insert(0, str(path))
            from WhisPlay import WhisPlayBoard  # type: ignore

            self.board = WhisPlayBoard()
            self.board.set_backlight(max(0, min(100, int(self.backlight))))
            self.board.set_rgb(20, 80, 255)
        except Exception as exc:
            print(f"[display] Whisplay unavailable, using console display: {exc}")
            self.board = None

    def show(
        self,
        title: str,
        body: str = "",
        footer: str = "",
        accent: tuple[int, int, int] = (60, 130, 255),
        status: str = "",
    ) -> None:
        if not self.board:
            print(f"[display] {title} | {body} | {footer}")
            return
        try:
            from PIL import Image, ImageDraw, ImageFont
        except Exception as exc:
            print(f"[display] Pillow unavailable: {exc}")
            return

        with self._lock:
            img = Image.new("RGB", (self.width, self.height), (8, 10, 14))
            draw = ImageDraw.Draw(img)
            font_title = self._font(22, bold=True)
            font_body = self._font(17)
            font_footer = self._font(13)
            font_status = self._font(10)

            draw.rounded_rectangle((0, 0, self.width, 28), radius=0, fill=accent)
            if status:
                draw.text((8, 8), status[:36], fill=(7, 11, 18), font=font_status)
            draw.text((12, 42), title[:24], fill=(245, 248, 255), font=font_title)

            y = 84
            for line in self._wrap(body, 24)[:7]:
                draw.text((12, y), line, fill=(210, 220, 235), font=font_body)
                y += 24

            if footer:
                draw.rectangle((0, self.height - 34, self.width, self.height), fill=(16, 19, 27))
                draw.text((12, self.height - 26), footer[:32], fill=(150, 165, 190), font=font_footer)

            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(*accent)

    def show_card(
        self,
        title: str,
        body: str = "",
        subtitle: str = "",
        icon: str = "spark",
        style: str = "info",
        accent: tuple[int, int, int] = (60, 130, 255),
        footer: str = "",
        progress: float | None = None,
    ) -> None:
        if not self.board:
            print(f"[display] card {style}:{icon} {title} | {body} | {subtitle}")
            return
        try:
            from PIL import Image, ImageDraw
        except Exception as exc:
            print(f"[display] Pillow unavailable: {exc}")
            return

        palette = self._style_palette(style, accent)
        with self._lock:
            img = Image.new("RGB", (self.width, self.height), palette["bg"])
            draw = ImageDraw.Draw(img)
            font_title = self._font(20, bold=True)
            font_body = self._font(15)
            font_subtitle = self._font(12)
            font_footer = self._font(11)

            draw.rounded_rectangle((8, 8, self.width - 8, self.height - 8), radius=20, fill=palette["panel"], outline=palette["accent"], width=2)
            draw.ellipse((-38, -18, 95, 120), fill=palette["wash1"])
            draw.ellipse((155, 0, 298, 140), fill=palette["wash2"])

            self._draw_icon(draw, icon, (self.width // 2, 66), 34, palette["accent"], palette["panel"])

            title_y = 112
            for i, line in enumerate(self._wrap(title, 21)[:2]):
                box = draw.textbbox((0, 0), line, font=font_title)
                draw.text(((self.width - (box[2] - box[0])) // 2, title_y + i * 23), line, fill=(246, 250, 255), font=font_title)

            y = 162
            if subtitle:
                for line in self._wrap(subtitle, 28)[:2]:
                    box = draw.textbbox((0, 0), line, font=font_subtitle)
                    draw.text(((self.width - (box[2] - box[0])) // 2, y), line, fill=palette["muted"], font=font_subtitle)
                    y += 15
                y += 4

            for line in self._wrap(body, 27)[:4]:
                box = draw.textbbox((0, 0), line, font=font_body)
                draw.text(((self.width - (box[2] - box[0])) // 2, y), line, fill=(210, 222, 238), font=font_body)
                y += 18

            if progress is not None:
                value = max(0.0, min(1.0, float(progress)))
                bar = (28, self.height - 44, self.width - 28, self.height - 34)
                draw.rounded_rectangle(bar, radius=5, fill=(25, 31, 43))
                draw.rounded_rectangle((bar[0], bar[1], bar[0] + int((bar[2] - bar[0]) * value), bar[3]), radius=5, fill=palette["accent"])

            if footer:
                box = draw.textbbox((0, 0), footer, font=font_footer)
                draw.text(((self.width - (box[2] - box[0])) // 2, self.height - 24), footer[:30], fill=palette["muted"], font=font_footer)

            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(*palette["accent"])

    def show_dashboard(
        self,
        title: str,
        metric_label: str,
        metric_value: str,
        items: list[str],
        accent: tuple[int, int, int] = (60, 130, 255),
        footer: str = "",
    ) -> None:
        if not self.board:
            print(f"[display] dashboard {title} | {metric_label}={metric_value} | {items}")
            return
        try:
            from PIL import Image, ImageDraw
        except Exception as exc:
            print(f"[display] Pillow unavailable: {exc}")
            return

        with self._lock:
            img = Image.new("RGB", (self.width, self.height), (6, 9, 14))
            draw = ImageDraw.Draw(img)
            font_title = self._font(18, bold=True)
            font_metric = self._font(30, bold=True)
            font_label = self._font(12)
            font_item = self._font(13)

            draw.rounded_rectangle((8, 8, self.width - 8, self.height - 8), radius=20, fill=(10, 15, 24), outline=accent, width=2)
            draw.text((18, 20), title[:24], fill=(245, 250, 255), font=font_title)
            draw.rounded_rectangle((18, 52, self.width - 18, 120), radius=16, fill=(17, 28, 42), outline=(50, 70, 92))
            draw.text((30, 62), metric_label[:20], fill=(155, 180, 205), font=font_label)
            draw.text((30, 80), metric_value[:10], fill=accent, font=font_metric)

            y = 138
            for item in items[:5]:
                draw.rounded_rectangle((18, y, self.width - 18, y + 24), radius=8, fill=(16, 21, 31))
                draw.ellipse((28, y + 8, 36, y + 16), fill=accent)
                draw.text((44, y + 5), item[:26], fill=(210, 222, 238), font=font_item)
                y += 29

            if footer:
                draw.text((18, self.height - 24), footer[:32], fill=(145, 165, 190), font=font_label)

            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(*accent)

    def show_notification_stack(
        self,
        title: str,
        notifications: list[dict],
        accent: tuple[int, int, int] = (60, 130, 255),
        footer: str = "",
    ) -> None:
        items = []
        for note in notifications[-5:][::-1]:
            note_title = str(note.get("title") or "Notification")
            note_body = str(note.get("body") or note.get("text") or "")
            items.append(f"{note_title}: {note_body}" if note_body else note_title)
        self.show_dashboard(title, f"{len(notifications)} saved", "Inbox", items, accent, footer)

    def show_approval(
        self,
        title: str,
        body: str,
        choices: list[str] | None = None,
        accent: tuple[int, int, int] = (255, 180, 50),
        footer: str = "",
    ) -> None:
        opts = choices or ["Short press approve", "Hold reject"]
        self.show_card(title, body, " / ".join(opts[:2]), "approval", "approval", accent, footer)

    def show_icon_grid(
        self,
        title: str,
        items: list[str],
        accent: tuple[int, int, int] = (60, 130, 255),
        footer: str = "",
    ) -> None:
        if not self.board:
            print(f"[display] icon-grid {title} | {items}")
            return
        try:
            from PIL import Image, ImageDraw
        except Exception as exc:
            print(f"[display] Pillow unavailable: {exc}")
            return

        with self._lock:
            img = Image.new("RGB", (self.width, self.height), (7, 10, 16))
            draw = ImageDraw.Draw(img)
            font_title = self._font(18, bold=True)
            font_item = self._font(10, bold=True)
            draw.rounded_rectangle((8, 8, self.width - 8, self.height - 8), radius=20, fill=(10, 15, 24), outline=accent, width=2)
            box = draw.textbbox((0, 0), title, font=font_title)
            draw.text(((self.width - (box[2] - box[0])) // 2, 22), title[:24], fill=(245, 250, 255), font=font_title)
            cells = items[:6] or ["workflow", "ai", "phone", "webhook", "alert", "done"]
            for idx, item in enumerate(cells):
                col = idx % 2
                row = idx // 2
                x = 28 + col * 98
                y = 58 + row * 58
                draw.rounded_rectangle((x, y, x + 84, y + 48), radius=12, fill=(18, 25, 37), outline=(45, 60, 82))
                self._draw_icon(draw, item, (x + 22, y + 24), 15, accent, (18, 25, 37))
                draw.text((x + 42, y + 18), item[:10], fill=(210, 222, 238), font=font_item)
            if footer:
                draw.text((18, self.height - 24), footer[:32], fill=(145, 165, 190), font=self._font(11))
            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(*accent)

    def show_progress(
        self,
        title: str,
        body: str = "",
        phase: int = 0,
        footer: str = "",
        accent: tuple[int, int, int] = (60, 130, 255),
        status: str = "",
    ) -> None:
        if not self.board:
            print(f"[display] {title} phase={phase} | {body} | {footer}")
            return
        try:
            from PIL import Image, ImageDraw
        except Exception as exc:
            print(f"[display] Pillow unavailable: {exc}")
            return

        with self._lock:
            img = Image.new("RGB", (self.width, self.height), (6, 9, 14))
            draw = ImageDraw.Draw(img)
            font_title = self._font(22, bold=True)
            font_body = self._font(15)
            font_footer = self._font(12)
            font_status = self._font(10)

            draw.rounded_rectangle((10, 10, self.width - 10, self.height - 10), radius=18, fill=(10, 14, 23), outline=accent, width=2)
            if status:
                draw.text((18, 18), status[:34], fill=(150, 205, 235), font=font_status)

            cx, cy = self.width // 2, 92
            for i in range(12):
                step = (i + phase) % 12
                alpha = 55 + step * 15
                r = 38 + step
                x = cx + int(r * math.cos(i * 0.5236))
                y = cy + int(r * math.sin(i * 0.5236))
                color = tuple(min(255, int(c * alpha / 220)) for c in accent)
                draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=color)

            title_box = draw.textbbox((0, 0), title, font=font_title)
            draw.text(((self.width - (title_box[2] - title_box[0])) // 2, 145), title[:22], fill=(245, 248, 255), font=font_title)

            y = 180
            for line in self._wrap(body, 26)[:3]:
                line_box = draw.textbbox((0, 0), line, font=font_body)
                draw.text(((self.width - (line_box[2] - line_box[0])) // 2, y), line, fill=(190, 210, 232), font=font_body)
                y += 19

            if footer:
                draw.text((16, self.height - 28), footer[:32], fill=(155, 170, 195), font=font_footer)

            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(*accent)

    def show_qr(
        self,
        title: str,
        data: str,
        footer: str = "",
        accent: tuple[int, int, int] = (60, 130, 255),
        status: str = "",
    ) -> None:
        if not self.board:
            print(f"[display] QR {title}: {data} | {footer}")
            return
        try:
            from PIL import Image, ImageDraw
            import qrcode
        except Exception as exc:
            print(f"[display] QR unavailable: {exc}")
            self.show(title, data[:120], footer, accent, status)
            return

        with self._lock:
            img = Image.new("RGB", (self.width, self.height), (7, 10, 16))
            draw = ImageDraw.Draw(img)
            font_title = self._font(18, bold=True)
            font_footer = self._font(12)
            font_status = self._font(10)

            draw.rounded_rectangle((8, 8, self.width - 8, self.height - 8), radius=16, fill=(10, 15, 24), outline=accent, width=2)
            if status:
                draw.text((18, 16), status[:34], fill=(160, 210, 235), font=font_status)
            title_box = draw.textbbox((0, 0), title, font=font_title)
            draw.text(((self.width - (title_box[2] - title_box[0])) // 2, 34), title[:22], fill=(245, 248, 255), font=font_title)

            qr = qrcode.QRCode(border=1, box_size=6)
            qr.add_data(data)
            qr.make(fit=True)
            qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
            qr_img.thumbnail((176, 176), Image.Resampling.NEAREST)
            x = (self.width - qr_img.width) // 2
            y = 70
            draw.rounded_rectangle((x - 8, y - 8, x + qr_img.width + 8, y + qr_img.height + 8), radius=10, fill=(245, 248, 255))
            img.paste(qr_img, (x, y))

            if footer:
                footer_box = draw.textbbox((0, 0), footer, font=font_footer)
                draw.text(((self.width - (footer_box[2] - footer_box[0])) // 2, self.height - 28), footer[:30], fill=(170, 210, 240), font=font_footer)

            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(*accent)

    def show_startup_logo(
        self,
        subtitle: str = "Physical automations online",
        footer: str = "NodeSparkHub",
        logo_path: str = "",
        status: str = "",
    ) -> None:
        if not self.board:
            print(f"[display] NodeSparkHub startup | {subtitle} | {footer}")
            return
        try:
            from PIL import Image, ImageDraw
        except Exception as exc:
            print(f"[display] Pillow unavailable: {exc}")
            return

        with self._lock:
            img = Image.new("RGB", (self.width, self.height), (5, 8, 13))
            draw = ImageDraw.Draw(img)
            font_title = self._font(22, bold=True)
            font_subtitle = self._font(13)
            font_footer = self._font(12, bold=True)
            font_status = self._font(10)

            for y in range(self.height):
                blue = 14 + int(38 * y / max(1, self.height - 1))
                img.paste((5, 8 + int(10 * y / self.height), blue), (0, y, self.width, y + 1))

            draw.rounded_rectangle((10, 10, self.width - 10, self.height - 10), radius=20, fill=(8, 13, 22), outline=(35, 200, 255), width=2)
            if status:
                draw.text((18, 18), status[:34], fill=(160, 220, 245), font=font_status)
            draw.ellipse((-30, 20, 85, 135), fill=(8, 60, 105))
            draw.ellipse((165, -10, 285, 120), fill=(92, 20, 120))

            logo = self._load_logo(logo_path)
            if logo:
                logo.thumbnail((178, 190), Image.Resampling.LANCZOS)
                x = (self.width - logo.width) // 2
                y = 24
                glow = Image.new("RGBA", (logo.width + 22, logo.height + 22), (0, 0, 0, 0))
                glow_draw = ImageDraw.Draw(glow)
                glow_draw.rounded_rectangle((0, 0, glow.width, glow.height), radius=28, fill=(0, 190, 255, 55))
                img.paste(glow, (x - 11, y - 11), glow)
                img.paste(logo.convert("RGBA"), (x, y), logo.convert("RGBA"))

            title = "NodeSparkHub"
            title_box = draw.textbbox((0, 0), title, font=font_title)
            draw.text(((self.width - (title_box[2] - title_box[0])) // 2, 205), title, fill=(245, 250, 255), font=font_title)

            for i, line in enumerate(self._wrap(subtitle, 28)[:2]):
                line_box = draw.textbbox((0, 0), line, font=font_subtitle)
                draw.text(((self.width - (line_box[2] - line_box[0])) // 2, 235 + i * 16), line, fill=(172, 218, 245), font=font_subtitle)

            draw.rounded_rectangle((38, self.height - 24, self.width - 38, self.height - 10), radius=7, fill=(22, 34, 48), outline=(50, 190, 255))
            footer_box = draw.textbbox((0, 0), footer, font=font_footer)
            draw.text(((self.width - (footer_box[2] - footer_box[0])) // 2, self.height - 23), footer[:24], fill=(255, 95, 205), font=font_footer)

            self.board.draw_image(0, 0, self.width, self.height, self._rgb565(img))
            self.board.set_rgb(0, 190, 255)

    def set_rgb(self, r: int, g: int, b: int) -> None:
        if not self.board:
            print(f"[display] rgb=({r}, {g}, {b})")
            return
        try:
            self.board.set_rgb(max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b)))
        except Exception as exc:
            print(f"[display] RGB update failed: {exc}")

    def set_button_handlers(self, on_press=None, on_release=None) -> bool:
        if not self.board:
            return False
        try:
            if on_press:
                self.board.on_button_press(on_press)
            if on_release:
                self.board.on_button_release(on_release)
            return True
        except Exception as exc:
            print(f"[buttons] Wisp button unavailable: {exc}")
            return False

    def cleanup(self) -> None:
        if self.board:
            try:
                self.board.cleanup()
            except Exception:
                pass

    @staticmethod
    def _load_logo(logo_path: str = ""):
        from PIL import Image

        candidates: list[Path] = []
        if logo_path:
            candidates.append(Path(logo_path).expanduser())
        try:
            bundled = resources.files("nodespark_wisp").joinpath("assets/startup_logo.png")
            with resources.as_file(bundled) as path:
                candidates.append(path)
                for candidate in candidates:
                    if candidate.exists():
                        return Image.open(candidate).convert("RGBA")
        except Exception:
            for candidate in candidates:
                if candidate.exists():
                    return Image.open(candidate).convert("RGBA")
        return None

    @staticmethod
    def _font(size: int, bold: bool = False):
        from PIL import ImageFont

        candidates = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        ]
        for path in candidates:
            if path and os.path.exists(path):
                return ImageFont.truetype(path, size)
        return ImageFont.load_default()

    @staticmethod
    def _wrap(text: str, width: int) -> list[str]:
        lines: list[str] = []
        for paragraph in (text or "").splitlines() or [""]:
            lines.extend(textwrap.wrap(paragraph, width=width) or [""])
        return lines

    @staticmethod
    def _style_palette(style: str, accent: tuple[int, int, int]) -> dict[str, tuple[int, int, int]]:
        palettes = {
            "success": {"accent": (35, 190, 95), "bg": (4, 12, 10), "panel": (8, 22, 18), "wash1": (4, 70, 40), "wash2": (12, 54, 42), "muted": (150, 215, 185)},
            "error": {"accent": (255, 70, 70), "bg": (14, 6, 8), "panel": (28, 10, 14), "wash1": (90, 10, 20), "wash2": (60, 18, 38), "muted": (235, 160, 165)},
            "warning": {"accent": (255, 180, 50), "bg": (15, 10, 4), "panel": (28, 20, 8), "wash1": (95, 54, 0), "wash2": (78, 40, 12), "muted": (240, 205, 150)},
            "approval": {"accent": (255, 180, 50), "bg": (15, 10, 4), "panel": (28, 20, 8), "wash1": (95, 54, 0), "wash2": (78, 40, 12), "muted": (240, 205, 150)},
            "ai": {"accent": (120, 90, 255), "bg": (9, 7, 18), "panel": (16, 12, 31), "wash1": (40, 20, 100), "wash2": (85, 20, 105), "muted": (190, 178, 255)},
            "voice": {"accent": (255, 90, 205), "bg": (15, 5, 16), "panel": (28, 10, 30), "wash1": (95, 15, 82), "wash2": (30, 60, 100), "muted": (245, 170, 225)},
            "info": {"accent": accent, "bg": (6, 9, 14), "panel": (10, 15, 24), "wash1": (6, 54, 105), "wash2": (50, 20, 115), "muted": (165, 205, 235)},
        }
        return palettes.get(style.lower(), palettes["info"])

    @staticmethod
    def _draw_icon(draw, icon: str, center: tuple[int, int], radius: int, accent: tuple[int, int, int], bg: tuple[int, int, int]) -> None:
        name = (icon or "spark").lower().replace("_", "-")
        cx, cy = center
        r = radius
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=accent)
        inner = (max(0, bg[0] + 10), max(0, bg[1] + 12), max(0, bg[2] + 16))
        white = (245, 250, 255)

        if any(k in name for k in ["mail", "email"]):
            draw.rounded_rectangle((cx - 17, cy - 11, cx + 17, cy + 11), radius=3, fill=inner, outline=white)
            draw.line((cx - 17, cy - 10, cx, cy + 2, cx + 17, cy - 10), fill=white, width=2)
        elif any(k in name for k in ["calendar", "date"]):
            draw.rounded_rectangle((cx - 16, cy - 17, cx + 16, cy + 17), radius=4, fill=inner, outline=white, width=2)
            draw.rectangle((cx - 16, cy - 10, cx + 16, cy - 4), fill=white)
            draw.ellipse((cx - 5, cy + 1, cx + 5, cy + 11), fill=white)
        elif any(k in name for k in ["approval", "check", "success", "done"]):
            draw.line((cx - 14, cy + 0, cx - 4, cy + 10, cx + 16, cy - 12), fill=white, width=5)
        elif any(k in name for k in ["warning", "alert", "error"]):
            draw.polygon([(cx, cy - 18), (cx - 18, cy + 16), (cx + 18, cy + 16)], fill=inner, outline=white)
            draw.line((cx, cy - 7, cx, cy + 6), fill=white, width=3)
            draw.ellipse((cx - 2, cy + 10, cx + 2, cy + 14), fill=white)
        elif any(k in name for k in ["payment", "order", "stripe", "sale"]):
            draw.rounded_rectangle((cx - 17, cy - 12, cx + 17, cy + 12), radius=4, fill=inner, outline=white, width=2)
            draw.rectangle((cx - 15, cy - 5, cx + 15, cy - 1), fill=white)
            draw.text((cx - 4, cy + 0), "$", fill=white)
        elif any(k in name for k in ["package", "ship"]):
            draw.polygon([(cx, cy - 18), (cx + 17, cy - 8), (cx + 17, cy + 12), (cx, cy + 20), (cx - 17, cy + 12), (cx - 17, cy - 8)], fill=inner, outline=white)
            draw.line((cx, cy - 18, cx, cy + 20), fill=white, width=2)
        elif any(k in name for k in ["ai", "brain", "spark"]):
            for i in range(8):
                angle = i * math.pi / 4
                x = cx + int(math.cos(angle) * 17)
                y = cy + int(math.sin(angle) * 17)
                draw.line((cx, cy, x, y), fill=white, width=2)
            draw.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), fill=white)
        elif any(k in name for k in ["home", "house"]):
            draw.polygon([(cx, cy - 18), (cx - 18, cy - 2), (cx - 13, cy - 2), (cx - 13, cy + 16), (cx + 13, cy + 16), (cx + 13, cy - 2), (cx + 18, cy - 2)], fill=inner, outline=white)
        else:
            draw.rounded_rectangle((cx - 16, cy - 16, cx + 16, cy + 16), radius=8, fill=inner, outline=white, width=2)
            draw.ellipse((cx - 5, cy - 5, cx + 5, cy + 5), fill=white)

    @staticmethod
    def _rgb565(img) -> list[int]:
        data: list[int] = []
        for r, g, b in img.convert("RGB").getdata():
            value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data.extend([(value >> 8) & 0xFF, value & 0xFF])
        return data
