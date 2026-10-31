import re
import os

parts = [
    (r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part1_chapters_1_to_3.html', 1, 3),
    (r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part2_chapters_4_to_7.html', 4, 7),
    (r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part3_chapters_8_to_10.html', 8, 10),
]

for filepath, start_ch, end_ch in parts:
    print(f"=== Inspecting {os.path.basename(filepath)} (Chapters {start_ch} to {end_ch}) ===")
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Mini chapters
    mini_ids = re.findall(r'<section id="mini-([^"]+)"', content)
    h2s = re.findall(r'<h2>Mini-Chapter ([^:]+):', content)
    print(f"Mini chapter sections found: {len(mini_ids)} -> {mini_ids}")
    print(f"Mini chapter H2 headings found: {len(h2s)} -> {h2s}")
    
    # Tables
    tables = len(re.findall(r'<table', content))
    wraps = len(re.findall(r'<div class="spec-table-wrap">', content))
    print(f"Tables: {tables}, Spec Table Wrappers: {wraps}")
    
    # Code blocks
    pre_blocks = len(re.findall(r'<pre>', content))
    code_blocks = len(re.findall(r'<code>', content))
    print(f"<pre> count: {pre_blocks}, <code> count: {code_blocks}")
    
    # Labs
    lab_divs = re.findall(r'class="[^"]*hands-on-lab[^"]*"', content)
    lab_headers = re.findall(r'Hands-On Laboratory [^\n<]+', content)
    course_exercises = re.findall(r'Course Exercise [^\n<]+', content)
    print(f"Lab elements: divs={len(lab_divs)}, headers={len(lab_headers)}, exercises={len(course_exercises)}")
    for lh in lab_headers[:5]:
        print(f"  Sample lab header: {lh}")
    
    # Check MathJax / LaTeX
    math_inline = len(re.findall(r'\\\(.*?\\\)', content))
    math_display = len(re.findall(r'\$\$.*?\$\$', content, re.DOTALL))
    print(f"Math: inline \\( \\)={math_inline}, display $$ $$={math_display}")
    print()
