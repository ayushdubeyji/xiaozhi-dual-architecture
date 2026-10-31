import os
import re

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
PART1_PATH = os.path.join(DIR, 'part1_chapters_1_to_3.html')
PART2_PATH = os.path.join(DIR, 'part2_chapters_4_to_7.html')
PART3_PATH = os.path.join(DIR, 'part3_chapters_8_to_10.html')
MASTER_OUTPUT = os.path.join(DIR, 'cyberdeck_rover_megaseries.html')
INDEX_OUTPUT = os.path.join(DIR, 'index.html')

print("1. Loading part files...")
with open(PART1_PATH, 'r', encoding='utf-8') as f:
    p1 = f.read()

with open(PART2_PATH, 'r', encoding='utf-8') as f:
    p2 = f.read()

with open(PART3_PATH, 'r', encoding='utf-8') as f:
    p3 = f.read()

# ==============================================================================
# ENHANCE PART 1
# ==============================================================================
print("2. Enhancing Part 1 (adding 15 labs & MathJax formulas)...")

# Convert math-block <code> to $$
def convert_math_blocks(text):
    def repl(m):
        code_content = m.group(1).strip()
        # Clean up any inner HTML entities or tags
        clean_math = code_content.replace('&approx;', '\\approx ').replace('&gt;', '>').replace('&lt;', '<')
        return f'<div class="math-block">$$\n{clean_math}\n$$'
    return re.sub(r'<div class="math-block">\s*<code>(.*?)</code>', repl, text, flags=re.DOTALL)

p1 = convert_math_blocks(p1)

# Definitions of labs for Part 1
lab_1_1 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 1.1</span>
        <h4>I2C1 Hardware Interrogation &amp; Touch Controller Binding</h4>
        <p>Verify electrical coexistence across the 40-pin header by probing the Goodix GT911 capacitive digitizer on <code>I2C1</code> (GPIO 2/3) while validating zero conflict with the DPI parallel bus:</p>
        <ol>
          <li>Scan the hardware I2C1 bus for the Goodix digitizer address:
            <pre><code class="language-bash">sudo i2cdetect -y 1</code></pre>
            Confirm presence of device address <code>0x14</code> (or <code>UU</code> when the <code>goodix-gt911</code> kernel driver is actively bound).
          </li>
          <li>Monitor hardware edge-triggered interrupt activity on <code>GPIO 4</code> during touchscreen contact:
            <pre><code class="language-bash">sudo gpiomon -c gpiochip0 4</code></pre>
            Observe low-to-high state transitions on finger press, verifying sub-5ms tactile responsiveness.
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="1.1">
            <span class="checkmark"></span>
            Mark Laboratory 1.1 Complete
          </label>
        </div>
      </div>
"""

lab_1_2 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 1.2</span>
        <h4>DPI-1 Parallel Bus Driver Binding &amp; Pixel Clock Verification</h4>
        <p>Validate Sitronix ST7265 800x480 RGB666 parallel interface timing and confirm zero frame drops via DRM/KMS:</p>
        <ol>
          <li>Query active DRM KMS display connector parameters:
            <pre><code class="language-bash">kmsprint -m | grep -A 8 "DPI-1"</code></pre>
            Verify mode timing: <code>800x480@60Hz</code>, pixel clock <code>25.0 MHz</code>, HFP=8, HSYNC=4, HBP=8, VFP=8, VSYNC=4, VBP=8.
          </li>
          <li>Validate the device-tree overlay symbol compilation:
            <pre><code class="language-bash">dtc -I fs /proc/device-tree | grep -i "st7265"</code></pre>
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="1.2">
            <span class="checkmark"></span>
            Mark Laboratory 1.2 Complete
          </label>
        </div>
      </div>
"""

lab_1_3 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 1.3</span>
        <h4>MP3302 Inductive Boost Benchmarking &amp; Quadratic Dimming Curve</h4>
        <p>Audit the active-low transistor PWM dimming circuit on <code>GPIO 12</code> and measure physical boost converter efficiency:</p>
        <ol>
          <li>Probe the anode of the display backlight LED string using a digital multimeter or oscilloscope:
            <pre><code class="language-bash">/usr/local/bin/backlight-set 100  # Inverted PWM duty cycle 0% (maximum LED current)
