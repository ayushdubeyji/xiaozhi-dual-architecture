import re
import os

paths = [
    r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part1_chapters_1_to_3.html',
    r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part2_chapters_4_to_7.html',
    r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part3_chapters_8_to_10.html'
]

for p in paths:
    name = os.path.basename(p)
    with open(p, 'r', encoding='utf-8') as f:
        text = f.read()
    
    tables = len(re.findall(r'<table', text))
    wraps = len(re.findall(r'<div class="spec-table-wrap">', text))
    
    # Check if any table is NOT immediately inside a wrap
    # Look for table not preceded by <div class="spec-table-wrap">
    unwrapped = []
    for match in re.finditer(r'<table', text):
        pos = match.start()
        prefix = text[max(0, pos-100):pos]
        if 'spec-table-wrap' not in prefix:
            unwrapped.append(pos)
            
    print(f"{name}:")
    print(f"  Total tables: {tables}, wraps: {wraps}, unwrapped tables: {len(unwrapped)}")
