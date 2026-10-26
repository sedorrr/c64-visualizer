#!/usr/bin/env python3
"""
Draws the minimal wiring (docs/schematic_minimal.svg = English, docs/schematic_minimal_de.svg = German) and,
if Google Chrome is installed, the PNG versions (headless screenshot).
    python3 docs/make_schematic_minimal.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1180, 1080
FONT = "DejaVu Sans, Arial, Helvetica, sans-serif"
BLUE, RED, BLACK, GREEN, GREY, VIOLET = "#1f5fbf", "#c0392b", "#1a1a1a", "#1e8449", "#7f8c8d", "#7d3c98"

T = {
    "en": {
        "title": "Minimal wiring  /  Pico 2 or Pico 2 W",
        "sub": "USB power, no level shifter. Signal = blue, +5 V = red, 3.3 V = violet, GND = black. Dashed = optional.",
        "pico_sub": "USB-powered (5 V / 2 A charger)",
        "audio_l": "C64 AUDIO (left)", "audio_l2": "A/V port pin 3 or Line-Out L",
        "audio_r": "AUDIO right (optional, stereo)", "audio_r2": "Line-Out R  ·  setting @stereo on",
        "gnd_src": "C64 GND (A/V pin 2 / cable shield)",
        "button": "Button (optional)", "button2": "or a jumper wire to GND",
        "drive": "Ultimate LED header (optional)", "drive2": "drive activity pin", "drive3": "header GND",
        "panel": "WS2812B 8x32 panel", "panel2": "(power at the DIN end)", "data": "data (short wire)",
        "sensor": "VEML7700 light sensor (optional)", "sensor2": "I2C address 0x10", "nc": "not connected",
        "notes": [
            "1) Power only via the Pico's USB: use a 2 A phone charger, not a PC port. Firmware *_usb-350mA.uf2.",
            "2) No level shifter: 3.3 V data usually works on a short wire. If the panel flickers, add the 74AHCT125 from the full schematic.",
            "3) Bias resistors to ADC_VREF (35) and AGND (33), C64 GND to AGND: supply noise cancels out. Panel GND to another GND pin.",
            "4) Pico 2 W with the Ultimate's audio stream: both audio inputs may stay unconnected (the stream carries left and right).",
            "5) Drive LED: measure first - up to 3.6 V: 1 kΩ as shown; about 5 V: 10 kΩ in series + 20 kΩ from GP14 to GND.",
            "6) Light sensor: VIN to 3V3 OUT (36) or to GP3 (pin 5, firmware drives it to 3.3 V), never to 5 V; leave 3Vo open. Settings @light, @lightmin.",
            "7) Right channel without a second wire on 35/33 (plug-in only): upper 100 kΩ to GP28 (34), lower to a free GND pin (e.g. 28, not 38 = sensor); @stereo on drives GP28 to 3.3 V.",
        ],
    },
    "de": {
        "title": "Minimalaufbau  /  Pico 2 oder Pico 2 W",
        "sub": "USB-Versorgung, kein Pegelwandler. Signal = blau, +5 V = rot, 3,3 V = violett, GND = schwarz. Gestrichelt = optional.",
        "pico_sub": "Versorgung über USB (Ladegerät 5 V / 2 A)",
        "audio_l": "C64 AUDIO (links)", "audio_l2": "A/V-Buchse Pin 3 oder Line-Out L",
        "audio_r": "AUDIO rechts (optional, Stereo)", "audio_r2": "Line-Out R  ·  Einstellung @stereo on",
        "gnd_src": "C64-Masse (A/V Pin 2 / Kabelschirm)",
        "button": "Taster (optional)", "button2": "oder ein Kabel nach GND",
        "drive": "LED-Header des Ultimate (optional)", "drive2": "Pin Laufwerksaktivität", "drive3": "GND des Headers",
        "panel": "WS2812B-Matrix 8x32", "panel2": "(Strom am DIN-Ende)", "data": "Daten (kurzes Kabel)",
        "sensor": "Lichtsensor VEML7700 (optional)", "sensor2": "I2C-Adresse 0x10", "nc": "frei lassen",
        "notes": [
            "1) Strom nur über den USB des Pico: ein 2-A-Handyladegerät, kein PC-Anschluss. Firmware *_usb-350mA.uf2.",
            "2) Kein Pegelwandler: 3,3-V-Daten gehen über ein kurzes Kabel meist. Flackert das Panel, 74AHCT125 aus dem vollen Schaltplan nachrüsten.",
            "3) Bias-Widerstände an ADC_VREF (35) und AGND (33), C64-Masse an AGND: Netzteil-Störungen heben sich auf. Panel-GND an einen anderen GND-Pin.",
            "4) Pico 2 W mit Audio-Stream des Ultimate: beide Audioeingänge dürfen unbeschaltet bleiben (der Stream bringt links und rechts mit).",
            "5) Laufwerks-LED: erst messen – bis 3,6 V: 1 kΩ wie gezeichnet; etwa 5 V: 10 kΩ in Reihe + 20 kΩ von GP14 nach GND.",
            "6) Lichtsensor: VIN an 3V3 OUT (36) oder an GP3 (Pin 5, die Firmware schaltet ihn auf 3,3 V), nie an 5 V; 3Vo frei lassen. @light, @lightmin.",
            "7) Rechter Kanal ohne zweiten Draht an 35/33 (nur stecken): oberer 100 kΩ an GP28 (34), unterer an freien GND-Pin (z. B. 28, nicht 38 = Sensor); @stereo on schaltet GP28 auf 3,3 V.",
        ],
    },
}


class Svg:
    def __init__(self):
        self.o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
                  f'<rect width="{W}" height="{H}" fill="#ffffff"/>']

    def text(self, x, y, s, size=12, anchor="start", bold=False, color=BLACK):
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.o.append(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" text-anchor="{anchor}" '
                      f'font-weight="{"bold" if bold else "normal"}" fill="{color}">{s}</text>')

    def line(self, pts, color=BLACK, w=2.2, dash=False):
        p = " ".join(f"{x},{y}" for x, y in pts)
        d = ' stroke-dasharray="7,5"' if dash else ""
        self.o.append(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{w}"{d} '
                      f'stroke-linejoin="round" stroke-linecap="round"/>')

    def box(self, x, y, w, h, fill, dash=False):
        d = ' stroke-dasharray="7,5"' if dash else ""
        self.o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill}" stroke="{BLACK}" stroke-width="2"{d}/>')

    def pin(self, x, y):
        self.o.append(f'<circle cx="{x}" cy="{y}" r="5" fill="#fff" stroke="{BLACK}" stroke-width="2"/>')

    def dot(self, x, y, color=BLACK):
        self.o.append(f'<circle cx="{x}" cy="{y}" r="4" fill="{color}"/>')

    def ground(self, x, y):
        self.line([(x, y), (x, y + 8)])
        for i, half in enumerate((14, 9, 4)):
            self.line([(x - half, y + 8 + 6 * i), (x + half, y + 8 + 6 * i)], w=2.5)

    def resistor_v(self, x, y0, y1, name, value, dash=False, label_left=True):   # vertical, body 44 px
        m = (y0 + y1) / 2
        self.line([(x, y0), (x, m - 22)], dash=dash)
        self.line([(x, m + 22), (x, y1)], dash=dash)
        d = ' stroke-dasharray="5,4"' if dash else ""
        self.o.append(f'<rect x="{x - 9}" y="{m - 22}" width="18" height="44" fill="#fff" stroke="{BLACK}" stroke-width="2"{d}/>')
        xa, an = (x - 15, "end") if label_left else (x + 15, "start")
        self.text(xa, m - 3, name, 13, an, True)
        self.text(xa, m + 14, value, 12, an)

    def resistor_h(self, x0, x1, y, name, value, color=BLUE, dash=False):
        m = (x0 + x1) / 2
        self.line([(x0, y), (m - 22, y)], color, dash=dash)
        self.line([(m + 22, y), (x1, y)], color, dash=dash)
        d = ' stroke-dasharray="5,4"' if dash else ""
        self.o.append(f'<rect x="{m - 22}" y="{y - 9}" width="44" height="18" fill="#fff" stroke="{BLACK}" stroke-width="2"{d}/>')
        self.text(m, y - 16, name, 13, "middle", True)
        self.text(m, y + 28, value, 12, "middle")

    def cap_h(self, x, y, name, value, dash=False):           # plates at x-5 / x+5, labels below
        self.line([(x - 5, y - 14), (x - 5, y + 14)], w=3)
        self.line([(x + 5, y - 14), (x + 5, y + 14)], w=3)
        self.text(x, y + 32, name, 13, "middle", True)
        self.text(x, y + 48, value, 12, "middle")

    def flag(self, x, y, label, color, right=True):           # net label (same name = connected)
        w = 18 + 7.6 * len(label)
        x0 = x if right else x - w
        tip = [(x0, y), (x0 + 8, y - 10), (x0 + w, y - 10), (x0 + w, y + 10), (x0 + 8, y + 10), (x0, y)] if right else \
              [(x0 + w, y), (x0 + w - 8, y - 10), (x0, y - 10), (x0, y + 10), (x0 + w - 8, y + 10), (x0 + w, y)]
        p = " ".join(f"{a},{b}" for a, b in tip)
        self.o.append(f'<polygon points="{p}" fill="#fff" stroke="{color}" stroke-width="1.8"/>')
        self.text(x0 + (w + 8) / 2 if right else x0 + (w - 8) / 2, y + 4, label, 11, "middle", True, color)

    def save(self, path):
        self.o.append("</svg>")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(self.o) + "\n")


def draw(lang):
    t = T[lang]
    s = Svg()
    s.text(W / 2, 32, t["title"], 21, "middle", True)
    s.text(W / 2, 54, t["sub"], 12, "middle")

    # ---- Pico ----
    PX0, PX1, PY0, PY1 = 470, 740, 90, 870
    s.box(PX0, PY0, PX1 - PX0, PY1 - PY0, "#eef4ff")
    s.text((PX0 + PX1) / 2, PY0 + 28, "Raspberry Pi Pico 2 / 2 W", 16, "middle", True)
    s.text((PX0 + PX1) / 2, PY0 + 46, t["pico_sub"], 11, "middle")
    left = {"vref": (180, "ADC_VREF  (pin 35)"), "gp26": (310, "GP26 · ADC0  (pin 31)"), "gp27": (510, "GP27 · ADC1  (pin 32)"),
            "agnd": (630, "AGND  (pin 33)"), "gp15": (720, "GP15  (pin 20)"), "gp14": (800, "GP14  (pin 19)")}
    right = {"gp16": (180, "GP16  (pin 21)"), "vbus": (260, "VBUS +5 V  (pin 40)"), "gnd1": (300, "GND  (pin 23)"),
             "3v3": (520, "3V3 OUT  (pin 36)"), "gnd2": (580, "GND  (pin 38)"), "gp5": (650, "GP5 · I2C0 SCL  (pin 7)"),
             "gp4": (710, "GP4 · I2C0 SDA  (pin 6)")}
    for _k, (y, lab) in left.items():
        s.pin(PX0, y); s.text(PX0 + 12, y + 4, lab)
    for _k, (y, lab) in right.items():
        s.pin(PX1, y); s.text(PX1 - 12, y + 4, lab, anchor="end")
    yl = {k: v[0] for k, v in left.items()}
    yr = {k: v[0] for k, v in right.items()}

    # Pico-side net flags for the bias references
    s.line([(PX0 - 5, yl["vref"]), (PX0 - 40, yl["vref"])], RED)
    s.flag(PX0 - 40, yl["vref"], "ADC_VREF", RED, right=False)
    s.line([(PX0 - 5, yl["agnd"]), (PX0 - 40, yl["agnd"])])
    s.flag(PX0 - 40, yl["agnd"], "AGND", BLACK, right=False)

    # ---- audio channels (left solid, right dashed) ----
    def channel(y, name_c, rt, rb, title, title2, gp, dash):
        s.text(30, y - 40, title, 13, "start", True)
        s.text(30, y - 22, title2, 11)
        s.pin(40, y)
        s.line([(45, y), (165, y)], BLUE, dash=dash)
        s.cap_h(170, y, name_c, "100 nF – 1 µF", dash)
        s.line([(175, y), (300, y)], BLUE, dash=dash)
        s.dot(300, y, BLUE)
        s.line([(300, y), (PX0 - 5, y)], BLUE, dash=dash)
        s.resistor_v(300, y - 80, y, rt, "100 kΩ", dash)
        s.line([(300, y - 80), (330, y - 80)], RED, dash=dash)
        s.flag(330, y - 80, "ADC_VREF", RED)
        s.resistor_v(300, y, y + 80, rb, "100 kΩ", dash)
        s.line([(300, y + 80), (330, y + 80)], dash=dash)
        s.flag(330, y + 80, "AGND", BLACK)
        s.text(PX0 - 12, y - 8, gp, 10.5, "end", False, BLUE)

    channel(yl["gp26"], "C1", "R1", "R2", t["audio_l"], t["audio_l2"], "", False)
    channel(yl["gp27"], "C2", "R3", "R4", t["audio_r"], t["audio_r2"], "", True)
    # C64 ground
    yg = yl["agnd"] + 10
    s.text(30, yg - 18, t["gnd_src"], 11, "start", True)
    s.pin(40, yg)
    s.line([(45, yg), (100, yg)])
    s.flag(100, yg, "AGND", BLACK)

    # ---- button ----
    yb = yl["gp15"]
    s.box(30, yb - 28, 240, 50, "#f2f2f2", dash=True)
    s.text(150, yb - 8, t["button"], 12, "middle", True)
    s.text(150, yb + 10, t["button2"], 11, "middle")
    s.line([(270, yb), (PX0 - 5, yb)], BLUE, dash=True)
    s.line([(150, yb + 22), (150, yb + 30)], dash=True)
    s.ground(150, yb + 30)

    # ---- drive LED ----
    yd = yl["gp14"]
    s.box(30, yd - 8, 240, 74, "#f7f2fb", dash=True)
    s.text(150, yd + 12, t["drive"], 12, "middle", True)
    s.text(262, yd + 34, t["drive2"], 10.5, "end")
    s.text(262, yd + 56, t["drive3"], 10.5, "end")
    s.pin(270, yd + 30)
    s.pin(270, yd + 52)
    s.line([(275, yd + 30), (320, yd + 30), (320, yd)], BLUE, dash=True)
    s.resistor_h(320, PX0 - 5, yd, "R5", "1 kΩ", BLUE, dash=True)
    s.line([(275, yd + 52), (300, yd + 52), (300, yd + 62)], dash=True)
    s.ground(300, yd + 62)

    # ---- panel ----
    s.line([(PX1 + 5, yr["gp16"]), (900, yr["gp16"])], BLUE)
    s.text(830, yr["gp16"] - 9, t["data"], 10.5, "middle", False, BLUE)
    s.box(900, 130, 240, 200, "#eaf7ee")
    s.text(1020, 156, t["panel"], 13, "middle", True)
    s.text(1020, 226, t["panel2"], 10.5, "middle")
    for y, lab, col in ((yr["gp16"], "DIN", BLACK), (yr["vbus"], "+5V", RED), (yr["gnd1"], "GND", BLACK)):
        s.pin(900, y); s.text(912, y + 4, lab, 12, "start", True, col)
    s.line([(PX1 + 5, yr["vbus"]), (895, yr["vbus"])], RED)
    s.line([(PX1 + 5, yr["gnd1"]), (895, yr["gnd1"])])

    # ---- light sensor ----
    s.box(900, 460, 240, 290, "#fbf7ea", dash=True)
    s.text(1020, 484, t["sensor"], 12.5, "middle", True)
    s.text(1020, 502, t["sensor2"], 10.5, "middle")
    pins = [("VIN", yr["3v3"]), ("3Vo", 550), ("GND", yr["gnd2"]), ("SCL", yr["gp5"]), ("SDA", yr["gp4"])]
    for lab, y in pins:
        s.pin(900, y); s.text(912, y + 4, lab, 12, "start", True)
    s.line([(PX1 + 5, yr["3v3"]), (895, yr["3v3"])], VIOLET, dash=True)
    s.line([(PX1 + 5, yr["gnd2"]), (895, yr["gnd2"])], dash=True)
    s.line([(PX1 + 5, yr["gp5"]), (895, yr["gp5"])], BLUE, dash=True)
    s.line([(PX1 + 5, yr["gp4"]), (895, yr["gp4"])], BLUE, dash=True)
    s.line([(888, 544), (876, 556)], RED, 2)       # X: not connected
    s.line([(876, 544), (888, 556)], RED, 2)
    s.text(870, 554, t["nc"], 10.5, "end", False, RED)

    # ---- notes ----
    y = 920
    for n in t["notes"]:
        s.text(30, y, n, 11.5)
        y += 22
    return s


def chrome():
    for c in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "google-chrome", "chromium"):
        if os.path.exists(c) or subprocess.run(["which", c], capture_output=True).returncode == 0:
            return c
    return None


if __name__ == "__main__":
    for lang, name in (("en", "schematic_minimal"), ("de", "schematic_minimal_de")):
        svg = os.path.join(HERE, name + ".svg")
        draw(lang).save(svg)
        print("written", svg)
        c = chrome()
        if c:
            png = os.path.join(HERE, name + ".png")
            subprocess.run([c, "--headless", "--disable-gpu", "--hide-scrollbars", f"--window-size={W},{H}",
                            "--force-device-scale-factor=2", f"--screenshot={png}", "file://" + svg],
                           capture_output=True, timeout=60)
            print("written", png if os.path.exists(png) else "(PNG failed)")
        else:
            print("no Chrome found - PNG not written", file=sys.stderr)