/usr/local/bin/backlight-set 50   # Duty cycle 75% via Stevens' Law curve</code></pre>
            Verify that output rail measures $\\approx 18.2\\text{ V}$ at 100% and drops smoothly to $\\approx 15.1\\text{ V}$ at 10% without acoustic inductor whine.
          </li>
          <li>Check cold-boot GPIO state in <code>/boot/firmware/config.txt</code> to prevent uninitialized black screen hangs:
            <pre><code class="language-bash">pinctrl get 12  # Must confirm 'op pd dl' (Output, Pull-Down, Driven Low)</code></pre>
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="1.3">
            <span class="checkmark"></span>
            Mark Laboratory 1.3 Complete
          </label>
        </div>
      </div>
"""

lab_1_4 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 1.4</span>
        <h4>Dynamic Thermal Hysteresis Fan Tuning &amp; 2GB RAM Power Profiling</h4>
        <p>Stress-test the Cortex-A72 quad-core cluster and tune the hardware hysteresis loop on <code>GPIO 21</code>:</p>
        <ol>
          <li>Execute a 4-core thermal saturation benchmark:
            <pre><code class="language-bash">stress-ng --cpu 4 --cpu-method matrixprod --timeout 180s &
while true; do vcgencmd measure_temp; sleep 2; done</code></pre>
          </li>
          <li>Confirm fan activation at $55.0^\\circ\\text{C}$ and deactivation at $50.0^\\circ\\text{C}$ (5000 m°C hysteresis):
            <pre><code class="language-bash">vcgencmd get_throttled  # Expected: 0x0 (Zero throttling occurred)</code></pre>
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="1.4">
            <span class="checkmark"></span>
            Mark Laboratory 1.4 Complete
          </label>
        </div>
      </div>
"""

lab_2_1 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 2.1</span>
        <h4>Sony IMX519 V4L2 60FPS Buffer Bloat Elimination</h4>
        <p>Configure the 2-lane MIPI CSI-2 interface for minimal pipeline latency:</p>
        <ol>
          <li>Configure 1280x720 60FPS YUYV capture format:
            <pre><code class="language-bash">v4l2-ctl -d /dev/video0 --set-fmt-video=width=1280,height=720,pixelformat=YUYV --set-parm=60</code></pre>
          </li>
          <li>Measure capture latency and verify zero driver queue buildup:
            <pre><code class="language-bash">v4l2-ctl -d /dev/video0 --stream-mmap=3 --stream-count=300</code></pre>
            Confirm continuous 60.00 FPS throughput without dropped V4L2 buffers.
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="2.1">
            <span class="checkmark"></span>
            Mark Laboratory 2.1 Complete
          </label>
        </div>
      </div>
"""

lab_2_2 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 2.2</span>
        <h4>C++20 Lockless SPSC Ring Buffer Benchmarking</h4>
        <p>Compile and verify the 64-byte cache-line aligned lockless SPSC queue:</p>
        <ol>
          <li>Compile the standalone atomic benchmark:
            <pre><code class="language-bash">g++ -O3 -std=c++20 -pthread -march=armv8-a test_spsc_queue.cpp -o test_spsc_queue
./test_spsc_queue</code></pre>
          </li>
          <li>Verify atomic memory order semantics:
            Confirm sub-12 nanosecond push/pop operation latency with zero memory allocations across 1,000,000 continuous frames.
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="2.2">
            <span class="checkmark"></span>
            Mark Laboratory 2.2 Complete
          </label>
        </div>
      </div>
"""

lab_2_3 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 2.3</span>
        <h4>ARMv8-A 128-Bit NEON SIMD Laplacian Focus Benchmark</h4>
        <p>Benchmark NEON vector assembly against scalar C loops on the Cortex-A72:</p>
        <ol>
          <li>Compile the NEON Laplacian operator benchmark:
            <pre><code class="language-bash">g++ -O3 -std=c++20 -march=armv8-a+simd bench_neon_focus.cpp -o bench_neon_focus
