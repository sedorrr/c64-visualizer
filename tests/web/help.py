import http.server, os, re, sys, threading
import asyncio, json
from playwright.async_api import async_playwright
# The "Hilfe" tab: on the Pico the manual comes from GET /hilfe (gzip, like the firmware sends it), in the offline
# editor it is part of the page. Context links in the other tabs jump to their section.
HTML=open('../../tools/text_editor.html','rb').read()
GZ=bytes(int(x) for x in re.findall(r'\d+', open('../../src/net/help_page.h').read().split('{',1)[1].split('}')[0]))
HTML_PICO=re.sub(rb'<div class=card id=helpbody>.*?</div></section>', b'<div class=card id=helpbody></div></section>', HTML, flags=re.S)
hits=[]; fails=[]
def check(ok, what):
    print(('ok   ' if ok else 'FAIL ')+what)
    if not ok: fails.append(what)
class Pico(http.server.BaseHTTPRequestHandler):
    """a real HTTP server, so the browser itself unpacks the gzip like it does with the Pico"""
    def log_message(self, *a): pass
    def send(self, body, ctype, extra=()):
        self.send_response(200); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body)))
        for k, v in extra: self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        u=self.path
        if u=='/status': self.send(json.dumps({"wifi":"MeinWLAN","ip":"192.168.1.231","net":True,"pps":250,"u64state":"Stream läuft","ssid":"MeinWLAN","u64":"192.168.1.168","stream":True,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"lux10":-10,"lightpct":100,"bright":20}).encode(),'application/json')
        elif u=='/texts': self.send(b"@silence 8000\nHello\n",'text/plain')
        elif u=='/hilfe': hits.append(u); self.send(GZ,'text/html; charset=utf-8',[('Content-Encoding','gzip')])
        elif u=='/jb/state': self.send(json.dumps({"state":"idle","msg":"","pos":-1,"count":0,"prefs":{"src":0,"dir":"/"},"sldb":{"count":0,"busy":False}}).encode(),'application/json')
        elif u=='/': self.send(HTML_PICO,'text/html; charset=utf-8')
        else: self.send_response(404); self.send_header('Content-Length','0'); self.end_headers()
    def do_POST(self):
        n=int(self.headers.get('Content-Length') or 0); self.rfile.read(n); self.send(b'{"ok":true}','application/json')
srv=http.server.ThreadingHTTPServer(('127.0.0.1',0),Pico); threading.Thread(target=srv.serve_forever,daemon=True).start()
URL='http://127.0.0.1:%d/'%srv.server_address[1]
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":900}); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.goto(URL); await pg.wait_for_timeout(800)
        check(not hits, "manual not loaded before the tab is opened")
        await pg.click("nav.tabs button[data-t=help]"); await pg.wait_for_timeout(500)
        h2=await pg.evaluate("[...document.querySelectorAll('#helpbody h2')].map(e=>e.textContent)")
        check(len(hits)==1 and len(h2)>8, f"Pico: tab loads /hilfe once, {len(h2)} chapters")
        check(await pg.evaluate("getComputedStyle(document.getElementById('savebar')).display=='none'"), "no save bar in the help tab")
        await pg.click("nav.tabs button[data-t=ctl]"); await pg.click("nav.tabs button[data-t=help]"); await pg.wait_for_timeout(300)
        check(len(hits)==1, "second opening uses the loaded manual")
        await pg.click("nav.tabs button[data-t=jb]"); await pg.click("section[data-tab=jb] .hl a"); await pg.wait_for_timeout(500)
        top=await pg.evaluate("document.getElementById('h-sid-jukebox').getBoundingClientRect().top")
        check(await pg.evaluate("document.querySelector('nav.tabs button[aria-selected=true]').dataset.t")=='help' and abs(top)<5,
              f"jukebox tab: context link opens the help at 'SID-Jukebox' (top {top:.0f})")
        await pg.click(".htoc a[data-h=lichtsensor]"); await pg.wait_for_timeout(300)
        top=await pg.evaluate("document.getElementById('h-lichtsensor').getBoundingClientRect().top")
        check(abs(top)<5, "table of contents jumps to 'Lichtsensor'")
        bad=await pg.evaluate("[...document.querySelectorAll('#helpbody a[data-h]')].filter(a=>!document.getElementById('h-'+a.dataset.h)).map(a=>a.dataset.h)")
        check(not bad, f"all links inside the manual have a target {bad}")
        wide=await pg.evaluate("document.documentElement.scrollWidth<=window.innerWidth")
        check(wide, "no horizontal page scroll at phone width (tables scroll in their box)")
        await pg.evaluate("window.scrollTo(0,0)"); await pg.screenshot(path="tab_help.png")
        pg3=await b.new_page(viewport={"width":420,"height":900}); pg3.on("pageerror",lambda e: errs.append(str(e)))
        await pg3.goto("file://"+os.path.abspath("../../tools/text_editor.html")); await pg3.wait_for_timeout(700)
        tabs=await pg3.evaluate("[...document.querySelectorAll('nav.tabs button')].filter(b=>getComputedStyle(b).display!='none').map(b=>b.textContent).join(' | ')")
        check(tabs=='Lauftexte | Einstellungen | Hilfe', "offline editor tabs: "+tabs)
        await pg3.click("nav.tabs button[data-t=set]")
        await pg3.click("section[data-tab=set] .hl a"); await pg3.wait_for_timeout(300)
        n=await pg3.evaluate("document.querySelectorAll('#helpbody h2').length")
        top=await pg3.evaluate("document.getElementById('h-einstellungen-in-texts-txt').getBoundingClientRect().top")
        check(n>8 and abs(top)<5, "offline: manual is built in, settings link jumps to the table")
        check(not errs, f"no JS errors {errs}"); await b.close()
    print('all checks passed' if not fails else f'{len(fails)} checks FAILED'); sys.exit(1 if fails else 0)
asyncio.run(main())
