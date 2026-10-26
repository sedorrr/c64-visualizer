import os, sys
import asyncio, json
from playwright.async_api import async_playwright
# Devices in the network (card in "Steuerung") and two matrices as one picture (settings): the Pico is mocked.
HTML=open('../../tools/text_editor.html','rb').read()
fails=[]; saved=[]; queries=[]
def check(ok, what):
    print(('ok   ' if ok else 'FAIL ')+what)
    if not ok: fails.append(what)
def dev(**k): return json.dumps(k)
DEVICES={"list":[
  {"self":True,"age":0,"j":dev(t="viz",n="c64viz",ip="192.168.1.231",b="Oct 18 2026 10:12:00",img="c64viz_wifi_ws2812_usb-350mA_debug",st=1,sm=2,rx=250,**{"from":"192.168.1.79"},fw="c64viz2.local",fwip="192.168.1.224",fp=250,src="192.168.1.168",srcl="Elite II",wide="lead",wpart="c64viz2.local",wside="left",wok=True)},
  {"age":3,"j":dev(t="audio",n="c64audio",ip="192.168.1.79",b="Oct 18 2026 10:20:00",img="c64audio",st=1,rx=250,**{"from":"192.168.1.168"},fw="c64viz.local",fwip="192.168.1.231",fp=250,src="192.168.1.168",srcl="Elite II")},
  {"age":20,"j":dev(t="viz",n="c64viz2",ip="192.168.1.224",b="Oct 18 2026 10:12:00",img="c64viz_wifi_ws2812_usb-350mA",st=1,sm=2,rx=250,**{"from":"192.168.1.231"},fw="",fwip="",fp=0,src="",srcl="",wide="follow",wpart="",wside="left",wok=True)},
  {"age":1,"j":dev(t="viz",n="kueche",ip="192.168.1.50",b="Oct 18 2026 10:12:00",img="c64viz_wifi_ws2812_usb-350mA",st=0,sm=0,rx=0,**{"from":""},fw="c64viz3.local",fwip="",fp=0,src="",srcl="")},
  {"age":1,"j":"{kaputt"}]}
ME="c64viz"
def star(pi_on=True):
    """a Raspberry Pi jukebox as distributor: it feeds every Pico itself (one of its targets is off)"""
    pi = dict(t="pi", n="c64pi", ip="192.168.1.78", b="1.0", st=1 if pi_on else 0, sm=0, rx=251 if pi_on else 0,
              fw="c64audio,c64viz,c64viz2,c64viz3", fwip="192.168.1.79", fp=251, src="192.168.1.168", srcl="Elite II",
              title="Commando", **{"from": "192.168.1.168" if pi_on else ""})
    picos = [dict(t=t, n=n, ip=ip, st=1, sm=2, rx=251 if pi_on else 0, fw="", fwip="", fp=0, **{"from": "192.168.1.78" if pi_on else ""})
             for t, n, ip in (("audio", "c64audio", "192.168.1.79"), ("viz", "c64viz", "192.168.1.231"), ("viz", "c64viz2", "192.168.1.224"))]
    return [{"self": d["n"] == ME, "age": 2, "j": json.dumps(d)} for d in [pi] + picos]
