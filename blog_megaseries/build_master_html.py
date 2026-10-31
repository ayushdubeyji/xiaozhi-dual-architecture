import os
import re
import json

DIR = r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries'
PART1_PATH = os.path.join(DIR, 'part1_chapters_1_to_3.html')
PART2_PATH = os.path.join(DIR, 'part2_chapters_4_to_7.html')
PART3_PATH = os.path.join(DIR, 'part3_chapters_8_to_10.html')
TOC_PATH = os.path.join(DIR, 'toc.json')
OUTPUT_PATH = os.path.join(DIR, 'cyberdeck_rover_megaseries.html')
INDEX_OUTPUT_PATH = os.path.join(DIR, 'index.html')

with open(TOC_PATH, 'r', encoding='utf-8') as f:
    toc = json.load(f)

with open(PART1_PATH, 'r', encoding='utf-8') as f:
    p1 = f.read()

with open(PART2_PATH, 'r', encoding='utf-8') as f:
    p2 = f.read()

with open(PART3_PATH, 'r', encoding='utf-8') as f:
    p3 = f.read()

# Extract inner HTML from part1: from first <section id="chapter-1"> to end
def extract_part1_content(html):
    # Strip any top-level styles or script
    html = re.sub(r'<style>.*?</style>', '', html, flags=re.DOTALL)
    m = re.search(r'(<section id="chapter-1">.*)', html, re.DOTALL)
    if m:
        return m.group(1).strip()
    return html.strip()

def extract_part2_content(html):
    html = re.sub(r'<style>.*?</style>', '', html, flags=re.DOTALL)
    # Part 2 starts at Chapter 4
    m = re.search(r'(<section[^>]*id="chapter-4".*)', html, re.DOTALL)
    if m:
        return m.group(1).strip()
    # Or find first chapter-wrap
    m = re.search(r'(<article[^>]*>.*)', html, re.DOTALL)
    if m:
        return m.group(1).strip()
    return html.strip()

def extract_part3_content(html):
    html = re.sub(r'<style>.*?</style>', '', html, flags=re.DOTALL)
    # Part 3 starts at Chapter 8
    m = re.search(r'(<section[^>]*id="chapter-8".*)', html, re.DOTALL)
    if m:
        # cut before trailing </section></article> if part-wrapper
        res = m.group(1).strip()
        # strip trailing </article></section> comments
        res = re.sub(r'</article>\s*</section>\s*$', '', res)
        return res
    m = re.search(r'(<article[^>]*>.*)', html, re.DOTALL)
    if m:
        res = m.group(1).strip()
        res = re.sub(r'</article>\s*</section>\s*$', '', res)
        return res
    return html.strip()

c1 = extract_part1_content(p1)
c2 = extract_part2_content(p2)
c3 = extract_part3_content(p3)

# Build Sidebar TOC HTML
sidebar_items = []
for ch_num_str, ch_data in toc.items():
    ch_num = int(ch_num_str)
    ch_title = ch_data["title"]
    minis = ch_data["minis"]
    
    mini_links = []
    for mn in minis:
        num = mn["num"]
        mid = mn["id"]
        title = mn["title"]
        mini_links.append(f"""
          <li class="sidebar-mini-item" data-mini-id="{mid}">
            <a href="#{mid}" class="sidebar-mini-link">
              <span class="mini-status-icon" id="status-{num}"></span>
              <span class="mini-num">{num}</span>
              <span class="mini-text">{title}</span>
            </a>
          </li>""")
    
    mini_links_html = "\n".join(mini_links)
    sidebar_items.append(f"""
      <div class="sidebar-chapter-group" data-chapter="{ch_num}">
        <div class="sidebar-chapter-header" onclick="toggleChapterGroup({ch_num})">
          <span class="ch-expander" id="expander-{ch_num}">▼</span>
          <span class="ch-badge">CH {ch_num}</span>
          <span class="ch-name">{ch_title}</span>
        </div>
        <ul class="sidebar-mini-list" id="mini-list-{ch_num}">
          {mini_links_html}
        </ul>
      </div>""")

sidebar_html = "\n".join(sidebar_items)

