import json, sys, urllib.parse
import asyncio
from playwright.async_api import async_playwright
# Setup assistant: chain receiver -> c64viz -> c64viz2 from the Ultimate; settings links (#setup=) filled in on the
# target page; ticks from the beacons; the check at the end. The Picos are mocked.
HTML=open('../../tools/text_editor.html','rb').read()
fails=[]; configs=[]
def check(ok, what):
    print(('ok   ' if ok else 'FAIL ')+what)
    if not ok: fails.append(what)
def dev(**k): return json.dumps(k)
WRONG=[dict(t="audio",n="c64audio",ip="192.168.1.79",st=1,rx=250,src="192.168.1.168",fw="",fp=0,**{"from":"192.168.1.168"}),
       dict(t="viz",n="c64viz",ip="192.168.1.231",st=0,sm=2,rx=0,src="192.168.1.168",fw="c64viz2.local",fp=0,**{"from":""}),
       dict(t="viz",n="c64viz2",ip="192.168.1.224",st=1,sm=2,rx=0,src="",fw="",fp=0,**{"from":""})]
RIGHT=[dict(t="audio",n="c64audio",ip="192.168.1.79",st=1,rx=250,src="192.168.1.168",fw="c64viz.local",fp=250,**{"from":"192.168.1.168"}),
       dict(t="viz",n="c64viz",ip="192.168.1.231",st=1,sm=2,rx=250,src="192.168.1.168",fw="c64viz2.local",fp=250,**{"from":"192.168.1.79"}),
       dict(t="viz",n="c64viz2",ip="192.168.1.224",st=1,sm=2,rx=249,src="192.168.1.168",fw="",fp=0,**{"from":"192.168.1.231"})]
DEVS={"now":WRONG}
def devices(): return {"list":[{"self":d["n"]=="c64viz","age":1,"j":json.dumps(d)} for d in DEVS["now"]]}
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.231","net":False,"pps":0,"u64state":"Stream aus","ssid":"MeinWLAN","u64":"192.168.1.168","stream":False,"thr":30,"pwdefault":False,"sta":True,"smode":2,"group":"-","silent":0,"sidbusy":False,"bright":20,"lux10":-10,"lightpct":100,
        "cfgname":"","fwd":"c64viz2.local","src":0,"u64list":[{"label":"Elite II","addr":"192.168.1.168","pw":False},{"label":"","addr":"","pw":False},{"label":"","addr":"","pw":False},{"label":"","addr":"","pw":False}],"wide":"","wok":False}
