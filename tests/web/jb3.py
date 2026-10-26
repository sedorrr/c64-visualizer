import asyncio, json, sys
from urllib.parse import unquote, parse_qs, urlparse
from playwright.async_api import async_playwright
# song length files: pick them on the Ultimate's stick (picker #pkls), the Pico remembers them, then the PC source
HTML=open('../../tools/text_editor.html','rb').read(); posts=[]; chunks=[]; fails=[]
LS={"/":[{"d":1,"n":"Usb0"}],"/Usb0":[{"d":1,"n":"C64Music"},{"d":0,"n":"user_songlengths_export.txt"}],
    "/Usb0/C64Music":[{"d":1,"n":"DOCUMENTS"},{"d":0,"n":"Commando.sid"}],"/Usb0/C64Music/DOCUMENTS":[{"d":0,"n":"Songlengths.md5"},{"d":0,"n":"STIL.txt"}]}
cur=["/"]; prefs={"src":0,"jbsrc":0,"hvsc":"","user":"","dir":"/"}
HVSC="/Usb0/C64Music/DOCUMENTS/Songlengths.md5"; USER="/Usb0/user_songlengths_export.txt"
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
def state(): return {"state":"play","msg":"spielt","pos":0,"count":1,"dur":-1,"elapsed":5,"shuffle":False,"title":"Commando","author":"Rob Hubbard",
  "file":"/Usb0/C64Music/Commando.sid","md5":"d35a9bc7666fdcb4a1b2c3d4e5f60718","prefs":prefs,
  "sldb":{"count":61234,"busy":False,"chunk":False,"msg":"","hvsc":"PC: Songlengths.md5","user":""}}
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"1","stream":True,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False}),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u.startswith('/jb/ls') and r.method=='POST': cur[0]=unquote(u.split('path=')[1]); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u=='/jb/ls': await route.fulfill(body=json.dumps({"state":2,"stage":"","path":cur[0],"err":"","items":LS.get(cur[0].rstrip('/') or '/',[])}),content_type='application/json')
    elif u=='/jb/state': await route.fulfill(body=json.dumps(state()),content_type='application/json')
    elif u.startswith('/jb/prefs'):
        q={k:v[0] for k,v in parse_qs(urlparse(u).query,keep_blank_values=True).items()}
        if 'src' in q: prefs['src']=int(q['src'])
        for k in ('hvsc','user','dir'):
            if k in q: prefs[k]=q[k]
        posts.append('prefs '+json.dumps(q)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u=='/sldb/chunk': chunks.append(len(r.post_data_buffer)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif r.method=='POST': posts.append(unquote(u)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def run(b, errs):
    pg=await b.new_page(viewport={"width":420,"height":1500}); pg.on("pageerror",lambda e: errs.append(str(e)))
    await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
    await pg.click("nav.tabs button[data-t=jb]"); await pg.wait_for_timeout(200)
    return pg
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await run(b, errs)
        np=(await pg.inner_text("#stnp")).replace("\n"," / "); print("now playing:", np)
        check("Länge unbekannt" in np, "unknown length hint with MD5")
        # stick: choose the HVSC file in the picker
        await pg.click(".slrow:has(#slhvscname) >> text=Auswählen …"); await pg.wait_for_timeout(500)
        check(await pg.inner_text("#pktitle")=="HVSC-Datei auf dem Ultimate wählen", "picker title for the HVSC file")
        check(not await pg.is_visible("#pkdir"), "no 'choose folder' button in file mode")
        await pg.click("#pkls >> text=Usb0"); await pg.wait_for_timeout(500)
        await pg.click("#pkls >> text=C64Music"); await pg.wait_for_timeout(500)
        await pg.click("#pkls >> text=DOCUMENTS"); await pg.wait_for_timeout(500)
        ls=await pg.inner_text("#pkls"); print("DOCUMENTS:", ls.replace("\n"," | "))
        check("Songlengths.md5" in ls and "STIL.txt" in ls, "file picker shows all files")
        await pg.click("#pkls >> text=Songlengths.md5"); await pg.wait_for_timeout(300)
        check(not await pg.is_visible("#picker"), "picker closed after choosing a file")
        # own file: the picker starts in the folder of the last choice
        await pg.click(".slrow:has(#slusername) >> text=Auswählen …"); await pg.wait_for_timeout(500)
        print("picker opens in:", await pg.inner_text("#pkpath"))
        check(await pg.inner_text("#pkpath")=="/Usb0/C64Music/DOCUMENTS", "picker opens in the last folder")
        await pg.click("#pkls >> text=.."); await pg.wait_for_timeout(500)
        await pg.click("#pkls >> text=.."); await pg.wait_for_timeout(500)
        await pg.click("#pkls >> text=user_songlengths_export.txt"); await pg.wait_for_timeout(300)
        h,us=await pg.inner_text("#slhvscname"), await pg.inner_text("#slusername"); print("chosen (stick):", h, "|", us)
        check(h==HVSC and us==USER, "both stick files shown")
        await pg.click("text=Songlängen einlesen"); await pg.wait_for_timeout(300)
        check("/sldb/stick?hvsc=%s&user=%s" % (HVSC, USER) in posts, "stick import requested with both paths")
        # reload: the Pico remembers
        pg2=await run(b, errs)
        h,us=await pg2.inner_text("#slhvscname"), await pg2.inner_text("#slusername"); print("after reload:", h, "|", us)
        check(h==HVSC and us==USER, "Pico remembers both files")
        # PC source
        await pg2.check("input[name=slsrc][value='1']"); await pg2.wait_for_timeout(200)
        check(await pg2.inner_text("#slhvscname")=="–", "PC source starts empty")
        check(prefs['src']==1, "PC source remembered")
        await pg2.set_input_files("#slfh","fixtures/hvsc.md5"); await pg2.set_input_files("#slfu","fixtures/user.txt")
        h,us=await pg2.inner_text("#slhvscname"), await pg2.inner_text("#slusername"); print("chosen (PC):", h, "|", us)
        check(h=="hvsc.md5" and us=="user.txt", "both PC files shown")
        await pg2.click("text=Songlängen einlesen"); await pg2.wait_for_timeout(1500)
        m=await pg2.inner_text("#msg"); print("message:", m, "| chunks:", chunks)
        check(m.endswith("Songlängen übertragen.") and len(chunks)==1 and chunks[0]>0, "PC import sent")
        await pg2.click(".slrow:has(#slusername) >> text=✕"); await pg2.wait_for_timeout(100)
        check(await pg2.inner_text("#slusername")=="–" and await pg2.inner_text("#slhvscname")=="hvsc.md5", "clear removes only the own file")
        await pg2.check("input[name=slsrc][value='0']"); await pg2.wait_for_timeout(200)
        check(await pg2.inner_text("#slhvscname")==HVSC, "back to stick shows the stick file again")
        # delete the database on the Pico: cancel in the dialog first, then confirm
        pg2.once("dialog", lambda d: asyncio.ensure_future(d.dismiss()))
        await pg2.click("text=Datenbank löschen"); await pg2.wait_for_timeout(300)
        check('/sldb/clear' not in posts, "cancel in the dialog sends nothing")
        pg2.once("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg2.click("text=Datenbank löschen"); await pg2.wait_for_timeout(600)
        m=await pg2.inner_text("#msg"); print("after delete:", m)
        check('/sldb/clear' in posts, "delete requests /sldb/clear")
        check(m=="Songlängen-Datenbank gelöscht.", "delete message shown")
        print("requests:", posts); print("JS errors:", errs)
        check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