# Master Template
master_html = f"""<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, minimum-scale=1.0, maximum-scale=5.0, viewport-fit=cover">
  <title>Physical AI Cyberdeck &amp; Autonomous Mecanum Rover: The Complete Engineering Megaseries &amp; Course</title>
  <meta name="description" content="A comprehensive 10-chapter, 50-mini-chapter technical megaseries and hands-on laboratory course on building an embodied Linux cyberdeck and autonomous 4-wheel Mecanum rover with C++20 NEON vision, RP2040 PIO acoustic localization, and cosmic epistemology.">
  <meta name="author" content="Ayush Dubey">
  <link rel="icon" type="image/png" href="/favicon.png">

  <!-- Google Fonts: Inter & Fira Code -->
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Fira+Code:wght@400;500;600;700&family=Inter:wght@300;400;500;600;700;800;900&display=swap" rel="stylesheet">

  <!-- MathJax 3 LaTeX Formulation Config -->
  <script>
    window.MathJax = {{
      tex: {{
        inlineMath: [['$', '$'], ['\\\\(', '\\\\)']],
        displayMath: [['$$', '$$'], ['\\\\[', '\\\\]']],
        processEscapes: true
      }},
      options: {{
        skipHtmlTags: ['script', 'noscript', 'style', 'textarea', 'pre', 'code']
      }}
    }};
  </script>
  <script id="MathJax-script" async src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js"></script>

  <style>
    /* ==========================================================================
       AYUSH DUBEY DARK HUD DESIGN SYSTEM (ZERO-OVERFLOW MOBILE RESPONSIVE)
       ========================================================================== */
    :root {{
      --bg-deep: #040813;
      --bg-surface: #081125;
      --bg-card: rgba(13, 27, 58, 0.75);
      --bg-card-border: rgba(0, 240, 255, 0.22);
      --bg-code: #030712;
      
      --cyan-neon: #00f0ff;
      --cyan-dim: rgba(0, 240, 255, 0.12);
      --cyan-glow: rgba(0, 240, 255, 0.45);
      
      --emerald-neon: #10b981;
      --emerald-dim: rgba(16, 185, 129, 0.15);
      
      --amber-neon: #f59e0b;
      --purple-neon: #a855f7;
      --rose-neon: #f43f5e;
      
      --text-primary: #e2e8f0;
      --text-secondary: #94a3b8;
      --text-muted: #64748b;
      --text-bright: #ffffff;
      
      --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      --font-mono: 'Fira Code', ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      
      --header-height: 64px;
      --sidebar-width: 280px;
      --ease-hud: cubic-bezier(0.16, 1, 0.3, 1);
    }}


    *, *::before, *::after {{
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }}

    html {{
      scroll-behavior: smooth;
      background-color: var(--bg-deep);
      color: var(--text-primary);
      font-family: var(--font-sans);
      font-size: 16px;
      line-height: 1.7;
      text-rendering: optimizeLegibility;
      -webkit-font-smoothing: antialiased;
      -moz-osx-font-smoothing: grayscale;
      overflow-x: hidden;
      max-width: 100vw;
      width: 100%;
    }}

    body {{
      background: radial-gradient(circle at 50% 0%, #0a1b3a 0%, var(--bg-deep) 60%),
                  radial-gradient(circle at 100% 100%, #061124 0%, var(--bg-deep) 70%);
      background-attachment: fixed;
      min-height: 100vh;
      overflow-x: hidden;
      max-width: 100vw;
      width: 100%;
      display: flex;
      flex-direction: column;
    }}

    /* Top HUD Navigation Bar */
    .hud-header {{
      position: sticky;
      top: 0;
      z-index: 1000;
      height: var(--header-height);
      background: rgba(4, 8, 19, 0.88);
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      border-bottom: 1px solid var(--bg-card-border);
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 0 1.5rem;
    }}

    .hud-header-left {{
      display: flex;
      align-items: center;
      gap: 1rem;
    }}

    .hud-brand {{
      display: flex;
      align-items: center;
      gap: 0.75rem;
      text-decoration: none;
      color: var(--cyan-neon);
      font-weight: 800;
      font-size: 1.1rem;
      letter-spacing: 0.05em;
      text-transform: uppercase;
    }}

    .hud-brand .brand-badge {{
      background: var(--cyan-dim);
      border: 1px solid var(--cyan-neon);
      color: var(--cyan-neon);
      font-size: 0.7rem;
      padding: 2px 8px;
      border-radius: 4px;
      font-family: var(--font-mono);
    }}

    .hud-telemetry {{
      display: flex;
      align-items: center;
      gap: 1.25rem;
      font-family: var(--font-mono);
      font-size: 0.75rem;
      color: var(--text-secondary);
    }}

    .telemetry-item {{
      display: flex;
      align-items: center;
      gap: 0.4rem;
    }}

    .telemetry-dot {{
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background-color: var(--emerald-neon);
      box-shadow: 0 0 8px var(--emerald-neon);
      animation: pulse-dot 2s infinite ease-in-out;
    }}

    @keyframes pulse-dot {{
      0%, 100% {{ opacity: 1; transform: scale(1); }}
      50% {{ opacity: 0.4; transform: scale(0.85); }}
    }}

    .hud-drawer-toggle {{
      display: none;
      min-width: 44px;
      min-height: 44px;
      background: var(--cyan-dim);
      border: 1px solid var(--cyan-neon);
      color: var(--cyan-neon);
      border-radius: 6px;
      align-items: center;
      justify-content: center;
      font-size: 1.25rem;
      cursor: pointer;
      touch-action: manipulation;
    }}

    /* Layout Wrapper */
    .megaseries-layout {{
      display: grid;
      grid-template-columns: var(--sidebar-width) minmax(0, 1fr);
      min-height: calc(100vh - var(--header-height));
      max-width: 1600px;
      margin: 0 auto;
      width: 100%;
    }}

    /* Sidebar Table of Contents */
    .sidebar-toc {{
      position: sticky;
      top: var(--header-height);
      height: calc(100vh - var(--header-height));
      overflow-y: auto;
      background: rgba(6, 14, 32, 0.95);
      border-right: 1px solid var(--bg-card-border);
      padding: 1.25rem 1rem 2.5rem;
      display: flex;
      flex-direction: column;
      gap: 1.25rem;
      scrollbar-width: thin;
      scrollbar-color: var(--cyan-dim) transparent;
    }}

    .sidebar-toc::-webkit-scrollbar {{
      width: 5px;
    }}
    .sidebar-toc::-webkit-scrollbar-thumb {{
      background: var(--cyan-dim);
      border-radius: 4px;
    }}

    /* Course Progress Gauge Box */
    .progress-hud-box {{
      background: linear-gradient(135deg, rgba(0, 240, 255, 0.08) 0%, rgba(16, 185, 129, 0.08) 100%);
      border: 1px solid var(--cyan-neon);
      border-radius: 8px;
      padding: 1rem;
      box-shadow: 0 4px 20px rgba(0, 240, 255, 0.12);
    }}

    .progress-hud-title {{
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-family: var(--font-mono);
      font-size: 0.8rem;
      color: var(--cyan-neon);
      text-transform: uppercase;
      margin-bottom: 0.5rem;
    }}

    .progress-bar-wrap {{
      width: 100%;
      height: 8px;
      background: rgba(255, 255, 255, 0.08);
      border-radius: 4px;
      overflow: hidden;
      margin-bottom: 0.5rem;
    }}

    .progress-bar-fill {{
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, var(--cyan-neon), var(--emerald-neon));
      border-radius: 4px;
      transition: width 0.4s var(--ease-hud);
      box-shadow: 0 0 10px var(--cyan-glow);
    }}

    .progress-hud-stats {{
      display: flex;
      justify-content: space-between;
      font-size: 0.75rem;
      color: var(--text-secondary);
      font-family: var(--font-mono);
    }}

    .btn-reset-progress {{
      background: transparent;
      border: none;
      color: var(--text-muted);
      cursor: pointer;
      text-decoration: underline;
      font-size: 0.7rem;
    }}
    .btn-reset-progress:hover {{
      color: var(--rose-neon);
    }}

    /* TOC Search Filter */
    .sidebar-search {{
      position: relative;
    }}
    .sidebar-search input {{
      width: 100%;
      background: var(--bg-surface);
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 6px;
      padding: 0.55rem 0.75rem;
      color: var(--text-primary);
      font-size: 0.85rem;
      outline: none;
      transition: border-color 0.2s;
    }}
    .sidebar-search input:focus {{
      border-color: var(--cyan-neon);
      box-shadow: 0 0 8px var(--cyan-dim);
    }}

    /* Chapter Accordion Group */
    .sidebar-chapters {{
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
    }}

    .sidebar-chapter-group {{
      border: 1px solid rgba(255, 255, 255, 0.06);
      border-radius: 6px;
      background: rgba(10, 20, 42, 0.5);
      overflow: hidden;
    }}

    .sidebar-chapter-header {{
      display: flex;
      align-items: center;
      gap: 0.5rem;
      padding: 0.65rem 0.75rem;
      cursor: pointer;
      background: rgba(13, 27, 58, 0.6);
      user-select: none;
      transition: background 0.2s;
      min-height: 44px;
    }}

    .sidebar-chapter-header:hover {{
      background: rgba(0, 240, 255, 0.1);
    }}

    .ch-expander {{
      font-size: 0.65rem;
      color: var(--cyan-neon);
      transition: transform 0.3s var(--ease-hud);
    }}
    .ch-expander.collapsed {{
      transform: rotate(-90deg);
    }}

    .ch-badge {{
      font-family: var(--font-mono);
      font-size: 0.68rem;
      background: var(--cyan-dim);
      color: var(--cyan-neon);
      padding: 2px 6px;
      border-radius: 4px;
      font-weight: 700;
      white-space: nowrap;
    }}

    .ch-name {{
      font-size: 0.82rem;
      font-weight: 600;
      color: var(--text-primary);
      line-height: 1.3;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }}

    .sidebar-mini-list {{
      list-style: none;
      padding: 0.4rem 0.5rem 0.5rem 0.75rem;
      display: flex;
      flex-direction: column;
      gap: 0.25rem;
    }}
    .sidebar-mini-list.hidden {{
      display: none;
    }}

    .sidebar-mini-item {{
      border-radius: 4px;
      transition: background 0.15s;
    }}

    .sidebar-mini-link {{
      display: flex;
      align-items: center;
      gap: 0.5rem;
      padding: 0.35rem 0.5rem;
      text-decoration: none;
      color: var(--text-secondary);
      font-size: 0.78rem;
      min-height: 36px;
      border-radius: 4px;
    }}

    .sidebar-mini-link:hover,
    .sidebar-mini-item.active .sidebar-mini-link {{
      color: var(--cyan-neon);
      background: rgba(0, 240, 255, 0.08);
    }}

    .sidebar-mini-item.active {{
      border-left: 2px solid var(--cyan-neon);
    }}

    .mini-status-icon {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      border: 1px solid var(--text-muted);
      flex-shrink: 0;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      font-size: 0.65rem;
    }}

    .mini-status-icon.completed {{
      border-color: var(--emerald-neon);
      background: var(--emerald-dim);
      color: var(--emerald-neon);
    }}
    .mini-status-icon.completed::after {{
      content: '✓';
      font-weight: 900;
    }}

    .mini-num {{
      font-family: var(--font-mono);
      font-weight: 700;
      font-size: 0.72rem;
      color: var(--cyan-neon);
      flex-shrink: 0;
    }}

    .mini-text {{
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }}

    /* Main Article Container */
    .megaseries-content {{
      padding: 2.5rem 2.5rem 6rem;
      max-width: 1050px;
      width: 100%;
      margin: 0 auto;
      box-sizing: border-box;
      overflow-x: hidden;
    }}

    /* Hero Banner */
    .hero-banner {{
      background: linear-gradient(135deg, rgba(8, 17, 37, 0.9) 0%, rgba(4, 8, 19, 0.95) 100%);
      border: 1px solid var(--bg-card-border);
      border-radius: 12px;
      padding: 2.5rem;
      margin-bottom: 3rem;
      box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5), 0 0 20px rgba(0, 240, 255, 0.15);
      position: relative;
      overflow: hidden;
    }}

    .hero-banner::before {{
      content: '';
      position: absolute;
      top: -100px;
      right: -100px;
      width: 300px;
      height: 300px;
      background: radial-gradient(circle, rgba(0, 240, 255, 0.18), transparent 70%);
      pointer-events: none;
    }}

    .hero-pretitle {{
      font-family: var(--font-mono);
      color: var(--cyan-neon);
      font-size: 0.85rem;
      text-transform: uppercase;
      letter-spacing: 0.1em;
      margin-bottom: 0.75rem;
      display: flex;
      align-items: center;
      gap: 0.5rem;
    }}

    .hero-title {{
      font-size: clamp(2rem, 5vw, 3.2rem);
      font-weight: 900;
      line-height: 1.15;
      color: var(--text-bright);
      margin-bottom: 1.25rem;
      letter-spacing: -0.02em;
    }}

    .hero-title .gradient-text {{
      background: linear-gradient(135deg, #00f0ff 0%, #10b981 50%, #a855f7 100%);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }}

    .hero-lead {{
      font-size: 1.15rem;
      line-height: 1.8;
      color: var(--text-secondary);
      margin-bottom: 1.75rem;
      max-width: 900px;
    }}

    .hero-meta {{
      display: flex;
      flex-wrap: wrap;
      gap: 1.5rem;
      font-family: var(--font-mono);
      font-size: 0.8rem;
      color: var(--text-muted);
      border-top: 1px solid rgba(255, 255, 255, 0.08);
      padding-top: 1.25rem;
    }}

    .hero-meta-item {{
      display: flex;
      align-items: center;
      gap: 0.4rem;
    }}
    .hero-meta-item strong {{
      color: var(--cyan-neon);
    }}

    /* Chapters & Sections Styling */
    section[id^="chapter-"] {{
      margin-top: 4rem;
      padding-top: 2rem;
      border-top: 2px solid rgba(0, 240, 255, 0.2);
    }}

    section[id^="chapter-"] > h1 {{
      font-size: clamp(1.85rem, 4.5vw, 2.5rem);
      font-weight: 800;
      color: var(--text-bright);
      margin-bottom: 1.5rem;
      letter-spacing: -0.02em;
      display: flex;
      align-items: baseline;
      gap: 0.75rem;
      border-left: 4px solid var(--cyan-neon);
      padding-left: 1rem;
    }}

    section[id^="mini-"],
    section.mini-chapter {{
      margin-top: 3rem;
      padding-top: 1.5rem;
      border-top: 1px solid rgba(255, 255, 255, 0.08);
    }}

    h2, h3.mini-chapter-title {{
      font-size: clamp(1.35rem, 3.5vw, 1.75rem);
      font-weight: 700;
      color: #7dd3fc;
      margin: 1.5rem 0 1rem;
      letter-spacing: -0.01em;
      line-height: 1.35;
    }}

    h4 {{
      font-size: 1.15rem;
      font-weight: 600;
      color: #93c5fd;
      margin: 1.5rem 0 0.75rem;
    }}

    p {{
      margin-bottom: 1.25rem;
      color: var(--text-primary);
      font-size: 1.02rem;
    }}

    ul, ol {{
      margin: 0 0 1.25rem 1.75rem;
      color: var(--text-primary);
    }}

    li {{
      margin-bottom: 0.5rem;
    }}

    strong {{
      color: #f1f5f9;
    }}

    a {{
      color: var(--cyan-neon);
      text-decoration: none;
      transition: color 0.2s;
    }}
    a:hover {{
      text-decoration: underline;
    }}

    /* Callout & Bio/Cosmic Boxes */
    .callout,
    .quote-box,
    .biomimetic-box,
    .callout-cosmic,
    .callout-biomimetic,
    .cosmic-box {{
      background: rgba(10, 22, 48, 0.75);
      border-left: 4px solid var(--cyan-neon);
      border-radius: 0 8px 8px 0;
      padding: 1.25rem 1.5rem;
      margin: 1.75rem 0;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
    }}

    .callout-cosmic,
    .cosmic-box {{
      border-left-color: var(--purple-neon);
      background: linear-gradient(135deg, rgba(20, 10, 48, 0.7) 0%, rgba(10, 15, 35, 0.8) 100%);
    }}

    .callout-biomimetic,
    .biomimetic-box {{
      border-left-color: var(--emerald-neon);
      background: linear-gradient(135deg, rgba(8, 35, 25, 0.7) 0%, rgba(10, 20, 35, 0.8) 100%);
    }}

    /* Math Blocks & MathJax Zero-Overflow */
    .math-block {{
      background: #020617;
      border: 1px solid rgba(0, 240, 255, 0.2);
      border-radius: 8px;
      padding: 1.25rem;
      margin: 1.5rem 0;
      text-align: center;
      overflow-x: auto;
      max-width: 100%;
      -webkit-overflow-scrolling: touch;
    }}

    mjx-container[jax="CHTML"],
    mjx-container[jax="CHTML"][display="true"] {{
      overflow-x: auto !important;
      max-width: 100% !important;
      display: block !important;
      padding: 0.35rem 0 !important;
      -webkit-overflow-scrolling: touch !important;
    }}

    .math-desc {{
      font-size: 0.75rem;
      color: var(--text-secondary);
      font-family: var(--font-mono);
      margin-top: 0.5rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }}

    /* Zero-Overflow Code Blocks & Copy Button */
    pre {{
      background: var(--bg-code) !important;
      border: 1px solid rgba(255, 255, 255, 0.12);
      border-radius: 8px;
      padding: 1.25rem;
      margin: 1.5rem 0;
      overflow-x: auto;
      max-width: 100%;
      position: relative;
      font-family: var(--font-mono);
      font-size: 0.88rem;
      line-height: 1.55;
      -webkit-overflow-scrolling: touch;
    }}

    code {{
      font-family: var(--font-mono);
      font-size: 0.9em;
      color: #38bdf8;
      background: rgba(56, 189, 248, 0.08);
      padding: 2px 6px;
      border-radius: 4px;
      word-break: break-word;
    }}

    pre code {{
      color: #e2e8f0;
      background: transparent;
      padding: 0;
      border-radius: 0;
      word-break: normal;
      white-space: pre;
    }}

    .code-copy-btn {{
      position: absolute;
      top: 8px;
      right: 8px;
      background: rgba(255, 255, 255, 0.1);
      border: 1px solid rgba(255, 255, 255, 0.15);
      color: var(--text-secondary);
      font-family: var(--font-mono);
      font-size: 0.7rem;
      padding: 3px 8px;
      border-radius: 4px;
      cursor: pointer;
      opacity: 0;
      transition: opacity 0.2s, background 0.2s;
    }}

    pre:hover .code-copy-btn {{
      opacity: 1;
    }}

    .code-copy-btn:hover {{
      background: var(--cyan-dim);
      color: var(--cyan-neon);
      border-color: var(--cyan-neon);
    }}

    /* Zero-Overflow Tables */
    .spec-table-wrap {{
      width: 100%;
      overflow-x: auto;
      margin: 1.5rem 0;
      border-radius: 8px;
      border: 1px solid rgba(0, 240, 255, 0.18);
      background: rgba(8, 17, 37, 0.8);
      -webkit-overflow-scrolling: touch;
    }}

    table {{
      width: 100%;
      min-width: 600px;
      border-collapse: collapse;
      font-size: 0.9rem;
      text-align: left;
    }}

    thead {{
      background: rgba(0, 240, 255, 0.08);
      border-bottom: 2px solid rgba(0, 240, 255, 0.25);
    }}

    th {{
      padding: 0.75rem 1rem;
      font-family: var(--font-mono);
      color: var(--cyan-neon);
      font-size: 0.8rem;
      letter-spacing: 0.05em;
      text-transform: uppercase;
    }}

    td {{
      padding: 0.75rem 1rem;
      border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      color: var(--text-primary);
    }}

    tr:last-child td {{
      border-bottom: none;
    }}

    tr:hover td {{
      background: rgba(0, 240, 255, 0.03);
    }}

    /* Interactive Hands-On Lab Cards */
    .lab-card,
    .exercise-box {{
      background: linear-gradient(135deg, rgba(8, 20, 48, 0.85) 0%, rgba(6, 14, 32, 0.9) 100%);
      border: 1px solid rgba(16, 185, 129, 0.35);
      border-radius: 10px;
      padding: 1.75rem;
      margin: 2.25rem 0;
      box-shadow: 0 8px 30px rgba(0, 0, 0, 0.4), 0 0 15px rgba(16, 185, 129, 0.1);
      position: relative;
    }}

    .lab-badge,
    .ch-badge {{
      display: inline-block;
      font-family: var(--font-mono);
      font-size: 0.75rem;
      font-weight: 700;
      color: var(--emerald-neon);
      background: var(--emerald-dim);
      border: 1px solid rgba(16, 185, 129, 0.4);
      padding: 3px 10px;
      border-radius: 4px;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      margin-bottom: 0.75rem;
    }}

    .lab-card h4,
    .exercise-box h5 {{
      font-size: 1.25rem;
      color: #a7f3d0;
      margin-bottom: 1rem;
      font-weight: 700;
    }}

    /* Interactive Lab Checkbox Container */
    .lab-completion-wrap {{
      margin-top: 1.5rem;
      padding-top: 1.25rem;
      border-top: 1px solid rgba(255, 255, 255, 0.08);
      display: flex;
      align-items: center;
      justify-content: flex-end;
    }}

    .lab-check-container {{
      display: inline-flex;
      align-items: center;
      gap: 0.65rem;
      cursor: pointer;
      font-family: var(--font-mono);
      font-size: 0.85rem;
      color: var(--emerald-neon);
      user-select: none;
      min-height: 44px;
      padding: 0 0.5rem;
      border-radius: 6px;
      background: rgba(16, 185, 129, 0.06);
      border: 1px solid rgba(16, 185, 129, 0.2);
      transition: all 0.2s;
    }}

    .lab-check-container:hover {{
      background: rgba(16, 185, 129, 0.12);
      border-color: var(--emerald-neon);
    }}

    .lab-checkbox {{
      width: 18px;
      height: 18px;
      accent-color: var(--emerald-neon);
      cursor: pointer;
    }}

    /* Mobile Drawer & Overlay Styles */
    .drawer-overlay {{
      display: none;
      position: fixed;
      inset: 0;
      background: rgba(0, 0, 0, 0.75);
      backdrop-filter: blur(4px);
      z-index: 998;
    }}

    /* Responsive Breakpoints */
    @media (max-width: 850px) {{
      .hud-header {{
        padding: 0 0.75rem;
      }}

      .hud-telemetry {{
        display: none !important;
      }}

      .hud-drawer-toggle {{
        display: inline-flex;
      }}

      .megaseries-layout {{
        grid-template-columns: 1fr;
        overflow-x: hidden;
        width: 100%;
        max-width: 100vw;
      }}

      .sidebar-toc {{
        position: fixed;
        top: var(--header-height);
        left: 0;
        bottom: 0;
        width: 82vw;
        max-width: 340px;
        z-index: 999;
        transform: translateX(-100%);
        transition: transform 0.3s var(--ease-hud);
        box-shadow: 10px 0 30px rgba(0, 0, 0, 0.7);
      }}

      .sidebar-toc.open {{
        transform: translateX(0);
      }}

      .drawer-overlay.active {{
        display: block;
      }}

      .megaseries-content {{
        padding: 1.5rem 1rem 4rem;
        overflow-x: hidden;
        width: 100%;
        max-width: 100%;
      }}

      .hero-banner {{
        padding: 1.5rem 1.25rem;
      }}
    }}

    @media (max-width: 480px) {{
      .hud-brand span.brand-text {{
        font-size: 0.9rem;
      }}
      .hero-lead {{
        font-size: 1rem;
      }}
    }}
  </style>
</head>
<body>

  <!-- Top HUD Bar -->
  <header class="hud-header">
    <div class="hud-header-left">
      <button class="hud-drawer-toggle" id="drawerToggle" aria-label="Toggle Syllabus Table of Contents">☰</button>
      <a href="https://ayushdubey.tech" class="hud-brand">
        <span class="brand-text">Feynman Workbench</span>
        <span class="brand-badge">Physical AI</span>
      </a>
    </div>
    
    <div class="hud-telemetry">
      <div class="telemetry-item">
        <span class="telemetry-dot"></span>
        <span>CYBERDECK: 60FPS</span>
      </div>
      <div class="telemetry-item">
        <span>ROVER: KINEMATICS OK</span>
      </div>
      <div class="telemetry-item">
        <span>ASR: GROQ 318MS</span>
      </div>
    </div>
  </header>

  <!-- Mobile Drawer Backdrop Overlay -->
  <div class="drawer-overlay" id="drawerOverlay"></div>

  <!-- Master Megaseries Layout -->
  <div class="megaseries-layout">
    
    <!-- Sticky Interactive Sidebar TOC -->
    <aside class="sidebar-toc" id="sidebarToc">
      
      <!-- Course Progress Box -->
      <div class="progress-hud-box">
        <div class="progress-hud-title">
          <span>Course Progress</span>
          <span id="progressPct">0%</span>
        </div>
        <div class="progress-bar-wrap">
          <div class="progress-bar-fill" id="progressBar"></div>
        </div>
        <div class="progress-hud-stats">
          <span id="progressCount">0 / 50 Labs</span>
          <button class="btn-reset-progress" onclick="resetCourseProgress()">Reset</button>
        </div>
      </div>

      <!-- Live Mini-Chapter Search -->
      <div class="sidebar-search">
        <input type="text" id="tocSearch" placeholder="Filter 50 mini-chapters..." aria-label="Search chapters">
      </div>

      <!-- 10 Chapters Accordion -->
      <nav class="sidebar-chapters">
        {sidebar_html}
      </nav>

    </aside>

    <!-- Main Content Area -->
    <main class="megaseries-content" id="mainContent">
      
      <!-- Hero Header -->
      <header class="hero-banner">
        <div class="hero-pretitle">
          <span>★ Master Technical Monograph &amp; Follow-Along Course</span>
        </div>
        <h1 class="hero-title">
          Building a <span class="gradient-text">Physical AI Cyberdeck</span> &amp; Autonomous Mecanum Rover
        </h1>
        <p class="hero-lead">
          A 10-chapter, 50-mini-chapter complete engineering megaseries and hands-on laboratory course. From DPI-1 quadratic backlight physics and Cortex-A72 C++20 NEON 60FPS vision, to sub-600ms Groq ASR, MCP over WebSockets, RP2040 PIO acoustic localization, and David Deutsch’s epistemology of physical agency.
        </p>
        <div class="hero-meta">
          <div class="hero-meta-item">Author: <strong>Ayush Dubey</strong> (Lead Hardware Architect &amp; Systems Engineer)</div>
          <div class="hero-meta-item">Mini-Chapters: <strong>50 Complete</strong></div>
          <div class="hero-meta-item">Laboratories: <strong>50 Hands-On</strong></div>
          <div class="hero-meta-item">Platform: <strong>Raspberry Pi 4B + Waveshare ESP32-S3-Zero + RP2040</strong></div>
        </div>
      </header>

      <!-- CHAPTERS 1 TO 3 -->
      {c1}

      <!-- CHAPTERS 4 TO 7 -->
      {c2}

      <!-- CHAPTERS 8 TO 10 -->
      {c3}

    </main>

  </div>

  <!-- Client-Side Runtime Script -->
  <script>
    // 1. Interactive Course Progress & LocalStorage
    const STORAGE_KEY = 'megaseries_course_progress';
    let completedLabs = new Set();

    try {{
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved) {{
        completedLabs = new Set(JSON.parse(saved));
      }}
    }} catch(e) {{
      console.warn('LocalStorage error:', e);
    }}

    function updateHUD() {{
      const total = 50;
      const count = completedLabs.size;
      const pct = Math.round((count / total) * 100);
      
      document.getElementById('progressCount').textContent = `${{count}} / ${{total}} Labs`;
      document.getElementById('progressPct').textContent = `${{pct}}%`;
      document.getElementById('progressBar').style.width = `${{pct}}%`;

      // Update checkboxes & sidebar status icons
      document.querySelectorAll('.lab-checkbox').forEach(cb => {{
        const labId = cb.getAttribute('data-lab');
        const isDone = completedLabs.has(labId);
        cb.checked = isDone;
        
        const statusIcon = document.getElementById(`status-${{labId}}`);
        if (statusIcon) {{
          if (isDone) {{
            statusIcon.classList.add('completed');
          }} else {{
            statusIcon.classList.remove('completed');
          }}
        }}
      }});
    }}

    document.addEventListener('change', (e) => {{
      if (e.target.classList.contains('lab-checkbox')) {{
        const labId = e.target.getAttribute('data-lab');
        if (e.target.checked) {{
          completedLabs.add(labId);
        }} else {{
          completedLabs.delete(labId);
        }}
        try {{
          localStorage.setItem(STORAGE_KEY, JSON.stringify([...completedLabs]));
        }} catch(err) {{}}
        updateHUD();
      }}
    }});

    function resetCourseProgress() {{
      if (confirm('Reset all 50 course laboratory progress markers?')) {{
        completedLabs.clear();
        localStorage.removeItem(STORAGE_KEY);
        updateHUD();
      }}
    }}

    // 2. Mobile Drawer Toggle
    const drawerToggle = document.getElementById('drawerToggle');
    const sidebarToc = document.getElementById('sidebarToc');
    const drawerOverlay = document.getElementById('drawerOverlay');

    function toggleDrawer(open) {{
      if (open) {{
        sidebarToc.classList.add('open');
        drawerOverlay.classList.add('active');
        document.body.style.overflow = 'hidden';
      }} else {{
        sidebarToc.classList.remove('open');
        drawerOverlay.classList.remove('active');
        document.body.style.overflow = '';
      }}
    }}

    if (drawerToggle) {{
      drawerToggle.addEventListener('click', () => {{
        const isOpen = sidebarToc.classList.contains('open');
        toggleDrawer(!isOpen);
      }});
    }}

    if (drawerOverlay) {{
      drawerOverlay.addEventListener('click', () => toggleDrawer(false));
    }}

    // Close drawer when clicking a link on mobile
    document.querySelectorAll('.sidebar-mini-link').forEach(link => {{
      link.addEventListener('click', () => {{
        if (window.innerWidth <= 850) {{
          toggleDrawer(false);
        }}
      }});
    }});

    // 3. Chapter Accordion
    function toggleChapterGroup(chNum) {{
      const list = document.getElementById(`mini-list-${{chNum}}`);
      const expander = document.getElementById(`expander-${{chNum}}`);
      if (list && expander) {{
        const isHidden = list.classList.toggle('hidden');
        expander.classList.toggle('collapsed', isHidden);
      }}
    }}

    // 4. Live TOC Filter
    const searchInput = document.getElementById('tocSearch');
    if (searchInput) {{
      searchInput.addEventListener('input', (e) => {{
        const term = e.target.value.toLowerCase().trim();
        document.querySelectorAll('.sidebar-mini-item').forEach(item => {{
          const text = item.textContent.toLowerCase();
          const matches = text.includes(term);
          item.style.display = matches ? '' : 'none';
        }});
      }});
    }}

    // 5. Code Block Copy Buttons
    document.querySelectorAll('pre').forEach(pre => {{
      const btn = document.createElement('button');
      btn.className = 'code-copy-btn';
      btn.textContent = 'Copy';
      btn.addEventListener('click', async () => {{
        const code = pre.querySelector('code');
        const textToCopy = code ? code.innerText : pre.innerText;
        try {{
          await navigator.clipboard.writeText(textToCopy);
          btn.textContent = 'Copied!';
          setTimeout(() => btn.textContent = 'Copy', 2000);
        }} catch(err) {{
          btn.textContent = 'Failed';
        }}
      }});
      pre.appendChild(btn);
    }});

    // 6. Scrollspy
    const miniSections = document.querySelectorAll('section[id^="mini-"]');
    const navItems = document.querySelectorAll('.sidebar-mini-item');

    window.addEventListener('scroll', () => {{
      let currentId = '';
      const offset = 120;
      miniSections.forEach(sec => {{
        const top = sec.getBoundingClientRect().top;
        if (top <= offset) {{
          currentId = sec.id;
        }}
      }});

      if (currentId) {{
        navItems.forEach(item => {{
          if (item.getAttribute('data-mini-id') === currentId) {{
            item.classList.add('active');
          }} else {{
            item.classList.remove('active');
          }}
        }});
      }}
    }}, {{ passive: true }});

    // Initialize HUD on load
    updateHUD();
  </script>
</body>
</html>
"""

print("Writing master file to:", OUTPUT_PATH)
with open(OUTPUT_PATH, 'w', encoding='utf-8') as f:
    f.write(master_html)

print("Writing index file to:", INDEX_OUTPUT_PATH)
with open(INDEX_OUTPUT_PATH, 'w', encoding='utf-8') as f:
    f.write(master_html)

print("SUCCESS: Master megaseries document assembled successfully!")
