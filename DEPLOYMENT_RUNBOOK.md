# DEPLOYMENT RUNBOOK: DUAL-NODE XIAOZHI AI ARCHITECTURE & ESP32-S3-ZERO EDGE ACOUSTIC/HID NODE

## 1. System Overview & Architecture

The system implements a distributed dual-node voice AI architecture paired with a dedicated USB-attached acoustic/HID edge node:
- **Edge Acoustic & HID Trigger Node:** Waveshare ESP32-S3-Zero (Dual-core LX7 @ 240MHz, 4MB Flash, 2MB PSRAM). Directly attached via native USB-C to Raspberry Pi 4B. Handles dual-mic beamformed wake word detection (`esp-sr` WakeNet), dedicated Push-To-Talk (PTT), non-blocking USB HID keystroke trigger (`Ctrl+Shift+Alt+W`), ESP-NOW rover telemetry relay, and optional emergency RNDIS Ethernet tethering.
- **Node A (Local Voice Bridge - Raspberry Pi 4B, 2GB RAM):** IP `192.168.0.106`. Runs local `xiaozhi-pi` daemon, USB HID listener, low-latency ASR routing, and non-blocking desktop widgets. Memory budget strictly constrained under 450MB.
- **Node B (Master Workbench Hub - Lubuntu i3 Laptop, 4GB RAM):** IP `192.168.0.111`. Hosts secondary Xiaozhi instance (`xiaozhi-workbench`), Nginx reverse proxy, and central SQLite shared memory database (`federation_memory.db`).
- **Federation Protocol:** MCP-over-WebSocket (`ws://192.168.0.111:8000/ws/xiaozhi-peer`).

---

## 2. Hardware Pinout & Wiring Matrix (Waveshare ESP32-S3-Zero)

All peripherals share the compact Waveshare ESP32-S3-Zero form factor. Non-strapping pins are strictly assigned to prevent bootloader lockups.

| Peripheral | Signal | ESP32-S3 Pin | Hardware Connection Notes |
| :--- | :--- | :--- | :--- |
| **INMP441 Mic Left** | `BCLK` | **GPIO 4** | Shared Bit Clock line with Right Mic |
| | `WS` | **GPIO 5** | Shared Word Select / LRCLK line |
| | `SD` (DIN) | **GPIO 6** | Shared Serial Data Out into ESP32 I2S DMA |
| | `L/R` | **GND** | Tied to GND (Configures Left Channel) |
| | `VDD` / `GND` | **3.3V / GND** | Clean regulated rail |
| **INMP441 Mic Right**| `BCLK` | **GPIO 4** | Shared Bit Clock |
| | `WS` | **GPIO 5** | Shared Word Select |
| | `SD` (DIN) | **GPIO 6** | Shared Serial Data |
| | `L/R` | **3.3V** | Tied to 3.3V (Configures Right Channel) |
| | `VDD` / `GND` | **3.3V / GND** | Clean regulated rail |
| **Push-To-Talk (PTT)**| `PTT_SW` | **GPIO 0** / **GPIO 1** | Normally Open momentary tactile switch to GND (Internal Pull-Up enabled) |
| **Status WS2812** | `DATA` | **GPIO 21** | Onboard NeoPixel (RMT driver: Idle=Dim Blue, Wake=Cyan, Streaming=Green, Error=Red) |
| **Native USB D-** | `USB_DM` | **GPIO 19** | Routed directly through USB-C to Pi 4B USB-A |
| **Native USB D+** | `USB_DP` | **GPIO 20** | Routed directly through USB-C to Pi 4B USB-A |
| **Reserved SPI2 Display** | `DC` | **GPIO 7** | *Reserved in `board_pins.h` for future SPI LCD* |
| | `RST` | **GPIO 8** | *Reserved* |
| | `BLK` | **GPIO 9** | *Reserved (Backlight PWM)* |
| | `CS` | **GPIO 10** | *Reserved (SPI Chip Select)* |
| | `MOSI` | **GPIO 11** | *Reserved (SPI Data Out)* |
| | `SCK` | **GPIO 12** | *Reserved (SPI Clock)* |
| | `MISO` | **GPIO 13** | *Reserved (SPI Data In)* |

---

## 3. Firmware Flashing (ESP32-S3-Zero)

### 3.1 Prerequisites & Toolchain
Ensure ESP-IDF v5.1 or v5.2 is sourced:
```bash
# Sourcing ESP-IDF environment
. $HOME/esp/esp-idf/export.sh
```

