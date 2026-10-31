import os
import re

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
PART1_PATH = os.path.join(DIR, 'part1_chapters_1_to_3.html')
PART2_PATH = os.path.join(DIR, 'part2_chapters_4_to_7.html')
PART3_PATH = os.path.join(DIR, 'part3_chapters_8_to_10.html')
OUTPUT_PATH = os.path.join(DIR, 'cyberdeck_rover_megaseries.html')
INDEX_OUTPUT_PATH = os.path.join(DIR, 'index.html')

print("Reading input part files...")
with open(PART1_PATH, 'r', encoding='utf-8') as f:
    part1_raw = f.read()

with open(PART2_PATH, 'r', encoding='utf-8') as f:
    part2_raw = f.read()

with open(PART3_PATH, 'r', encoding='utf-8') as f:
    part3_raw = f.read()

print(f"Loaded Part 1: {len(part1_raw)} chars")
print(f"Loaded Part 2: {len(part2_raw)} chars")
print(f"Loaded Part 3: {len(part3_raw)} chars")
