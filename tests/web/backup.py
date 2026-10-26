import asyncio, json, sys, os, tempfile
from urllib.parse import parse_qs
from playwright.async_api import async_playwright
# backup: download = /backup + /texts in one JSON file; restore = /restore (settings), then /texts (restart); wrong files refused
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; posts=[]
STATUS={"wifi":"MeinWLAN","ip":"192.168.1.231","net":False,"pps":0,"u64state":"-","ssid":"MeinWLAN","u64":"192.168.1.168","stream":False,"thr":30,
        "pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":"Oct 15 2026","load":12,
        "busymax":2900,"framemax":16000,"stallmax":20000,"stereo":"","waits":0,"lux10":-10,"lightpct":100,"name":"wohnzimmer","cfgname":"wohnzimmer",
        "bright":20,"fwd":"","fwdip":"-","fpps":0,"fwderr":0,"src":0,"u64list":[],"scanning":False}
BACKUP={"fmt":"c64viz-backup","ver":1,"build":"Oct 15 2026","name":"wohnzimmer","netcfg":"05ab","jbprefs":"32","settings":"ff"*4096}
TEXTS="@brightness 20\nHallo {color:red}Welt\n"
STATE={"texts_refused":1}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]
    if u=='/status': await route.fulfill(body=json.dumps(STATUS),content_type='application/json')
    elif u=='/texts' and r.method=='GET': await route.fulfill(body=TEXTS)
    elif u=='/texts':
        posts.append(('texts',r.post_data))
        if STATE["texts_refused"]>0: STATE["texts_refused"]-=1; await route.fulfill(status=400,body='Ungültige Daten.')
        else: await route.fulfill(body='Gespeichert - der Pico startet neu.')
    elif u=='/backup': await route.fulfill(body=json.dumps(BACKUP),content_type='application/json')
    elif u=='/restore':
        posts.append(('restore',r.post_data))
        q=parse_qs(r.post_data)
        if q.get("netcfg")==["bad"]: await route.fulfill(status=400,body='Die Sicherung passt nicht (WLAN-/Ultimate-Einstellungen fehlen oder sind beschädigt).')
        else: await route.fulfill(body='Einstellungen wiederhergestellt - der Pico startet gleich neu.')
    elif u.startswith('/jb/') or u.startswith('/fw/') or u.startswith('/u64/'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    tmp=tempfile.mkdtemp()
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        ctx=await b.new_context(accept_downloads=True,viewport={"width":420,"height":1600})
        pg=await ctx.new_page(); pg.on("pageerror",lambda e: errs.append(str(e)))
        dialogs=[]; pg.on("dialog",lambda d: (dialogs.append(d.message), asyncio.ensure_future(d.accept())))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(1000)
        await pg.click("nav.tabs button[data-t=net]"); await pg.wait_for_timeout(200)
        async with pg.expect_download() as dl: await pg.click("text=Sicherung herunterladen")
        d=await dl.value; path=os.path.join(tmp,d.suggested_filename); await d.save_as(path)
        data=json.load(open(path))
        check(d.suggested_filename.startswith("c64viz-wohnzimmer-") and d.suggested_filename.endswith(".json"), "file name c64viz-<name>-<date>.json: "+d.suggested_filename)
        check(data["netcfg"]=="05ab" and data["settings"]==BACKUP["settings"] and data["texts"]==TEXTS and data.get("date"), "file has the Pico's blocks, the texts and the date")
        # restore it
        await pg.set_input_files("#bkfile", path); await pg.wait_for_timeout(2500)
        check(dialogs and "wohnzimmer" in dialogs[-1] and "Lauftexte" in dialogs[-1], "asks first (name, what is replaced)")
        kinds=[k for k,_ in posts]
        check(kinds[:1]==["restore"] and kinds.count("texts")==2, "settings first, then the texts (once refused, sent again): %s" % kinds)
        q=parse_qs(posts[0][1])
        check(q.get("netcfg")==["05ab"] and q.get("jbprefs")==["32"] and "reboot" not in q, "restore carries the blocks, no immediate restart")
        check(posts[-1][1]==TEXTS, "texts sent unchanged")
        check("startet neu" in await pg.inner_text("#bkmsg"), "message: restarts")
        # a file without texts: restart at once
        posts.clear(); nt=dict(data); del nt["texts"]; p2=os.path.join(tmp,"nt.json"); json.dump(nt,open(p2,"w"))
        await pg.set_input_files("#bkfile", p2); await pg.wait_for_timeout(800)
        check(len(posts)==1 and parse_qs(posts[0][1]).get("reboot")==["1"], "without texts: restore with reboot=1")
        # wrong files
        posts.clear(); p3=os.path.join(tmp,"x.json"); open(p3,"w").write('{"hello":1}')
        await pg.set_input_files("#bkfile", p3); await pg.wait_for_timeout(500)
        check(not posts and "keine Sicherung" in await pg.inner_text("#bkmsg"), "foreign file: refused in the browser")
        bad=dict(data); bad["netcfg"]="bad"; p4=os.path.join(tmp,"bad.json"); json.dump(bad,open(p4,"w"))
        await pg.set_input_files("#bkfile", p4); await pg.wait_for_timeout(800)
        check([k for k,_ in posts]==["restore"] and "passt nicht" in await pg.inner_text("#bkmsg"), "Pico refuses: message shown, texts not sent")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
