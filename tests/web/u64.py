import asyncio, json, sys
from urllib.parse import parse_qs
from playwright.async_api import async_playwright
# several Ultimates: list in the WLAN tab (sent with /config), source choice in the control tab (/src), network search
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; posts=[]; srcs=[]
L=[{"label":"Elite II","addr":"192.168.1.168","pw":False},{"label":"C64U","addr":"192.168.1.170","pw":True},
   {"label":"","addr":"","pw":False},{"label":"","addr":"","pw":False}]
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.231","net":True,"pps":251,"u64state":"Stream läuft","ssid":"MeinWLAN","u64":"192.168.1.168","stream":True,"thr":30,
        "pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"Oct 15 2026","load":12,
        "busymax":2900,"framemax":16000,"stallmax":20000,"stereo":"","waits":0,"lux10":-10,"lightpct":100,"name":"c64viz","cfgname":"",
        "bright":20,"fwd":"","fwdip":"-","fpps":0,"fwderr":0,"src":0,"u64list":L,"scanning":False}
SCAN={"n":0}
def scan_json():
    SCAN["n"]+=1; n=SCAN["n"]
    found=[{"ip":"192.168.1.202","alt":"192.168.1.168","link":"wlan","name":"Elite2","product":"Ultimate 64-II"}]
    if n>=3: found.append({"ip":"192.168.1.41","alt":"","link":"eth","name":"Ultimate-64-Elite1","product":"Ultimate 64 Elite"})
    return {"running":n<4,"done":min(254,n*70),"total":254,"found":found}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u=='/config': posts.append(r.post_data); await route.fulfill(body='Gespeichert - der Pico startet neu und verbindet sich.')
    elif u.startswith('/src?i='): srcs.append(u); STATUS["src"]=int(u[7:]); await route.fulfill(body='Quelle wird gewechselt.')
    elif u=='/u64/scan': await route.fulfill(body=json.dumps(scan_json()),content_type='application/json')
    elif u.startswith('/jb/') or u.startswith('/fw/'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1600}); pg.on("pageerror",lambda e: errs.append(str(e))); pg.on("dialog",lambda d: asyncio.ensure_future(d.accept()))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(1200)
        st=await pg.inner_text("#st")
        check("Ultimate (Elite II): Stream läuft" in st, "status names the source")
        check(await pg.is_visible("#srcrow"), "two Ultimates: source choice shown")
        opts=await pg.eval_on_selector_all("#src option","o=>o.map(x=>x.textContent)")
        check(opts==["Elite II (192.168.1.168)","C64U (192.168.1.170)"], "choice lists the two: %s" % opts)
        await pg.select_option("#src","1"); await pg.wait_for_timeout(300)
        check(srcs==["/src?i=1"], "choosing sends /src?i=1")
        await pg.click("nav.tabs button[data-t=net]"); await pg.wait_for_timeout(300)
        check(await pg.input_value("#ul0")=="Elite II" and await pg.input_value("#ua1")=="192.168.1.170", "list filled from the Pico")
        check(await pg.get_attribute("#up1","placeholder")=="(gesetzt)" and await pg.is_visible("#ux1") and not await pg.is_visible("#ux0"), "password set: shown, option to remove it")
        await pg.click("#scanbtn"); await pg.wait_for_timeout(3500)
        res=await pg.inner_text("#scanres"); print("   scan:", res.replace("\n"," | "))
        check("Ultimate-64-Elite1" in res and "192.168.1.41" in res, "search finds the second device")
        check("(eingetragen)" in res, "a device already in the list is marked (by its other address)")
        check("192.168.1.41 (Ethernet)" in res and "auch unter 192.168.1.168" in res, "connection and second address shown")
        check("nur per WLAN gefunden" in res and res.count("nur per WLAN")==1, "a device found only over WLAN is warned about (streams over Ethernet only)")
        check("2 gefunden" in await pg.inner_text("#scanst"), "search finished: 2 found")
        await pg.click("#scanres button"); await pg.wait_for_timeout(200)
        check(await pg.input_value("#ua2")=="192.168.1.41" and await pg.input_value("#ul2")=="Ultimate-64-Elit", "taken into the first free slot (name cut to 16)")
        await pg.fill("#ua3","ultimate.fritz.box"); await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(300)
        check(not posts, "name instead of IP: nothing sent")
        await pg.fill("#ua3",""); await pg.check("#uxp1"); await pg.evaluate("saveCfg()"); await pg.wait_for_timeout(400)
        q=parse_qs(posts[-1], keep_blank_values=True) if posts else {}
        check(q.get("a0")==["192.168.1.168"] and q.get("l2")==["Ultimate-64-Elit"] and q.get("a2")==["192.168.1.41"] and q.get("xp1")==["1"] and "xp0" not in q,
              "list sent with /config (password removal for 2 only)")
        check("u64" not in q and q.get("p0")==[""], "no old single address field, empty password = unchanged")
        L[1]["addr"]=""; STATUS["src"]=0; await pg.evaluate("stat()"); await pg.wait_for_timeout(400)
        check(not await pg.is_visible("#srcrow"), "only one Ultimate: no source choice")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
