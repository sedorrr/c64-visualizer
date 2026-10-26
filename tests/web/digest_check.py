import asyncio, hashlib, json, re, secrets, threading, http.server
PW="Geheim64"; REALM="C64 Visualizer"; NONCES={}; LOG=[]
md5=lambda s: hashlib.md5(s.encode()).hexdigest()
HTML=open('../../tools/text_editor.html','rb').read()
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self,*a): pass
    def deny(self, stale=False):
        n=secrets.token_hex(16); NONCES[n]=0
        self.send_response(401); self.send_header('WWW-Authenticate',f'Digest realm="{REALM}", qop="auth", algorithm=MD5, nonce="{n}"'+(', stale=true' if stale else ''))
        self.send_header('Content-Length','0'); self.end_headers()
    def ok(self):
        a=self.headers.get('Authorization','')
        if not a.startswith('Digest '): return self.deny()
        p=dict(re.findall(r'(\w+)=(?:"([^"]*)"|([^,\s]*))', a[7:]) and [(k, v1 or v2) for k,v1,v2 in re.findall(r'(\w+)=(?:"([^"]*)"|([^,\s]*))', a[7:])])
        ha1=md5(f"{p['username']}:{REALM}:{PW}"); ha2=md5(f"{self.command}:{p['uri']}")
        good = p.get('qop')=='auth' and p['uri']==self.path and md5(f"{ha1}:{p['nonce']}:{p['nc']}:{p['cnonce']}:auth:{ha2}")==p['response']
        LOG.append((self.command,self.path,'ok' if good else 'denied', p.get('nc')))
        if not good: self.deny(); return False
        if p['nonce'] not in NONCES: self.deny(True); return False
        return True
    def do_GET(self):
        if not self.ok(): return
        if self.path=='/': b,t=HTML,'text/html; charset=utf-8'
        elif self.path=='/texts': b,t=b'@silence 8000\nHello\n','text/plain'
        else: b,t=json.dumps({"wifi":"x","ip":"1","net":False,"pps":0,"u64state":"-","ssid":"x","u64":"","stream":True,"thr":30,"pwdefault":False,"sta":True}).encode(),'application/json'
        self.send_response(200); self.send_header('Content-Type',t); self.send_header('Content-Length',str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_POST(self):
        if not self.ok(): return
        self.send_response(200); self.send_header('Content-Length','2'); self.end_headers(); self.wfile.write(b'OK')
srv=http.server.ThreadingHTTPServer(('127.0.0.1',8777),H); threading.Thread(target=srv.serve_forever,daemon=True).start()
from playwright.async_api import async_playwright
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); ctx=await b.new_context(http_credentials={"username":"admin","password":PW})
        pg=await ctx.new_page(); pg.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg.goto("http://127.0.0.1:8777/"); await pg.wait_for_timeout(1500)
        await pg.click("text=Nächster Modus"); await pg.wait_for_timeout(500)
        NONCES.clear()     # simulate a Pico restart: all nonces forgotten
        await pg.click("text=Helligkeit"); await pg.wait_for_timeout(800)
        print("page text shows status:", "Rauschschwelle" in await pg.inner_text("#st"))
        await b.close()
asyncio.run(main()); srv.shutdown()
for e in LOG: print(e)
