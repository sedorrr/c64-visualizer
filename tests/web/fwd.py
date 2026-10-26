import asyncio, json, sys
from urllib.parse import parse_qs
from playwright.async_api import async_playwright
# stream forwarding: WLAN tab field + receiver mode "forwarded" (sent with /config), status line with target and rate
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; posts=[]
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.231","net":True,"pps":251,"u64state":"Stream läuft","ssid":"MeinWLAN","u64":"192.168.1.168","stream":True,"thr":30,
        "pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"Oct 14 2026","load":12,
        "busymax":2900,"framemax":16000,"stallmax":20000,"stereo":"","waits":0,"lux10":-10,"lightpct":100,"name":"c64viz","cfgname":"",
        "bright":20,"fwd":"c64viz2.local","fwdip":"192.168.1.224","fpps":251,"fwderr":0}
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
        st=await pg.inner_text("#st"); line=[l for l in st.split("\n") if "Weiterleitung" in l]; print("status:", line)
        check(line and "c64viz2.local" in line[0] and "192.168.1.224" in line[0] and "251 Pakete/s" in line[0], "status: target, address and rate")
        check("Multicast-Gruppe" not in st, "no multicast group shown in unicast mode")
        await pg.click("nav.tabs button[data-t=net]"); await pg.wait_for_timeout(300)
        check(await pg.input_value("#fwd")=="c64viz2.local" and await pg.input_value("#smode")=="0", "fields filled from the Pico")
        await pg.fill("#fwd","c64viz/2"); await pg.wait_for_timeout(100)
        check(await pg.is_visible("#fwdwarn"), "invalid target: warning while typing")
        await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(300)
        check(not posts, "invalid target: nothing sent")
        await pg.fill("#fwd"," 192.168.1.224 "); await pg.select_option("#smode","2"); await pg.wait_for_timeout(100)
        check(not await pg.is_visible("#fwdwarn") and not await pg.is_visible("#smwarn"), "valid: no warnings (and no multicast warning for mode 2)")
        await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(400)
        q=parse_qs(posts[-1], keep_blank_values=True) if posts else {}
        check(q.get("fwd")==["192.168.1.224"] and q.get("smode")==["2"] and q.get("ssid")==["MeinWLAN"], "sent with /config (trimmed)")
        await pg.fill("#fwd",""); await pg.select_option("#smode","0"); await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(400)
        check(len(posts)==2 and parse_qs(posts[-1], keep_blank_values=True).get("fwd")==[""], "empty = forwarding off")
        STATUS.update(fwd="c64viz-3.local", fwdip="-", fpps=0); await pg.evaluate("stat()"); await pg.wait_for_timeout(400)
        st=await pg.inner_text("#st")
        check("Name wird gesucht" in st, "name not found yet: shown")
        STATUS.update(fwd="", smode=2, u64state="Stream kommt weitergeleitet"); await pg.evaluate("stat()"); await pg.wait_for_timeout(400)
        st=await pg.inner_text("#st")
        check("Weiterleitung" not in st and "Stream kommt weitergeleitet" in st, "second Pico: no forwarding line, state shown")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
