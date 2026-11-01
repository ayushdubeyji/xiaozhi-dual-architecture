#!/usr/bin/env bash
# ==============================================================================
# Provisioning Script for Node A: Raspberry Pi 4B (2GB RAM)
# Configures 2048MB swap, udev rules, Python venv, desktop shortcuts, and systemd.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_USER="${SUDO_USER:-$(logname 2>/dev/null || echo "ayush")}"
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)

echo "=== [1/6] Validating Privileges and Target User ==="
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: setup_pi4b.sh must be executed with sudo privileges." >&2
    exit 1
fi
echo "Target User: $TARGET_USER"
echo "Target Home: $TARGET_HOME"
echo "Script Dir:  $SCRIPT_DIR"

echo "=== [2/6] Configuring 2048MB Swap File (/etc/dphys-swapfile) ==="
SWAP_CONF="/etc/dphys-swapfile"
if [ -f "$SWAP_CONF" ]; then
    CURRENT_SWAP=$(grep -E "^CONF_SWAPSIZE=" "$SWAP_CONF" | cut -d= -f2 || echo "0")
    if [ "$CURRENT_SWAP" -lt 2048 ]; then
        echo "Updating CONF_SWAPSIZE from ${CURRENT_SWAP}MB to 2048MB..."
        sed -i -E 's/^CONF_SWAPSIZE=[0-9]+/CONF_SWAPSIZE=2048/' "$SWAP_CONF"
        if ! grep -q "^CONF_SWAPSIZE=2048" "$SWAP_CONF"; then
            echo "CONF_SWAPSIZE=2048" >> "$SWAP_CONF"
        fi
        echo "Rebuilding dphys-swapfile..."
        dphys-swapfile swapoff || true
        dphys-swapfile setup
        dphys-swapfile swapon
        echo "Swap updated to 2048MB successfully."
    else
        echo "Swap is already configured to ${CURRENT_SWAP}MB (>= 2048MB)."
    fi
else
    echo "Notice: /etc/dphys-swapfile not found; skipping dphys swap check."
fi

echo "=== [3/6] Installing Udev Rules for ESP32-S3-Zero (VID 0x303A) ==="
UDEV_DEST="/etc/udev/rules.d/99-esp32-s3-zero.rules"
cp "$SCRIPT_DIR/99-esp32-s3-zero.rules" "$UDEV_DEST"
chmod 644 "$UDEV_DEST"
udevadm control --reload-rules
udevadm trigger
echo "Udev rules installed. Reloaded and triggered."

# Ensure user belongs to input, dialout, audio, netdev
for grp in input dialout audio netdev; do
    if getent group "$grp" >/dev/null 2>&1; then
        usermod -aG "$grp" "$TARGET_USER"
        echo "Added $TARGET_USER to group '$grp'."
    fi
done

echo "=== [4/6] Installing System Dependencies (apt) ==="
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y --no-install-recommends \
    python3-venv \
    python3-pip \
    python3-tk \
    libevdev2 \
    libasound2-dev \
    libportaudio2 \
    libopus0 \
    libopus-dev \
    libnotify-bin \
    network-manager \
    espeak-ng

echo "=== [5/6] Setting Up Python Virtual Environment ==="
VENV_DIR="$SCRIPT_DIR/venv"
if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment at $VENV_DIR..."
    sudo -u "$TARGET_USER" python3 -m venv "$VENV_DIR"
fi

echo "Installing/upgrading Python dependencies..."
sudo -u "$TARGET_USER" "$VENV_DIR/bin/pip" install --upgrade pip setuptools wheel
sudo -u "$TARGET_USER" "$VENV_DIR/bin/pip" install -r "$SCRIPT_DIR/requirements.txt"

echo "=== [6/6] Installing Desktop Shortcuts & Systemd Services ==="
# 1. Desktop shortcuts & flashing scripts
DESKTOP_DIR="$TARGET_HOME/Desktop"
if [ -d "$DESKTOP_DIR" ]; then
    cp "$SCRIPT_DIR/S3_Internet.desktop" "$DESKTOP_DIR/"
    cp "$SCRIPT_DIR/Xiaozhi_PTT.desktop" "$DESKTOP_DIR/"
    cp "$SCRIPT_DIR/S3_Update.desktop" "$DESKTOP_DIR/"
    chown "$TARGET_USER:$TARGET_USER" "$DESKTOP_DIR/S3_Internet.desktop" "$DESKTOP_DIR/Xiaozhi_PTT.desktop" "$DESKTOP_DIR/S3_Update.desktop"
    chmod +x "$DESKTOP_DIR/S3_Internet.desktop" "$DESKTOP_DIR/Xiaozhi_PTT.desktop" "$DESKTOP_DIR/S3_Update.desktop"
    echo "Desktop shortcuts installed to $DESKTOP_DIR."
fi

# Ensure scripts are executable
chmod +x "$SCRIPT_DIR/s3_internet_toggle.sh"
chmod +x "$SCRIPT_DIR/s3_flash.sh"

# Install lowprio-build system helper
if [ -f "$SCRIPT_DIR/lowprio-build" ]; then
    cp "$SCRIPT_DIR/lowprio-build" /usr/local/bin/lowprio-build
    chmod +x /usr/local/bin/lowprio-build
    echo "lowprio-build installed to /usr/local/bin/lowprio-build."
fi

# 2. Systemd service units
SYSTEMD_DIR="/etc/systemd/system"
cp "$SCRIPT_DIR/xiaozhi-pi.service" "$SYSTEMD_DIR/"
cp "$SCRIPT_DIR/s3-hid-wake.service" "$SYSTEMD_DIR/"
chmod 644 "$SYSTEMD_DIR/xiaozhi-pi.service" "$SYSTEMD_DIR/s3-hid-wake.service"

systemctl daemon-reload
echo "Systemd unit files registered with MemoryMax=450M cgroup limits."
echo "To enable and start services run:"
echo "  sudo systemctl enable --now xiaozhi-pi.service"
echo "  sudo systemctl enable --now s3-hid-wake.service"

echo "=== Pi 4B Host Bridge Provisioning Complete! ==="
