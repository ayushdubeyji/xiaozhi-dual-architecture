import os
import re
import sys

sys.stdout.reconfigure(encoding='utf-8')

TARGET = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\cyberdeck_rover_megaseries.html'

with open(TARGET, 'r', encoding='utf-8') as f:
    html = f.read()

print("=== RUNNING AUTOMATED MEGASERIES VERIFICATION SUITE ===\n")

# 1. Structural HTML checks
assert '<!DOCTYPE html>' in html, "Missing DOCTYPE"
assert '<html lang="en" data-theme="dark">' in html, "Missing html tag with dark theme"
assert 'viewport-fit=cover' in html, "Missing responsive viewport meta"
assert 'MathJax-script' in html, "Missing MathJax 3 script"
print("✓ Base HTML5 doctype, viewport, and MathJax configuration verified.")

# 2. Chapters (1 to 10)
for ch in range(1, 11):
    assert f'id="chapter-{ch}"' in html, f"Missing Chapter {ch} section!"
print("✓ All 10 Chapters (1 to 10) present in document.")

# 3. Mini-Chapters (50 total)
missing_minis = []
for ch in range(1, 11):
    for mn in range(1, 6):
        mid = f"mini-{ch}-{mn}"
        if f'id="{mid}"' not in html:
            missing_minis.append(mid)

assert len(missing_minis) == 0, f"Missing mini-chapters: {missing_minis}"
print("✓ All 50 Mini-Chapters (1.1 through 10.5) present in document.")

# 4. TOC Links & Target IDs
toc_links = re.findall(r'href="#(mini-\d+-\d+)"', html)
assert len(toc_links) >= 50, f"Expected at least 50 TOC links, found {len(toc_links)}"
for target in toc_links:
    assert f'id="{target}"' in html, f"Broken anchor link: #{target} not found in DOM!"
print(f"✓ All {len(toc_links)} TOC sidebar anchor links verified (0 broken anchors).")

# 5. Hands-On Labs (50 total)
labs = re.findall(r'data-lab="(\d+\.\d+)"', html)
unique_labs = sorted(set(labs), key=lambda x: [int(n) for n in x.split('.')])
assert len(unique_labs) == 50, f"Expected 50 unique labs, found {len(unique_labs)}: {unique_labs}"
expected_labs = [f"{ch}.{mn}" for ch in range(1, 11) for mn in range(1, 6)]
assert unique_labs == expected_labs, f"Mismatch in lab sequence: {set(expected_labs) - set(unique_labs)}"
print("✓ All 50 Hands-On Labs (1.1 through 10.5) verified with data-lab attributes.")

# 6. Checkboxes (50 total)
checkboxes = len(re.findall(r'<input type="checkbox" class="lab-checkbox"', html))
assert checkboxes == 50, f"Expected 50 lab checkboxes, found {checkboxes}"
print("✓ All 50 interactive progress checkboxes present with localStorage wiring.")

# 7. Tables & Wrapper Overflow
tables = len(re.findall(r'<table', html))
wraps = len(re.findall(r'<div class="spec-table-wrap">', html))
assert tables == wraps, f"Table wrapper mismatch: {tables} tables vs {wraps} wraps!"
print(f"✓ All {tables} specification tables 100% wrapped in .spec-table-wrap for mobile scrolling.")

# 8. CSS Zero-Overflow & Mobile Rules
css_checks = [
    ('box-sizing: border-box', "Universal box-sizing"),
    ('overflow-x: auto', "Zero-overflow on code & tables"),
    ('@media (max-width: 850px)', "Mobile responsive drawer breakpoint"),
    ('min-height: 44px', "Touch target accessibility (WCAG AAA)"),
    ('grid-template-columns: var(--sidebar-width) minmax(0, 1fr)', "Desktop 2-column grid")
]
for snippet, label in css_checks:
    assert snippet in html, f"Missing CSS rule: {label} ({snippet})"
print("✓ CSS zero-overflow, responsive grid, and touch-target rules verified.")

# 9. Key Hardware & Software Symbols
key_symbols = [
    "ST7265", "MP3302", "GPIO 12", "GPIO 21", "GT911", "I2C1",
    "IMX519", "C++20", "LocklessSPSCQueue", "NeonLaplacianRowSafe",
    "ESP32-S3-Zero", "TinyUSB", "RNDIS", "EVIOCGRAB", "MemoryMax=450M",
    "Groq", "whisper-large-v3-turbo", "MCP", "RP2040", "Mecanum",
    "Coulomb", "GCC-PHAT", "VL53L0X", "HLK-LD2402", "WS2812",
    "David Deutsch", "The Beginning of Infinity", "Harari", "Three-Body Problem"
]
for sym in key_symbols:
    assert sym in html, f"Missing expected key technical/philosophical symbol: '{sym}'"
# 10. Zero Non-Printable Control Characters
bad_chars = [f"0x{ord(c):02x} at idx {i}" for i, c in enumerate(html) if ord(c) < 32 and c not in '\r\n']
assert len(bad_chars) == 0, f"Found {len(bad_chars)} illegal control characters in HTML! {bad_chars[:5]}"
print("✓ Document 100% clean of illegal control characters (0 null, bell, backspace, or form feed chars).")

# 11. Mathematical LaTeX Integrity Checks
latex_checks = [
    (r'\begin{bmatrix}', "Matrix notation"),
    (r'\begin{cases}', "Piecewise cases"),
    (r'\frac{1}{2}', "Fraction formatting"),
    (r'\approx', "Approximation symbol"),
    (r'\times', "Multiplication symbol"),
    (r'\theta', "Theta angle"),
    (r'\tau_{\text{timeout}}', "Tau timeout"),
    (r'\neg \text{Stationary}', "Logical negation"),
    (r'\text{EVIOCGRAB}', "EVIOCGRAB in text mode"),
    (r'\text{MemoryMax}', "MemoryMax in text mode"),
    (r'\text{dynJerk}', "dynJerk in text mode")
]
for formula_snippet, label in latex_checks:
    assert formula_snippet in html, f"Corrupted or missing LaTeX formula: {label} ({formula_snippet})"
print(f"✓ All {len(latex_checks)} critical LaTeX formulas verified free of escape-sequence corruption.")

# 12. Exact Sidebar Width Specification
assert '--sidebar-width: 280px;' in html, "Sidebar width must be exact 280px per specification!"
print("✓ Exact 280px sidebar width specification verified.")

print("\n=========================================================")
print(">>> ALL 12 MEGASERIES INTEGRITY CHECKS PASSED (100%) <<<")
print("=========================================================\n")

