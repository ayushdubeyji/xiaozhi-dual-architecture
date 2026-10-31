import os
import re

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
PART1_PATH = os.path.join(DIR, 'part1_chapters_1_to_3.html')

with open(PART1_PATH, 'r', encoding='utf-8') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if '<section id="mini-' in line:
        print(f"Line {i+1}: {line.strip()}")
