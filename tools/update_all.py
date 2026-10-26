#!/usr/bin/env python3
"""
Brings every C64 device in the home network up to date: visualizers (Pico 2 W) and audio receivers.
    python3 tools/update_all.py              list the devices, then ask
    python3 tools/update_all.py --yes        update without asking
    python3 tools/update_all.py --list       only show what would be done
    python3 tools/update_all.py --only c64viz2,c64audio
    python3 tools/update_all.py --force      also devices that already run the same build

The devices answer a query on UDP port 11064 (their beacons, as for the device list on the web page). Every beacon
names the firmware file the device needs ("img", e.g. c64viz_wifi_ws2812_usb-350mA_debug); it is taken from
firmware/ of this project or of the audio receiver project (--audio-dir). A device is up to date when its build stamp
is inside that file. Passwords: asked once per device; on macOS they can be kept in the keychain (service
"c64-pico", account = device name) - answer "j" when asked, or --no-keychain to never touch it.
Needs: pip install requests (like tools/ota_upload.py, which does the upload).
"""
import argparse
import getpass
import json
import os
import re
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ota_upload import UploadError, uf2_image, upload   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 11064
KEYCHAIN = "c64-pico"
MARKER = {"viz": b"c64viz_wifi", "audio": b"c64audio-fw"}
KIND = {"viz": "Visualizer", "audio": "Audio-Empfänger"}


def discover(secs):
    """Ask all devices (broadcast) and collect their beacons for secs seconds: {ip: beacon}."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("", PORT))
    s.settimeout(0.2)
    found, end, asked = {}, time.time() + secs, 0
    while time.time() < end:
        if time.time() - asked > 1.0:                  # ask a few times: one broadcast can get lost in the WLAN
            s.sendto(b"C64DEV1?", ("255.255.255.255", PORT))
            asked = time.time()
        try:
            data, (ip, _) = s.recvfrom(1024)
        except socket.timeout:
            continue
        if data.startswith(b"C64DEV1 {"):
            try:
                b = json.loads(data[8:].decode("utf-8", "replace"))
            except ValueError:
                continue
            if b.get("img"):
                found[ip] = b
    s.close()
    return found


def firmware_file(b, audio_dir):
    base = ROOT if b.get("t") == "viz" else audio_dir
    return os.path.join(base, "firmware", b["img"] + ".uf2")


def build_stamps(path):
    """Build stamps ("Oct 18 2026 09:22:30") inside a .uf2 - the running firmware reports one of them."""
    img = uf2_image(open(path, "rb").read())
    return set(m.decode() for m in re.findall(rb"[A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d", img))


def keychain_get(name):
    if sys.platform != "darwin":
        return None
    r = subprocess.run(["security", "find-generic-password", "-s", KEYCHAIN, "-a", name, "-w"],
                       capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def keychain_put(name, pw):
    subprocess.run(["security", "add-generic-password", "-U", "-s", KEYCHAIN, "-a", name, "-w", pw],
                   capture_output=True)


def main():
    ap = argparse.ArgumentParser(description="Alle C64-Picos im Netz aktualisieren")
    ap.add_argument("--yes", action="store_true", help="ohne Rückfrage aktualisieren")
    ap.add_argument("--list", action="store_true", help="nur anzeigen")
    ap.add_argument("--force", action="store_true", help="auch Geräte mit gleichem Stand")
    ap.add_argument("--only", default="", help="nur diese Geräte (Namen, mit Komma getrennt)")
    ap.add_argument("--audio-dir", default=os.path.join(ROOT, "..", "..", "Pico2_c64_audio"),
                    help="Projekt des Audio-Empfängers (für c64audio*.uf2)")
    ap.add_argument("--wait", type=float, default=4.0, help="Sekunden warten auf Antworten")
    ap.add_argument("--no-keychain", action="store_true", help="Passwörter nie im Schlüsselbund ablegen/lesen")
    a = ap.parse_args()

    print(f"Suche Geräte im Netz ({a.wait:.0f} s) ...")
    found = discover(a.wait)
    if not found:
        sys.exit("Keine Geräte gefunden. Sind sie im selben WLAN und läuft auf ihnen eine Firmware mit Geräteliste?")
    only = {x.strip() for x in a.only.split(",") if x.strip()}
    todo = []
    for ip, b in sorted(found.items(), key=lambda kv: kv[1].get("n", "")):
        name, kind = b.get("n", ip), KIND.get(b.get("t"), b.get("t", "?"))
        path = firmware_file(b, os.path.abspath(a.audio_dir))
        line = f"  {name:<14} {kind:<16} {ip:<15} läuft: {b.get('b', '?'):<20}"
        if only and name not in only:
            print(line + " (nicht ausgewählt)")
            continue
        if not os.path.exists(path):
            print(line + f" ! {os.path.relpath(path)} fehlt")
            continue
        current = b.get("b", "") in build_stamps(path)
        new = time.strftime("%d.%m. %H:%M", time.localtime(os.path.getmtime(path)))
        print(line + (" aktuell" if current else f" -> {os.path.basename(path)} ({new})"))
        if not current or a.force:
            todo.append((ip, b, path))
    if not todo:
        print("Alle Geräte sind aktuell.")
        return
    if a.list:
        return
    if not a.yes and input(f"{len(todo)} Gerät(e) aktualisieren? [j/N] ").strip().lower() not in ("j", "ja", "y", "yes"):
        return
    failed = 0
    for ip, b, path in todo:
        name = b.get("n", ip)
        print(f"\n== {name} ({ip})")
        pw = None if a.no_keychain else keychain_get(name)
        asked = False
        for attempt in range(3):
            if pw is None:
                pw = getpass.getpass(f"Web-Passwort von {name} (leer = ohne): ") or None
                asked = True
            try:
                upload(ip, path, pw, MARKER.get(b.get("t"), b"c64viz_wifi"), say=lambda t: print("   " + t))
                if asked and pw and not a.no_keychain and sys.platform == "darwin" and \
                        input("   Passwort im Schlüsselbund merken? [j/N] ").strip().lower() in ("j", "ja"):
                    keychain_put(name, pw)
                break
            except UploadError as e:
                print(f"   Fehler: {e}")
                if "Passwort" in str(e) and attempt < 2:
                    pw = None
                    continue
                failed += 1
                break
            except (OSError, ValueError) as e:
                print(f"   Fehler: {e}")
                failed += 1
                break
    print("\nfertig" + (f" - {failed} Gerät(e) nicht aktualisiert" if failed else " - alle Geräte aktualisiert"))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
