import re
import os
import json

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
MASTER_FILE = os.path.join(DIR, 'cyberdeck_rover_megaseries.html')

with open(MASTER_FILE, 'r', encoding='utf-8') as f:
    html = f.read()

ids = set(re.findall(r'id=["\']([^"\']+)["\']', html))
hrefs = re.findall(r'href=["\'](#[^"\']+)["\']', html)
broken = [h for h in hrefs if h[1:] not in ids]

print(f"Total IDs: {len(ids)}")
print(f"Total internal links: {len(hrefs)}")
print(f"Broken links: {len(broken)} -> {broken}")

srcs = re.findall(r'src=["\']([^"\']+)["\']', html)
print(f"Script/Image srcs: {srcs}")

# Check tables and table wraps
tables = re.findall(r'<table[\s>]', html)
wraps = re.findall(r'<div class="spec-table-wrap">', html)
print(f"Tables: {len(tables)}, Wraps: {len(wraps)}")

# Check chapters and mini chapters
chapters = re.findall(r'id="chapter-(\d+)"', html)
print(f"Chapters found: {len(chapters)} -> {chapters}")

# Check duplicate IDs
all_ids = re.findall(r'\s+id=["\']([^"\']+)["\']', html)
id_counts = {}
for i in all_ids:
    id_counts[i] = id_counts.get(i, 0) + 1
p2_path = os.path.join(DIR, 'part2_chapters_4_to_7.html')
with open(p2_path, 'r', encoding='utf-8') as f:
    p2_text = f.read()

p2_displays = re.findall(r'\$\$(.*?)\$\$', p2_text, re.DOTALL)
print(f"Total display math blocks in Part 2: {len(p2_displays)}")
for i, d in enumerate(p2_displays):
    bad = [f"{ord(c):#x}" for c in d if ord(c) < 32 and c not in '\n\r']
    has_tab = '\t' in d
    if bad or has_tab:
        print(f"Block {i+1}: bad chars={bad}, has_tab={has_tab}")
        print(f"   Content: {repr(d[:80])}")







import urllib.request

url = 'https://ayushdubey.tech/boost/boost/'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0', 'Cache-Control': 'no-cache'})
with urllib.request.urlopen(req) as resp:
    data = resp.read()
    headers = dict(resp.getheaders())

print(f"\n=== LIVE PROBE: {url} ===")
print(f"Status: {resp.status}")
print(f"X-Appwrite-Deployment-Id: {headers.get('x-appwrite-deployment-id')}")
print(f"Total bytes: {len(data)}")

bad = {}
for c in data:
    if c < 32 and c not in (10, 13):
        bad[c] = bad.get(c, 0) + 1
print(f"Control characters in live response: {bad}")

text = data.decode('utf-8')
print("Integrity checks in live response:")
print("  \\text{EVIOCGRAB}:", "\\text{EVIOCGRAB}" in text)
print("  \\begin{bmatrix}:", "\\begin{bmatrix}" in text)
print("  \\frac{1}{2}:", "\\frac{1}{2}" in text)
print("  \\approx:", "\\approx" in text)
print("  \\neg \\text{Stationary}:", "\\neg \\text{Stationary}" in text)
print("  --sidebar-width: 280px:", "--sidebar-width: 280px" in text)