./bench_neon_focus</code></pre>
          </li>
          <li>Record execution speed: Verify that the 1280x720 focus score calculation completes in under $3.2\\text{ ms}$ (a 4.6x speedup over scalar execution), leaving $>13\\text{ ms}$ per frame for downstream actuation.</li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="2.3">
            <span class="checkmark"></span>
            Mark Laboratory 2.3 Complete
          </label>
        </div>
      </div>
"""

lab_2_4 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 2.4</span>
        <h4>Cortex-A72 Real-Time CPU Core Affinity Pinning</h4>
        <p>Isolate hardware processing threads using Linux real-time FIFO scheduling:</p>
        <ol>
          <li>Launch the vision pipeline binary with Core 2 and Core 3 affinity:
            <pre><code class="language-bash">taskset -c 2,3 chrt -f 80 ./imx519_vision_pipeline</code></pre>
          </li>
          <li>Audit scheduling jitter and CPU core utilization:
            <pre><code class="language-bash">pidstat -p $(pgrep imx519_vision) -t 1</code></pre>
            Verify that Camera Worker executes strictly on Core 2 while NEON Processing Worker executes strictly on Core 3 with zero involuntary context switches.
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="2.4">
            <span class="checkmark"></span>
            Mark Laboratory 2.4 Complete
          </label>
        </div>
      </div>
"""

lab_3_1 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 3.1</span>
        <h4>ESP32-S3-Zero Octal PSRAM Allocation &amp; CPU Benchmark</h4>
        <p>Bring up the Waveshare ESP32-S3-Zero and verify dual-core 240MHz operation with 2MB PSRAM:</p>
        <ol>
          <li>Flash the hardware test app via ESP-IDF:
            <pre><code class="language-bash">idf.py -p /dev/ttyACM0 set-target esp32s3
idf.py -p /dev/ttyACM0 flash monitor</code></pre>
          </li>
          <li>Verify heap initialization in the serial console:
            <pre><code class="language-text">I (320) heap_init: At 0x3c000000 len 0x200000 (2048 KiB): PSRAM
I (340) cpu_start: Pro cpu up, start_cpu_reset = 0
I (350) cpu_start: App cpu up, 240 MHz Dual-Core</code></pre>
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="3.1">
            <span class="checkmark"></span>
            Mark Laboratory 3.1 Complete
          </label>
        </div>
      </div>
"""

lab_3_2 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 3.2</span>
        <h4>Compile-Time Pinout Assertions &amp; Bus Conflict Elimination</h4>
        <p>Audit hardware pin definitions with C11 <code>_Static_assert</code> rules:</p>
        <ol>
          <li>Trigger intentional assertion failure to verify bus protection:
            <pre><code class="language-c">// In board_config.h, set I2S_PIN_DIN = JOYSTICK_ADC_X_GPIO (Pin 4)
#define I2S_PIN_DIN 4
_Static_assert(I2S_PIN_DIN != JOYSTICK_ADC_X_GPIO, "FATAL: I2S DIN collides with Joystick ADC!");</code></pre>
          </li>
          <li>Build firmware and verify compilation fails immediately:
            <pre><code class="language-bash">idf.py build  # Must halt with 'FATAL: I2S DIN collides with Joystick ADC!'</code></pre>
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="3.2">
            <span class="checkmark"></span>
            Mark Laboratory 3.2 Complete
          </label>
        </div>
      </div>
"""

lab_3_3 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 3.3</span>
        <h4>Dual I2S MEMS Microphones &amp; esp-sr Beamforming In PSRAM</h4>
        <p>Interface the dual MSM261S4030H0R microphones and tune spatial noise suppression:</p>
        <ol>
          <li>Probe <code>I2S0</code> signals on oscilloscope:
            Verify continuous 1.024 MHz BCLK on GPIO 41, 16.0 kHz WS frame strobe on GPIO 42, and stereo 24-bit TDM data on GPIO 2.
          </li>
          <li>Stream live beamformed PCM audio over USB CDC:
            <pre><code class="language-bash">python -m sounddevice  # Confirm 'Xiaozhi S3 16kHz Clean Audio' input device</code></pre>
            Speak from $0^\\circ$ and $90^\\circ$ azimuths; verify $\\approx 14\\text{ dB}$ suppression of off-axis background noise.
          </li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="3.3">
            <span class="checkmark"></span>
            Mark Laboratory 3.3 Complete
          </label>
        </div>
      </div>
