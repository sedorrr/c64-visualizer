import asyncio, json, sys
from urllib.parse import parse_qs
from playwright.async_api import async_playwright
# device name: WLAN tab field (checked while typing, sent with /config), status shows the name in use and a taken name
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; posts=[]
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.224","net":False,"pps":0,"u64state":"-","ssid":"MeinWLAN","u64":"192.168.1.168","stream":False,"thr":30,
        "pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"Oct 13 2026","load":12,
        "busymax":2900,"framemax":16000,"stallmax":20000,"stereo":"","waits":0,"lux10":-10,"lightpct":100,"name":"test-2","cfgname":"test"}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u=='/config': posts.append(r.post_data); await route.fulfill(body='Gespeichert - der Pico startet neu und verbindet sich.')
    elif u.startswith('/jb/') or u.startswith('/fw/'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1400}); pg.on("pageerror",lambda e: errs.append(str(e))); pg.on("dialog",lambda d: asyncio.ensure_future(d.accept()))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(1200)
        st=await pg.inner_text("#st"); print("status:", st.split("\n")[1])
        check("erreichbar als test-2.local" in st and "„test“ ist im Netz schon vergeben" in st, "status: name in use and the taken one")
        await pg.click("nav.tabs button[data-t=net]"); await pg.wait_for_timeout(300)
        check(await pg.input_value("#devname")=="test" and await pg.inner_text("#devurl")=="test.local", "field filled from the Pico")
        await pg.fill("#devname","Wohn zimmer"); await pg.wait_for_timeout(100)
        check(await pg.is_visible("#devwarn"), "invalid name: warning while typing")
        await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(300)
        check(not posts, "invalid name: nothing sent")
        await pg.fill("#devname","Kueche"); await pg.wait_for_timeout(100)
        check(not await pg.is_visible("#devwarn") and await pg.inner_text("#devurl")=="kueche.local", "valid: warning gone, preview kueche.local")
        await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(400)
        q=parse_qs(posts[-1]) if posts else {}
        check(q.get("name")==["kueche"] and q.get("ssid")==["MeinWLAN"], "sent with /config in lower case")
        await pg.fill("#devname",""); await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(400)
        check(len(posts)==2 and "name=&" in posts[-1], "empty = back to c64viz")
        STATUS.update(name="c64viz", cfgname=""); await pg.evaluate("stat()"); await pg.wait_for_timeout(400)
        st=await pg.inner_text("#st")
        check("erreichbar als c64viz.local" in st and "vergeben" not in st, "default name: no warning")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
