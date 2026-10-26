import os
import asyncio
from playwright.async_api import async_playwright
async def run(block):
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(); pg.on("pageerror",lambda e: errs.append(str(e)))
        if block: await pg.add_init_script("WebAssembly.instantiate=()=>Promise.reject(new Error('blocked by CSP'))")
        await pg.goto("file://"+os.path.abspath("../../tools/simulator.html")); await pg.wait_for_timeout(600)
        await pg.click("#bdemo"); await pg.wait_for_timeout(2000)
        lit=await pg.evaluate("(()=>{const d=g.getImageData(0,0,cv.width,cv.height).data;let n=0;for(let i=0;i<d.length;i+=4)if(d[i]+d[i+1]+d[i+2]>200)n++;return n})()")
        print(("WebAssembly blocked -> JS fallback" if block else "WebAssembly")+": bright pixels", lit, "errors", errs); await b.close()
async def main(): await run(False); await run(True)
asyncio.run(main())