TX=b"@silence 8000\n@wide lead\n@widepartner c64viz2.local\n@wideside right\nHello\n"
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.231","net":True,"pps":250,"u64state":"Stream kommt weitergeleitet","ssid":"MeinWLAN","u64":"192.168.1.168","stream":True,"thr":30,"pwdefault":False,"sta":True,"smode":2,"group":"-","silent":0,"sidbusy":False,"bright":20,"lux10":-10,"lightpct":100,"wide":"lead","wpart":"c64viz2.local","wside":"right","wok":True}
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/devices': await route.fulfill(body=json.dumps(DEVICES),content_type='application/json')
    elif u=='/devices/query': queries.append(1); await route.fulfill(body='OK')
    elif u=='/texts' and r.method=='GET': await route.fulfill(body=TX)
    elif u=='/texts': saved.append(r.post_data); await route.fulfill(body='Gespeichert')
    elif u=='/jb/state': await route.fulfill(body=json.dumps({"state":"idle","msg":"","pos":-1,"count":0,"prefs":{"src":0,"dir":"/"},"sldb":{"count":0,"busy":False}}),content_type='application/json')
    elif r.method=='POST': await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1000}); pg.on("pageerror",lambda e: errs.append(str(e)))
        pg.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(500)
        await pg.click("nav.tabs button[data-t=ctl]"); await pg.wait_for_timeout(700)
        chain=await pg.inner_text("#devchain"); lst=await pg.inner_text("#devlist")
        print("   "+chain.replace("\n"," | ")); print("   "+lst.replace("\n"," | ")[:400])
        check("Elite II (192.168.1.168) → c64audio 250/s → c64viz 250/s → c64viz2 250/s" in chain, "the way of the sound, from the Ultimate along the chain")
        check(lst.count("Visualizer")>=3 and "Audio-Empfänger" in lst and "kaputt" not in lst, "four devices, a broken entry is skipped")
        check("leitet nicht weiter an c64viz3.local" in lst, "warning: forwarding entered but the stream is off")
        check("(seit 20 s still)" in lst, "a device not heard of for a while is marked")
        check("Zwei Matrizen: rechnet für c64viz2.local (läuft)" in lst and "zeigt die Hälfte eines anderen (läuft)" in lst, "two matrices in the list")
        check(await pg.get_attribute("#devlist a >> nth=1","href")=="http://c64audio.local/", "names link to the device's page")
        await pg.click("text=Neu suchen"); await pg.wait_for_timeout(1200)
        check(len(queries)==1, "\"Neu suchen\" asks all devices")
        await pg.screenshot(path="tab_devices.png", full_page=True)
        await pg.click("nav.tabs button[data-t=set]"); await pg.wait_for_timeout(300)
        check(await pg.input_value("#wide")=="lead" and await pg.input_value("#widepartner")=="c64viz2.local" and await pg.input_value("#wideside")=="right", "settings read from texts.txt")
        check("Läuft: ein Bild über beide Matrizen" in await pg.inner_text("#widest"), "status: running")
        await pg.select_option("#wide","follow"); vis=await pg.is_visible("#widelead")
        check(not vis, "partner fields only for the Pico that draws")
        await pg.click("#save"); await pg.wait_for_timeout(400)
        t=saved[-1] if saved else ""
        check("@wide follow" in t and "@widepartner" not in t, "saved: @wide follow")
        await pg.select_option("#wide","lead"); await pg.fill("#widepartner",""); n=len(saved)
        await pg.click("#save"); await pg.wait_for_timeout(300)
        check(len(saved)==n and "Partners eintragen" in await pg.inner_text("#msg"), "draws without a partner: not saved, says why")
        await pg.fill("#widepartner","192.168.1.224"); await pg.select_option("#wideside","left"); await pg.click("#save"); await pg.wait_for_timeout(300)
        t=saved[-1]
        check("@wide lead" in t and "@widepartner 192.168.1.224" in t and "@wideside left" in t, "saved: lead, partner, side")
        await pg.click("nav.tabs button[data-t=help]"); await pg.wait_for_timeout(200)
        pg2=await b.new_page(viewport={"width":420,"height":900}); pg2.on("pageerror",lambda e: errs.append(str(e)))
        await pg2.goto("file://"+os.path.abspath("../../tools/text_editor.html")); await pg2.wait_for_timeout(500)
        await pg2.click("nav.tabs button[data-t=set]")
        check(await pg2.is_visible("#wide"), "offline editor: the setting is there too (for texts.uf2)")
        await pg.click("nav.tabs button[data-t=ctl]"); await pg.wait_for_timeout(300)
        # a Raspberry Pi as distributor: the way of the sound branches out, the Pi has its own name and shows the title
        DEVICES["list"] = star(); await pg.evaluate("devLoad()"); await pg.wait_for_timeout(300)
        chain = await pg.inner_text("#devchain"); lst = await pg.inner_text("#devlist")
        print("   " + chain.replace("\n", " | "))
        check("Elite II (192.168.1.168) → c64pi 251/s → [c64audio 251/s, c64viz 251/s, c64viz2 251/s, c64viz3 (bekommt nichts)]" in chain,
              "Pi feeding several Picos: a tree, a target that gets nothing is marked")
        check("c64pi – Raspberry Pi, 192.168.1.78" in lst and "♪ Commando" in lst and "pi," not in lst, "the Pi is named as such, with its title")
        DEVICES["list"] = star(False); await pg.evaluate("devLoad()"); await pg.wait_for_timeout(300)
        lst = await pg.inner_text("#devlist")
        check("c64pi – Raspberry Pi" in lst and "leitet" not in lst.split("c64audio")[0], "Pi with the distributor off: no forwarding line")
        check(not errs, f"no JS errors {errs}"); await b.close()
    print('all checks passed' if not fails else f'{len(fails)} checks FAILED'); sys.exit(1 if fails else 0)
asyncio.run(main())