### 3.2 Build & Flash Commands
Navigate to the firmware directory:
```bash
cd xiaozhi_dual_architecture/firmware/esp32-s3-zero

# 1. Set ESP32-S3 target
idf.py set-target esp32s3

# 2. Verify sdkconfig.defaults
# Ensure 4MB Flash QIO, 2MB Quad PSRAM, TinyUSB HID, and esp-sr AFE are enabled
idf.py reconfigure

# 3. Compile the firmware
idf.py build

# 4. Put S3-Zero in Download Mode (Hold BOOT/GPIO 0, tap RESET, release BOOT)
# Flash over native USB CDC/ACM
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 4. Node A Provisioning (Raspberry Pi 4B - 192.168.0.106)

### 4.1 System Setup & Memory Cgroup Protection (<500MB RAM)
Run the automated Pi provisioning script:
```bash
cd xiaozhi_dual_architecture/nodes/pi4b-bridge
chmod +x setup_pi4b.sh
sudo ./setup_pi4b.sh
```
What `setup_pi4b.sh` configures:
1. **2048MB Swapfile:** Configures `/etc/dphys-swapfile` with `CONF_SWAPSIZE=2048` to prevent compilation/runtime OOM.
2. **Udev Rules:** Adds `/etc/udev/rules.d/99-s3-hid.rules` granting non-root read permissions on the TinyUSB HID device node (`/dev/input/by-id/usb-Espressif*`).
3. **Python Virtual Environment:** Creates an isolated environment under `/opt/xiaozhi-pi-venv` with `evdev`, `websockets`, `aiohttp`, and `psutil`.

### 4.2 Systemd Unit Installation
Install the services with strict memory cgroup constraints (`MemoryMax=450M`):
```bash
sudo cp s3-hid-wake.service /etc/systemd/system/
sudo cp xiaozhi-pi.service /etc/systemd/system/

sudo systemctl daemon-reload
sudo systemctl enable --now s3-hid-wake.service
sudo systemctl enable --now xiaozhi-pi.service
```

### 4.3 Emergency RNDIS Ethernet Toggle Shortcut (`S3_Internet.desktop`)
When the primary Wi-Fi or router fails, the S3 can act as an emergency RNDIS USB modem connecting to Wi-Fi SSID `Ayush` / `alibaba2123`:
1. Copy `S3_Internet.desktop` to the Raspberry Pi desktop:
   ```bash
   cp /nodes/pi4b-bridge/S3_Internet.desktop ~/Desktop/
   chmod +x ~/Desktop/S3_Internet.desktop
   gio set ~/Desktop/S3_Internet.desktop metadata::trusted true
   ```
2. **Behavior:**
   - Default state: S3 runs TinyUSB HID + Audio + ESP-NOW. Pi uses standard Wi-Fi adapter.
   - When clicked: The script sends a control command to the S3 to associate with SSID `Ayush` (pass: `alibaba2123`), initiates USB RNDIS network interface `usb0`, triggers `dhclient usb0`, and routes traffic through the S3.
   - Double-clicking again cleanly tears down `usb0` and restores default gateway to Wi-Fi adapter.

### 4.4 Push-To-Talk Desktop Trigger (`Xiaozhi_PTT.desktop`)
For non-blocking voice input directly from the desktop:
```bash
cp /nodes/pi4b-bridge/Xiaozhi_PTT.desktop ~/Desktop/
chmod +x ~/Desktop/Xiaozhi_PTT.desktop
gio set ~/Desktop/Xiaozhi_PTT.desktop metadata::trusted true
```
Clicking or pressing the global shortcut immediately dispatches a listening trigger to `xiaozhi-pi` without stealing window focus.

### 4.5 ESP-NOW Mecanum Rover Direction Relay
When S3 is connected and RNDIS is idle:
- Direction commands (`FORWARD`, `BACKWARD`, `STRAFE_LEFT`, `STRAFE_RIGHT`, `ROTATE_CW`, `ROTATE_CCW`, `STOP`) received by `xiaozhi-pi` are relayed through the S3-Zero's onboard 2.4GHz radio via ESP-NOW to the Mecanum Rover bridge (ESP32-C3 / RP2040).
- Zero interference with local Wi-Fi when channel is locked to Channel 6.

---

## 5. Node B Setup (Master Workbench Hub - Lubuntu i3 @ 192.168.0.111)

### 5.1 Nginx Reverse Proxy Configuration
Deploy the Nginx federation configuration snippet:
```bash
sudo cp xiaozhi_dual_architecture/federation/workbench-bridge/nginx_xiaozhi_federation.conf /etc/nginx/sites-available/xiaozhi-federation.conf
sudo ln -sf /etc/nginx/sites-available/xiaozhi-federation.conf /etc/nginx/sites-enabled/

# Verify syntax and reload
sudo nginx -t
sudo systemctl reload nginx
```
The configuration restricts the `/ws/xiaozhi-peer` WebSocket endpoint strictly to `127.0.0.1` and `192.168.0.0/24`.

### 5.2 Launch Federation MCP Server
```bash
cd xiaozhi_dual_architecture/federation/workbench-bridge
python3 -m venv venv
source venv/bin/activate
pip install fastapi uvicorn websockets psutil

