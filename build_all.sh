#!/bin/bash
# Builds ALL firmware variants into firmware/ - always both boards: Pico 2 and Pico 2 W.
# Needs: PICO_SDK_PATH (Pico SDK 2.1.1 with submodules lib/cyw43-driver and lib/lwip), cmake, arm-none-eabi-gcc.
set -e
cd "$(dirname "$0")"
: "${PICO_SDK_PATH:?PICO_SDK_PATH is not set}"
JOBS=${JOBS:-8}
python3 tools/make_web_page.py                      # src/net/web_page.h + tools/text_editor.html
build() {   # $1 = suffix, $2 = board, $3.. = cmake options
  local name=$1 board=$2; shift 2
  local d=build/$name-$board; rm -rf "$d"; mkdir -p "$d"
  (cd "$d" && cmake ../.. -DPICO_BOARD=$board "$@" >/dev/null && make -j$JOBS >/dev/null)
  echo "$d"
}
# ---- Pico 2: visualizer, matrix test, demo
for v in "ws2812_usb-350mA:-DPOWER_LIMIT_MA=350" "ws2812_psu-1500mA:-DPOWER_LIMIT_MA=1500" "max7219_8x32:-DDISPLAY=MAX7219 -DMAX7219_MODULES=4"; do
  n=${v%%:*}; o=${v#*:}; d=$(build $n pico2 $o)
  cp $d/c64_visualizer.uf2 firmware/c64viz_$n.uf2; cp $d/matrix_test.uf2 firmware/matrix_test_$n.uf2; cp $d/c64viz_demo.uf2 firmware/demo_$n.uf2
  echo "Pico 2   $n"
done
d=$(build max7219_8x8 pico2 -DDISPLAY=MAX7219 -DMAX7219_MODULES=1); cp $d/c64_visualizer.uf2 firmware/c64viz_max7219_8x8.uf2; echo "Pico 2   max7219_8x8"
# ---- Pico 2 W: visualizer + WLAN
for v in "usb-350mA:-DPOWER_LIMIT_MA=350" "psu-1500mA:-DPOWER_LIMIT_MA=1500"; do
  n=${v%%:*}; o=${v#*:}; d=$(build wifi_$n pico2_w $o)
  cp $d/c64viz_wifi.uf2 firmware/c64viz_wifi_ws2812_$n.uf2; echo "Pico 2 W $n"
done
# ---- Pico 2 W debug: diagnostics on USB serial (screen /dev/tty.usbmodem* 115200)
d=$(build wifi_debug pico2_w -DPOWER_LIMIT_MA=350 -DDEBUG_USB=ON)
cp $d/c64viz_wifi.uf2 firmware/c64viz_wifi_ws2812_usb-350mA_debug.uf2; echo "Pico 2 W usb-350mA debug"
python3 tools/make_texts_uf2.py texts.txt firmware/texts_example.uf2 >/dev/null
python3 tools/make_wlan_reset_uf2.py firmware/wlan_reset.uf2 >/dev/null
echo "done - firmware/ is up to date"
