import asyncio, json, sys
from urllib.parse import urlparse, parse_qs
from playwright.async_api import async_playwright
# firmware update over the web page: .uf2 -> flat image in the browser, chunks to the (mocked) Pico, check, apply
HTML=open('../../tools/text_editor.html','rb').read(); fails=[]; reqs=[]
WIFI_UF2='../../firmware/c64viz_wifi_ws2812_usb-350mA.uf2'
PICO2_UF2='../../firmware/c64viz_ws2812_usb-350mA.uf2'
WIFI_BIN='../../build/wifi_usb-350mA-pico2_w/c64viz_wifi.bin'
fw={"state":"idle","got":0,"size":0,"msg":"","max":974848,"build":"Oct 11 2026 10:00:00","data":bytearray(),"hash":None,"rebooted":False}
def check(ok, what):
    print(("ok   " if ok else "FAIL ")+what)
    if not ok: fails.append(what)
def fnv(b):
    h=0x811c9dc5
    for x in b: h=((h^x)*0x01000193)&0xffffffff
    return h
def st(): return json.dumps({k:fw[k] for k in ("state","got","size","msg","max","build")})
async def handler(route):
    r=route.request; u=r.url.split('c64viz.local')[1]; p=urlparse(u); q={k:v[0] for k,v in parse_qs(p.query).items()}
    if p.path=='/status': await route.fulfill(body=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"1","stream":False,"thr":30,"pwdefault":False,"sta":True,"smode":0,"group":"-","silent":0,"sidbusy":False,"mcwarn":False,"build":fw["build"]}),content_type='application/json')
    elif p.path=='/texts': await route.fulfill(body='Hello\n')
    elif p.path=='/fw/state':
        if fw["state"]=="apply": fw.update(state="idle",build="Oct 12 2026 12:34:56",rebooted=True)   # the "reboot"
        await route.fulfill(body=st(),content_type='application/json')
    elif p.path=='/fw/begin': reqs.append(u); fw.update(state="recv",size=int(q["size"]),got=0,data=bytearray()); await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif p.path=='/fw/chunk':
        off=int(q["off"]); b=r.post_data_buffer; reqs.append('chunk %d %d'%(off,len(b)))
        ok=off==fw["got"] and len(b)<=32768 and off%4096==0
        if ok: fw["data"]+=b; fw["got"]=off+len(b)
        await route.fulfill(status=200 if ok else 400,body='{"ok":%s}'%('true' if ok else 'false'),content_type='application/json')
    elif p.path=='/fw/end':
        reqs.append(u); fw["hash"]=int(q["hash"],16)
        fw.update(state="ready" if fw["hash"]==fnv(fw["data"]) else "error",msg="geprüft - bereit")
        await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif p.path=='/fw/apply': reqs.append(u); fw["state"]="apply"; await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif r.method=='POST': await route.fulfill(body='{"ok":true}',content_type='application/json')
    elif p.path.startswith('/jb/') or p.path.startswith('/diag'): await route.fulfill(body='{}',content_type='application/json')
    else: await route.fulfill(body=HTML, content_type='text/html; charset=utf-8')
async def main():
    async with async_playwright() as pw:
        b=await pw.chromium.launch(); errs=[]
        pg=await b.new_page(viewport={"width":420,"height":1600}); pg.on("pageerror",lambda e: errs.append(str(e)))
        await pg.route("http://c64viz.local/**", handler); await pg.goto("http://c64viz.local/"); await pg.wait_for_timeout(900)
        await pg.click("nav.tabs button[data-t=net]"); await pg.wait_for_timeout(300)
        cur=await pg.inner_text("#fwcur"); print("shown:", cur)
        check("Oct 11 2026 10:00:00" in cur, "running firmware build shown")
        # wrong firmware (Pico 2 without WLAN): refused in the browser, nothing sent
        await pg.set_input_files("#fwfile", PICO2_UF2); await pg.wait_for_timeout(800)
        m=await pg.inner_text("#fwmsg"); print("Pico 2 file:", m)
        check("keine WLAN-Firmware" in m and not reqs, "Pico 2 firmware refused before upload")
        # cancel in the confirmation: nothing sent
        pg.once("dialog", lambda d: asyncio.ensure_future(d.dismiss()))
        await pg.set_input_files("#fwfile", WIFI_UF2); await pg.wait_for_timeout(800)
        check(not reqs, "cancel sends nothing")
        # the real update
        pg.once("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg.set_input_files("#fwfile", WIFI_UF2)
        for _ in range(60):
            await pg.wait_for_timeout(500)
            if fw["rebooted"] and "Fertig" in await pg.inner_text("#fwmsg"): break
        m=await pg.inner_text("#fwmsg"); print("end:", m)
        binimg=open(WIFI_BIN,'rb').read()
        data=bytes(fw["data"])
        print("received %d bytes in %d chunks, build .bin %d bytes" % (len(data), sum(1 for x in reqs if x.startswith('chunk')), len(binimg)))
        check(data[:len(binimg)]==binimg and set(data[len(binimg):])<={0x00,0xFF}, "received image equals the built .bin (UF2 pads the last block)")
        check(fw["hash"]==fnv(data), "hash sent by the page matches the data")
        check('/fw/apply' in reqs, "apply sent after a good check")
        check("Fertig" in m and "Oct 12 2026 12:34:56" in m, "new build reported after the restart")
        print("JS errors:", errs); check(not errs, "no JS errors")
        await b.close()
asyncio.run(main())
print("FAILED: %d" % len(fails) if fails else "all checks passed"); sys.exit(1 if fails else 0)
