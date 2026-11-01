#!/usr/bin/env python3
"""Display, Tablet, Haptics & LED Aesthetics Settings GUI for Waveshare ESP32-S3 and Pi 4B."""

import os
import socket
import subprocess
import tkinter as tk
from tkinter import ttk

os.environ.setdefault("DISPLAY", ":0")
os.environ.setdefault("WAYLAND_DISPLAY", "wayland-0")
os.environ.setdefault("XDG_RUNTIME_DIR", "/run/user/1000")

ORIENTATION_FILE = os.path.expanduser("~/.config/display-orientation")
HAPTIC_LEVELS = [("Off", 0), ("Subtle", 80), ("Crisp", 180), ("Strong", 255)]
LED_THEMES = [
    ("🌌 Cosmic Nebula", 0),
    ("✨ Cyberpunk Aurora", 1),
    ("⚡ Quantum Wave", 3),
    ("🔥 Solar Flare", 4)
]


def send_s3_command(cmd: str):
    """Dispatch command to S3 via hid_wake_daemon UDP listener (port 8085) with CDC fallback."""
    if not cmd.endswith("\n"):
        cmd += "\n"
    # Primary: dispatch over UDP 8085 to hid_wake_daemon bridge
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.2)
        sock.sendto(cmd.encode("ascii"), ("127.0.0.1", 8085))
        sock.close()
        return
    except Exception:
        pass

    # Secondary: fallback to direct serial if available
    for port in ["/dev/ttyS3Zero", "/dev/ttyACM0"]:
        if os.path.exists(port):
            try:
                import serial
                with serial.Serial(port, 921600, timeout=0.1) as ser:
                    ser.write(cmd.encode("ascii"))
                    return
            except Exception:
                pass


def set_orientation(mode):
    try:
        env = os.environ.copy()
        env["WAYLAND_DISPLAY"] = "wayland-0"
        env["XDG_RUNTIME_DIR"] = "/run/user/1000"
        subprocess.run(["wlr-randr", "--output", "DPI-1", "--transform", mode], env=env)
        with open(ORIENTATION_FILE, "w") as f:
            f.write(mode)
    except Exception as e:
        print(f"Rotation error: {e}")


def get_saved_backlight():
    try:
        with open("/var/lib/backlight-level", "r") as f:
            return int(f.read().strip())
    except Exception:
        return 100


def apply_backlight(val):
    lvl = int(float(val))
    subprocess.Popen(["sudo", "/usr/local/bin/backlight-set", str(lvl)])


def trigger_screen_off():
    subprocess.Popen(["/home/ayush/.local/bin/screen_off.py"])


class SettingsApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Display & Hardware Settings")
        self.root.geometry("760x450+15+15")
        self.root.configure(bg="#0b132b")
        self.root.resizable(False, False)

        self.current_theme_id = 0
        self.current_hue = 0
        self.haptic_intensity = 180

        self._build_ui()

    def _build_ui(self):
        # Top Header Bar
        top_bar = tk.Frame(self.root, bg="#0b132b")
        top_bar.pack(fill="x", padx=14, pady=(6, 2))
        hdr = tk.Label(top_bar, text="⚙ Display, Haptics & LED Aesthetics", font=("Inter", 11, "bold"), fg="#ffffff", bg="#0b132b")
        hdr.pack(side="left")

        close_top = tk.Button(top_bar, text="✕ Close", font=("Inter", 9, "bold"), bg="#1c2541", fg="#ffffff",
                              activebackground="#3a86ff", command=self.root.destroy, padx=10, pady=1)
        close_top.pack(side="right")

        # --- Section 1: Brightness & Screen Rotation in 2 Columns ---
        top_row = tk.Frame(self.root, bg="#0b132b")
        top_row.pack(fill="x", padx=14, pady=2)

        # Brightness (Left)
        b_frame = tk.LabelFrame(top_row, text=" Backlight ", font=("Inter", 9, "bold"), fg="#48cae4", bg="#0b132b", padx=6, pady=1)
        b_frame.pack(side="left", fill="both", expand=True, padx=(0, 4))
        cur_b = get_saved_backlight()
        slider = tk.Scale(b_frame, from_=1, to=100, orient="horizontal", width=14, sliderlength=30,
                          font=("Inter", 8, "bold"), command=apply_backlight, bg="#0b132b", fg="#ffffff",
                          activebackground="#0077b6", highlightthickness=0)
        slider.set(cur_b)
        slider.pack(fill="x", pady=1)

        # Orientation (Right)
        o_frame = tk.LabelFrame(top_row, text=" Orientation ", font=("Inter", 9, "bold"), fg="#48cae4", bg="#0b132b", padx=6, pady=1)
        o_frame.pack(side="right", fill="both", expand=True, padx=(4, 0))
        btn_row = tk.Frame(o_frame, bg="#0b132b")
        btn_row.pack(fill="x", pady=2)
        for label, mode in [("💻 Land", "normal"), ("📱 Port", "90"), ("🙃 Inv L", "180"), ("🔄 Inv P", "270")]:
            b = tk.Button(btn_row, text=label, font=("Inter", 8, "bold"), bg="#1c2541", fg="#ffffff",
                          activebackground="#3a86ff", padx=1, pady=3,
                          command=lambda m=mode: set_orientation(m))
            b.pack(side="left", expand=True, fill="x", padx=1)

        # --- Section 2: Haptics & LED Aesthetics Card ---
        card = tk.LabelFrame(self.root, text=" Haptics & LED Aesthetics (ESP32-S3 GPIO 16 / 21) ",
                             font=("Inter", 10, "bold"), fg="#00f5d4", bg="#0b132b", padx=10, pady=4)
        card.pack(fill="x", padx=14, pady=3)

        # Haptic Feedback Row
        h_row = tk.Frame(card, bg="#0b132b")
        h_row.pack(fill="x", pady=2)

        h_lbl = tk.Label(h_row, text="Haptic Intensity:", font=("Inter", 9, "bold"), fg="#e0e1dd", bg="#0b132b")
        h_lbl.pack(side="left", padx=(0, 8))

        self.haptic_var = tk.IntVar(value=2)  # Default Crisp (index 2)
        for idx, (name, val) in enumerate(HAPTIC_LEVELS):
            rb = tk.Radiobutton(h_row, text=name, variable=self.haptic_var, value=idx,
                                command=self._on_haptic_changed, font=("Inter", 8, "bold"),
                                bg="#0b132b", fg="#ffffff", selectcolor="#1c2541", activebackground="#0b132b")
            rb.pack(side="left", padx=4)

        test_btn = tk.Button(h_row, text="💥 Test Haptic Tick", font=("Inter", 8, "bold"),
                             bg="#7209b7", fg="#ffffff", activebackground="#f72585", padx=8, pady=2,
                             command=self._test_haptic)
        test_btn.pack(side="right", padx=4)

        detent_btn = tk.Button(h_row, text="🔄 Detent", font=("Inter", 8, "bold"),
                               bg="#3a0ca3", fg="#ffffff", activebackground="#4cc9f0", padx=6, pady=2,
                               command=self._test_detent)
        detent_btn.pack(side="right", padx=2)

        # LED Themes Row
        l_row = tk.Frame(card, bg="#0b132b")
        l_row.pack(fill="x", pady=(6, 2))

        t_lbl = tk.Label(l_row, text="LED Theme:", font=("Inter", 9, "bold"), fg="#e0e1dd", bg="#0b132b")
        t_lbl.pack(side="left", padx=(0, 8))

        self.theme_btns = []
        for name, theme_id in LED_THEMES:
            bg_col = "#3a86ff" if theme_id == self.current_theme_id else "#1c2541"
            btn = tk.Button(l_row, text=name, font=("Inter", 8, "bold"), bg=bg_col, fg="#ffffff",
                            activebackground="#3a86ff", padx=6, pady=3,
                            command=lambda tid=theme_id: self._select_theme(tid))
            btn.pack(side="left", padx=3)
            self.theme_btns.append((btn, theme_id))

        # Hue Fine-Tuning Slider Row
        hue_row = tk.Frame(card, bg="#0b132b")
        hue_row.pack(fill="x", pady=2)

        hue_lbl = tk.Label(hue_row, text="Hue Fine-Tune (0°-360°):", font=("Inter", 8, "bold"), fg="#e0e1dd", bg="#0b132b")
        hue_lbl.pack(side="left", padx=(0, 6))

        self.hue_scale = tk.Scale(hue_row, from_=0, to=360, orient="horizontal", width=12, sliderlength=24,
                                  font=("Inter", 8), command=self._on_hue_changed, bg="#0b132b", fg="#ffffff",
                                  activebackground="#00f5d4", highlightthickness=0)
        self.hue_scale.set(0)
        self.hue_scale.pack(side="left", fill="x", expand=True, padx=4)

        # --- Section 3: Bottom Screen Off Action ---
        a_frame = tk.Frame(self.root, bg="#0b132b")
        a_frame.pack(fill="x", padx=14, pady=(4, 6))

        off_btn = tk.Button(a_frame, text="🌙 Turn Screen Off (Double-Tap Screen to Wake)", font=("Inter", 9, "bold"),
                            bg="#c1121f", fg="#ffffff", activebackground="#780000", pady=6,
                            command=trigger_screen_off)
        off_btn.pack(side="left", expand=True, fill="x")

    def _on_haptic_changed(self):
        idx = self.haptic_var.get()
        self.haptic_intensity = HAPTIC_LEVELS[idx][1]
        if self.haptic_intensity > 0:
            send_s3_command(f"CMD:HAPTIC 1 {self.haptic_intensity} 15\n")

    def _test_haptic(self):
        send_s3_command(f"CMD:HAPTIC 1 {self.haptic_intensity} 15\n")

    def _test_detent(self):
        send_s3_command(f"CMD:HAPTIC 4 {self.haptic_intensity} 12\n")

    def _select_theme(self, theme_id):
        self.current_theme_id = theme_id
        for btn, tid in self.theme_btns:
            btn.configure(bg="#3a86ff" if tid == theme_id else "#1c2541")
        self._dispatch_led_pattern()

    def _on_hue_changed(self, val):
        self.current_hue = int(float(val))
        self._dispatch_led_pattern()

    def _dispatch_led_pattern(self):
        send_s3_command(f"CMD:LED_PATTERN {self.current_theme_id} {self.current_hue} 100\n")


def main():
    root = tk.Tk()
    app = SettingsApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
