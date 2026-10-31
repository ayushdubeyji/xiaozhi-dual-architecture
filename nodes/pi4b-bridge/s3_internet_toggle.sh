#!/usr/bin/env bash
# ==============================================================================
# S3 Emergency RNDIS Ethernet Toggle
# Toggles USB RNDIS Ethernet on Waveshare ESP32-S3-Zero for Raspberry Pi 4B.
# Sends CMD:RNDIS_ON:Ayush:alibaba2123 or CMD:RNDIS_OFF over USB CDC (/dev/ttyACM0).
# Configures route-metric 50 to avoid route clashes with wlan0.
# ==============================================================================

set -u

STATE_FILE="/tmp/s3_rndis.active"
SSID="Ayush"
PASS="alibaba2123"
ROUTE_METRIC=50

# Locate S3 CDC Serial Port
find_serial_port() {
    if [ -e "/dev/ttyS3Zero" ]; then
        echo "/dev/ttyS3Zero"
        return 0
    elif [ -e "/dev/ttyACM0" ]; then
        echo "/dev/ttyACM0"
        return 0
    fi
    for p in /dev/serial/by-id/*ESP32* /dev/serial/by-id/*303a*; do
        if [ -e "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

# Locate RNDIS Network Interface (usb0, eth1, or rndis0)
find_rndis_iface() {
    for iface in usb0 eth1 rndis0; do
        if [ -d "/sys/class/net/$iface" ]; then
            echo "$iface"
            return 0
        fi
    done
    return 1
}

send_serial_cmd() {
    local port="$1"
    local cmd="$2"
    # Write command via Python to avoid stty tty locking glitches
    python3 -c "
import sys, time
try:
    with open('$port', 'wb', buffering=0) as f:
        f.write('$cmd\n'.encode('utf-8'))
        time.sleep(0.1)
except Exception as e:
    sys.exit(1)
" 2>/dev/null || echo -ne "$cmd\n" > "$port" 2>/dev/null
}

show_toast() {
    local title="$1"
    local msg="$2"
    local icon="$3"
    if command -v notify-send >/dev/null 2>&1; then
        notify-send -u normal -t 3500 -i "$icon" "$title" "$msg" &
    fi
    logger -t s3_internet "$title: $msg"
}

SERIAL_PORT=$(find_serial_port)
if [ -z "$SERIAL_PORT" ]; then
    show_toast "S3 Internet Error" "ESP32-S3 not found on USB CDC." "dialog-error"
    exit 1
fi

# Check current toggle state
if [ -f "$STATE_FILE" ]; then
    # -------------------------------------------------------------
    # TURN OFF RNDIS
    # -------------------------------------------------------------
    echo "[S3-RNDIS] Toggling OFF emergency internet..."
    send_serial_cmd "$SERIAL_PORT" "CMD:RNDIS_OFF"
    send_serial_cmd "$SERIAL_PORT" "CMD:NET_DISABLE"
    
    IFACE=$(find_rndis_iface || true)
    if [ -n "$IFACE" ]; then
        if command -v nmcli >/dev/null 2>&1; then
            nmcli device disconnect "$IFACE" 2>/dev/null || true
        fi
        ip link set dev "$IFACE" down 2>/dev/null || true
    fi

    rm -f "$STATE_FILE"
    show_toast "S3 Internet Deactivated" "RNDIS Disabled. Primary Wi-Fi (wlan0) restored." "network-wireless"
    echo "[S3-RNDIS] RNDIS disabled successfully."
else
    # -------------------------------------------------------------
    # TURN ON RNDIS
    # -------------------------------------------------------------
    echo "[S3-RNDIS] Toggling ON emergency internet (SSID: $SSID)..."
    send_serial_cmd "$SERIAL_PORT" "CMD:RNDIS_ON:$SSID:$PASS"
    send_serial_cmd "$SERIAL_PORT" "CMD:NET_ENABLE"

    # Wait up to 8 seconds for interface to appear
    IFACE=""
    for i in $(seq 1 16); do
        sleep 0.5
        IFACE=$(find_rndis_iface || true)
        if [ -n "$IFACE" ]; then
            break
        fi
    done

    if [ -z "$IFACE" ]; then
        show_toast "S3 Internet Warning" "RNDIS interface did not appear in time." "dialog-warning"
        exit 1
    fi

    echo "[S3-RNDIS] Found interface $IFACE. Bringing up with metric $ROUTE_METRIC..."

    # Configure NetworkManager or ip
    if command -v nmcli >/dev/null 2>&1; then
        CON_NAME="S3-RNDIS-$IFACE"
        if ! nmcli connection show "$CON_NAME" >/dev/null 2>&1; then
            nmcli connection add type ethernet ifname "$IFACE" con-name "$CON_NAME" ipv4.method auto ipv4.route-metric "$ROUTE_METRIC" 2>/dev/null || true
        else
            nmcli connection modify "$CON_NAME" ipv4.route-metric "$ROUTE_METRIC" 2>/dev/null || true
        fi
        nmcli connection up "$CON_NAME" 2>/dev/null || nmcli device connect "$IFACE" 2>/dev/null || true
    else
        ip link set dev "$IFACE" up
        if command -v dhclient >/dev/null 2>&1; then
            dhclient -4 -v "$IFACE" 2>/dev/null &
        fi
    fi

    touch "$STATE_FILE"
    show_toast "S3 Internet Active" "Emergency RNDIS connected on $IFACE\nMetric: $ROUTE_METRIC | SSID: $SSID" "network-wired"
    echo "[S3-RNDIS] RNDIS active on $IFACE."
fi

exit 0
