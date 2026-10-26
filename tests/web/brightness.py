import asyncio, json, sys
from playwright.async_api import async_playwright
# brightness slider on the control tab: shows the Pico's percent, sends while dragging (throttled) and on release
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; cmds=[]
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.231","net":False,"pps":0,"u64state":"-","ssid":"MeinWLAN","u64":"1","stream":False,"thr":30,
        "pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"x","load":12,
        "busymax":2900,"framemax":16000,"stallmax":20000,"stereo":"","waits":0,"lux10":-10,"lightpct":100,"name":"c64viz","cfgname":"",
        "bright":20}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u.startswith('/cmd'):
        cmds.append(u)
        if 'bright&p=' in u: STATUS['bright']=int(u.split('p=')[1])
        await route.fulfill(body='OK')
    elif u.startswith('/jb/') or u.startswith('/fw/'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":400,"height":1000}); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(1200)
        await pg.click("nav.tabs button[data-t=ctl]"); await pg.wait_for_timeout(200)
        check(await pg.input_value("#brange")=="20" and await pg.inner_text("#brval")=="20 %", "slider shows the Pico's 20 %")
        check(await pg.get_attribute("#brange","min")=="4" and await pg.get_attribute("#brange","max")=="100", "range 4..100 %")
        box=await pg.locator("#brange").bounding_box(); print("slider width:", round(box["width"]), "px of 400")
        check(box["width"]>=140 and box["x"]+box["width"]<=400, "slider fits the phone width")
        # drag: many input events quickly -> throttled sends, the last value always arrives
        for v in range(20, 81, 2):
            await pg.evaluate(f"(()=>{{const r=$('brange');r.value={v};r.dispatchEvent(new Event('input'))}})()"); await pg.wait_for_timeout(20)
        await pg.wait_for_timeout(400)
        sent=[c for c in cmds if 'bright&p=' in c]; print("sent while dragging:", len(sent), "requests, last", sent[-1] if sent else "-")
        check(2 <= len(sent) <= 6, "dragging is throttled (not one request per pixel)")
        check(sent and sent[-1]=="/cmd?c=bright&p=80", "the final value 80 % is sent")
        check(await pg.inner_text("#brval")=="80 %", "label follows the slider")
        await pg.evaluate("stat()"); await pg.wait_for_timeout(300)
        check(await pg.input_value("#brange")=="80", "status poll does not jump back while/after dragging")
        await pg.evaluate("(()=>{const r=$('brange');r.value=45;r.dispatchEvent(new Event('change'))})()"); await pg.wait_for_timeout(300)
        check(cmds[-1]=="/cmd?c=bright&p=45", "release sends at once")
        STATUS.update(bright=7, lux10=1101, lightpct=84); await pg.wait_for_timeout(3200); await pg.evaluate("stat()"); await pg.wait_for_timeout(400)
        check(await pg.input_value("#brange")=="7" and await pg.inner_text("#brval")=="7 %", "button on the Pico (7 %) shows up after a few seconds")
        l=await pg.inner_text("#brlight"); print("light:", l)
        check("110 Lux" in l and "84 %" in l, "with light sensor: current dimming shown")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
