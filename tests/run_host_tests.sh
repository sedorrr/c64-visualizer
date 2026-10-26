#!/bin/bash
# Host tests: firmware code compiled for the PC with small fakes of the Pico SDK / lwIP.
# Usage: tests/run_host_tests.sh   (from the project root or from tests/)
set -u
cd "$(dirname "$0")"
ROOT=..; SRC=$ROOT/src; H=host; B=$H/build
mkdir -p $B
# copies without the real dhcpserver.h next to them, so the fake headers are found first
cp $SRC/net/net.c $B/net_copy.c; cp $SRC/net/jukebox.c $B/jukebox_copy.c; cp $SRC/net/jukebox.c $B/jukebox.c
NETI="-I$H/fakes -I$B -I$SRC -I$SRC/net -DDISPLAY_WS2812 -DC64VIZ_WIDE"
fail=0
run() { local name=$1; shift; echo "=== $name"; if ! "$@"; then echo "!!! $name FAILED"; fail=1; fi; }
net() { local t=$1; shift; gcc -w $NETI -o $B/$t $H/net/$t.c $H/fakes/fake_lwip.c $B/net_copy.c $B/jukebox_copy.c $SRC/net/md5.c $SRC/net/diag.c $SRC/net/ota.c $SRC/net/mdns_lite.c $SRC/net/u64scan.c $SRC/net/devices.c $SRC/net/wide.c $SRC/net/news.c $SRC/stereo.c $H/fakes/fake_ota_apply.c $H/fakes/fake_display.c -lm && (cd $B && ./$t "$@"); }
net_inc() { local t=$1; shift; gcc -w $NETI -o $B/$t $H/net/$t.c $H/fakes/fake_lwip.c $B/net_copy.c $SRC/net/md5.c $SRC/net/diag.c $SRC/net/ota.c $SRC/net/mdns_lite.c $SRC/net/u64scan.c $SRC/net/devices.c $SRC/net/wide.c $SRC/net/news.c $SRC/stereo.c $H/fakes/fake_ota_apply.c $H/fakes/fake_display.c -lm && (cd $B && ./$t "$@"); }
txt() { gcc -w -I$H/textfx/fakes -I$SRC -DDISPLAY_WS2812 -DCYCLE_DEFAULT_S=20 -o $B/$1 $H/textfx/$1.c -lm && $B/$1; }

run "web page, settings save, restart"   bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; net web_basics && net web_basics 2"
run "digest authentication"              net digest_auth
run "texts.txt saved from the web page"  bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; net texts_save && net texts_save big"
run "audio stream on/off"                bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; net stream_on_off; net stream_on_off on"
run "multicast group"                    bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; net multicast 0; net multicast 1 239.0.1.70:11001; net multicast 1 192.168.1.20"
run "stream forwarding to a second Pico" bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; for p in migrate forward running self name receiver config keep; do net stream_forward \$p || exit 1; done"
run "devices, titles along the chain, two matrices" bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; for p in devices relay lead follow; do net ecosystem \$p || exit 1; done"
run "retro news from the Pi jukebox"     bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; for p in http busy udp; do net news \$p || exit 1; done"
run "WLAN trouble: access point retries, reconnect, DHCP hang" bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; for p in ap busy lost noip noip_ok join badauth authflip lostnoip; do net wifi_recover \$p || exit 1; done"
run "several Ultimates, source switch, search" bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; for p in migrate config switch lock scan; do net multi_u64 \$p || exit 1; done"
run "backup and restore of the settings"   bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; for p in restore reboot broken socket; do net backup \$p || exit 1; done"
run "jukebox: upload from the PC"        net jukebox_pc_upload
run "jukebox: USB stick over FTP"        net jukebox_stick_ftp
run "jukebox: now playing from the PC"   net jukebox_now_playing
run "song lengths: staging parser"       net_inc sldb_stage_parser
run "song lengths: flash writer"         net_inc sldb_flash_writer
run "song lengths: parse files"          net_inc sldb_parse_files ../../web/fixtures/user.txt ../../web/fixtures/hvsc.md5
run "song lengths: import from the PC"   net sldb_pc_import
run "song lengths: 60000 entries"        net sldb_60000_entries
run "jukebox settings survive restart"   net jukebox_prefs
run "firmware update over the web page"  net ota_update
run "mDNS: c64viz.local without delay"  net mdns_answer
run "device name: setting, AP, DHCP, -2" bash -c "$(declare -f net); NETI='$NETI'; H=$H; B=$B; SRC=$SRC; net device_name migrate && net device_name default && net device_name nofield && net device_name ap && net device_name free && net device_name taken"
run "text effects"                       txt text_effects
run "settings: sleep/night/clock"        txt settings_time
run "title with playing time"            txt title_with_time
run "settings: news, symbols in the font" txt news_setting
run "settings: sleep colour"             txt settings_sleepcolor
run "settings: brightness kept with new texts" bash -c "gcc -O2 -w -DHOST_TEST -I$SRC -o $B/settings_keep $H/textfx/settings_keep.c $SRC/settings.c && $B/settings_keep"
run "stream jitter buffer (10 min simulated)" bash -c "gcc -O2 -w -I$SRC -I$SRC/net -o $B/jbuf_sim $H/audio/jbuf_sim.c $SRC/net/jbuf.c -lm && $B/jbuf_sim"
run "light sensor maths + settings"     bash -c "gcc -O2 -w -I$H/textfx/fakes -I$SRC -DDISPLAY_WS2812 -DCYCLE_DEFAULT_S=20 -o $B/light_calc $H/light/light_calc.c $SRC/light_calc.c -lm && $B/light_calc"
run "stereo check (second channel)"     bash -c "gcc -O2 -w -I$SRC -o $B/stereo_check $H/audio/stereo_check.c $SRC/stereo.c -lm && $B/stereo_check"
run "stereo modes (visuals)"             bash -c "gcc -O2 -w -I$H/visuals/fakes -I$SRC -DDISPLAY_WS2812 -o $B/stereo_modes $H/visuals/stereo_modes.c $SRC/analysis.c $SRC/visuals.c -lm && $B/stereo_modes"
run "noise gate (visuals)"               bash -c "gcc -O2 -w -I$H/visuals/fakes -I$SRC -DDISPLAY_WS2812 -o $B/noise_gate $H/visuals/noise_gate.c $SRC/analysis.c $SRC/visuals.c -lm && $B/noise_gate"
run "two matrices: all modes 64 wide (ASan)" bash -c "gcc -g -O1 -fsanitize=address -w -I$H/visuals/fakes -I$SRC -DDISPLAY_WS2812 -DC64VIZ_WIDE -o $B/wide_modes $H/visuals/wide_modes.c $SRC/analysis.c $SRC/visuals.c -lm && (cd $B && ./wide_modes)"
echo; [ $fail = 0 ] && echo "ALL HOST TESTS RAN" || echo "SOME TESTS FAILED"
exit $fail
