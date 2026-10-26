import asyncio, json, sys
from urllib.parse import unquote, parse_qs, urlparse
from playwright.async_api import async_playwright
HTML=open('../../tools/text_editor.html','rb').read(); posts=[]; playing=[False]; fails=[]
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
LS={"/":[{"d":1,"n":"Usb0"}],"/Usb0":[{"d":1,"n":"C64Music"},{"d":0,"n":"readme.txt"}],"/Usb0/C64Music":[{"d":1,"n":"Hubbard"},{"d":0,"n":"Commando.sid"},{"d":0,"n":"notes.txt"}]}
cur=["/"]; prefs={"src":0,"jbsrc":0,"hvsc":"","user":"","dir":"/"}
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"1","stream":False,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False}),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u.startswith('/jb/ls') and r.method=='POST': cur[0]=unquote(u.split('path=')[1]); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u=='/jb/ls': await route.fulfill(body=json.dumps({"state":2,"stage":"","path":cur[0],"err":"","items":LS.get(cur[0].rstrip('/') or '/',[])}),content_type='application/json')
    elif u=='/jb/state':
        s={"state":"idle","msg":"","pos":-1,"count":0,"dur":-1,"elapsed":0,"shuffle":False,"title":"","author":"","file":"","md5":"","prefs":prefs,"sldb":{"count":0,"busy":False,"chunk":False,"msg":"","hvsc":"","user":""}}
        if playing[0]: s.update({"state":"play","msg":"spielt","pos":1,"count":3,"elapsed":5,"title":"Monty on the Run","author":"Rob Hubbard","file":"/Usb0/C64Music/Hubbard/Monty_on_the_Run.sid","sidaddr":"Sockel 2 für dieses Stück auf $D500","sidcfg":"SID 1: D400  SID 2: D500"})
        await route.fulfill(body=json.dumps(s),content_type='application/json')
    elif u=='/jb/list': await route.fulfill(body=json.dumps({"pos":1,"items":["Commando.sid","Monty_on_the_Run.sid","Last_Ninja.sid"],"trunc":False}),content_type='application/json')
    elif u.startswith('/jb/play'): playing[0]=True; posts.append(unquote(u)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u.startswith('/jb/plimport'): playing[0]=True; posts.append(unquote(u)+' :: '+(r.post_data or '').replace('\n','|')); await route.fulfill(body='{"ok":true,"n":3}',content_type='application/json')
    elif u.startswith('/jb/prefs'):
        q={k:v[0] for k,v in parse_qs(urlparse(u).query).items()}
        if 'jbsrc' in q: prefs['jbsrc']=int(q['jbsrc'])
        if 'dir' in q: prefs['dir']=q['dir']
        posts.append('prefs '+json.dumps(q)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u.startswith('/sid'): posts.append(u+' '+str(len(r.post_data_buffer))); await route.fulfill(body='{"ok":true,"secs":-1,"md5":"x"}',content_type='application/json')
    elif r.method=='POST': posts.append(unquote(u)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); ctx=await b.new_context(viewport={"width":420,"height":1200}); errs=[]
        pg=await ctx.new_page(); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
        await pg.click("nav.tabs button[data-t=jb]"); await pg.wait_for_timeout(300)
        vis=lambda: pg.evaluate("[...document.querySelectorAll('.pconly,.u64only')].filter(e=>getComputedStyle(e).display!='none').map(e=>e.id||e.className).join(',')")
        print("Ultimate mode shows:", await vis())
        await pg.click("text=Auswählen … >> nth=0"); await pg.wait_for_timeout(500)
        print("picker:", (await pg.inner_text("#pkls")).replace("\n"," | "))
        await pg.click("#pkls >> text=Usb0"); await pg.wait_for_timeout(400); await pg.click("#pkls >> text=C64Music"); await pg.wait_for_timeout(400)
        print("in C64Music:", (await pg.inner_text("#pkls")).replace("\n"," | "))
        await pg.screenshot(path="jb_picker.png")
        await pg.click("#pkdir"); await pg.wait_for_timeout(300)
        print("chosen folder:", await pg.inner_text("#jbdir"))
        await pg.click("text=▶ Abspielen"); await pg.wait_for_timeout(300); await pg.click("text=⏭"); await pg.wait_for_timeout(300)
        # playlist held in the Pico: shown once the state poll reports playing, click plays that title
        await pg.wait_for_timeout(2600)
        stpl=(await pg.inner_text("#stpl")).replace("\n"," | "); print("stick list:", stpl)
        check("1. Commando.sid" in stpl and "3. Last_Ninja.sid" in stpl, "stick playlist shown")
        hl=await pg.evaluate("[...document.querySelectorAll('#stpl div')].findIndex(d=>d.style.background)")
        check(hl==1, "current title highlighted")
        await pg.click("#stpl >> text=1. Commando.sid"); await pg.wait_for_timeout(300)
        check("/jb/goto?i=0" in posts, "click sends /jb/goto")
        # SID addresses: switch stored in the Pico, message of the last switch shown
        check(not await pg.is_checked("#sidauto"), "SID address switch off by default")
        await pg.check("#sidauto"); await pg.wait_for_timeout(200)
        check(any('prefs {"sidauto": "1"}'==p for p in posts), "ticking sends sidauto=1 to the Pico")
        check("Sockel 2 für dieses Stück auf $D500" in await pg.inner_text("#sidaddrmsg"), "last switch shown")
        cfg=await pg.inner_text("#sidcfg"); print("socket line:", cfg)
        check(cfg=="Sockel jetzt: 1 = $D400, 2 = $D500.", "current sockets shown readably")
        await pg.check("#sidshow"); await pg.wait_for_timeout(200)
        check(any('prefs {"sidshow": "1"}'==p for p in posts), "ticking sends sidshow=1 to the Pico")
        # shared playlist of the Ultimate64 SID Player: file names go to the Pico, search folder = chosen folder
        await pg.set_input_files("#plfile","fixtures/test_u64playlist.json"); await pg.wait_for_timeout(500)
        pli=[p for p in posts if p.startswith('/jb/plimport')]
        print("plimport:", pli)
        check(len(pli)==1 and '/jb/plimport?path=/Usb0/C64Music&sub=1' in pli[0], "playlist import uses the chosen folder")
        check('Commando.sid|Monty_on_the_Run.sid|Fehlt_am_Stick.sid' in pli[0], "all file names sent in order")
        m=await pg.inner_text("#msg"); print("msg:", m)
        check('Testliste' in m and 'suche 3 Titel' in m, "import message names playlist and count")
        pg2=await ctx.new_page(); await pg2.route("http://c64viz.local/**", handler); await pg2.goto("http://c64viz.local/"); await pg2.wait_for_timeout(900)
        print("after reload:", await pg2.inner_text("#jbdir"), "| source", await pg2.evaluate("document.querySelector('input[name=jbsrc]:checked').value"))
        await pg2.check("input[name=jbsrc][value='1']"); await pg2.wait_for_timeout(200)
        print("PC mode shows:", await pg2.evaluate("[...document.querySelectorAll('.pconly,.u64only')].filter(e=>getComputedStyle(e).display!='none').map(e=>e.id||e.className).join(',')"))
        await pg2.set_input_files("#siddir","fixtures/sids/Musiker"); await pg2.wait_for_timeout(300)
        print("PC folder (with sub-folders):", await pg2.inner_text("#jbdir"), "|", (await pg2.inner_text("#pl")).replace("\n"," | "))
        await pg2.uncheck("#stsub"); await pg2.wait_for_timeout(300)
        print("without sub-folders:", await pg2.inner_text("#jbdir"), "|", (await pg2.inner_text("#pl")).replace("\n"," | "))
        await pg2.click("text=▶ Abspielen"); await pg2.wait_for_timeout(500)
        print("requests:", posts); print("JS errors:", errs)
        el=await pg2.query_selector("section[data-tab=jb] .card"); await el.screenshot(path="jb_tab.png")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
