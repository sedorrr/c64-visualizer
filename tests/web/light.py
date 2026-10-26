import asyncio, json, sys
from playwright.async_api import async_playwright
# @light / @lightmin: read from texts.txt, written back by the settings form; status page shows lux and dimming
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]
TEXTS="@camera off\n@light off\n@lightmin 40\n@lightcurve strong\n@speed 60\nHello\n"
STATUS={"wifi":"x","ip":"1","net":True,"pps":252,"u64state":"Stream läuft","ssid":"x","u64":"1","stream":True,"thr":30,"pwdefault":False,
        "sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"Oct 12 2026","load":12,"busymax":2900,
        "framemax":16000,"stallmax":61000,"stereo":"rechts = links - Mono","waits":3,"lux10":1101,"lightpct":84}
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
        check("Licht: 110 Lux – Matrix auf 84 % der eingestellten Helligkeit" in st.replace("\xa0"," "), "status shows lux and dimming")
        await pg.click("nav.tabs button[data-t=set]"); await pg.wait_for_timeout(300)
        check(not await pg.is_checked("#light") and await pg.input_value("#lightmin")=="40" and await pg.input_value("#lightcurve")=="strong", "@light off, @lightmin 40, @lightcurve strong -> form")
        out=await pg.evaluate("build()")
        check("@light off" in out and "@lightmin 40" in out and "@lightcurve strong" in out and out.count("@lightmin")==1 and out.count("@light ")==1 and out.count("@lightcurve")==1, "written back once")
        await pg.check("#light"); await pg.fill("#lightmin","250"); await pg.select_option("#lightcurve","weak")
        out=await pg.evaluate("build()")
        check("@light on" in out and "@lightmin 100" in out and "@lightcurve weak" in out, "tick + 250 % + weak -> @light on, @lightmin 100 (clamped), @lightcurve weak")
        await pg.evaluate("parse('Hello\\n')"); await pg.wait_for_timeout(100)
        check(await pg.input_value("#lightmin")=="10" and await pg.input_value("#lightcurve")=="medium" and await pg.is_checked("#light"), "no settings in the file: on, 10 %, medium")
        STATUS["lux10"]=-10; await pg.evaluate("stat()"); await pg.wait_for_timeout(400)
        check("Licht" not in await pg.inner_text("#st"), "no sensor: no light line")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
