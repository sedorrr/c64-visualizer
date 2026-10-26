# Tests

## Host tests (C, on the PC)
`tests/run_host_tests.sh` – compiles firmware modules for the PC with small fakes of the Pico SDK and lwIP
(`tests/host/fakes/`) and runs scenario tests. Most print their results for reading (no strict asserts):

| Test | covers |
|---|---|
| net/web_basics | web server, settings save, restart (AP mode) |
| net/digest_auth | HTTP Digest login: right/wrong password, replay, stale nonce, long URLs |
| net/stream_on_off, net/multicast | audio stream reception, multicast group from the Ultimate's config |
| net/stream_forward | forwarding to a second Pico: v3 -> v4 settings, packets passed on unchanged, never back to the sender or to itself, target by name (mDNS), mode "forwarded" never asks the Ultimate, /config field |
| net/wifi_recover | WLAN trouble: the fallback access point tries the home WLAN every 5 min (never while its page is in use), a lost link is joined again after 10 s without the access point, password needed while reconnecting; at the start NOIP (router there, no DHCP address) is left and joined again (access point after 3 joins), NONET/FAIL get one more join, BADAUTH (held 2 s) and "never past joining" (20 s) go to the access point |
| net/multi_u64 | several Ultimates: v4 -> v5 settings, the list in /config (IP only, password removal), source switch (old one stopped, new one asked at once, stored without restart), one stream at a time, network search (/24, Ultimates found, router not, own address skipped) |
| net/ecosystem | devices in the network: beacon fields, query/answer, foreign beacons (broken ones and own address refused), forgetting after 35 s, at most 12; titles along the chain (C64NP1 sent on, received like a PC title, not taken for audio, passed on); two matrices: lead asks the partner, goes 64 wide when it answers, sends the other half with brightness/light/dither, shows its own half 2 frames late, narrow again after 3 s silence; partner answers, waits for a reserve, shows the frames with the lead's brightness, its own picture again after 0.5 s |
| net/news | retro news from the Pi jukebox: POST /news taken over only by the main program, umlauts spelt out, { } and unknown characters dropped, long headlines cut, sent on along the chain, /status count, empty list clears, > 760 bytes refused, busy (409) while a list waits; C64NEWS1 over UDP (not audio, passed on), forgotten after 3 h |
| net/backup | backup and restore: format, restore on another Pico (settings, jukebox, buttons), restart, damaged files change nothing, a switched SID socket is never restored |
| net/jukebox_pc_upload | SID upload from the PC -> multipart POST to the Ultimate, title + playing time |
| net/jukebox_stick_ftp | fake FTP server of the Ultimate: browse, PASV with wrong address, 15 s timeout, oversized listing -> "trunc" flag, song lengths from the stick, playlist, auto-next by song length |
| net/sldb_* | song length database: parser, flash writer, PC import, 60000 entries |
| net/jukebox_prefs | remembered jukebox settings survive a restart |
| net/ota_update | firmware update over the web page: staging, checks (hash, boot image, WLAN firmware), refusals, flash boundaries |
| net/mdns_answer | own mDNS responder: A + NSEC, NSEC for AAAA (no 5 s wait on macOS), legacy queries, broken input |
| textfx/news_setting | `@news` on/off (default on), the symbols of headlines have glyphs, a headline line plays |
| textfx/* | text effects (typewriter, wave, sparkle, cursor), settings parser, title with time |
| visuals/wide_modes | all 17 modes drawn 64 wide and back to 32 under AddressSanitizer, both halves lit, picture wide_modes.ppm |
| visuals/noise_gate | modes stay dark on pure noise when the audio gate is closed |
| audio/jbuf_sim | stream jitter buffer: 10 minutes simulated with Wi-Fi bursts, clock drift and a pause; compared with the old way |
| audio/stereo_check | stereo check of the second channel: stereo, empty, copy, crosstalk, no flicker |
| visuals/stereo_modes | a tone on one side lights only that side (spectrum, VU, mirror, scope) |

## Browser tests (Python + Playwright, the Pico is mocked)
`pip install playwright && playwright install chromium`, then in `tests/web/`: `python3 tabs.py`,
`jb2.py` (stick folder in the FTP picker + play, PC song length import byte-identical to the Pico's parser – needs
`fixtures/c_records.bin`, written by the host test sldb_parse_files), `jb3.py` (song length files: pick on the stick, delete the database with confirmation,
remembered by the Pico, PC files, clear), `jb4.py` (jukebox source stick/PC, folder picker, the Pico's playlist with click-to-play, shared-playlist import),
`jb5.py` (multicast warning, "Liste gekürzt" notice), `stereo.py` (@stereo setting round trip, stereo and stream status), `u64.py` (several Ultimates: list, source choice, network search), `backup.py` (backup download and restore), `fwd.py` (stream forwarding: field, receiver mode, status line), `wizard.py` (setup assistant: devices, Ultimate, order, receiver first, settings links, own page filled in and saved with src, ticks and final check from beacons, pre-filled target page), `eco.py` (device list card: way of the sound, warnings, "Neu suchen"; two-matrix settings round trip), `help.py` (the "Hilfe" tab: manual from `/hilfe` via a real HTTP server with gzip, loaded once, context links, offline editor has it built in), `fw.py` (firmware update: .uf2 -> image byte-identical to the built .bin, wrong firmware refused, confirm, restart), `shot5.py` (editor preview), `simt2.py` (simulator),
`digest_check.py` (real Chromium against a Python Digest server). They use `tools/text_editor.html` / `tools/simulator.html`
– run `python3 tools/make_web_page.py` / `tools/make_simulator.py` first after changes.
`jb2.py` and `jb3.py` check their results and end with `all checks passed` (exit code 1 on a failed check).
