import os
import asyncio, json
from playwright.async_api import async_playwright
HTML=open('../../tools/text_editor.html','rb').read(); saved=[]
TX=b"@silence 8000\n@sleep 30\n@sleepstyle clock\n@sleepcolor FF4000\n{color:cyan}Hello\n"
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps({"wifi":"Fritz!Box 7590","ip":"192.168.1.57","net":True,"pps":250,"u64state":"Stream läuft","ssid":"Fritz!Box 7590","u64":"192.168.1.64","stream":True,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False}),content_type='application/json')
    elif u=='/texts' and r.method=='GET': await route.fulfill(body=TX)
    elif u=='/texts': saved.append(r.post_data); await route.fulfill(body='Gespeichert')
    elif u=='/jb/state': await route.fulfill(body=json.dumps({"state":"idle","msg":"","pos":-1,"count":0,"dur":-1,"elapsed":0,"shuffle":False,"title":"","author":"","file":"","md5":"","prefs":{"src":0,"hvsc":"/Usb0/C64Music/DOCUMENTS/Songlengths.md5","user":"","dir":"/"},"sldb":{"count":61234,"busy":False,"chunk":False,"msg":"","hvsc":"/Usb0/C64Music/DOCUMENTS/Songlengths.md5","user":""}}),content_type='application/json')
    elif r.method=='POST': await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":900}, device_scale_factor=2); pg.on("pageerror",lambda e: errs.append(str(e)))
        pg.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
        for t,name in [("ctl","Steuerung"),("txt","Lauftexte"),("set","Einstellungen"),("jb","SID-Jukebox"),("net","WLAN")]:
            await pg.click(f"nav.tabs button[data-t={t}]"); await pg.wait_for_timeout(400)
            vis=await pg.evaluate("[...document.querySelectorAll('section[data-tab]')].filter(s=>getComputedStyle(s).display!='none').map(s=>s.dataset.tab).join(',')")
            bar=await pg.evaluate("getComputedStyle(document.getElementById('savebar')).display!='none'")
            print(f"{name:14s} visible: {vis:5s} save bar: {bar}")
            await pg.screenshot(path=f"tab_{t}.png")
        await pg.click("nav.tabs button[data-t=set]")
        print("sleep colour radio:", await pg.evaluate("document.querySelector('input[name=slc]:checked').value"), await pg.input_value("#sleepcolor"))
        await pg.click("#save"); await pg.wait_for_timeout(400)
        print("saved settings:", [l for l in saved[0].split("\n") if l.startswith("@sleep")])
        pg2=await b.new_page(viewport={"width":420,"height":900}); await pg2.route("http://c64viz.local/**", handler); await pg2.goto("http://c64viz.local/"); await pg2.wait_for_timeout(900)
        print("reopened on tab:", await pg2.evaluate("[...document.querySelectorAll('nav.tabs button')].find(b=>b.getAttribute('aria-selected')=='true').dataset.t"))
        pg3=await b.new_page(viewport={"width":420,"height":900}); pg3.on("pageerror",lambda e: errs.append(str(e)))
        await pg3.goto("file://"+os.path.abspath("../../tools/text_editor.html")); await pg3.wait_for_timeout(700)
        tabs=await pg3.evaluate("[...document.querySelectorAll('nav.tabs button')].filter(b=>getComputedStyle(b).display!='none').map(b=>b.textContent).join(' | ')")
        print("offline tabs:", tabs); await pg3.screenshot(path="tab_offline.png")
        print("JS errors:", errs); await b.close()
asyncio.run(main())
