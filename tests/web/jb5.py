import asyncio, json
from urllib.parse import unquote
from playwright.async_api import async_playwright
# multicast warning in the WLAN tab + "Liste gekürzt" notice in the FTP picker
HTML=open('../../tools/text_editor.html','rb').read()
LS={"/":{"items":[{"d":1,"n":"Usb0"}],"trunc":False},
    "/Usb0":{"items":[{"d":0,"n":"Tune_%03d.sid"%i} for i in range(340)],"trunc":True}}
cur=["/"]
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"1","stream":True,"thr":30,"pwdefault":False,"sta":True,"smode":1,"group":"239.0.1.65","silent":0,"sidbusy":False}),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u.startswith('/jb/ls') and r.method=='POST': cur[0]=unquote(u.split('path=')[1]); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u=='/jb/ls':
        l=LS.get(cur[0].rstrip('/') or '/',{"items":[],"trunc":False})
        await route.fulfill(body=json.dumps({"state":2,"stage":"","path":cur[0],"err":"","items":l["items"],"trunc":l["trunc"]}),content_type='application/json')
    elif u=='/jb/state': await route.fulfill(body=json.dumps({"state":"idle","msg":"","pos":-1,"count":0,"dur":-1,"elapsed":0,"shuffle":False,"title":"","author":"","file":"","md5":"","prefs":{"src":0,"jbsrc":0,"hvsc":"","user":"","dir":"/"},"sldb":{"count":0,"busy":False,"chunk":False,"msg":"","hvsc":"","user":""}}),content_type='application/json')
    elif r.method=='POST': await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1400}); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
        await pg.click("nav.tabs button[data-t=net]"); await pg.wait_for_timeout(200)
        vis=lambda: pg.evaluate("getComputedStyle(document.getElementById('smwarn')).display!='none'")
        print("status says multicast -> warning shown:", await vis())
        await pg.select_option("#smode","0"); await pg.wait_for_timeout(100)
        print("switched to unicast  -> warning shown:", await vis())
        await pg.select_option("#smode","1"); await pg.wait_for_timeout(100)
        print("switched to multicast-> warning shown:", await vis())
        print("warning text:", (await pg.inner_text("#smwarn")).split(":")[0])
        await pg.click("nav.tabs button[data-t=jb]"); await pg.wait_for_timeout(200)
        await pg.click("text=Auswählen … >> nth=0"); await pg.wait_for_timeout(600)
        t=await pg.inner_text("#pkls"); print("root listing truncated notice:", "Liste gekürzt" in t)
        await pg.click("#pkls >> text=Usb0"); await pg.wait_for_timeout(600)
        t=await pg.inner_text("#pkls")
        print("big folder: %d entries shown, truncated notice: %s" % (t.count(".sid"), "Liste gekürzt" in t))
        print("JS errors:", errs)
        await b.close()
asyncio.run(main())
