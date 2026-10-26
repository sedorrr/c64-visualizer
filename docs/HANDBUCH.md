# C64 Sound-Visualizer – Handbuch

Der Visualizer macht aus dem Ton eines C64 bzw. Ultimate 64 eine Lichtshow auf einer RGB-Matrix mit 8 × 32 LEDs.
Ohne Musik zeigt er Lauftexte, auf Wunsch die Uhrzeit, und nach längerer Stille schaltet er die Matrix aus.

Es gibt ihn in zwei Ausführungen. Dieselbe Bedienung gilt für beide, die WLAN-Teile nur für den Pico 2 W:

| | **Pico 2** | **Pico 2 W** |
|---|---|---|
| Ton | analoger Eingang (GP26, optional GP27 für Stereo) | analoger Eingang **oder** Audio-Stream des Ultimate über WLAN |
| Lauftexte und Einstellungen | Offline-Editor am PC → `texts.uf2` auf den Pico kopieren | Weboberfläche des Pico, „Speichern“ genügt |
| SID-Jukebox, Uhr, mehrere Ultimates, Sicherung, Update übers Netz | – | ja |

Dieses Handbuch steht auch in der Weboberfläche im Tab **Hilfe** und im Offline-Editor. Den Aufbau der Hardware
(Schaltpläne, Stückliste, Gehäuse) beschreibt die README des Projekts.

## Erste Schritte

### Die richtige Firmware

Alle fertigen Dateien liegen im Ordner `firmware/`:

| Datei | Wofür |
|---|---|
| `c64viz_ws2812_usb-350mA.uf2` | Pico 2, Matrix über USB versorgt (Stromgrenze 350 mA) |
| `c64viz_ws2812_psu-1500mA.uf2` | Pico 2, Matrix am 5-V-Netzteil (1500 mA) |
| `c64viz_wifi_ws2812_usb-350mA.uf2` | Pico 2 W, über USB versorgt |
| `c64viz_wifi_ws2812_psu-1500mA.uf2` | Pico 2 W, am Netzteil |
| `c64viz_wifi_ws2812_usb-350mA_debug.uf2` | Pico 2 W mit zusätzlicher Protokoll-Ausgabe über USB-Seriell |
| `c64viz_max7219_8x32.uf2`, `..._8x8.uf2` | Pico 2 mit einfarbiger MAX7219-Matrix |
| `demo_*.uf2` | Vorführung ohne C64: eingebaute Chiptune-Musik, alle Modi nacheinander |
| `matrix_test_*.uf2` | Test der Matrix ohne Ton („READY“, „C64 Sound Visualizer“, glühendes „SID“) |
| `wlan_reset.uf2` | löscht nur WLAN-, Ultimate- und Passwort-Einstellungen (Pico 2 W) |

Die WLAN-Firmware läuft nur auf dem Pico 2 W, die anderen nur auf dem Pico 2. Die Pins sind bei beiden gleich.

### Aufspielen

- **BOOTSEL:** Die BOOTSEL-Taste auf dem Pico gedrückt halten und das USB-Kabel einstecken. Der Pico erscheint als
  Laufwerk, die `.uf2` darauf kopieren. Er startet danach von selbst.
- **Ohne BOOTSEL (eingebauter Pico):** den **Modustaster** gedrückt halten und dabei das USB-Kabel einstecken. Die Matrix
  zeigt kurz einen **blauen Rahmen**, dann meldet sich der Pico als Laufwerk.
