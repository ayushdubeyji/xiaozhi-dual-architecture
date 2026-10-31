import os
import re

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
paths = [
    os.path.join(DIR, 'part1_chapters_1_to_3.html'),
    os.path.join(DIR, 'part2_chapters_4_to_7.html'),
    os.path.join(DIR, 'part3_chapters_8_to_10.html')
]

total_labs = 0
all_labs = []

for p in paths:
    fname = os.path.basename(p)
    with open(p, 'r', encoding='utf-8') as f:
        text = f.read()
    cbs = re.findall(r'data-lab="([^"]+)"', text)
    print(f"{fname}: {len(cbs)} labs found -> {cbs}")
    total_labs += len(cbs)
    all_labs.extend(cbs)

print(f"\nTOTAL LABS ACROSS ALL 3 PARTS: {total_labs}")
expected = [f"{ch}.{m}" for ch in range(1, 11) for m in range(1, 6)]
missing = set(expected) - set(all_labs)
if missing:
    print(f"MISSING LABS: {sorted(missing)}")
else:
    print("SUCCESS: EXACTLY ALL 50/50 LABS PRESENT (1.1 through 10.5)!")
