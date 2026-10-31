import sys
import os
import asyncio
from playwright.async_api import async_playwright

sys.stdout.reconfigure(encoding='utf-8')

FILE_URL = "file:///" + os.path.abspath(r'C:\Users\dell\xiaozhi_dual_architecture\blog_megaseries\cyberdeck_rover_megaseries.html').replace('\\', '/')
VIEWPORTS = [
    {"width": 360, "height": 740, "name": "Galaxy S8 (360px)"},
    {"width": 375, "height": 667, "name": "iPhone SE (375px)"},
    {"width": 390, "height": 844, "name": "iPhone 14 (390px)"},
    {"width": 412, "height": 915, "name": "Pixel 7 (412px)"},
    {"width": 600, "height": 960, "name": "Small Tablet (600px)"},
    {"width": 850, "height": 1100, "name": "Responsive Breakpoint (850px)"},
    {"width": 1280, "height": 800, "name": "Desktop (1280px)"}
]

async def run_tests():
    print(f"Testing HTML at: {FILE_URL}\n")
    async with async_playwright() as p:
        chrome_path = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
        browser = await p.chromium.launch(headless=True, executable_path=chrome_path)
        
        for vp in VIEWPORTS:
            page = await browser.new_page(viewport={"width": vp["width"], "height": vp["height"]})
            await page.goto(FILE_URL, wait_until="domcontentloaded")
            
            # Check horizontal overflow
            scroll_width = await page.evaluate("() => document.documentElement.scrollWidth")
            inner_width = await page.evaluate("() => window.innerWidth")
            
            overflow = scroll_width - inner_width
            print(f"[{vp['name']}] innerWidth: {inner_width}px, scrollWidth: {scroll_width}px -> Overflow: {overflow}px")
            assert overflow <= 0, f"Horizontal overflow detected on {vp['name']}! ({overflow}px overflow)"
            
            # Check mobile drawer toggle touch target size when viewport <= 850px
            if vp["width"] <= 850:
                toggle_box = await page.locator("#drawerToggle").bounding_box()
                assert toggle_box is not None, "Drawer toggle not visible on mobile!"
                print(f"  Drawer toggle bounding box: {toggle_box['width']:.1f}x{toggle_box['height']:.1f}px (Target >= 44x44px)")
                assert toggle_box['width'] >= 43.5 and toggle_box['height'] >= 43.5, "Toggle touch target too small!"
                
                # Test drawer interaction
                await page.click("#drawerToggle")
                is_open = await page.evaluate("() => document.getElementById('sidebarToc').classList.contains('open')")
                assert is_open, "Drawer did not open on click!"
                
                # Close via overlay outside drawer width (82vw)
                click_x = int(vp["width"] * 0.92)
                await page.click("#drawerOverlay", position={"x": click_x, "y": 200})
                is_closed = await page.evaluate("() => !document.getElementById('sidebarToc').classList.contains('open')")
                assert is_closed, "Drawer did not close when tapping overlay!"
            
            await page.close()
            
        await browser.close()
        print("\n✓ ALL 7 VIEWPORTS PASSED ZERO-OVERFLOW & ACCESSIBILITY TESTS!")

if __name__ == "__main__":
    asyncio.run(run_tests())