async def handler(route):
    r=route.request; u=r.url.split('://',1)[1].split('/',1)[1]; u='/'+u
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/devices': await route.fulfill(body=json.dumps(devices()),content_type='application/json')
    elif u=='/texts': await route.fulfill(body=b"Hello\n")
    elif u=='/config': configs.append(dict(urllib.parse.parse_qsl(r.post_data))); await route.fulfill(body='Gespeichert')
    elif u=='/jb/state': await route.fulfill(body=json.dumps({"state":"idle","msg":"","pos":-1,"count":0,"prefs":{"src":0,"dir":"/"},"sldb":{"count":0,"busy":False}}),content_type='application/json')
    elif r.method=='POST': await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); ctx=await b.new_context(viewport={"width":420,"height":900}); errs=[]
        await ctx.route("http://c64viz.local/**", handler); await ctx.route("http://192.168.1.79/**", handler)
        pg=await ctx.new_page(); pg.on("pageerror",lambda e: errs.append(str(e))); pg.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(600)
        await pg.click("nav.tabs button[data-t=ctl]"); await pg.click("section[data-tab=ctl] button:has-text('Einricht-Assistent')"); await pg.wait_for_timeout(500)
        body=await pg.inner_text("#wzbody")
        check(await pg.locator("#wzbody input[type=checkbox]:checked").count()==3 and "c64audio" in body, "step 1: the three devices found, all chosen")
        await pg.click("#wznext"); await pg.wait_for_timeout(200)
        check(await pg.is_checked("input[name=wzu][value='192.168.1.168']"), "step 2: the current source (Elite II) is suggested")
        await pg.click("#wznext"); await pg.wait_for_timeout(200)
        chain=await pg.inner_text(".wzchain")
        check(chain=="Elite II (192.168.1.168) → c64audio → c64viz → c64viz2", "step 3: receiver first, then this Pico, then the others: "+chain)
        await pg.click("#wzbody .wzdev >> nth=2 >> button >> nth=0"); await pg.wait_for_timeout(100)   # c64viz2 up
        check(await pg.inner_text(".wzchain")=="Elite II (192.168.1.168) → c64audio → c64viz2 → c64viz", "order can be changed")
        await pg.click("#wzbody .wzdev >> nth=1 >> button >> nth=0"); await pg.wait_for_timeout(100)   # c64viz2 up again: stays behind the receiver
        await pg.click("#wzbody .wzdev >> nth=2 >> button >> nth=0"); await pg.wait_for_timeout(100)   # c64viz up
        check(await pg.inner_text(".wzchain")=="Elite II (192.168.1.168) → c64audio → c64viz → c64viz2", "the receiver always stays first")
        await pg.click("#wznext"); await pg.wait_for_timeout(200)
        t=await pg.inner_text("#wzbody")
        check(t.count("noch nicht eingestellt")==3, "step 4: nothing set up yet (beacons do not match)")
        check("von einem anderen Pico weitergeleitet" in t and "Weiterleiten an: c64viz2.local" in t and "Weiterleiten: aus (letztes Gerät)" in t, "settings per device")
        async with pg.expect_popup() as pop:
            await pg.click("#wzbody button:has-text('Bei c64audio einstellen')")
        p2=await pop.value; url=p2.url
        w=json.loads(urllib.parse.unquote(url.split("#setup=",1)[1])) if "#setup=" in url else {}
        print("   link: "+urllib.parse.unquote(url))
        check(url.startswith("http://192.168.1.79/#setup=") and w.get("fwd")=="c64viz.local" and w.get("u64")=="192.168.1.168" and "smode" not in w, "receiver: its page with forwarding to c64viz and the Ultimate")
        await p2.close()
        await pg.click("#wzbody button:has-text('Hier einstellen')"); await pg.wait_for_timeout(300)
        check(not await pg.is_visible("#wiz") and await pg.is_visible("#wizbanner") and await pg.is_checked("#stream")
              and await pg.input_value("#smode")=="2" and await pg.input_value("#fwd")=="c64viz2.local", "this Pico: WLAN tab filled in, banner")
        check(await pg.locator(".wizmark").count()>=1 and "wizmark" in (await pg.get_attribute("#stream","class") or ""), "changed fields are marked")
        await pg.click("section[data-tab=net] button:has-text('Speichern und neu starten')"); await pg.wait_for_timeout(300)
        print("   msg: "+await pg.inner_text("#msg"))
        c=configs[-1] if configs else {}
        check(c.get("stream")=="1" and c.get("smode")=="2" and c.get("fwd")=="c64viz2.local" and c.get("src")=="0", "saved: stream on, forwarded, on to c64viz2, source Elite II")
        DEVS["now"]=RIGHT
        await pg.click("nav.tabs button[data-t=ctl]"); await pg.click("section[data-tab=ctl] button:has-text('Einricht-Assistent')"); await pg.wait_for_timeout(400)
        for _ in range(3): await pg.click("#wznext"); await pg.wait_for_timeout(150)
        check((await pg.inner_text("#wzbody")).count("✔ eingestellt")==3, "beacons match: all three ticked off")
        await pg.click("#wznext"); await pg.wait_for_timeout(200)
        t=await pg.inner_text("#wzbody")
        check("Alles läuft" in t and t.count("Pakete/s")==3, "step 5: the sound arrives everywhere")
        await pg.screenshot(path="tab_wizard.png")
        # a target page opened by the assistant with an Ultimate that is not in its list yet
        p3=await ctx.new_page(); p3.on("pageerror",lambda e: errs.append(str(e))); p3.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
        w={"v":1,"by":"c64viz","stream":1,"smode":0,"fwd":"","u64":"192.168.1.41","u64l":"Elite"}
        await p3.goto("http://c64viz.local/#setup="+urllib.parse.quote(json.dumps(w))); await p3.wait_for_timeout(800)
        check(await p3.input_value("#ua1")=="192.168.1.41" and await p3.input_value("#ul1")=="Elite" and await p3.input_value("#smode")=="0"
              and await p3.input_value("#fwd")=="" and "#setup" not in p3.url, "target page: new Ultimate in the next free place, unicast, no forwarding, link cleaned")
        await p3.click("section[data-tab=net] button:has-text('Speichern und neu starten')"); await p3.wait_for_timeout(300)
        check(len(configs)>1 and configs[-1].get("src")=="1" and configs[-1].get("a1")=="192.168.1.41", "saved with that Ultimate as the source")
        check(not errs, f"no JS errors {errs}"); await b.close()
    print('all checks passed' if not fails else f'{len(fails)} checks FAILED'); sys.exit(1 if fails else 0)
asyncio.run(main())