- **Pico 2 W über die Webseite:** Tab „WLAN & Ultimate“ → „Firmware (.uf2) wählen …“, siehe
  [Firmware-Update](#firmware-update-ueber-die-webseite).

Lauftexte, Einstellungen und Songlängen bleiben beim Aufspielen einer neuen Firmware erhalten.

### Der Start

Nach dem Einschalten passiert der Reihe nach:

1. **Startanimation:** roter Punkt oben links, ein grüner Lauf über die oberste Zeile und die linke Spalte hinunter.
   Läuft sie anders, ist die Matrix anders verdrahtet als erwartet (siehe README, „Kalibrierung“).
2. **Kamera-Modus:** „Cam an“ (grün) oder „Cam aus“ (rot).
3. **Rauschmessung:** Das Panel leuchtet 1,5 s lang zur Hälfte, währenddessen misst der Pico das Grundrauschen am
   Eingang. Deshalb am besten **in Stille einschalten**.
4. Beim Pico 2 W danach die Netzwerkadresse, z. B. „WLAN 192.168.1.231  c64viz.local“.

Danach zeigt er Musik als Visualizer und nach einigen Sekunden Stille die Lauftexte.

### Pico 2 W einrichten

1. Beim ersten Start (noch ohne WLAN-Daten) öffnet der Pico einen eigenen Zugangspunkt **C64-Visualizer**,
   Passwort **commodore64**. Die Matrix zeigt „WLAN C64-Visualizer 192.168.4.1“.
2. Mit Handy oder PC in dieses WLAN gehen und **http://192.168.4.1** öffnen. Hier fragt die Seite nach keinem Passwort.
3. Im Tab **WLAN & Ultimate** den WLAN-Namen (nur 2,4 GHz) und das WLAN-Passwort eintragen, dazu ein eigenes Passwort für
   die Webseite und – falls vorhanden – das Ultimate (oder „Netzwerk nach Ultimates durchsuchen“). „Speichern und neu
   starten“ drücken.
4. Der Pico verbindet sich mit dem Heimnetz und lässt seine Adresse durchlaufen. Ab jetzt ist er unter
   **http://c64viz.local** erreichbar (oder unter der IP-Adresse).
5. Im Heimnetz fragt der Browser nach Benutzername und Passwort. Der **Benutzername ist beliebig**, das Passwort ist das
   der Webseite (ab Werk **commodore64** – bitte ändern, der Status warnt so lange).

## Bedienung mit dem Taster

Der Taster hängt an GP15 und schaltet gegen GND.

| Aktion | Wirkung |
|---|---|
| kurz drücken | nächster Visualizer-Modus (vorher läuft sein Name durch; während des Namens erneut drücken springt weiter). Beendet auch die Lauftexte |
| zweimal kurz | Kamera-Modus an/aus |
| dreimal kurz | Rauschen neu messen („Ruhe bitte“, danach z. B. „Schwelle 34“) – vorher die Musik anhalten |
| 0,7–3 s halten | nächste Helligkeit: 7 → 20 → 45 → 100 % |
| länger als 3 s halten | automatischer Moduswechsel („automatic“) oder nur per Taster („Button“) |
| beim Einstecken des USB-Kabels gehalten | USB-Laufwerksmodus zum Aufspielen einer Firmware (blauer Rahmen) |

Helligkeit, Automatik/Taster und Kamera-Modus werden im Flash gespeichert und gelten auch nach einem Neustart.
Aus der ausgeschalteten Matrix (Schlaf) weckt ein Tastendruck sie für eine Minute.

## Was die Matrix zeigt

Die Matrix ist immer in einem von vier Zuständen:

| Zustand | Wann |
|---|---|
| **Visualizer** | sobald Ton über der Rauschschwelle ankommt (mindestens 0,16 s lang) |
| **Lauftexte** | nach einer einstellbaren Zeit Stille (Standard 8 s), Zeile für Zeile, beim Pico 2 W dazwischen die Uhrzeit |
| **Schlaf** | nach einer einstellbaren Zahl Minuten Stille (Standard: nie): dunkel, sanftes Atmen oder auf- und abblendende Uhr |
| **Nacht** | in den eingestellten Nachtstunden (Pico 2 W) dunkel, außer es kommt Musik |

Zusätzlich können erscheinen:

- **Zwei rote Pixel oben links:** Das Ultimate lädt gerade von seinem Laufwerk (wenn die Laufwerks-LED an GP14
  angeschlossen ist).
- **Ein magentafarbener Pixel oben rechts:** Der analoge Eingang wird übersteuert – den Pegel zurückdrehen.
- **Eine schwache Linie in der untersten Zeile:** Fortschritt des laufenden Jukebox-Titels, wenn seine Länge bekannt ist.
- **Meldungen** als Lauftext, zum Beispiel:

| Meldung | Bedeutung |
|---|---|
| `spectrum`, `fire` … | Name des neuen Modus |
| `automatic` / `Button` | Moduswechsel automatisch bzw. nur per Taster |
| `Cam an` / `Cam aus` | Kamera-Modus |
| `Ruhe bitte`, `Schwelle 34` | Rauschmessung läuft bzw. neue Schwelle |
| `WLAN 192.168.1.231  c64viz.local` | Pico 2 W ist im Heimnetz, mit Adresse und Namen |
| `WLAN C64-Visualizer 192.168.4.1` | Pico 2 W hat seinen eigenen Zugangspunkt geöffnet |
| `Stereo`, `rechts kein Signal - Mono`, `rechts = links - Mono` | Ergebnis der Stereo-Prüfung |
| Titel, Komponist, `4:36` | neuer Jukebox-Titel mit Spielzeit |
| `SID 1: D400  SID 2: D500` | Belegung der SID-Sockel des Ultimate (wenn eingeschaltet) |

## Die Visualizer-Modi

Alle Modi nutzen dieselbe Analyse: 16 Frequenzbänder von 100 Hz bis 6 kHz, 62 Bilder pro Sekunde, Pegel passen sich
der Lautstärke an. Welche Modi in welcher Reihenfolge laufen, steht unter „Einstellungen“ bzw. in `@modes`.

| Name | Was du siehst |
|---|---|
| `spectrum` | 16 Balken grün/gelb/rot mit fallenden Spitzenpunkten |
| `vu` | waagerechte Pegelanzeige mit Spitzenmarke |
| `mirror` | Bass in der Mitte, höhere Bänder zu den Rändern, nach oben und unten, wandernder Regenbogen |
| `waterfall` | Spektrogramm: die Zeit läuft nach links, Bass unten, laute Stellen blau, cyan, weiß |
| `scope` | Oszilloskop: die Wellenform selbst mit Nachleuchten |
| `fire` | Flammen, jede Spalte von ihrem Frequenzband gespeist |
| `pulse` | jeder Beat schickt eine Druckwelle von der Mitte zu den Rändern |
| `fountain` | jedes Band schießt Funken nach oben, die zurückfallen |
| `plasma` | Demoszene-Plasma, schneller bei Lautstärke, Blitz bei Beats |
| `starfield` | Sterne fliegen aus der Mitte nach außen |
| `raster` | C64-Rasterbalken für Bass, Mitten und Höhen |
| `scroller` | Sinus-Scroller mit dem eigenen Text aus `@scroller` |
| `loader` | C64-Ladestreifen: breit und ruhig bei leisen Stellen, schmal und schnell bei lauten |
| `logo` | ein Wort (Standard „C64“) füllt sich wie Equalizer-Balken |
| `melody` | der lauteste Ton als laufende Linie, jeder Notenname in seiner Farbe |
| `rain` | digitaler Regen, lautere Bänder lassen mehr Tropfen fallen |
| `life` | Conways Game of Life, jeder Beat sät neue Zellen |

Beim automatischen Wechsel läuft jeder Modus `@cycle` Sekunden (nur solange Musik spielt, Standard 20). Der nächste
beginnt auf dem nächsten Beat und wird weich eingeblendet. Mit **Stereo** zeigen `spectrum`, `vu`, `mirror` und `scope`
links und rechts getrennt; die anderen Modi nutzen die Summe.

## Die Weboberfläche

Nur Pico 2 W. Aufruf über **http://c64viz.local** (bzw. den eigenen Gerätenamen) oder die IP-Adresse. Die Seite
funktioniert ohne Internet. Oben steht immer der **Statuskasten**, darunter die Tabs.

### Der Statuskasten

| Zeile | Bedeutung |
|---|---|
| WLAN, IP | verbundenes WLAN und Adresse, dazu der Name, unter dem der Pico erreichbar ist |
| Ton | „Netzwerk-Stream (250 Pakete/s)“ oder „analoger Eingang“. Um 250 Pakete/s sind normal |
| Ultimate | Zustand der Verbindung: „Stream läuft“, „wird gestartet“, „nicht erreichbar“, „Passwort falsch (403)“ … |
| Weiterleitung | Ziel und weitergeleitete Pakete pro Sekunde (nur wenn eingerichtet) |
| Rauschschwelle | aktuelle Schwelle des analogen Eingangs |
| Auslastung | Prozessorlast und längste Arbeit pro 16-ms-Bild |
| Licht | gemessene Helligkeit des Raums und wie weit die Matrix gerade abgesenkt ist (nur mit Lichtsensor) |
| Stereo | „Stereo“, „rechts = links - Mono“, „Mono (1 SID laut Datei)“ … (nur bei eingeschaltetem Stereo) |
| Warnungen | Standardpasswort noch aktiv, Multicast stört das WLAN |

### Tab „Steuerung“

Dieselben Funktionen wie der Taster: nächster Modus, Kamera an/aus, Automatik/Taster, Rauschen neu messen. Dazu:

- **Helligkeit** als Regler von 4 bis 100 %. Der Wert gilt sofort und wird 2 s nach der letzten Änderung gespeichert.
  100 % ist die hellste Stufe des Tasters; die Stromgrenze der Firmware gilt weiterhin.
- **Quelle:** erscheint, sobald mehr als ein Ultimate eingetragen ist – siehe [Mehrere Ultimates](#mehrere-ultimates).
- **Geräte im Netz:** alle Visualizer und Audio-Empfänger im Heimnetz mit dem Weg des Tons – siehe
  [Geräte im Netz](#geraete-im-netz).

### Tab „Lauftexte“

Jeder Lauftext ist eine Zeile. Tippst du eine Zeile an, erscheinen darunter eine **Live-Vorschau** der Matrix und die
Knöpfe für Farben und Effekte. Den Cursor an die Stelle setzen, an der der Effekt beginnen soll, dann den Knopf
antippen – der passende Code (z. B. `{blink:3}`) wird eingefügt und die Vorschau spielt ihn ab. Mit „nach oben/unten“
änderst du die Reihenfolge. Was die Codes bewirken, steht unter [Lauftexte und texts.txt](#lauftexte-und-texts-txt).

„Speichern und neu starten“ schreibt Texte und Einstellungen in den Pico, der danach neu startet. Der
**Experten-Modus** zeigt die ganze `texts.txt` als Text zum direkten Bearbeiten.

### Tab „Einstellungen“

Alle Einstellungen aus `texts.txt` als Formular: Modi und Wechsel, Modusnamen, Scroller-Text und Logo-Wort,
Lauftext-Wartezeit und Tempo, Kamera-Modus, Laufwerksanzeige, Stereo, Lichtsensor, Schlaf, zwei Matrizen als ein Bild,
Uhr, Retro-News, Nacht und Zeitzone.
Gespeichert wird ebenfalls mit „Speichern und neu starten“. Die Bedeutung jeder Einstellung steht in der
[Tabelle der Einstellungen](#einstellungen-in-texts-txt).

### Tab „SID-Jukebox“

Spielt SID-Dateien auf dem Ultimate ab – vom USB-Stick am Ultimate oder vom PC. Ausführlich unter
[SID-Jukebox](#sid-jukebox).

### Tab „WLAN & Ultimate“

| Bereich | Inhalt |
|---|---|
| Gerätename | Name im Netz, z. B. `wohnzimmer` → http://wohnzimmer.local (1–16 Zeichen: a–z, 0–9, Bindestrich) |
| WLAN | Name (2,4 GHz) und Passwort; leeres Passwortfeld = unverändert |
| Ultimate-Geräte | bis zu vier: Name, IP-Adresse (Ethernet), Netzwerk-Passwort; „Netzwerk nach Ultimates durchsuchen“ |
| Audio-Stream | Stream verwenden, Empfänger: Unicast, Multicast oder weitergeleitet; Weiterleitungsziel |
| Passwort der Webseite | 6–32 Zeichen, leer = unverändert |
| Sicherung | alle Einstellungen herunterladen oder einspielen |
| Firmware-Update | neue Firmware ohne USB-Kabel aufspielen |
| Diagnose | Protokoll des Pico anzeigen |

„Speichern und neu starten“ in diesem Tab speichert nur die Netzwerk-Einstellungen.

### Tab „Hilfe“

Dieses Handbuch. Die Links „Hilfe“ in den anderen Tabs springen direkt zum passenden Abschnitt.

## Lauftexte und texts.txt

Alle Lauftexte und Einstellungen stehen in einer Textdatei, `texts.txt`. Die Weboberfläche und der Offline-Editor
bearbeiten genau diese Datei.

- Zeilen mit `#` am Anfang und Leerzeilen werden ignoriert.
- Zeilen mit `@` am Anfang sind [Einstellungen](#einstellungen-in-texts-txt).
- Jede andere Zeile ist ein Lauftext.
- Grenzen: 32 Texte, etwa 100 Zeichen pro Text, 64 KB insgesamt.
- Zeichensatz: A–Z, a–z, 0–9, Leerzeichen und `! . - ? :`. Andere Zeichen (auch Umlaute) erscheinen als Kästchen.

### Codes in Lauftexten

Codes stehen in geschweiften Klammern direkt im Text.

| Code | Wirkung |
|---|---|
| `{color:red}`, `{color:FF8000}` | Farbe ab hier: `red green blue yellow orange white cyan magenta purple pink` oder RGB-Hex |
| `{rainbow}` | Regenbogen ab hier |
| `{blink}`, `{blink:5}` | das **nächste** Wort hält mittig an und blinkt (Standard 3-mal) |
| `{glow}`, `{glow:5000}` | das **nächste** Wort hält an und glüht wie heißes Metall (Millisekunden, Standard 3000) |
| `{typewriter}` | das **nächste** Wort wird Buchstabe für Buchstabe getippt |
| `{explode}` | das **vorige** Wort hält an und explodiert |
| `{hold:2000}` | das **vorige** Wort hält so viele Millisekunden mittig an |
| `{pause:800}` | Dunkelpause nach dem vorigen Wort (Millisekunden) |
| `{cursor}` | blinkender C64-Cursor hinter dem vorigen Wort |
| `{wave}`, `{wave:0}` | Buchstaben schwingen ab hier bzw. nicht mehr |
| `{sparkle}`, `{sparkle:0}` | weißes Funkeln ab hier bzw. nicht mehr |
| `{speed:40}` | Tempo ab hier (Millisekunden pro Pixel; kleiner = schneller) |

Beispiele:

```
{color:cyan}Insert {color:yellow}Disk {color:cyan}and press {color:white}{blink:2}PLAY{hold:800}
{color:8080FF}{typewriter}READY{cursor}{hold:3000}
```

### Einstellungen in texts.txt

| Einstellung | Bedeutung | Standard |
|---|---|---|
| `@silence 8000` | Lauftexte nach so vielen Millisekunden Stille | 8000 |
| `@speed 60` | Lauftext-Tempo in Millisekunden pro Pixel (90 langsam, 35 schnell) | 60 |
| `@brightness 20` | Helligkeit in Prozent (4–100); 0–3 = die alten Stufen 7/20/45/100 % | gespeicherter Wert |
| `@threshold auto` | Rauschschwelle: `auto` = beim Start messen, eine Zahl = fest | auto |
| `@modes spectrum, fire …` | welche Modi in welcher Reihenfolge | alle 17 |
| `@cycle 20` | Sekunden pro Modus beim automatischen Wechsel | 20 |
| `@switching auto` | `auto` = automatischer Wechsel, `button` = nur per Taster | auto |
| `@names manual` | Modusname anzeigen: `manual` (beim Tastendruck), `always`, `off` | manual |
| `@scroller TEXT` | Text für den Modus `scroller` | – |
| `@logo C64` | Wort für den Modus `logo` (2–5 Buchstaben) | C64 |
| `@camera on` | Kamera-Modus beim Start | off |
| `@driveled off` | Laufwerksanzeige ausblenden | on |
| `@stereo on` | zweiten Eingang (GP27) bzw. Stereo aus dem Stream nutzen | off |
| `@light off` | Lichtsensor nicht verwenden | on |
| `@lightmin 10` | Helligkeit im Dunkeln in Prozent der eingestellten | 10 |
| `@lightcurve medium` | Stärke der Anpassung: `weak`, `medium`, `strong` | medium |
| `@sleep 30` | Matrix nach so vielen Minuten Stille ausschalten (0 = nie) | 0 |
| `@sleepstyle breathe` | ausgeschaltet: `off` (dunkel), `breathe` (Atmen), `clock` (Uhr, nur Pico 2 W) | off |
| `@sleepcolor FF4000` | Farbe für Atmen/Uhr, `auto` = Standard | auto |
| `@clock off` | keine Uhrzeit zwischen den Lauftexten (Pico 2 W) | on |
| `@news off` | keine Retro-News vom Raspberry Pi zwischen den Lauftexten (Pico 2 W, siehe [Schlaf, Uhr und Nacht](#schlaf-uhr-und-nacht)) | on |
| `@night 23:00-07:00` | Matrix in diesen Stunden dunkel (Pico 2 W) | aus |
| `@timezone europe` | `europe` (MEZ/MESZ mit Sommerzeit), `utc` oder fest wie `+1` | europe |
| `@wide lead` | zwei Matrizen als ein Bild: `off`, `lead` (dieser Pico rechnet), `follow` (zeigt die Hälfte eines anderen; Pico 2 W) | off |
| `@widepartner c64viz2.local` | bei `lead`: Name oder IP-Adresse des anderen Pico | – |
| `@wideside left` | bei `lead`: diese Matrix ist die `left` (linke) oder `right` (rechte) | left |

Helligkeit, Automatik/Taster und Kamera-Modus merkt sich der Pico zusätzlich im Flash. Wird eine **geänderte**
`texts.txt` aufgespielt, gelten wieder deren Werte.

### Pico 2 ohne WLAN: der Offline-Editor

`tools/text_editor.html` in einem beliebigen Browser am PC öffnen – ohne Internet, ohne Installation. Er sieht aus
wie die Weboberfläche (Tabs Lauftexte, Einstellungen, Hilfe). Optional mit „texts.txt laden“ eine vorhandene Datei
öffnen, bearbeiten und „texts.uf2 herunterladen“. Die `texts.uf2` wie eine Firmware auf den Pico kopieren
(BOOTSEL bzw. Modustaster beim Einstecken). Die Firmware bleibt dabei unangetastet.

## Ton

### Analoger Eingang

Der Ton kommt über einen Kondensator an **GP26**, zwei 100-kΩ-Widerstände halten ihn auf halber Referenzspannung
(siehe README, „Minimalaufbau“). Bei Stille misst der Pico das Grundrauschen und legt die **Rauschschwelle** knapp
darüber; nur Ton über der Schwelle zählt als Musik.

- **Kurze Störspitzen** zählen nicht: Ton muss mindestens 0,16 s anhalten. Folgt auf einen kurzen „Ton“ knapp über der
  Schwelle wieder Stille, wertet der Pico ihn als Störung und hebt die Schwelle etwas an (höchstens 120).
- **Neu messen:** Musik anhalten, dreimal kurz drücken oder „Rauschen neu messen“ auf der Webseite.
- **Feste Schwelle:** `@threshold 60` schaltet die Automatik ab.

### Audio-Stream vom Ultimate (Pico 2 W)

Mit eingetragenem Ultimate und „Audio-Stream vom Ultimate verwenden“ bittet der Pico das Ultimate, ihm seinen
Audio-Stream zu schicken (Ultimate-Firmware ab 3.11, besser ab 3.14d). Der Stream ist dasselbe Signal wie an HDMI und
der DIN-Buchse, UltiSID und echte SIDs funktionieren also beide. Kommt kein Ton an, fragt der Pico alle 10 s erneut.
Solange Pakete ankommen, wird der analoge Eingang ignoriert; bleiben sie 0,25 s aus, übernimmt wieder der analoge.

Die Ultimates senden den Stream nur über **Ethernet**. Deshalb immer die Ethernet-Adresse des Ultimate eintragen.

Das Ultimate streamt immer nur an **ein** Ziel. Startet ein anderes Programm einen eigenen Stream, nimmt es ihn dem Pico
weg; der Pico holt ihn sich zurück, sobald das Programm aufhört.

### Empfänger: Unicast, Multicast, Weiterleitung

| Einstellung | Wann |
|---|---|
| **nur dieser Pico (Unicast)** | Standard und Empfehlung |
| **mehrere Geräte gleichzeitig (Multicast)** | mehrere Empfänger, z. B. Pico und PC. **Achtung:** Bei vielen WLAN-Routern stauen sich die Pakete, Webseite und Jukebox sind dann kaum erreichbar |
| **von einem anderen Pico weitergeleitet** | zweiter Visualizer, der den Stream von einem ersten Pico bekommt |

**Zwei Visualizer gleichzeitig:** Beim ersten Pico „nur dieser Pico (Unicast)“ lassen und unter „Stream weiterleiten
an“ den zweiten eintragen (IP oder Name, z. B. `c64viz-2.local`). Beim zweiten Pico den Stream einschalten und „von
einem anderen Pico weitergeleitet“ wählen. Der erste reicht jedes Paket unverändert weiter; der zweite fragt das
Ultimate nie selbst, sonst nähme er den Stream dem ersten weg. Der Statuskasten des ersten zeigt die weitergeleiteten
Pakete pro Sekunde. Schritt für Schritt, auch mit dem Audio-Empfänger an der Stereoanlage als erstem Glied:
[Beispiel: Audio-Empfänger und zwei Visualizer](#beispiel-audio-empfaenger-und-zwei-visualizer).

### Stereo

Mit `@stereo on` (Häkchen „Stereo“ unter „Einstellungen“) nutzt der Pico zwei Kanäle: beim analogen Eingang **GP27**
für rechts, beim Stream links und rechts aus dem Stream. Er prüft selbst, ob rechts wirklich ein eigenes Signal
ankommt (jeweils über etwa 2 s Musik):

- rechts nichts → Mono vom linken Eingang („rechts kein Signal - Mono“),
- rechts dasselbe wie links → Mono („rechts = links - Mono“),
- zwei verschiedene Signale → Stereo.

Zurück auf Mono geht er erst nach etwa 60 s, weil Stereo-Stücke oft lange nur einen SID nutzen. Bei Stücken aus der
Jukebox entscheidet der SID-Kopf: ein SID = Mono, zwei oder drei = Stereo.

Verdrahtung ohne Löten am Pico, wenn ADC_VREF und AGND schon belegt sind: Bei `@stereo on` schaltet die Firmware
**GP28 (Pin 34)** auf 3,3 V. Der obere 100-kΩ-Widerstand des rechten Kanals kommt dann an GP28, der untere an einen
freien GND-Pin (z. B. Pin 28).

## Mehrere Ultimates

Unter „WLAN & Ultimate“ lassen sich bis zu **vier** Ultimates eintragen (Ultimate 64, Elite, C64 Ultimate …), je mit
eigenem Namen, IP-Adresse und Netzwerk-Passwort (nur nötig, wenn im Ultimate eines gesetzt ist).

**Netzwerk durchsuchen:** „Netzwerk nach Ultimates durchsuchen“ fragt jede Adresse des Heimnetzes (etwa 30 s) und
listet jedes gefundene Ultimate einmal, mit Ethernet-Adresse. Ein Gerät, das nur per WLAN antwortet, wird markiert.
„übernehmen“ trägt es in den nächsten freien Platz ein. Ultimates mit Netzwerk-Passwort antworten ohne Passwort nicht
und werden nicht gefunden – diese von Hand eintragen.

**Quelle wählen:** Ab zwei Geräten erscheint unter „Steuerung“ die Auswahl **Quelle**. Das bisherige Gerät wird gebeten,
den Stream zu beenden, das neue sofort gefragt; die Wahl wird ohne Neustart gespeichert. Jukebox, Songlängen und
SID-Adressen nutzen immer die Quelle. Solange die Jukebox des Pico spielt (oder ein umgestellter SID-Sockel noch
zurückgestellt werden muss), lehnt der Pico den Wechsel ab – erst „Stopp“ drücken.

## Einricht-Assistent: Geräte verketten

Der Ultimate sendet seinen Audio-Stream nur an **ein** Ziel. Sollen mehrere Geräte denselben Ton bekommen, reicht jedes
ihn an das nächste weiter (eine Kette, siehe [Beispiel](#beispiel-audio-empfaenger-und-zwei-visualizer)). Der
**Einricht-Assistent** führt Schritt für Schritt durch das Einstellen. Er steht im Tab „Steuerung“ (Karte „Geräte im
Netz“) und im Tab „WLAN & Ultimate“ und läuft auf jedem Visualizer.

1. **Geräte:** Alle Visualizer und Audio-Empfänger im Netz werden angezeigt – Häkchen bei denen, die mitmachen sollen.
   Fehlt eines, ist es aus, in einem anderen WLAN oder hat eine ältere Firmware (dann zuerst aktualisieren).
2. **Ultimate:** von welchem Ultimate der Ton kommt (seine Ethernet-Adresse).
3. **Reihenfolge:** Das erste Gerät holt den Ton vom Ultimate, jedes reicht ihn weiter. Ein Audio-Empfänger steht immer
   am Anfang (pro Kette nur einer); die Visualizer lassen sich mit ▲▼ umsortieren.
4. **Einstellen:** Für jedes Gerät zeigt der Assistent, was es braucht, und öffnet mit **„Bei … einstellen“** dessen
   Webseite mit den Werten schon eingetragen (gelb markiert). Dort mit dem Passwort *dieses* Geräts anmelden, prüfen
   und „Speichern und neu starten“ drücken. Sobald sich das Gerät mit den neuen Werten meldet (bis 15 s), bekommt es im
   Assistenten einen Haken. Für den Pico, auf dem der Assistent läuft, heißt der Knopf „Hier einstellen“.
5. **Prüfen:** Der Assistent zeigt, ob der Ton bei jedem Gerät ankommt (Pakete pro Sekunde, vom richtigen Vorgänger).

Was er einstellt: Beim ersten Gerät „Audio-Stream verwenden“ an, „nur dieser Pico (Unicast)“; bei allen weiteren
Visualizern „von einem anderen Pico weitergeleitet“; bei jedem Gerät die Weiterleitung an das nächste (beim letzten
aus) und das gewählte Ultimate als Quelle (für Jukebox und SID-Adressen). Die Passwörter der Geräte bleiben, wo sie sind –
der Assistent braucht keines.

## Beispiel: Audio-Empfänger und zwei Visualizer

Das Ultimate schickt seinen Audio-Stream immer nur an **ein** Ziel. Sollen mehrere Geräte denselben Ton bekommen,
reicht jedes Gerät die Pakete unverändert an das nächste weiter – eine Kette. Am einfachsten richtet man sie mit dem
[Einricht-Assistenten](#einricht-assistent-geraete-verketten) ein; hier steht, was er von Hand einstellen würde. Das läuft im WLAN genauso flüssig wie ein
einzelner Empfänger (gemessen: jedes Glied 250 Pakete/s, Ping 2 ms), anders als Multicast.

### Beispiel 1: zwei Visualizer

```
Ultimate
   │ Ethernet
   ▼
Visualizer 1   (c64viz.local)
   │ reicht jedes Paket weiter
   ▼
Visualizer 2   (c64viz2.local)
```

1. **Gerätenamen vergeben**, damit sich die Picos im Netz finden: beim zweiten Pico unter „WLAN & Ultimate“ →
   Gerätename `c64viz2` eintragen und speichern. Der erste bleibt `c64viz`.
2. **Visualizer 1** (Tab „WLAN & Ultimate“):

| Feld | Wert |
|---|---|
| Ultimate-Geräte | das Ultimate mit seiner **Ethernet**-Adresse |
| Audio-Stream vom Ultimate verwenden | an |
| Empfänger | nur dieser Pico (Unicast) |
| Stream weiterleiten an | `c64viz2.local` |

3. **Visualizer 2:**

| Feld | Wert |
|---|---|
| Ultimate-Geräte | dasselbe Ultimate (für Jukebox und SID-Adressen; für den Ton nicht nötig) |
| Audio-Stream vom Ultimate verwenden | an |
| Empfänger | **von einem anderen Pico weitergeleitet** |
| Stream weiterleiten an | leer |

4. Beide speichern (sie starten neu) und etwas auf dem Ultimate abspielen.

### Beispiel 2: Audio-Empfänger an der Stereoanlage und zwei Visualizer

So läuft es hier: Der **Audio-Empfänger** (eigener Pico 2 W mit DAC an der Stereoanlage) holt den Stream vom Ultimate,
spielt ihn ab und reicht ihn weiter; die beiden Visualizer hängen dahinter.

```
Ultimate
   │ Ethernet
   ▼
Audio-Empfänger ──► Stereoanlage
(c64audio.local)
   │ reicht jedes Paket weiter
   ▼
Visualizer 1   (c64viz.local)
   │ reicht jedes Paket weiter
   ▼
Visualizer 2   (c64viz2.local)
```

**Audio-Empfänger** (seine eigene Webseite, http://c64audio.local):

| Feld | Wert |
|---|---|
| Ultimate-Geräte (Quellen) | das Ultimate mit seiner Ethernet-Adresse |
| Weiterleitung an den Visualizer | an, Ziel `c64viz.local` |
| Passwort der Visualizer-Webseite | das Web-Passwort von Visualizer 1 – nur nötig, damit Titelmeldungen der PC-Jukebox ankommen |

**Visualizer 1** (Tab „WLAN & Ultimate“):

| Feld | Wert |
|---|---|
| Audio-Stream vom Ultimate verwenden | **an** – sonst nimmt er keine Pakete an und reicht auch keine weiter |
| Empfänger | nur dieser Pico (Unicast) **oder** von einem anderen Pico weitergeleitet (siehe unten) |
| Stream weiterleiten an | `c64viz2.local` |

**Visualizer 2:** wie in Beispiel 1 – Stream an, Empfänger „von einem anderen Pico weitergeleitet“, Weiterleitung leer.

Bei Visualizer 1 gibt es zwei sinnvolle Einstellungen für den Empfänger:

- **Unicast:** Er fragt das Ultimate nur dann selbst nach dem Stream, wenn keine Pakete kommen. Ist der Empfänger aus,
  holt er sich den Ton also direkt vom Ultimate und reicht ihn weiter an Visualizer 2. Läuft der Empfänger wieder, holt
  der sich den Stream zurück und Visualizer 1 bekommt ihn wieder von ihm.
- **Von einem anderen Pico weitergeleitet:** Er fragt das Ultimate nie. Ist der Empfänger aus, bleiben beide Visualizer
  ohne Stream (und nutzen ihren analogen Eingang, falls angeschlossen).

### Kontrolle

| Wo | Was dort stehen sollte |
|---|---|
| Statusseite des Empfängers | „Weiterleitung an c64viz.local (192.168.…): 250 Pakete/s“ |
| Statuskasten von Visualizer 1 | Ton: „Netzwerk-Stream (250 Pakete/s)“ und „Weiterleitung an c64viz2.local: 250 Pakete/s“ |
| Statuskasten von Visualizer 2 | Ton: „Netzwerk-Stream (250 Pakete/s)“, Ultimate: „Stream kommt weitergeleitet“ |

Wenn etwas fehlt:

- **„Name wird gesucht …“** bei der Weiterleitung: Der Name des nächsten Pico ist im Netz nicht zu finden. Gerätenamen
  prüfen oder statt des Namens die IP-Adresse eintragen.
- **Visualizer 2 „wartet auf weitergeleiteten Stream“:** Beim Glied davor fehlt die Weiterleitung oder dort ist
  „Audio-Stream vom Ultimate verwenden“ aus. Ein Pico reicht nur weiter, was er selbst empfängt.
- **Pakete/s deutlich unter 250:** Multicast ist irgendwo noch eingestellt – überall Unicast bzw. „weitergeleitet“ wählen.
- **Ein Visualizer nimmt dem anderen den Stream weg** (beide wechseln sich ab): Bei einem Pico, der weitergeleitet
  bekommt, steht „Unicast“ statt „von einem anderen Pico weitergeleitet“.
- Jeder Pico nimmt nur **einen** Stream an: Kommen Pakete von zwei Absendern, zählt der erste, bis er 0,5 s lang still ist.
- Titelmeldungen der PC-Jukebox gehen mit dem Ton die Kette entlang: Visualizer 1 reicht sie an Visualizer 2 weiter,
  ohne dass dort ein Passwort eingetragen werden muss. Beim Audio-Empfänger muss dafür das Passwort von Visualizer 1
  eingetragen sein (siehe Tabelle oben).
- Den ganzen Weg auf einen Blick zeigt die Karte [Geräte im Netz](#geraete-im-netz).

## Geräte im Netz

Jeder Visualizer und jeder Audio-Empfänger meldet sich alle 10 s im Heimnetz: wer er ist, welche Firmware er hat und
wie der Ton durch ihn fließt. Die Karte **Geräte im Netz** im Tab „Steuerung“ zeigt das für alle Geräte, egal auf
welchem man die Seite gerade geöffnet hat.

- **Weg des Tons:** zum Beispiel „Elite II (192.168.1.168) → c64audio 250/s → c64viz 250/s → c64viz2 250/s“. Bekommt ein
  Weiterleitungsziel nichts, steht es rot am Ende der Kette. Verteilt eine
  Raspberry-Pi-Jukebox den Stream, verzweigt der Weg am Pi: „Elite II → c64pi 251/s → [c64audio 251/s, c64viz 251/s,
  c64viz2 250/s]“; ein Ziel des Pi, das nichts bekommt, steht dort rot. Der Pi erscheint in der Liste als
  „Raspberry Pi“, mit dem laufenden Titel.
- **Je Gerät:** Name (ein Klick öffnet seine Webseite), Art, Adresse, Firmware-Stand, ob und von wem es den Stream
  bekommt, wohin es weiterleitet und ob es Teil von zwei Matrizen ist.
- **Warnungen** (rot): eine eingetragene Weiterleitung, die nicht läuft, weil bei diesem Gerät „Audio-Stream vom
  Ultimate verwenden“ aus ist; ein Weiterleitungsziel, dessen Name nicht gefunden wird; ein Gerät, das seit über 15 s
  nichts mehr gemeldet hat.
- **Neu suchen** fragt alle Geräte sofort. Ein Gerät, das 35 s lang stumm ist (ausgeschaltet), verschwindet aus der
  Liste.

Geräte mit älterer Firmware melden sich nicht und fehlen in der Liste – nach einem Update sind sie dabei.

## Zwei Matrizen als ein Bild

Zwei Visualizer mit ihren Matrizen nebeneinander zeigen auf Wunsch **ein Bild mit 64 × 8 Punkten**: Lauftexte laufen
von einer Matrix in die andere, das Spektrum und alle anderen Modi werden doppelt so breit. Die Funktion ist
abschaltbar und ab Werk aus.

So funktioniert es: Ein Pico **rechnet** das ganze Bild, zeigt seine Hälfte und schickt die andere Hälfte jedes Bild
(62-mal pro Sekunde) an den Partner, der sie **zeigt**. Deshalb laufen die Hälften nie auseinander, auch nicht bei
Zufallseffekten wie Sternen oder Regen. Der Partner übernimmt dabei die Helligkeit des rechnenden Pico, damit beide
Hälften gleich hell sind.

**Einrichten** (Tab „Einstellungen“ → „Zwei Matrizen als ein Bild“, danach „Speichern und neu starten“):

| | Pico, der rechnet | Partner |
|---|---|---|
| Dieser Pico | rechnet das Bild für beide Matrizen | zeigt die Hälfte, die ein anderer Pico schickt |
| Partner | Name oder IP-Adresse des anderen, z. B. `c64viz2.local` | – |
| diese Matrix ist | links oder rechts | – |

Der rechnende Pico braucht den Ton (Stream oder analoger Eingang), der Partner nicht. Als rechnenden Pico nimmt man
deshalb den, der den Stream ohnehin bekommt.

**Verhalten:**

- Antwortet der Partner nicht (aus, anderes WLAN), zeichnet der rechnende Pico nach 3 s wieder nur für seine eigene
  Matrix. Meldet sich der Partner, wird das Bild nach etwa 1 s wieder breit.
- Kommen beim Partner 0,5 s lang keine Bilder, zeigt er wieder sein eigenes Bild (mit eigenem Ton und eigener
  Helligkeit).
- Beide Hälften laufen 2 Bilder (32 ms) hinter dem Ton her – die Reserve, damit kurze WLAN-Lücken nicht stören.
- Der Tab „Einstellungen“ zeigt, ob es gerade läuft („Läuft: ein Bild über beide Matrizen“), die Karte
  [Geräte im Netz](#geraete-im-netz) ebenso.
- Die beiden Picos brauchen das WLAN: 62 Pakete mit je 780 Byte pro Sekunde, das schafft jedes Heimnetz nebenbei.

## SID-Jukebox

Die Jukebox lässt das Ultimate SID-Dateien spielen und zeigt Titel, Komponist und Spielzeit auf der Matrix.

### Vom USB-Stick am Ultimate

1. „SIDs laden vom: USB-Stick am Ultimate“ wählen. Im Ultimate muss der **FTP-Dienst** eingeschaltet sein.
2. „Auswählen …“ öffnet einen Dialog, der den Speicher des Ultimate durchsucht. Zu einem Ordner durchklicken und
   „Diesen Ordner wählen“ drücken (wahlweise mit Unterordnern) oder eine einzelne SID anklicken.
3. „Abspielen“.

Der **Pico hält die Wiedergabeliste** (bis 256 Titel) und spielt sie selbstständig – auch bei geschlossener Webseite.
Ein Klick auf einen Titel der Liste spielt ihn sofort. Sehr große Ordner zeigt der Dialog bis etwa 720 Einträge, darüber
erscheint „Liste gekürzt“. Das Filterfeld hilft in großen Ordnern.

**Playlist (.json):** lädt eine mit dem Ultimate64 SID Player geteilte Playlist (`…_u64playlist.json`). Die Titel werden
per Dateiname im gewählten Ordner samt Unterordnern gesucht; was fehlt, wird übersprungen und gezählt.

### Vom PC

„SIDs laden vom: PC“, dann „Auswählen …“ (ein Ordner) oder „Einzelne Dateien …“. Die Seite schickt jede Datei an das
Ultimate. Hier lebt die Wiedergabeliste **in der geöffneten Webseite** – zum Weiterspielen die Seite offen lassen.
Ordnerauswahl klappt in Desktop-Browsern; auf dem iPhone meist nur einzelne Dateien.

### Wiedergabe steuern

| Knopf / Feld | Wirkung |
|---|---|
| ⏮ ▶ ⏭ | voriger Titel, abspielen, nächster Titel |
| Pause / Fortsetzen | hält das Ultimate an bzw. lässt es weiterlaufen |
| Stopp (Reset) | beendet die Wiedergabe und setzt das Ultimate zurück |
| Unterlied | erscheint bei Titeln mit mehreren Unterliedern |
| zufällige Reihenfolge | mischt die Liste |
| Nächster Titel nach … Minuten | Zeitlimit für Titel ohne bekannte Länge |
| oder nach 5 s Stille | springt weiter, wenn ein Titel endet und still wird |

### Songlängen

Mit einer Songlängen-Datenbank wechselt die Jukebox genau am Ende eines Titels, und die Matrix zeigt die Spielzeit.

1. Quelle wählen (USB-Stick am Ultimate oder PC).
2. Bei „HVSC-Datei“ die `Songlengths.md5` der HVSC auswählen, bei „Eigene Datei“ optional die Datei des Ultimate64 SID
   Player / der SID-Jukebox (`<md5>=<Sekunden>`). Eigene Einträge haben Vorrang.
3. „Songlängen einlesen“. Der Pico speichert die Datenbank im Flash (Platz für weit mehr als die ganze HVSC) und merkt
   sich die Pfade.

Ist die Länge eines Titels unbekannt, zeigt die Seite seine MD5, damit man sie in den Dateien nachsehen kann.
„Datenbank löschen“ entfernt nach einer Rückfrage alle Songlängen.

### SID-Adressen

- **SID-Adressen automatisch umstellen** (Standard aus): Braucht ein Stück einen SID an einer Adresse, die kein Sockel
  hat (z. B. $D500 für den zweiten SID), stellt der Pico den freien Sockel für dieses Stück um. Nur die echten SIDs in
  den Sockeln, nie die UltiSIDs, und nur im eindeutigen Fall. Zurückgestellt wird bei Stopp, beim nächsten Stück, nach
  60 s Stille und nach einem Neustart des Pico. Im Ultimate wird nichts dauerhaft gespeichert.
- **SID-Adressen auf der Matrix anzeigen:** Die Belegung läuft beim Einschalten, bei jeder Änderung (der Pico prüft
  jede Minute) und zusammen mit dem Titel über die Matrix. Der Jukebox-Tab zeigt sie immer.

### Ultimate64 SID Jukebox am PC

Spielt das PC-Programm **Ultimate64 SID Jukebox** (ab 1.82) die SIDs, meldet es dem Pico jeden Titel. Dort unter
*Settings → Preferences… → C64 Visualizer* einschalten, Adresse (`c64viz.local` oder IP) und das Web-Passwort des Pico
eintragen. Der Pico zeigt Titel, Komponist und Länge, lässt den Fortschritt laufen, schaltet nach dem SID-Kopf zwischen
Mono und Stereo und beendet eine eigene laufende Wiedergabe. Nutzt das Programm den Audio-Stream selbst, reicht es ihn
an den Pico weiter. Hängen weitere Visualizer an der Weiterleitung, bekommen sie die Titel ebenfalls (siehe
[Beispiel: Audio-Empfänger und zwei Visualizer](#beispiel-audio-empfaenger-und-zwei-visualizer)).

## Lichtsensor

Mit einem **VEML7700**-Lichtsensor passt sich die Helligkeit dem Raum an. Bei Tageslicht gilt die eingestellte
Helligkeit, im Dunkeln (unter 1 Lux) wird sie bis auf `@lightmin` Prozent abgesenkt (Standard 10), dazwischen
stufenlos so, wie das Auge Helligkeit empfindet. Heller wird es in wenigen Sekunden, dunkler langsamer.

| Raumlicht | schwach | **mittel** (Standard) | stark |
|---|---|---|---|
| 1 Lux | 10 % | 10 % | 10 % |
| 10 Lux | 34 % | 29 % | 26 % |
| 100 Lux (Wohnzimmer mit Licht) | 75 % | 58 % | 49 % |
| 300 Lux | 100 % | 77 % | 63 % |
| 1000 Lux | 100 % | 100 % | 81 % |

**Anschluss** – eine der beiden Varianten, 3Vo am Modul bleibt frei, nie an 5 V:

| Modul | Variante A | Variante B (Pin 36 schon belegt) |
|---|---|---|
| VIN | 3V3 OUT (Pin 36) | GP3 (Pin 5) – die Firmware schaltet GP3 immer auf 3,3 V |
| GND | GND (Pin 38) | GND (Pin 8) |
| SDA | GP4 (Pin 6) | GP4 (Pin 6) |
| SCL | GP5 (Pin 7) | GP5 (Pin 7) |

- Der Pico sucht alle 5 s nach dem Sensor; er darf auch später eingesteckt werden. Ohne Sensor ändert sich nichts.
- Der Sensor sieht auch das Licht der Matrix. Als Raumlicht zählt deshalb der kleinste Wert der letzten 20 s, und die
  ersten 5 s nach dem Start werden nicht gemessen. Trotzdem den Sensor so setzen, dass er die Matrix nicht direkt sieht.
- Der Statuskasten zeigt die gemessenen Lux. Findet der Pico den Sensor nicht, steht im
  [Diagnose-Protokoll](#diagnose-protokoll), woran es liegt.

## Schlaf, Uhr und Nacht

- **Schlaf:** `@sleep 30` schaltet die Matrix nach 30 Minuten Stille aus. `@sleepstyle breathe` lässt sie stattdessen
  sanft atmen, `@sleepstyle clock` zeigt die Uhrzeit, die langsam auf- und abblendet (Pico 2 W; der Pico 2 atmet dann).
  `@sleepcolor` legt die Farbe fest.
- **Uhr (Pico 2 W):** Die Uhrzeit kommt aus dem Internet (pool.ntp.org) und läuft zwischen den Lauftexten (`@clock on`).
  Ohne Internetzugang gibt es keine Uhr.
- **Retro-News (Pico 2 W):** Die Raspberry-Pi-Jukebox (`c64pi`) kann die Schlagzeilen ihrer Retro-News (Schlagzeilen aus RSS-Feeds wie
  Indie Retro News, amiga-news.de, Commodore News …) an die Picos schicken – auf der Seite des Pi im Tab „Geräte“, Karte „Picos und Stream-Verteiler“, einschalten. Im Leerlauf läuft dann nach jedem Lauftext (und der Uhr)
  eine Schlagzeile in Gelb durch, eingerahmt von `+++`. Der Pi schickt bis zu 6 Schlagzeilen alle 15 Minuten neu; kommt
  3 Stunden lang nichts, sind sie vergessen. Umlaute werden ausgeschrieben (ä → ae), Zeichen, die die Schrift der Matrix
  nicht kennt, fallen weg. Abschalten am Pico: Haken „Retro-News“ im Tab „Einstellungen“ (`@news off`). Ein Pico gibt
  die Schlagzeilen wie Titel an das Weiterleitungsziel weiter.
- **Nacht (Pico 2 W):** `@night 23:00-07:00` hält die Matrix in diesen Stunden dunkel.
- Musik weckt die Matrix immer, ein Tastendruck hält sie eine Minute wach.

## Sicherung, Update und Passwort

### Sicherung

„Sicherung herunterladen“ (Tab „WLAN & Ultimate“) speichert in einer Datei `c64viz-<name>-<datum>.json`: WLAN,
Webseiten-Passwort, Gerätename, die Ultimates, die Weiterleitung, die Jukebox-Einstellungen, Helligkeit/Taster und die
Lauftexte. „Sicherung einspielen …“ spielt sie zurück, auch auf einen anderen Pico. Der Pico prüft alles, bevor er
etwas schreibt, und startet danach neu. Die Songlängen sind nicht dabei – sie werden aus ihren Dateien neu eingelesen.
**Die Datei enthält Passwörter** – sicher aufbewahren.

### Firmware-Update über die Webseite

Tab „WLAN & Ultimate“ → „Firmware (.uf2) wählen …“ und die neue `c64viz_wifi_*.uf2` auswählen. Die Seite zeigt die
laufende Firmware (Bau-Datum) und prüft die Datei schon im Browser. Der Pico legt sie zuerst in einem freien Bereich ab,
prüft Übertragung, Startabbild und dass es eine WLAN-Firmware ist, und kopiert sie erst dann über sich selbst
(insgesamt etwa 25 s). Einstellungen, Lauftexte und Songlängen bleiben erhalten. **Während des Aufspielens nicht den
Strom trennen.** Geht doch etwas schief, startet der Pico im USB-Laufwerksmodus; dann die `.uf2` per USB kopieren.

### Alle Picos auf einmal aktualisieren

Ein Skript auf dem PC/Mac findet alle Visualizer und Audio-Empfänger im Netz und spielt jedem die passende Firmware
auf – auch die richtige Variante (normal oder Debug, USB oder Netzteil), denn jedes Gerät meldet, welche Datei es braucht:

```
python3 tools/update_all.py
```

Es listet die Geräte mit ihrem Stand („aktuell“ oder welche Datei es bekäme) und fragt vor dem Aktualisieren. Das
Web-Passwort fragt es je Gerät; auf dem Mac kann es sich die Passwörter im Schlüsselbund merken. Weitere Möglichkeiten:
`--list` (nur anzeigen), `--yes` (ohne Rückfrage), `--only c64viz2,c64audio` (nur diese), `--force` (auch aktuelle). Die
Dateien kommen aus `firmware/` dieses Projekts bzw. des Audio-Empfängers. Braucht Python mit `requests`
(`pip install requests`).

### Passwort vergessen

`firmware/wlan_reset.uf2` auf den Pico kopieren (BOOTSEL bzw. Modustaster beim Einstecken). Sie löscht nur die WLAN-,
Ultimate- und Passwort-Einstellungen; Lauftexte, Helligkeit und Songlängen bleiben. Danach öffnet der Pico wieder seinen
Zugangspunkt **C64-Visualizer** (Passwort **commodore64**).

### WLAN-Ausfälle

- Ist das Heim-WLAN beim Start nicht erreichbar, öffnet der Pico nach etwa 20 s seinen Zugangspunkt und versucht das
  Heim-WLAN alle 5 Minuten erneut – nicht, solange jemand die Seite am Zugangspunkt benutzt.
- Ein abgelehntes WLAN-Passwort öffnet den Zugangspunkt sofort.
- Vergibt der Router keine Adresse (DHCP hängt), meldet sich der Pico nach 10 s ab und neu an; den Zugangspunkt öffnet
  er erst nach drei solchen Versuchen.
- Bricht die Verbindung im Betrieb ab (Router neu gestartet), verbindet er sich nach 10 s neu, ohne Zugangspunkt.

## Fehlersuche

| Problem | Ursache / Abhilfe |
|---|---|
| Matrix bleibt dunkel | +5 V und gemeinsame Masse prüfen, Daten an das **DIN**-Ende der Matrix, GP16 |
| Flackern, falsche Farben | gemeinsame Masse fehlt, kein Pegelwandler oder Datenkabel zu lang |
| Bild gespiegelt oder gedreht | Ausrichtung in `src/config.h` anpassen (README, „Kalibrierung“) |
| Anzeige zuckt ohne Musik | Rauschen neu messen (dreimal drücken); R1 an ADC_VREF, R2 und C64-Masse an AGND; notfalls `@threshold 60` |
| Balken immer niedrig | Pegel zu klein: Poti weiter auf, oder Ausgang des C64 lauter |
| Magenta-Pixel oben rechts | Eingang übersteuert: Pegel zurückdrehen |
| Brummen bei Stille | Masseschleife (PC-USB und C64): Pico über ein USB-Ladegerät versorgen |
| Pico startet bei hoher Helligkeit neu | Netzteil zu schwach |
| Kein Ton per Stream | Ultimate-Adresse = **Ethernet**-Adresse? Statuszeile „Ultimate“ lesen; Netzwerk-Passwort richtig? |
| „Passwort falsch (403)“ | Netzwerk-Passwort des Ultimate stimmt nicht |
| Webseite langsam, Ping hoch | Multicast eingestellt – auf Unicast umstellen, für zwei Picos die Weiterleitung nutzen |
| Jukebox „Lese …“ hängt | FTP-Dienst im Ultimate einschalten; Multicast vermeiden |
| Quellenwechsel abgelehnt | Jukebox läuft noch oder ein SID-Sockel ist umgestellt – erst „Stopp“ |
| `c64viz.local` geht nicht | IP-Adresse aus dem Statuskasten oder vom Router nehmen; manche Android-Geräte kennen `.local` nicht |
| Lichtsensor wird nicht gefunden | Diagnose-Protokoll lesen (siehe unten) |

### Diagnose-Protokoll

Der Pico 2 W schreibt mit, was er tut: WLAN, jede Anfrage an das Ultimate, den FTP-Dialog der Jukebox, Lichtsensor,
Stereo und lange Bilder. Anzeigen: Tab „WLAN & Ultimate“ → **Diagnose laden** oder direkt `http://c64viz.local/diag`.
Jede Zeile beginnt mit den Sekunden seit dem Start. Die Debug-Firmware gibt dasselbe live über USB-Seriell aus
(115200 Baud, z. B. `screen /dev/tty.usbmodem* 115200`).

Häufige Zeilen:

| Zeile | Bedeutung |
|---|---|
| `wlan: verbunden mit '…', IP …` | im Heimnetz angemeldet |
| `rest: keine Antwort nach 10 s` | das Ultimate antwortet nicht (aus, falsche Adresse) |
| `rest: Antwort 403` | Netzwerk-Passwort des Ultimate falsch |
| `last: Bild stand 63 ms` | ein Bild kam verspätet, z. B. während einer Webanfrage – einzelne sind harmlos |
| `licht: VEML7700 gefunden` | Lichtsensor erkannt |
| `licht: kein Sensor - SDA (GP4) …, SCL (GP5) …` | Lichtsensor antwortet nicht; dahinter der Zustand der Leitungen |
| `zwei Matrizen: Partner antwortet - ein Bild über beide` | das breite Bild läuft |
| `zwei Matrizen: Partner antwortet nicht - nur diese Matrix` | der Partner ist weg; der rechnende Pico zeigt wieder sein eigenes Bild |
| `zwei Matrizen: Bilder vom Partner kommen` / `keine Bilder mehr` | beim Partner: zeigt die Hälfte bzw. wieder sein eigenes Bild |

Der Zustand der Sensorleitungen bedeutet:

| Anzeige | Bedeutung |
|---|---|
| `ok` | die Leitung ist mit dem Modul verbunden und das Modul hat Strom |
| `offen` | an der Leitung hängt nichts – Kabel fehlt oder steckt am falschen Pin |
| `an GND` | die Leitung wird nach Masse gezogen: Das Modul hat keinen Strom (VIN prüfen, VIN/GND vertauscht?) oder das Kabel steckt auf einem GND-Pin |

Beide `ok`, aber kein Sensor: SDA und SCL vertauscht oder das Modul hat eine andere Pinfolge – nach dem Aufdruck am
Modul richten.

## Anhang

### Anschlüsse am Pico

| Funktion | Pin |
|---|---|
| Matrix-Daten (WS2812B) | GP16 (Pin 21) |
| Audio links | GP26 (Pin 31); Bias über ADC_VREF (Pin 35) und AGND (Pin 33) |
| Audio rechts (`@stereo on`) | GP27 (Pin 32); Bias wahlweise über GP28 (Pin 34) und GND |
| Taster | GP15 (Pin 20) gegen GND |
| Laufwerks-LED des Ultimate | GP14 (Pin 19) über 1 kΩ |
| Lichtsensor | SDA GP4 (Pin 6), SCL GP5 (Pin 7), VIN an Pin 36 oder GP3 (Pin 5) |
| MAX7219 (Variante) | DIN GP19, CLK GP18, CS GP17 |

### HTTP-Schnittstelle (für eigene Programme)

Alle Anfragen brauchen im Heimnetz die Digest-Anmeldung mit dem Web-Passwort (Benutzername beliebig).

| Anfrage | Bedeutung |
|---|---|
| `GET /status` | Zustand als JSON (alles aus dem Statuskasten) |
| `POST /cmd?c=next` | nächster Modus; ebenso `camera`, `auto`, `calibrate`, `bright` |
| `POST /cmd?c=bright&p=40` | Helligkeit in Prozent (4–100) |
| `POST /cmd?c=u64pause` | Ultimate anhalten; ebenso `u64resume`, `u64reset` |
| `GET /texts`, `POST /texts` | `texts.txt` lesen bzw. speichern (danach Neustart) |
| `POST /src?i=1` | Quelle auf das zweite Ultimate stellen (0–3) |
| `GET /jb/state` | Zustand der Jukebox als JSON |
| `POST /jb/np?new=1&title=…&author=…&secs=117` | „Now Playing“ eines PC-Programms melden; `POST /jb/np?stop=1` am Ende |
| `POST /news` | Retro-News: Schlagzeilen als Text, eine je Zeile (höchstens 8 × 90 Zeichen, zusammen 760 Byte); leer = löschen; `409` = vorige noch nicht übernommen, gleich noch einmal |
| `GET /backup` | Sicherung als JSON |
| `GET /diag` | Diagnose-Protokoll als Text |
| `GET /hilfe` | dieses Handbuch |
| `GET /devices` | Geräte im Netz als JSON (`{"list":[{"self":true,"age":0,"j":"<Meldung>"},…]}`) |
| `POST /devices/query` | alle Geräte sofort fragen |

### Netzwerk-Protokolle

Alles per UDP im Heimnetz, ohne Passwort (wie der Audio-Stream des Ultimate selbst).

| Port | Inhalt |
|---|---|
| 11001 | Audio-Stream (vom Ultimate oder weitergeleitet). Zusätzlich `C64NP1 <Abfrage>`: eine Titelmeldung der PC-Jukebox, die die Kette entlang weitergereicht wird – dieselben Parameter wie `POST /jb/np`. Ebenso `C64NEWS1` + Zeilenumbruch + Schlagzeilen (wie `POST /news`) |
| 11064 | Gerätemeldungen: `C64DEV1 {JSON}` alle 10 s an alle (Broadcast), `C64DEV1?` fragt alle, jedes Gerät antwortet binnen 0,4 s |
| 11065 | zwei Matrizen: `C64WF` + Bildnummer (4 Byte) + Helligkeit % + Licht 0–255 + Flags + 32 × 8 × 3 Byte RGB (780 Byte); `C64WQ` = „bist du da?“, `C64WA` = „ja“ |

Felder einer Gerätemeldung: `t` Art (`viz`, `audio`), `n` Name, `ip`, `b` Firmware-Stand, `img` passende Firmware-Datei,
`up` Sekunden seit dem Start, `st` Stream an, `sm` Empfänger-Modus, `rx` Pakete/s, `from` Absender des Streams, `fw`
Weiterleitungsziel, `fwip` seine Adresse, `fp` weitergeleitete Pakete/s, `src`/`srcl` gewähltes Ultimate, `wide`
(`lead`/`follow`), `wpart`, `wside`, `wok` (zwei Matrizen laufen).
