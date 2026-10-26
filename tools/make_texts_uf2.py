#!/usr/bin/env python3
"""
Converts texts.txt into texts.uf2 for the C64 visualizer (Pico 2 / RP2350).
Wandelt texts.txt in texts.uf2 um.

Usage / Aufruf:   python3 make_texts_uf2.py texts.txt [texts.uf2]
Then hold BOOTSEL, plug in the Pico 2, copy texts.uf2 onto the RP2350 drive.
The firmware itself stays untouched (the texts live at flash offset 0x300000).

Options:
  --family data|arm     UF2 family used for the data blocks (default: data).
                        If the drive rejects the file, try --family arm.
"""
import struct, sys

FLASH_BASE   = 0x10000000
TEXTS_OFFSET = 0x300000          # must match TEXTS_FLASH_OFFSET in src/config.h
MAX_BYTES    = 0x10000           # must match TEXTS_MAX_BYTES
MAGIC        = b"C64VIZTXT1\n"

UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FAMILY = {"data": 0xE48BFF58, "arm": 0xE48BFF59}
FAMILY_ABSOLUTE = 0xE48BFF57

def block(addr, payload, blockno, nblocks, family, flags=0x2000):
    payload = payload.ljust(476, b"\0")
    return struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, flags, addr, 256, blockno, nblocks, family) \
           + payload + struct.pack("<I", UF2_MAGIC_END)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    family = "data"
    for a in sys.argv[1:]:
        if a.startswith("--family="): family = a.split("=", 1)[1]
    if "--family" in sys.argv:
        family = sys.argv[sys.argv.index("--family") + 1]
        args = [a for a in args if a != family]
    if not args:
        print(__doc__); sys.exit(1)
    src = args[0]
    dst = args[1] if len(args) > 1 else "texts.uf2"

    text = open(src, "r", encoding="utf-8").read().replace("\r\n", "\n").replace("\r", "\n")
    data = MAGIC + text.encode("utf-8") + b"\0"
    if len(data) > MAX_BYTES:
        sys.exit(f"text too large: {len(data)} bytes, max {MAX_BYTES}")
    if len(data) % 256:
        data += b"\0" * (256 - len(data) % 256)

    n = len(data) // 256
    out = bytearray()
    # RP2350 erratum E10 workaround, same as picotool: an 'absolute' block first
    out += block(0x10FFFF00, b"\xEF" * 256, 0, 2, FAMILY_ABSOLUTE, flags=0xA000)
    for i in range(n):
        out += block(FLASH_BASE + TEXTS_OFFSET + i * 256, data[i*256:(i+1)*256], i, n, FAMILY[family])
    open(dst, "wb").write(out)
    lines = [l for l in text.split("\n") if l.strip() and not l.lstrip().startswith(("#", "@"))]
    print(f"{dst}: {len(data)} bytes, {n} blocks, {len(lines)} text line(s), family {family}")

if __name__ == "__main__":
    main()
