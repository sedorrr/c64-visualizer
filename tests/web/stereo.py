import asyncio, json, sys
from playwright.async_api import async_playwright
# @stereo: read from texts.txt, written back by the settings form; status page shows the result of the check
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]
TEXTS="@camera off\n@stereo on\n@speed 60\nHello\n"
STATUS={"wifi":"x","ip":"1","net":True,"pps":252,"u64state":"Stream läuft","ssid":"x","u64":"1","stream":True,"thr":30,"pwdefault":False,
        "sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"Oct 12 2026","load":12,"busymax":2900,
        "framemax":16000,"stallmax":61000,"stereo":"rechts = links - Mono","waits":3}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    u=route.request.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/texts': await route.fulfill(body=TEXTS)
    elif u.startswith('/jb/') or u.startswith('/fw/'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1400}); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(1200)
        st=await pg.inner_text("#st"); print("status:", st.replace("\n"," | "))
        check("Stereo: rechts = links - Mono" in st, "status shows the result of the stereo check")
        check("3× auf Pakete gewartet" in st, "status shows the stream waits")
        await pg.click("nav.tabs button[data-t=set]"); await pg.wait_for_timeout(300)
        check(await pg.is_checked("#stereo"), "@stereo on -> box ticked")
        out=await pg.evaluate("build()")
        check("@stereo on" in out and out.count("@stereo")==1, "written back once as @stereo on")
        await pg.uncheck("#stereo")
        out=await pg.evaluate("build()")
        check("@stereo off" in out, "untick -> @stereo off")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
