import asyncio, json, os, sys
from urllib.parse import unquote
from playwright.async_api import async_playwright
# stick browsing in the FTP picker (#pkls) + play the folder + PC song length import:
# the records the browser sends must be byte-identical to the Pico's parser (fixtures/c_records.bin)
HTML=open('../../tools/text_editor.html','rb').read(); chunks=[]; posts=[]; fails=[]
if not os.path.exists('fixtures/c_records.bin'): sys.exit("fixtures/c_records.bin missing - run tests/run_host_tests.sh first (sldb_parse_files)")
LS={"/":[{"d":1,"n":"Usb0"},{"d":1,"n":"Flash"}],"/Usb0":[{"d":1,"n":"C64Music"},{"d":0,"n":"user_songlengths_export.txt"}],"/Usb0/C64Music":[{"d":0,"n":"Commando.sid"},{"d":0,"n":"readme.txt"},{"d":1,"n":"Sub"}]}
cur=["/"]; prefs={"src":0,"jbsrc":0,"hvsc":"","user":"","dir":"/"}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
def state():
    return {"state":"play","msg":"spielt","pos":0,"count":3,"dur":276,"elapsed":83,"shuffle":False,"title":"Commando","author":"Rob Hubbard","file":"/Usb0/C64Music/Commando.sid","md5":"","prefs":prefs,
            "sldb":{"count":19,"busy":False,"chunk":False,"msg":"","hvsc":"/Usb0/C64Music/DOCUMENTS/Songlengths.md5","user":""}}
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"1","stream":True,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False}),content_type='application/json')
    elif u=='/texts': await route.fulfill(body='Hello\n')
    elif u.startswith('/jb/ls') and r.method=='POST': cur[0]=unquote(u.split('path=')[1]); posts.append('ls '+cur[0]); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif u=='/jb/ls': await route.fulfill(body=json.dumps({"state":2,"stage":"","path":cur[0],"err":"","items":LS.get(cur[0].rstrip('/') or '/',[])}),content_type='application/json')
    elif u=='/jb/state': await route.fulfill(body=json.dumps(state()),content_type='application/json')
    elif u=='/sldb/chunk': chunks.append(r.post_data_buffer); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif r.method=='POST': posts.append(unquote(u)); await route.fulfill(body='{"ok":true}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1400}); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
        await pg.click("nav.tabs button[data-t=jb]"); await pg.wait_for_timeout(200)
        # stick: pick the folder in the picker
        await pg.click(".slrow:has(#jbdir) >> text=Auswählen …"); await pg.wait_for_timeout(600)
        print("root:", (await pg.inner_text("#pkls")).replace("\n"," | "))
        await pg.click("#pkls >> text=Usb0"); await pg.wait_for_timeout(600)
        await pg.click("#pkls >> text=C64Music"); await pg.wait_for_timeout(600)
        ls=await pg.inner_text("#pkls"); print("listing:", ls.replace("\n"," | "), "| path:", await pg.inner_text("#pkpath"))
        check(await pg.inner_text("#pkpath")=="/Usb0/C64Music", "picker is in /Usb0/C64Music")
        check("Commando.sid" in ls and "Sub" in ls and ".." in ls, "folder listing shows .., Sub, Commando.sid")
        check("readme.txt" not in ls, "folder picker hides non-SID files")
        await pg.click("#pkdir"); await pg.wait_for_timeout(300)
        check(await pg.inner_text("#jbdir")=="/Usb0/C64Music", "chosen folder shown")
        check(not await pg.is_visible("#picker"), "picker closed")
        await pg.click("text=▶ Abspielen"); await pg.wait_for_timeout(500)
        play=[x for x in posts if x.startswith('/jb/play')]; print("play request:", play)
        check(len(play)==1 and "path=/Usb0/C64Music&sub=1" in play[0], "play request for the folder with sub-folders")
        await pg.wait_for_timeout(2000)   # next /jb/state poll
        np=await pg.inner_text("#stnp"); print("now playing:", np); print("song lengths:", await pg.inner_text("#slinfo"))
        check("Commando" in np and "(1 von 3)" in np and "1:23 / 4:36" in np, "now playing shows title, position and time")
        # song lengths from the PC
        await pg.check("input[name=slsrc][value='1']"); await pg.wait_for_timeout(200)
        await pg.set_input_files("#slfu","fixtures/user.txt"); await pg.set_input_files("#slfh","fixtures/hvsc.md5")
        check(await pg.inner_text("#slhvscname")=="hvsc.md5" and await pg.inner_text("#slusername")=="user.txt", "PC files shown")
        await pg.click("text=Songlängen einlesen"); await pg.wait_for_timeout(1500)
        m=await pg.inner_text("#msg"); print("message:", m)
        check(m.endswith("Songlängen übertragen."), "import finished")
        data=b"".join(chunks); c=open('fixtures/c_records.bin','rb').read()
        print("browser records %d bytes, Pico parser %d bytes -> %s" % (len(data), len(c), "identical" if data==c else "DIFFERENT"))
        check(len(c)>0 and data==c, "browser records == Pico parser records")
        begin=[x for x in posts if x.startswith('/sldb/begin')]
        check(len(begin)==1 and "hvsc=PC:+hvsc.md5" in begin[0] and "user=PC:+user.txt" in begin[0], "begin names both PC files")
        check("/sldb/end" in posts, "end sent")
        print("requests:", [x for x in posts if not x.startswith('ls')]); print("JS errors:", errs)
        check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