"""

lab_3_4 = """
      <div class="lab-card">
        <span class="lab-badge">Hands-On Laboratory 3.4</span>
        <h4>Dual-Axis Analog Joystick Calibration &amp; Rotary Encoder Debouncing</h4>
        <p>Calibrate analog ADC non-linearities and test optical rotary encoder tracking:</p>
        <ol>
          <li>Read calibrated joystick vectors in the FreeRTOS telemetry queue:
            <pre><code class="language-bash">idf.py monitor | grep "JOYSTICK:"</code></pre>
            Confirm resting deadzone returns exact $(0.00, 0.00)$ and full deflections reach $(\\pm 1.00, \\pm 1.00)$.
          </li>
          <li>Rotate the salvaged mouse optical encoder 24 detents clockwise and counter-clockwise; verify zero missed pulses in the PCNT counter.</li>
        </ol>
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="3.4">
            <span class="checkmark"></span>
            Mark Laboratory 3.4 Complete
          </label>
        </div>
      </div>
"""

# Insert labs into Part 1 before closing </section> of each mini-chapter
def insert_lab_into_mini(html_text, mini_id, lab_html):
    pattern = rf'(<section id="{mini_id}".*?)(</section>)'
    def repl(m):
        return f"{m.group(1)}\n{lab_html}\n    {m.group(2)}"
    return re.sub(pattern, repl, html_text, count=1, flags=re.DOTALL)

p1 = insert_lab_into_mini(p1, 'mini-1-1', lab_1_1)
p1 = insert_lab_into_mini(p1, 'mini-1-2', lab_1_2)
p1 = insert_lab_into_mini(p1, 'mini-1-3', lab_1_3)
p1 = insert_lab_into_mini(p1, 'mini-1-4', lab_1_4)

# For mini-1-5, update existing Lab 1 badge and add checkbox
p1 = p1.replace(
    '<span class="lab-badge">Hands-On Laboratory 1</span>',
    '<span class="lab-badge">Hands-On Laboratory 1.5</span>'
)
checkbox_1_5 = """
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="1.5">
            <span class="checkmark"></span>
            Mark Laboratory 1.5 Complete
          </label>
        </div>
      </div>
    </section>"""
p1 = re.sub(r'(<span class="lab-badge">Hands-On Laboratory 1\.5</span>.*?</div>\s*</section>)',
            lambda m: m.group(1).replace('</div>\n    </section>', checkbox_1_5), p1, flags=re.DOTALL)

p1 = insert_lab_into_mini(p1, 'mini-2-1', lab_2_1)
p1 = insert_lab_into_mini(p1, 'mini-2-2', lab_2_2)
p1 = insert_lab_into_mini(p1, 'mini-2-3', lab_2_3)
p1 = insert_lab_into_mini(p1, 'mini-2-4', lab_2_4)

# For mini-2-5, update existing Lab 2 badge and add checkbox
p1 = p1.replace(
    '<span class="lab-badge">Hands-On Laboratory 2</span>',
    '<span class="lab-badge">Hands-On Laboratory 2.5</span>'
)
checkbox_2_5 = """
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="2.5">
            <span class="checkmark"></span>
            Mark Laboratory 2.5 Complete
          </label>
        </div>
      </div>
    </section>"""
p1 = re.sub(r'(<span class="lab-badge">Hands-On Laboratory 2\.5</span>.*?</div>\s*</section>)',
            lambda m: m.group(1).replace('</div>\n    </section>', checkbox_2_5), p1, flags=re.DOTALL)

p1 = insert_lab_into_mini(p1, 'mini-3-1', lab_3_1)
p1 = insert_lab_into_mini(p1, 'mini-3-2', lab_3_2)
p1 = insert_lab_into_mini(p1, 'mini-3-3', lab_3_3)
p1 = insert_lab_into_mini(p1, 'mini-3-4', lab_3_4)

# For mini-3-5, update existing Lab 3 badge and add checkbox
p1 = p1.replace(
    '<span class="lab-badge">Hands-On Laboratory 3</span>',
    '<span class="lab-badge">Hands-On Laboratory 3.5</span>'
)
checkbox_3_5 = """
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="3.5">
            <span class="checkmark"></span>
            Mark Laboratory 3.5 Complete
          </label>
        </div>
      </div>
    </section>"""
p1 = re.sub(r'(<span class="lab-badge">Hands-On Laboratory 3\.5</span>.*?</div>\s*</section>)',
            lambda m: m.group(1).replace('</div>\n    </section>', checkbox_3_5), p1, flags=re.DOTALL)


# ==============================================================================
# ENHANCE PART 2 (Add Checkboxes to all 20 labs)
# ==============================================================================
print("3. Enhancing Part 2 (adding checkboxes to 20 labs)...")
for ch in range(4, 8):
    for mini in range(1, 6):
        lab_id = f"{ch}.{mini}"
        # Look for Hands-On Lab {lab_id} in an exercise-box
        pattern = rf'(<div class="exercise-box">\s*<h5><span class="ch-badge">Hands-On Lab {re.escape(lab_id)}</span>.*?)(</div>)'
        
        def make_repl(target_id):
            return lambda m: m.group(1) + f"""
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="{target_id}">
            <span class="checkmark"></span>
            Mark Laboratory {target_id} Complete
          </label>
        </div>
      """ + m.group(2)
        
        p2 = re.sub(pattern, make_repl(lab_id), p2, count=1, flags=re.DOTALL)


# ==============================================================================
# ENHANCE PART 3 (Standardize IDs and add Checkboxes to all 15 labs)
# ==============================================================================
print("4. Enhancing Part 3 (standardizing IDs & adding checkboxes to 15 labs)...")
# Replace id="mc-X-Y" with id="mini-X-Y"
p3 = re.sub(r'id="mc-(\d+)-(\d+)"', r'id="mini-\1-\2"', p3)

for ch in range(8, 11):
    for mini in range(1, 6):
        lab_id = f"{ch}.{mini}"
        if lab_id == "10.5":
            pattern = rf'(<span class="lab-badge">Master Capstone Laboratory 10\.5</span>.*?)(</div>\s*</section>)'
        else:
            pattern = rf'(<span class="lab-badge">Hands-On Laboratory {re.escape(lab_id)}</span>.*?)(</div>\s*</section>)'
        
        def make_p3_repl(target_id):
            return lambda m: m.group(1) + f"""
        <div class="lab-completion-wrap">
          <label class="lab-check-container">
            <input type="checkbox" class="lab-checkbox" data-lab="{target_id}">
            <span class="checkmark"></span>
            Mark Laboratory {target_id} Complete
          </label>
        </div>
      """ + m.group(2)
        
        p3 = re.sub(pattern, make_p3_repl(lab_id), p3, count=1, flags=re.DOTALL)

# Strip redundant <style> tags from Part 2 and Part 3 so they inherit the unified design system
p2_clean = re.sub(r'<style>.*?</style>', '', p2, flags=re.DOTALL)
p3_clean = re.sub(r'<style>.*?</style>', '', p3, flags=re.DOTALL)

# Also strip any preamble comments or redundant DOCTYPE if present
p2_clean = re.sub(r'<!-- ===.*?=== -->', '', p2_clean, count=1, flags=re.DOTALL).strip()
p3_clean = re.sub(r'<!-- ===.*?=== -->', '', p3_clean, count=1, flags=re.DOTALL).strip()

print(f"Enriched Part 1 length: {len(p1):,} chars")
print(f"Cleaned Part 2 length: {len(p2_clean):,} chars")
print(f"Cleaned Part 3 length: {len(p3_clean):,} chars")

# Save enriched part 1 back to file
with open(PART1_PATH, 'w', encoding='utf-8') as f:
    f.write(p1)
print("Updated part1_chapters_1_to_3.html with 15 labs!")

# Save enriched part 2 back to file
with open(PART2_PATH, 'w', encoding='utf-8') as f:
    f.write(p2)
print("Updated part2_chapters_4_to_7.html with completion checkboxes!")

# Save enriched part 3 back to file
with open(PART3_PATH, 'w', encoding='utf-8') as f:
    f.write(p3)
print("Updated part3_chapters_8_to_10.html with completion checkboxes!")
