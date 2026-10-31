import re

with open(r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\part1_chapters_1_to_3.html', 'r', encoding='utf-8') as f:
    text = f.read()

tables = len(re.findall(r'<table', text))
wraps = len(re.findall(r'<div class="spec-table-wrap">', text))
minis = re.findall(r'<section id="mini-([1-3]-[1-5])"', text)
h2s = re.findall(r'<h2>Mini-Chapter ([1-3]\.[1-5]):', text)
labs = len(re.findall(r'Hands-On Laboratory [1-3]', text))
has_doctype = '<!DOCTYPE' in text
has_body = '<body' in text
has_html = '<html' in text

print(f"File size: {len(text.encode('utf-8'))} bytes, {len(text.splitlines())} lines")
print(f"Tables: {tables}, Table Wrappers: {wraps}")
print(f"Mini-Chapter IDs found ({len(minis)}): {minis}")
print(f"H2 Mini Headings found ({len(h2s)}): {h2s}")
print(f"Labs count: {labs}")
print(f"Has DOCTYPE: {has_doctype}, Has <html>: {has_html}, Has <body>: {has_body}")

assert tables == wraps, f"Mismatch: {tables} tables vs {wraps} wraps!"
assert len(minis) == 15, f"Expected 15 mini-chapters, got {len(minis)}"
assert len(h2s) == 15, f"Expected 15 H2 headings, got {len(h2s)}"
assert labs == 3, f"Expected 3 labs, got {labs}"
assert not has_doctype, "Should not contain DOCTYPE"
assert not has_body, "Should not contain <body>"
assert not has_html, "Should not contain <html>"

# Check for presence of key technical components
assert "Screen_Off.desktop" in text
assert "Settings.desktop" in text
assert "S3_Internet.desktop" in text
assert "Xiaozhi_PTT.desktop" in text
assert "NeonDownscaleYUYVTo800" in text
assert "NeonLaplacianRowSafe" in text
assert "FrameDescriptor" in text
assert "LocklessSPSCQueue" in text
assert "PinThreadToCore" in text
assert "QUAD_TABLE" in text
assert "_Static_assert" in text

print("ALL VERIFICATIONS PASSED SUCCESSFULLY!")
