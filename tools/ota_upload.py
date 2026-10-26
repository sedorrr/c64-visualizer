#!/usr/bin/env python3
"""
Firmware update of a Pico 2 W over WLAN, like the button on its web page ("Firmware (.uf2) wählen").
    python3 tools/ota_upload.py <host> <firmware.uf2> [password]
Settings, texts and song lengths stay. Needs: pip install requests
"""
import struct
import sys
import time

import requests
from requests.auth import HTTPDigestAuth

BASE, FAMILY, CHUNK = 0x10000000, 0xE48BFF59, 32768


def uf2_image(data):
    """.uf2 -> flat image of the RP2350 (ARM secure) blocks, gaps filled with 0xFF (same as the web page)."""
    parts, end = [], 0
    for o in range(0, len(data) - 511, 512):
        m0, m1, flags, addr, n = struct.unpack_from("<5I", data, o)
        if m0 != 0x0A324655 or m1 != 0x9E5D5157 or struct.unpack_from("<I", data, o + 508)[0] != 0x0AB16F30:
            raise ValueError("keine UF2-Datei")
        if not flags & 0x2000 or struct.unpack_from("<I", data, o + 28)[0] != FAMILY:
            continue                                   # other families, e.g. the RP2350 errata block
        if addr < BASE or n > 476:
            raise ValueError("unerwartete Adresse in der UF2-Datei")
        parts.append((addr - BASE, data[o + 32:o + 32 + n]))
        end = max(end, addr - BASE + n)
    if not parts:
        raise ValueError("keine Firmware für den RP2350 in der Datei")
    img = bytearray(b"\xff" * end)
    for a, d in parts:
        img[a:a + len(d)] = d
    return bytes(img)


def fnv_hex(b):
    h = 0x811C9DC5
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


class UploadError(Exception):
    pass


def upload(host, path, password=None, marker=b"c64viz_wifi", say=print):
    """Upload and apply; returns the build stamp running afterwards. marker: text the image must contain
    (a visualizer must not get the receiver's firmware and the other way round). Raises UploadError."""
    def session():
        s = requests.Session()
        if password:
            s.auth = HTTPDigestAuth("ota", password)
        return s
    s = session()
    url = f"http://{host}"
    img = uf2_image(open(path, "rb").read())
    if marker not in img:
        raise UploadError("falsche Firmware für dieses Gerät (" + marker.decode() + " fehlt in der Datei)")
    state = lambda: s.get(url + "/fw/state", timeout=5).json()

    def wait(cond, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                st = state()
                if cond(st):
                    return st
            except (requests.RequestException, ValueError):
                pass
            time.sleep(0.3)
        return None

    r = s.get(url + "/fw/state", timeout=5)
    if r.status_code == 401:
        raise UploadError("Passwort falsch")
    st0 = r.json()
    if len(img) > st0.get("max", 1 << 30):
        raise UploadError(f"zu groß ({len(img) // 1024} KB)")
    say(f"vorher: {st0.get('build')}, Bild {len(img) // 1024} KB")
    if not s.post(url + f"/fw/begin?size={len(img)}", timeout=10).ok:
        raise UploadError("Pico ist beschäftigt - gleich nochmal versuchen")
    t0 = time.time()
    for o in range(0, len(img), CHUNK):
        for _ in range(30):
            try:
                if s.post(url + f"/fw/chunk?off={o}", data=img[o:o + CHUNK], timeout=10,
                          headers={"Content-Type": "application/octet-stream"}).ok:
                    break
            except requests.RequestException:
                pass
            time.sleep(0.3)
        else:
            raise UploadError("Übertragung abgebrochen")
        if not wait(lambda st: st.get("got", 0) >= min(o + CHUNK, len(img)) or st.get("state") == "error", 20):
            raise UploadError("Pico antwortet nicht")
    say(f"übertragen in {time.time() - t0:.1f} s")
    s.post(url + f"/fw/end?hash={fnv_hex(img)}", timeout=10)
    st = wait(lambda st: st.get("state") in ("ready", "error"), 20)
    if not st or st.get("state") != "ready":
        raise UploadError("abgelehnt: " + (st.get("msg", "") if st else "keine Antwort"))
    s.post(url + "/fw/apply", timeout=10)
    say("Firmware wird aufgespielt - der Pico startet neu ...")
    time.sleep(6)
    s = session()                                      # new nonce after the restart
    for _ in range(40):
        try:
            n = s.get(url + "/fw/state", timeout=5).json()
            if n.get("state") == "idle":
                same = " (gleicher Stand wie vorher)" if n.get("build") == st0.get("build") else ""
                say(f"fertig - läuft jetzt mit der Firmware vom {n.get('build')}{same}")
                return n.get("build")
        except (requests.RequestException, ValueError):
            pass
        time.sleep(1.5)
    raise UploadError("der Pico meldet sich nicht zurück - dann die .uf2 per USB aufspielen")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    try:
        upload(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    except (UploadError, ValueError) as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
