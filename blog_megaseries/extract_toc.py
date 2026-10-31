import os
import re
import json

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
paths = [
    os.path.join(DIR, 'part1_chapters_1_to_3.html'),
    os.path.join(DIR, 'part2_chapters_4_to_7.html'),
    os.path.join(DIR, 'part3_chapters_8_to_10.html')
]

chapters = {
    1: {"title": "Physical AI Hardware & Cyberdeck Subsystems", "minis": []},
    2: {"title": "Embedded Vision: IMX519 C++20 Lockless Pipeline", "minis": []},
    3: {"title": "Waveshare ESP32-S3-Zero Hardware Coprocessor", "minis": []},
    4: {"title": "Linux Input, Cgroups & Wayland Touch HUD", "minis": []},
    5: {"title": "Sub-600ms Groq Whisper ASR & Multi-Brain Router", "minis": []},
    6: {"title": "Federated Multi-Agent Swarm (MCP over WebSockets)", "minis": []},
    7: {"title": "RP2040 Omnidirectional Mecanum Kinematics", "minis": []},
    8: {"title": "Acoustic Spatial Audition & Direction-of-Arrival", "minis": []},
    9: {"title": "ESP-NOW Radio Bridge & 3D Spatial Occupancy Mapping", "minis": []},
    10: {"title": "System Integration, Biomimicry & Cosmic Epistemology", "minis": []}
}

for p in paths:
    with open(p, 'r', encoding='utf-8') as f:
        text = f.read()
    
    # Extract sections
    matches = re.finditer(r'<section[^>]*id="mini-(\d+)-(\d+)"[^>]*>(.*?)(?=</section>)', text, re.DOTALL)
    for m in matches:
        ch = int(m.group(1))
        mn = int(m.group(2))
        body = m.group(3)
        h_match = re.search(r'<h[23][^>]*>(.*?)</h[23]>', body)
        title = h_match.group(1) if h_match else f"Mini-Chapter {ch}.{mn}"
        # Clean title
        title = re.sub(r'<[^>]+>', '', title)
        title = re.sub(r'^(Mini-Chapter\s+\d+\.\d+:\s*|\d+\.\d+\s+)', '', title).strip()
        chapters[ch]["minis"].append({"num": f"{ch}.{mn}", "id": f"mini-{ch}-{mn}", "title": title})

with open(os.path.join(DIR, 'toc.json'), 'w', encoding='utf-8') as f:
    json.dump(chapters, f, indent=2)

print("Extracted TOC for all 10 chapters:")
for ch, data in chapters.items():
    print(f"Chapter {ch}: {data['title']} ({len(data['minis'])} mini-chapters)")
    for mn in data['minis']:
        print(f"  {mn['num']}: {mn['title']}")
