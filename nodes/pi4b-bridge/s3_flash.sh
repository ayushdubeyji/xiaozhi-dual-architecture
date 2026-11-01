#!/usr/bin/env bash
# ==============================================================================
# ESP32-S3 Firmware Flasher & Recovery Tool for Node A (Raspberry Pi 4B)
# ==============================================================================
# 1. Stops s3-hid-wake.service to release the serial CDC port.
# 2. Sends CDC command 'CMD:BOOTLOADER' to reboot ESP32-S3 into ROM download mode.
# 3. Waits for ROM bootloader device (USB VID 303a PID 1001) at /dev/ttyACM*.
# 4. Flashes bootloader, partitions, srmodels, and app using venv esptool.
# 5. Restarts s3-hid-wake.service on exit (success or error).
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_DIR="${SCRIPT_DIR}/fw"
VENV_DIR="${SCRIPT_DIR}/venv"
ESPTOOL="${VENV_DIR}/bin/esptool"

echo "======================================================"
echo "    ESP32-S3-Zero Automated Firmware Updater (Pi 4B)  "
echo "======================================================"

# Ensure s3-hid-wake service is restarted on script completion or failure
cleanup() {
    echo ""
    echo "=== [5/5] Restoring s3-hid-wake.service ==="
    if sudo systemctl is-active --quiet s3-hid-wake.service 2>/dev/null; then
        echo "s3-hid-wake.service is already active."
    else
        echo "Restarting s3-hid-wake.service..."
        sudo systemctl restart s3-hid-wake.service || true
    fi
    echo "Done."
}
trap cleanup EXIT

# 1. Validate Firmware Files
echo "=== [1/5] Validating Firmware Binaries in ${FW_DIR} ==="
if [ ! -d "${FW_DIR}" ]; then
    echo "ERROR: Firmware directory not found: ${FW_DIR}" >&2
    exit 1
fi

BOOTLOADER="${FW_DIR}/bootloader.bin"
PARTITION_TABLE="${FW_DIR}/partition-table.bin"
SRMODELS="${FW_DIR}/srmodels.bin"

# Detect app binary with fallback: prefer firmware.bin, then esp32_s3_zero_acoustic_node.bin
if [ -f "${FW_DIR}/firmware.bin" ]; then
    APP_BIN="${FW_DIR}/firmware.bin"
elif [ -f "${FW_DIR}/esp32_s3_zero_acoustic_node.bin" ]; then
    APP_BIN="${FW_DIR}/esp32_s3_zero_acoustic_node.bin"
else
    APP_BIN=""
fi

MISSING=0
for f in "$BOOTLOADER" "$PARTITION_TABLE" "$SRMODELS"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: Missing required binary: $f" >&2
        MISSING=1
    fi
done

if [ -z "$APP_BIN" ] || [ ! -f "$APP_BIN" ]; then
    echo "ERROR: Missing application firmware: no 'firmware.bin' or 'esp32_s3_zero_acoustic_node.bin' found in ${FW_DIR}." >&2
    MISSING=1
fi

if [ "$MISSING" -ne 0 ]; then
    echo "ERROR: Incomplete firmware bundle. Please ensure bootloader, partition-table, srmodels, and firmware.bin exist in ${FW_DIR}." >&2
    exit 1
fi
echo "All firmware binaries validated:"
echo " - Bootloader:      $(ls -lh "$BOOTLOADER" | awk '{print $5}')"
echo " - Partition Table: $(ls -lh "$PARTITION_TABLE" | awk '{print $5}')"
echo " - SR Models:       $(ls -lh "$SRMODELS" | awk '{print $5}')"
echo " - App Image:       $(basename "$APP_BIN") ($(ls -lh "$APP_BIN" | awk '{print $5}'))"

# 2. Validate esptool
if [ ! -x "${ESPTOOL}" ]; then
    if command -v esptool >/dev/null 2>&1; then
        ESPTOOL="esptool"
    elif command -v esptool.py >/dev/null 2>&1; then
        ESPTOOL="esptool.py"
    else
        echo "ERROR: esptool not found in ${VENV_DIR}/bin/esptool or system PATH." >&2
        exit 1
    fi
fi
echo "Using esptool: ${ESPTOOL}"

# 3. Stop s3-hid-wake.service to free CDC serial port
echo ""
echo "=== [2/5] Stopping s3-hid-wake.service ==="
if sudo systemctl is-active --quiet s3-hid-wake.service 2>/dev/null; then
    echo "Stopping s3-hid-wake.service..."
    sudo systemctl stop s3-hid-wake.service
    sleep 1
else
    echo "s3-hid-wake.service is not currently running."
fi

