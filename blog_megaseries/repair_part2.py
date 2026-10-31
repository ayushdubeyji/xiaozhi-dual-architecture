import os
import re

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
P2_PATH = os.path.join(DIR, 'part2_chapters_4_to_7.html')

with open(P2_PATH, 'r', encoding='utf-8') as f:
    text = f.read()

# Fix literal null bytes in code snippet
text = text.replace("b'\x00\x00'", r"b'\x00\x00'")

# Fix \a (\x07) -> \approx
text = text.replace('\x07pprox', r'\approx')

# Fix \b (\x08) -> \begin or \b word boundary
text = text.replace('\x08egin{bmatrix}', r'\begin{bmatrix}')
text = text.replace('\x08egin{cases}', r'\begin{cases}')
text = text.replace('\x08(?:switch to|use|activate|brain to)', r'\b(?:switch to|use|activate|brain to)')
text = text.replace('groq\x08"', r'groq\b"')
text = text.replace('gemini\x08"', r'gemini\b"')
text = text.replace('grok\x08"', r'grok\b"')
text = text.replace('\x08"', r'\b"')


# Fix \f (\x0c) -> \frac
text = text.replace('\x0crac{1}{2}', r'\frac{1}{2}')

# Fix \right] that became ight]
text = text.replace(r'(0\text{x07})' + '\n' + '   ight]', r'(0\text{x07})\right]')
text = text.replace(r'(0\text{x07})' + '\r\n' + '   ight]', r'(0\text{x07})\right]')
text = text.replace('   ight] \\quad (1920 = 0\\text{x0780})', '\\right] \\quad (1920 = 0\\text{x0780})')

# Fix \neg that became newline + eg
text = text.replace('$$\\text{IsMoving} = (\n   eg \\text{Stationary})', '$$\\text{IsMoving} = (\\neg \\text{Stationary})')
text = text.replace('$$\\text{IsMoving} = (\r\n   eg \\text{Stationary})', '$$\\text{IsMoving} = (\\neg \\text{Stationary})')

# Fix \t followed by LaTeX commands
# \text
text = re.sub(r'\text\{', r'\\text{', text)
# \times
text = re.sub(r'\times', r'\\times', text)
# \theta
text = re.sub(r'\theta', r'\\theta', text)
# \tau
text = re.sub(r'\tau_', r'\\tau_', text)
# \mu\text{s}
text = re.sub(r'\\mu\text\{s\}', r'\\mu\\text{s}', text)

# Align stiction kick and minDuty with mecanum-rover skill and firmware
text = text.replace(
    'lasting <strong>180 ms</strong> ($18 \\times 10\\text{ ms}$ control loops)',
    'lasting <strong>220 ms</strong> ($22 \\times 10\\text{ ms}$ control loops)'
)
text = text.replace(
    '<td>38,000 counts</td>\n            <td>Minimum active PWM floor (58.0%)</td>',
    '<td>40,000 counts</td>\n            <td>Minimum active PWM floor (61.0%)</td>'
)
text = text.replace(
    '<td>38,000 counts</td>\r\n            <td>Minimum active PWM floor (58.0%)</td>',
    '<td>40,000 counts</td>\r\n            <td>Minimum active PWM floor (61.0%)</td>'
)
text = text.replace(
    '<td>18 ticks (180 ms)</td>',
    '<td>22 ticks (220 ms)</td>'
)

# Verify no remaining non-printable characters (< 32, not \r \n)
bad_chars = {}
for idx, c in enumerate(text):
    code = ord(c)
    if code < 32 and c not in '\r\n':
        bad_chars[code] = bad_chars.get(code, 0) + 1

print(f"Remaining bad characters in repaired text: {bad_chars}")

with open(P2_PATH, 'w', encoding='utf-8') as f:
    f.write(text)

print(f"Repaired {P2_PATH} successfully! Total size: {len(text)} bytes")
