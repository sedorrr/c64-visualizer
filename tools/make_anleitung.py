#!/usr/bin/env python3
"""
The illustrated guide for the whole set (visualizer, audio receiver, Pi jukebox):
docs/anleitung/anleitung.html -> docs/Anleitung_C64-Oekosystem.pdf (German) and docs/anleitung/guide_en.html ->
docs/Guide_C64-Ecosystem.pdf (English), both with the same pictures (Chromium via Playwright).

    python3 tools/make_anleitung.py            only the PDFs from the HTML and the pictures in docs/anleitung/bilder/
    python3 tools/make_anleitung.py --shots    take the screenshots of the three web pages first
    python3 tools/make_anleitung.py --copy     also copy the PDFs into docs/ of the other two projects (and the
                                               receiver's two screenshots for its README into its docs/screenshots/)

The screenshots come from the real pages with made-up data (no device needed): the Pico pages are served with a
mock of their HTTP interface (like tests/web/), the Pi page by the demo server of the Pi project
(tests/web/demo_server.py, run with that project's .venv). Addresses are neutral examples (192.168.1.x).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "docs", "anleitung")
IMG = os.path.join(DOC, "bilder")
PDFS = ((os.path.join(DOC, "anleitung.html"), os.path.join(ROOT, "docs", "Anleitung_C64-Oekosystem.pdf"),
         "C64-Ökosystem – Anleitung · Seite", "von"),
        (os.path.join(DOC, "guide_en.html"), os.path.join(ROOT, "docs", "Guide_C64-Ecosystem.pdf"),
         "C64 ecosystem – guide · page", "of"))
PHONE = {"width": 420, "height": 900}


def sibling(name):
    """the other projects lie next to this one (export) or one level up (working copy)"""
    for p in (os.path.join(ROOT, "..", name), os.path.join(ROOT, "..", "..", name)):
        if os.path.isdir(p):
            return os.path.abspath(p)
    return None


def dev(**k):
    return json.dumps(k)


# ---------------------------------------------------------------- the set used in all pictures
ULT = "192.168.1.168"
CHAIN = [  # without a Pi: Ultimate -> receiver -> visualizer -> second visualizer
    dict(t="audio", n="c64audio", ip="192.168.1.79", b="Oct 26 2026 18:40:00", img="c64audio", st=1, rx=250,
         fw="c64viz.local", fwip="192.168.1.231", fp=250, src=ULT, srcl="Elite II", **{"from": ULT}),
    dict(t="viz", n="c64viz", ip="192.168.1.231", b="Oct 26 2026 18:40:00", img="c64viz_wifi_ws2812_usb-350mA", st=1,
         sm=2, rx=250, fw="c64viz2.local", fwip="192.168.1.224", fp=250, src=ULT, srcl="Elite II", **{"from": "192.168.1.79"}),
    dict(t="viz", n="c64viz2", ip="192.168.1.224", b="Oct 26 2026 18:40:00", img="c64viz_wifi_ws2812_usb-350mA", st=1,
         sm=2, rx=249, fw="", fwip="", fp=0, src=ULT, srcl="Elite II", **{"from": "192.168.1.231"})]
PI_IP = "192.168.1.78"
STAR = [  # with the Pi as distributor: Ultimate -> Pi -> every Pico
    dict(t="audio", n="c64audio", ip="192.168.1.79", b="Oct 26 2026 18:40:00", st=1, rx=251, fw="", fp=0, src=ULT,
         srcl="Elite II", **{"from": PI_IP}),
    dict(t="viz", n="c64viz", ip="192.168.1.231", b="Oct 26 2026 18:40:00", st=1, sm=2, rx=251, fw="", fp=0, src=ULT,
         srcl="Elite II", **{"from": PI_IP}),
    dict(t="viz", n="c64viz2", ip="192.168.1.224", b="Oct 26 2026 18:40:00", st=1, sm=2, rx=250, fw="", fp=0, src=ULT,
         srcl="Elite II", **{"from": PI_IP})]
U64LIST = [{"label": "Elite II", "addr": ULT, "pw": False}] + [{"label": "", "addr": "", "pw": False}] * 3


def devices(lst, me):
    return {"list": [{"self": d["n"] == me, "age": 0 if d["n"] == me else 3, "j": json.dumps(d)} for d in lst]}


def clip(pg, name, top=None, bottom=None, pad=10):
    """screenshot of the page from the top edge of one element to an edge of another: (selector, "top"|"bottom")"""
    pg.evaluate("window.scrollTo(0, 0)")

    def edge(spec):
        sel, side = spec
        bb = pg.locator(sel).first.bounding_box()
        return bb["y"] + (bb["height"] if side == "bottom" else 0)
    y0 = max(0, edge(top) - pad) if top else 0
    y1 = edge(bottom) + pad if bottom else pg.evaluate("document.documentElement.scrollHeight")
    pg.screenshot(path=os.path.join(IMG, name + ".png"), full_page=True,
                  clip={"x": 0, "y": y0, "width": pg.viewport_size["width"], "height": y1 - y0})


# ---------------------------------------------------------------- the visualizer (Pico 2 W)
VIZ_STATUS = {"wifi": "MeinWLAN", "ip": "192.168.1.231", "net": True, "pps": 250, "u64state": "Stream kommt weitergeleitet",
              "ssid": "MeinWLAN", "u64": ULT, "stream": True, "thr": 30, "pwdefault": False, "sta": True, "smode": 2,
              "group": "-", "silent": 0, "sidbusy": False, "bright": 45, "lux10": -10, "lightpct": 100, "cfgname": "",
              "name": "c64viz", "fwd": "c64viz2.local", "fwdip": "192.168.1.224", "fpps": 250, "src": 0,
              "u64list": U64LIST, "wide": "", "wok": False, "load": 18, "busymax": 2900}
VIZ_TEXTS = (b"@silence 8000\n@clock on\n"
             b"{rainbow}Willkommen beim C64 Sound-Visualizer\n"
             b"{color:cyan}{typewriter}READY.{cursor}{hold:2000}\n"
             b"{glow:2500}{color:orange}Commodore 64 forever\n")


def viz_shots(pw):
    html = open(os.path.join(ROOT, "tools", "text_editor.html"), "rb").read()
    state = {"devs": CHAIN}

    def handler(route):
        r = route.request
        u = "/" + r.url.split("://", 1)[1].split("/", 1)[1]
        if u == "/status":
            route.fulfill(body=json.dumps(VIZ_STATUS), content_type="application/json")
        elif u == "/devices":
            route.fulfill(body=json.dumps(devices(state["devs"], "c64viz")), content_type="application/json")
        elif u == "/texts" and r.method == "GET":
            route.fulfill(body=VIZ_TEXTS)
        elif u == "/jb/state":
            route.fulfill(body=json.dumps({"state": "idle", "msg": "", "pos": -1, "count": 0,
                                           "prefs": {"src": 0, "dir": "/"}, "sldb": {"count": 61234, "busy": False}}),
                          content_type="application/json")
        elif r.method == "POST":
            route.fulfill(body='{"ok":true}', content_type="application/json")
        else:
            route.fulfill(body=html, content_type="text/html; charset=utf-8")

    b = pw.chromium.launch()
    ctx = b.new_context(viewport=PHONE, device_scale_factor=2)
    ctx.route("http://c64viz.local/**", handler)
    pg = ctx.new_page()
    pg.on("dialog", lambda d: d.accept())
    pg.goto("http://c64viz.local/")
    pg.wait_for_timeout(900)
    for tab, name, top, bottom in (
            ("ctl", "viz_steuerung", None, ("section[data-tab=ctl] button:has-text('Neu suchen')", "bottom")),
            ("txt", "viz_lauftexte", None, ("button:has-text('Experten-Modus')", "bottom")),
            ("net", "viz_wlan", ("section[data-tab=net] h3:has-text('Ultimate-Ger')", "top"), ("#fwd", "bottom"))):
        pg.click("nav.tabs button[data-t=%s]" % tab)
        pg.wait_for_timeout(900)
        clip(pg, name, top, bottom)
    pg.click("nav.tabs button[data-t=ctl]")
    pg.click("section[data-tab=ctl] button:has-text('Einricht-Assistent')")
    pg.wait_for_timeout(500)
    for _ in range(2):
        pg.click("#wznext")
        pg.wait_for_timeout(200)
    pg.locator("#wiz .dlg").screenshot(path=os.path.join(IMG, "viz_assistent3.png"))
    for _ in range(2):
        pg.click("#wznext")
        pg.wait_for_timeout(250)
    pg.locator("#wiz .dlg").screenshot(path=os.path.join(IMG, "viz_assistent5.png"))
    b.close()


# ---------------------------------------------------------------- the audio receiver
AUD_STATUS = {"fw": "c64audio-fw", "build": "Oct 26 2026 18:40:00", "wifi": "MeinWLAN", "ssid": "MeinWLAN",
              "ip": "192.168.1.79", "sta": True, "name": "c64audio", "cfgname": "", "pwdefault": False, "u64": ULT,
              "src": 0, "u64state": "Stream läuft", "play": True, "state": "spielt", "rx": True, "from": ULT,
              "direct": True, "pps": 250, "vol": 80, "fill": 81.9, "avg": 82.3, "target": 80, "ppm": -17,
              "rate": 47982.8, "missing": 0, "late": 0, "dup": 0, "underruns": 0, "concealed": 0, "silent": 0,
              "skipped": 0, "restarts": 0, "prefills": 1, "qdrop": 0, "behind": 0, "rejected": 0, "bad": 0,
              "fwd_on": True, "fwd": "c64viz.local", "fwdip": "192.168.1.231", "fpps": 250, "fwderr": 0, "taken": 0,
              "contested": False, "np": "Commando – Rob Hubbard", "lat": 80, "fwdpw": False,
              "npr": "gesendet (ohne Passwort)", "nprsent": 3, "scanning": False, "u64list": U64LIST}


def audio_shots(pw, audio):
    page = open(os.path.join(audio, "tools", "web", "index.html"), "rb").read()

    class Pico(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, body, ctype):
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/":
                self.send(page, "text/html; charset=utf-8")
            elif self.path == "/status":
                self.send(json.dumps(AUD_STATUS).encode(), "application/json")
            elif self.path == "/devices":
                self.send(json.dumps(devices(CHAIN, "c64audio")).encode(), "application/json")
            elif self.path == "/fw/state":
                self.send(json.dumps({"state": "idle", "got": 0, "size": 0, "msg": "", "max": 2000000,
                                      "build": AUD_STATUS["build"]}).encode(), "application/json")
            else:
                self.send(b"", "text/plain")

        def do_POST(self):
            self.rfile.read(int(self.headers.get("Content-Length") or 0))
            self.send(b"OK", "text/plain")

    srv = ThreadingHTTPServer(("127.0.0.1", 0), Pico)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    b = pw.chromium.launch()
    pg = b.new_page(viewport=PHONE, device_scale_factor=2)
    pg.goto("http://127.0.0.1:%d/" % srv.server_address[1])
    pg.wait_for_timeout(900)
    clip(pg, "audio_wiedergabe", None, ("h3:has-text('Puffer und Takt')", "top"), pad=-6)
    pg.click("nav.tabs button[data-t=set]")
    pg.wait_for_timeout(700)
    clip(pg, "audio_weiterleitung", ("h3:has-text('Ultimate-Ger')", "top"), ("#fwdpw", "bottom"))
    b.close()
    srv.shutdown()


# ---------------------------------------------------------------- the Pi jukebox (run with the Pi project's python)
def pi_shots(pi):
    sys.path.insert(0, os.path.join(pi, "tests", "web"))
    sys.path.insert(0, os.path.join(pi, "tests"))
    sys.path.insert(0, pi)
    import c64jukebox.core
    c64jukebox.core.local_ip = lambda towards="": PI_IP           # the page shows the Pi's own address
    from demo_server import Demo
    from playwright.sync_api import sync_playwright
    demo = Demo().start()
    c = demo.jb.config.data
    c["picos"] = [{"addr": a, "np": "udp", "stream": True, "pw": ""}
                  for a in ("c64audio.local", "c64viz.local", "c64viz2.local")]
    c["stream"]["on"] = True
    c["pico_news"] = True
    fake = re.compile(r"127\.0\.0\.1:\d+")
    me = None

    def handler(route):
        r = route.request
        res = route.fetch()
        body = res.text()
        path = r.url.split("/api/", 1)[1].split("?")[0]
        if path == "state":
            s = json.loads(body)
            s["stream"].update(on=True, active=True, pps=251, rejected=0, foreign=0, msg="", **{"from": ULT})
            s["stream"]["targets"] = [{"addr": d["n"] + ".local", "ip": d["ip"], "pps": d["rx"], "dropped": 0}
                                      for d in STAR]
            s["pico_news"] = {"n": 6}
            s["pwdefault"] = False
            body = json.dumps(s)
        elif path == "devices":
            pi_dev = dict(t="pi", n="c64pi", ip=PI_IP, b="1.0", st=1, sm=0, rx=251, fw="c64audio,c64viz,c64viz2",
                          fwip="192.168.1.79", fp=251, src=ULT, srcl="Elite II", title="Commando", **{"from": ULT})
            body = json.dumps({"list": [{"self": True, "age": 0, "j": json.dumps(pi_dev)}] +
                                       [{"age": 2, "j": json.dumps(d)} for d in STAR]})
        body = fake.sub(ULT, body)
        for t in {demo.tmp, os.path.realpath(demo.tmp)}:          # the demo's temporary folder -> a Pi-like path
            body = body.replace(t, "/media/usb")
        route.fulfill(status=res.status, headers=dict(res.headers, **{"content-length": str(len(body.encode()))}),
                      body=body)

    with sync_playwright() as p:
        b = p.chromium.launch()
        for size, suffix in ((PHONE, ""),):
            ctx = b.new_context(viewport=size, device_scale_factor=2)
            ctx.route(re.compile(r".*/api/.*"), handler)
            pg = ctx.new_page()
            pg.goto(demo.url)
            pg.fill("#lpw", "commodore64")
            pg.press("#lpw", "Enter")
            pg.wait_for_selector("#app:not([hidden])")
            pg.add_style_tag(content="#toast{display:none!important}")
            pg.click("#tabs button[data-t=files]")
            pg.wait_for_selector(".src.on")
            for n in ("USB2", "SID-Tunes", "MUSICIANS", "H", "Hubbard_Rob"):
                pg.click("#flist li.dir:has-text('%s')" % n)
                pg.wait_for_function("document.getElementById('fstatus').textContent.indexOf('Lese') < 0")
            pg.wait_for_selector("#flist li.sid:has-text('Rob Hubbard')")
            clip(pg, "pi_dateien", None, ("#flist", "bottom"), pad=24)
            pg.click("#flist li.sid:has-text('Commando')")
            pg.wait_for_function("document.getElementById('toast').textContent.includes('Commando')")
            pg.click("#tabs button[data-t=play]")
            pg.wait_for_selector("#nt:has-text('Commando')")
            pg.wait_for_timeout(1500)
            clip(pg, "pi_wiedergabe", None, ("#lrows .lrow[data-i='1']", "bottom"), pad=24)
            pg.click("#tabs button[data-t=dev]")
            pg.wait_for_timeout(1800)
            clip(pg, "pi_geraete", ("h3:has-text('Picos und Stream-Verteiler')", "top"), ("#devlist", "bottom"), pad=24)
        b.close()
    demo.stop()
    shots = os.path.join(pi, "tests", "web")                 # pictures of the TV output from the Pi's own tests
    for n in ("hdmi_titlecard.png", "hdmi_bars.png", "hdmi_news.png", "tft_play.png"):
        if os.path.exists(os.path.join(shots, n)):
            shutil.copy(os.path.join(shots, n), os.path.join(IMG, "pi_" + n))


# ---------------------------------------------------------------- PDF
def make_pdf(pw):
    b = pw.chromium.launch()
    pg = b.new_page()
    for html, pdf, page, of in PDFS:
        pg.goto("file://" + html)
        pg.wait_for_timeout(500)
        pg.pdf(path=pdf, format="A4", print_background=True, prefer_css_page_size=True,
               display_header_footer=True, header_template="<span></span>",
               footer_template='<div style="font:8px Helvetica,Arial,sans-serif;width:100%%;text-align:center;color:#888">'
                               '%s <span class="pageNumber"></span> %s <span class="totalPages"></span></div>' % (page, of),
               margin={"top": "14mm", "bottom": "16mm", "left": "14mm", "right": "14mm"})
        print("PDF:", pdf, "(%d KB)" % (os.path.getsize(pdf) // 1024))
    b.close()


def main():
    args = sys.argv[1:]
    if args[:1] == ["--pi-shots"]:                       # the child process with the Pi's python
        pi_shots(args[1])
        return
    from playwright.sync_api import sync_playwright
    os.makedirs(IMG, exist_ok=True)
    if "--shots" in args:
        audio, pi = sibling("Pico2_c64_audio"), sibling("Pi3_c64_jukebox")
        with sync_playwright() as pw:
            viz_shots(pw)
            if audio:
                audio_shots(pw, audio)
        if pi:
            py = os.path.join(pi, ".venv", "bin", "python")
            subprocess.run([py if os.path.exists(py) else sys.executable, os.path.abspath(__file__), "--pi-shots", pi],
                           check=True)
        for src, dst in ((os.path.join(ROOT, "docs", "modes_preview.png"), "viz_modi.png"),
                         (os.path.join(ROOT, "case", "preview_parts.png"), "viz_gehaeuse.png"),
                         (audio and os.path.join(audio, "case", "preview_receiver.png"), "audio_gehaeuse.png"),
                         (pi and os.path.join(pi, "case", "preview_front.png"), "pi_gehaeuse.png")):
            if src and os.path.exists(src):
                shutil.copy(src, os.path.join(IMG, dst))
        print("screenshots in", IMG)
    with sync_playwright() as pw:
        make_pdf(pw)
    if "--copy" in args:
        for name in ("Pico2_c64_audio", "Pi3_c64_jukebox"):
            p = sibling(name)
            for _, pdf, _, _ in PDFS if p else ():
                shutil.copy(pdf, os.path.join(p, "docs", os.path.basename(pdf)))
                print("copied to", os.path.join(p, "docs", os.path.basename(pdf)))
            if p and name == "Pico2_c64_audio":                     # its README shows two of the pictures
                os.makedirs(os.path.join(p, "docs", "screenshots"), exist_ok=True)
                for n in ("audio_wiedergabe.png", "audio_weiterleitung.png"):
                    shutil.copy(os.path.join(IMG, n), os.path.join(p, "docs", "screenshots", n))


if __name__ == "__main__":
    main()
