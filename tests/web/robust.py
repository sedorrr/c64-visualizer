import asyncio, json, sys
from playwright.async_api import async_playwright
# review fixes: values from the Pico are shown as text (no HTML), texts.txt is loaded reliably before saving
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; posts=[]; texts_fail=[2]
STATUS={"wifi":"<img src=x onerror=window.pwned=1>Net\"x","ip":"192.168.1.231","net":False,"pps":0,"u64state":"<b>x</b>","ssid":"<i>Net","u64":"1",
        "stream":False,"thr":30,"pwdefault":False,"sta":True,"smode":1,"group":"<u>g</u>","silent":0,"sidbusy":False,"mcwarn":False,"build":"x",
        "load":12,"busymax":2900,"framemax":16000,"stallmax":20000,"stereo":"","waits":0,"lux10":-10,"lightpct":100,"name":"c64viz","cfgname":"","bright":20}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/texts' and r.method=='GET':
        if texts_fail[0]>0: texts_fail[0]-=1; await route.fulfill(status=503,body='Speicher knapp')
        else: await route.fulfill(body='@silence 8000\nHallo Welt\n')
    elif u=='/texts': posts.append(r.post_data); await route.fulfill(body='Gespeichert')
    elif u.startswith('/jb/') or u.startswith('/fw/'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1200}); pg.on("pageerror",lambda e: errs.append(str(e)))
        pg.on("dialog",lambda d: asyncio.ensure_future(d.accept()))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(400)
        await pg.evaluate("save()"); await pg.wait_for_timeout(200)
        check(not posts, "texts not loaded yet (first answers 503): nothing saved")
        await pg.wait_for_timeout(2500)
        check(await pg.evaluate("TEXTS_OK") and "Hallo Welt" in await pg.evaluate("build()"), "texts loaded after the retries")
        st=await pg.inner_text("#st")
        check("<img src=x" in st and "<b>x</b>" in st and "<u>g</u>" in st, "SSID, state and group shown as plain text")
        check(not await pg.evaluate("!!window.pwned"), "no HTML from the status got executed")
        await pg.evaluate("save()"); await pg.wait_for_timeout(300)
        check(len(posts)==1 and "Hallo Welt" in posts[0], "after loading: saving works")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
