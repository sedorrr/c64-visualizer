#!/usr/bin/env python3
"""
Builds tools/simulator.html: the firmware's analysis and visualizer code (src/analysis.c,
src/visuals.c, src/demo_audio.c) compiled to WebAssembly with clang and embedded into
tools/sim/simulator.html. Needs clang with the wasm32 target and wasm-ld (apt install clang lld).
"""
import base64, os, subprocess
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S, SIM = os.path.join(ROOT, "src"), os.path.join(ROOT, "tools", "sim")
wasm = os.path.join(SIM, "sim.wasm")
subprocess.run(["clang", "--target=wasm32", "-O2", "-nostdlib", "-ffreestanding",
                "-fno-builtin-memset", "-fno-builtin-memcpy", "-fno-builtin-memmove",
                "-isystem", os.path.join(SIM, "inc"), "-I" + S, "-DDISPLAY_WS2812",
                "-Wl,--no-entry", "-Wl,--allow-undefined", "-Wl,--export-memory", "-Wl,--strip-all",
                "-o", wasm, os.path.join(SIM, "sim.c"), os.path.join(SIM, "libc.c"),
                os.path.join(S, "analysis.c"), os.path.join(S, "visuals.c"), os.path.join(S, "demo_audio.c")], check=True)
# the same code as plain JavaScript (fallback where WebAssembly is blocked); needs binaryen's wasm2js
js_path = os.path.join(SIM, "sim_wasm2js.js")
subprocess.run(["wasm2js", wasm, "--enable-sign-ext", "--enable-mutable-globals", "-O2", "-o", js_path],
               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
lines = open(js_path).read().split("\n")
start = next(i for i, l in enumerate(lines) if not l.startswith("import "))
end = next(i for i, l in enumerate(lines) if l.startswith("var retasmFunc"))
jscore = "\n".join(lines[start:end])
html = open(os.path.join(SIM, "simulator.html"), encoding="utf-8").read()
html = html.replace("__JSCORE__", jscore)
html = html.replace("__WASM__", base64.b64encode(open(wasm, "rb").read()).decode())
open(os.path.join(ROOT, "tools", "simulator.html"), "w", encoding="utf-8").write(html)
print("tools/simulator.html: %d bytes (wasm %d bytes)" % (len(html), os.path.getsize(wasm)))