# Start federation server
python3 xiaozhi_federation_mcp.py --role server --node-id xiaozhi-workbench --port 8000
```

---

## 6. Unified Cloud API Configuration & Hot-Swapping

All cloud credentials and routing configs are managed in `/shared/providers/.env` and `data/.config.yaml`.

### 6.1 Environment Configuration (`.env`)
```bash
# ==============================================================================
# XIAOZHI CLOUD PROVIDER CONFIGURATION
# ==============================================================================

# Google Gemini API Keys (Primary & Fallback)
GEMINI_API_KEY="YOUR_GEMINI_API_KEY_HERE"
GEMINI_API_KEY_BACKUP="YOUR_GEMINI_API_KEY_HERE"
GEMINI_BACKUP_API_KEY="YOUR_GEMINI_API_KEY_HERE"
GEMINI_DEFAULT_MODEL="gemini-2.5-flash"

# Groq Cloud API Key (Ultra-low latency Whisper ASR & LLM fallback)
GROQ_API_KEY="YOUR_GROQ_API_KEY_HERE"
GROQ_WHISPER_MODEL="whisper-large-v3-turbo"

# xAI Grok API Key (OpenAI-compatible endpoint)
XAI_GROK_API_KEY="your_xai_grok_key_here"
XAI_BASE_URL="https://api.x.ai/v1"

# Whisper ASR Routing (Options: "groq" or "openai")
WHISPER_PROVIDER="groq"
WHISPER_BASE_URL="https://api.groq.com/openai/v1"

# Inter-Node Federation Endpoints
PEER_WORKBENCH_URL="ws://192.168.0.111:8000/ws/xiaozhi-peer"
PEER_PI_URL="ws://192.168.0.106:8000/ws/xiaozhi-peer"
```

### 6.2 Hot-Swapping Brains via Voice Command or Config
- "Xiaozhi, switch brain to Grok" -> Triggers dynamic provider switch to xAI.
- "Xiaozhi, switch brain to Gemini" -> Reverts to Gemini 2.5 Flash.
- If primary Gemini quota triggers HTTP 429, the router automatically swaps to `GEMINI_API_KEY_BACKUP`, and if both exhaust, seamlessly falls back to Groq Llama 3.3.

---

## 7. Verification & Integration Testing

### 7.1 Verify S3-Zero USB HID Device on Pi 4B
```bash
# Check device enumeration
lsusb | grep -i espressif
ls -la /dev/input/by-id/ | grep -i espressif

# Test evdev listener output
python3 -c "import evdev; print([dev.name for dev in [evdev.InputDevice(path) for path in evdev.list_devices()]])"
```

### 7.2 Run Federation Integration Test Suite
Execute the automated test script verifying MCP handshake, tool calling, SQLite persistence, and 16kHz PCM audio turn generation:
```bash
cd xiaozhi_dual_architecture/federation/workbench-bridge
python test_federation.py
```
Expected output:
```
[INFO] Test 1 PASSED: Discovered tools ['ask_peer_xiaozhi', 'sync_shared_memory', 'agent_dialogue_mode', 'get_node_telemetry']
[INFO] Test 2 PASSED: Peer responded with: [xiaozhi-workbench-test] Processed peer query...
[INFO] Test 3 PASSED: Hub successfully queried client peer: [xiaozhi-pi] Client response...
[INFO] Test 4 PASSED: Verified SQLite persistence (row ID 1)
[INFO] Test 5 PASSED: 4-turn dialogue generated, persisted, and broadcast over WebSocket (1920B frames)
[INFO] Test 6 PASSED: Ingested raw binary PCM frame without protocol disruption
[INFO] Test 7 PASSED: Telemetry read successfully: CPU=0.0%, RAM=...MB
[INFO] Test 8 PASSED: Database cleanly unlinked with zero connection/handle leaks
[INFO] ALL TESTS COMPLETED: 8/8 PASSED
```

---

## 8. Troubleshooting Checklist

| Issue | Root Cause | Solution |
| :--- | :--- | :--- |
| **`PermissionDenied` on `/dev/input/event*`** | Missing udev rule or user not in `input` group | Run `sudo usermod -a -G input $USER` and reload udev: `sudo udevadm control --reload-rules && sudo udevadm trigger`. |
| **Audio capture is silent or clipping** | L/R pin floating on INMP441 microphones | Verify Left Mic L/R is soldered securely to GND, and Right Mic L/R is soldered to 3.3V. |
| **Pi 4B OOM / Process Killed** | Memory exceeded 450MB cgroup threshold | Check swap with `free -m`. Ensure swap is 2048MB. Do not run local LLMs or PyTorch on Node A. |
| **WebSocket connection refused to 192.168.0.111** | Nginx ACL blocking or Uvicorn not running | Verify `nginx_xiaozhi_federation.conf` has `allow 192.168.0.0/24;`. Check backend status: `curl http://192.168.0.111:8000/health`. |
| **RNDIS Ethernet not acquiring IP** | USB mode toggle missed DHCP trigger | Ensure `S3_Internet.desktop` executes `sudo dhclient -v usb0` after USB device initialization. |