# 4. Check if ESP32-S3 is already in ROM download mode (VID 303a PID 1001)
is_rom_mode() {
    for dev in /sys/bus/usb/devices/*; do
        if [ -f "$dev/idVendor" ] && [ -f "$dev/idProduct" ]; then
            local vid
            local pid
            vid=$(cat "$dev/idVendor" 2>/dev/null || true)
            pid=$(cat "$dev/idProduct" 2>/dev/null || true)
            if [ "$vid" = "303a" ] && [ "$pid" = "1001" ]; then
                return 0
            fi
        fi
    done
    return 1
}

find_rom_port() {
    # 1. Match via sysfs USB tree to exact /dev/tty node
    for dev in /sys/bus/usb/devices/*; do
        if [ -f "$dev/idVendor" ] && [ -f "$dev/idProduct" ]; then
            local vid
            local pid
            vid=$(cat "$dev/idVendor" 2>/dev/null || true)
            pid=$(cat "$dev/idProduct" 2>/dev/null || true)
            if [ "$vid" = "303a" ] && [ "$pid" = "1001" ]; then
                for tty in "$dev"/*/tty/* "$dev"/*/*/tty/*; do
                    if [ -d "$tty" ]; then
                        local tty_name
                        tty_name=$(basename "$tty")
                        if [ -e "/dev/$tty_name" ]; then
                            echo "/dev/$tty_name"
                            return 0
                        fi
                    fi
                done
            fi
        fi
    done
    # 2. Match via udevadm property lookup
    for port in /dev/ttyACM* /dev/ttyUSB*; do
        if [ -e "$port" ]; then
            local udev_out
            udev_out=$(udevadm info -q property -n "$port" 2>/dev/null || true)
            if echo "$udev_out" | grep -q "ID_VENDOR_ID=303a" && echo "$udev_out" | grep -q "ID_MODEL_ID=1001"; then
                echo "$port"
                return 0
            fi
        fi
    done
    return 1
}

find_app_port() {
    if [ -e "/dev/ttyS3Zero" ]; then
        echo "/dev/ttyS3Zero"
        return 0
    fi
    for port in /dev/ttyACM* /dev/serial/by-id/*ESP32*; do
        if [ -e "$port" ]; then
            echo "$port"
            return 0
        fi
    done
    return 1
}

echo ""
echo "=== [3/5] Entering ROM Download Mode ==="
if is_rom_mode; then
    echo "ESP32-S3 is ALREADY in ROM download mode (VID 303a PID 1001)."
else
    APP_PORT=$(find_app_port || true)
    if [ -z "$APP_PORT" ]; then
        echo "No active S3 CDC port detected. Waiting 3 seconds for connection..."
        sleep 3
        APP_PORT=$(find_app_port || true)
    fi

    if [ -n "$APP_PORT" ] && [ -e "$APP_PORT" ]; then
        echo "Sending CMD:BOOTLOADER to ${APP_PORT}..."
        PY_INTERP="python3"
        if [ -x "${VENV_DIR}/bin/python3" ]; then
            PY_INTERP="${VENV_DIR}/bin/python3"
        fi
        "${PY_INTERP}" -c "
import serial, time
try:
    with serial.Serial('${APP_PORT}', 115200, timeout=1.5, dsrdtr=False, rtscts=False) as ser:
        ser.dtr = True
        ser.rts = False
        time.sleep(0.05)
        ser.write(b'CMD:BOOTLOADER\n')
        ser.flush()
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if 'ACK:BOOTLOADER' in line:
            print('ACK received: Board rebooting into ROM download mode.')
        else:
            print(f'Command sent (response: {line}).')
except Exception as e:
    print(f'CDC dispatch note: {e}')
" 2>/dev/null || (stty -F "$APP_PORT" 115200 raw -echo 2>/dev/null || true; echo -ne "CMD:BOOTLOADER\n" > "$APP_PORT" 2>/dev/null || true)
    else
        echo "WARNING: Could not find application CDC port (/dev/ttyS3Zero or /dev/ttyACM*)."
        echo "If device is held in GPIO0 BOOT mode, continuing to ROM search..."
    fi

    echo "Waiting for ESP32-S3 ROM bootloader (VID 303a PID 1001)..."
    ROM_FOUND=0
    for i in {1..20}; do
        if is_rom_mode; then
            ROM_FOUND=1
            echo "ESP32-S3 ROM download mode detected after ${i} checks."
            break
        fi
        sleep 0.5
    done

    if [ "$ROM_FOUND" -ne 1 ]; then
        echo "ERROR: Timed out waiting for ESP32-S3 ROM download mode." >&2
        echo "Please verify USB connection." >&2
        exit 1
    fi
fi

sleep 1
ROM_PORT=$(find_rom_port || true)
if [ -z "$ROM_PORT" ]; then
    echo "ERROR: ROM device detected on USB but no /dev/ttyACM* port found." >&2
    exit 1
fi
echo "Target ROM Port: ${ROM_PORT}"

# 5. Flash Firmware using esptool
echo ""
echo "=== [4/5] Flashing Firmware Bundle via esptool ==="
"${ESPTOOL}" --chip esp32s3 -p "${ROM_PORT}" -b 460800 \
    --before default-reset --after hard-reset write-flash \
    --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x0 "${BOOTLOADER}" \
    0x8000 "${PARTITION_TABLE}" \
    0x20000 "${APP_BIN}" \
    0x260000 "${SRMODELS}"

echo ""
echo "Flash completed successfully! ESP32-S3 rebooting into application mode."
