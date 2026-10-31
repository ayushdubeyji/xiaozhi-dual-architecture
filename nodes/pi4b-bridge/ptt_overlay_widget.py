#!/usr/bin/env python3
"""Floating non-blocking Push-to-Talk (PTT) touch pill widget for 4.3" 800x480 screen."""

import os
import sys
import time
import json
import socket
import threading
import urllib.request
import logging

try:
    import tkinter as tk
except ImportError:
    tk = None

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [ptt_widget] %(message)s"
)
logger = logging.getLogger("ptt_overlay")

# Geometry and dimensions
WIDTH = 110
HEIGHT = 44
POS_TOP_CENTER = (345, 8)
POS_BOTTOM_RIGHT = (680, 426)

API_TRIGGER_URL = "http://127.0.0.1:8765/api/trigger"

# Colors
COLOR_BG_IDLE = "#1E293B"      # Dark slate
COLOR_BORDER_IDLE = "#475569"  # Muted slate
COLOR_TEXT_IDLE = "#E2E8F0"    # Off-white

COLOR_BG_ACTIVE = "#059669"    # Emerald green
COLOR_BORDER_ACTIVE = "#10B981"# Vibrant green
COLOR_TEXT_ACTIVE = "#FFFFFF"  # Pure white


def post_trigger_async(event_type: str):
    """Dispatches HTTP trigger to local xiaozhi agent in daemon thread."""
    def _post():
        try:
            req = urllib.request.Request(
                API_TRIGGER_URL,
                data=json.dumps({"event": event_type}).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST"
            )
            with urllib.request.urlopen(req, timeout=1.5) as resp:
                pass
        except Exception as ex:
            logger.debug(f"Failed to deliver {event_type} trigger: {ex}")

    t = threading.Thread(target=_post, daemon=True)
    t.start()


class PttOverlayApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.current_pos = POS_TOP_CENTER
        self.is_active = False
        self.drag_start_x = 0
        self.drag_start_y = 0
        self.drag_moved = False
        self.press_time = 0.0

        # Window settings
        self.root.overrideredirect(True)
        self.root.wm_attributes("-topmost", True)
        self.root.geometry(f"{WIDTH}x{HEIGHT}+{self.current_pos[0]}+{self.current_pos[1]}")
        self.root.configure(bg=COLOR_BG_IDLE)

        # Main canvas for rounded pill
        self.canvas = tk.Canvas(
            self.root,
            width=WIDTH,
            height=HEIGHT,
            bg=COLOR_BG_IDLE,
            highlightthickness=0
        )
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # Draw rounded pill shape
        self._draw_pill(is_pressed=False)

        # Event bindings
        self.canvas.bind("<ButtonPress-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        self.canvas.bind("<Double-Button-1>", self.toggle_dock_position)

    def _draw_pill(self, is_pressed: bool):
        self.canvas.delete("all")
        bg_color = COLOR_BG_ACTIVE if is_pressed else COLOR_BG_IDLE
        border_color = COLOR_BORDER_ACTIVE if is_pressed else COLOR_BORDER_IDLE
        text_color = COLOR_TEXT_ACTIVE if is_pressed else COLOR_TEXT_IDLE
        text_label = "🎙 TALK" if is_pressed else "🎙 PTT"

        r = 16  # corner radius
        # Rounded rectangle coordinates
        x1, y1, x2, y2 = 2, 2, WIDTH - 3, HEIGHT - 3
        points = [
            x1 + r, y1,
            x2 - r, y1,
            x2, y1,
            x2, y1 + r,
            x2, y2 - r,
            x2, y2,
            x2 - r, y2,
            x1 + r, y2,
            x1, y2,
            x1, y2 - r,
            x1, y1 + r,
            x1, y1
        ]
        self.canvas.create_polygon(points, fill=bg_color, outline=border_color, width=2, smooth=True)
        self.canvas.create_text(
            WIDTH // 2,
            HEIGHT // 2,
            text=text_label,
            fill=text_color,
            font=("DejaVu Sans", 11, "bold")
        )

    def on_press(self, event):
        self.drag_start_x = event.x
        self.drag_start_y = event.y
        self.drag_moved = False
        self.press_time = time.time()
        self.is_active = True
        self._draw_pill(is_pressed=True)
        post_trigger_async("ptt_start")

    def on_drag(self, event):
        dx = event.x - self.drag_start_x
        dy = event.y - self.drag_start_y
        if abs(dx) > 5 or abs(dy) > 5:
            self.drag_moved = True
            new_x = self.root.winfo_x() + dx
            new_y = self.root.winfo_y() + dy
            # Constrain to 800x480 boundary
            new_x = max(0, min(800 - WIDTH, new_x))
            new_y = max(0, min(480 - HEIGHT, new_y))
            self.root.geometry(f"+{new_x}+{new_y}")

    def on_release(self, event):
        self.is_active = False
        self._draw_pill(is_pressed=False)
        post_trigger_async("ptt_stop")

    def toggle_dock_position(self, event):
        """Toggles between top-center and bottom-right dock positions on double click."""
        if self.current_pos == POS_TOP_CENTER:
            self.current_pos = POS_BOTTOM_RIGHT
        else:
            self.current_pos = POS_TOP_CENTER
        self.root.geometry(f"+{self.current_pos[0]}+{self.current_pos[1]}")


def main():
    if not tk:
        logger.error("tkinter module not installed; cannot launch overlay widget")
        sys.exit(1)

    display = os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")
    if not display:
        logger.warning("No DISPLAY or WAYLAND_DISPLAY environment variable set; overlay exiting")
        sys.exit(0)

    root = tk.Tk()
    app = PttOverlayApp(root)
    logger.info("Xiaozhi PTT floating overlay initialized (110x44)")
    try:
        root.mainloop()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
