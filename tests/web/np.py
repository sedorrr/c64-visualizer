"""Jukebox tab: a tune of the Ultimate64 SID Jukebox on the PC (reported to the Pico via /jb/np) is shown."""
import asyncio, json, sys
from playwright.async_api import async_playwright
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; np={"on":False}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"1","stream":False,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False}),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u=='/jb/state':
        s={"state":"idle","msg":"Jukebox am PC spielt","pos":-1,"count":0,"dur":-1,"elapsed":0,"shuffle":False,"title":"","author":"","file":"","md5":"",
           "prefs":{"src":0,"jbsrc":0,"hvsc":"","user":"","dir":"/"},"np":np,"sldb":{"count":0,"busy":False,"chunk":False,"msg":"","hvsc":"","user":""}}
        await route.fulfill(body=json.dumps(s),content_type='application/json')
    elif r.method=='POST': await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); pg=await b.new_page(viewport={"width":420,"height":1200}); errs=[]
        pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
        await pg.click("nav.tabs button[data-t=jb]"); await pg.wait_for_timeout(2500)
        check(not await pg.is_visible("#extnp"), "nothing from the PC: no line")
        np.clear(); np.update({"on":True,"title":"Next Awakening","author":"Jeroen Tel","dur":117,"elapsed":65,"song":2,"songs":3,"chips":2,"sids":"D400 D420"})
        await pg.wait_for_timeout(2600)
        t=await pg.inner_text("#extnp"); print("line:", t)
        check(await pg.is_visible("#extnp"), "line shown")
        check("Next Awakening" in t and "Jeroen Tel" in t and "1:05 / 1:57" in t, "title, author, clock")
        check("Unterlied 2 von 3" in t and "2 SIDs ($D400, $D420)" in t, "sub-tune and SIDs")
        np.clear(); np.update({"on":True,"title":"Commando","author":"Rob Hubbard","dur":-1,"elapsed":5,"song":0,"songs":0,"chips":1,"sids":""})
        await pg.wait_for_timeout(2600)
        t=await pg.inner_text("#extnp"); print("line:", t)
        check("0:05" in t and "/" not in t.split("Rob Hubbard")[1] and t.endswith("1 SID"), "unknown length, one SID")
        np.clear(); np.update({"on":False})
        await pg.wait_for_timeout(2600)
        check(not await pg.is_visible("#extnp"), "stopped: line gone")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
