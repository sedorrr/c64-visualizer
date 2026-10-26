import os
import asyncio
from playwright.async_api import async_playwright
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1000}, device_scale_factor=2)
        pg.on("pageerror", lambda e: errs.append(str(e)))
        await pg.goto("file://"+os.path.abspath("../../tools/text_editor.html")); await pg.wait_for_timeout(500)
        await pg.click("text=+ Neuer Text")
        await pg.click("text=C64 „READY“ mit Cursor")
        print("line:", await pg.evaluate("lines[sel]"))
        await pg.wait_for_timeout(900)
        el=await pg.query_selector("#pv"); await el.screenshot(path="pv_ready_typing.png")
        await pg.wait_for_timeout(1500); await el.screenshot(path="pv_ready_done.png")
        await pg.fill("#ln%d" % (await pg.evaluate("sel")), "")
        await pg.click("button:has-text('Welle')"); await pg.keyboard.type("Wave ")
        await pg.click("button:has-text('Funkeln')"); await pg.keyboard.type("SID")
        await pg.wait_for_timeout(1300); await el.screenshot(path="pv_wave.png")
        tools=await pg.query_selector("#tools"); await tools.screenshot(path="tools.png")
        print("JS errors:", errs); await b.close()
asyncio.run(main())
