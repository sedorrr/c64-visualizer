// =====================================================================
// Case for a Raspberry Pi Pico 2 with soldered pin headers (pins pointing DOWN)
// and DuPont jumper wires plugged onto the pins.
// Gehaeuse fuer einen Pico 2 mit angeloeteten Stiftleisten (Pins nach UNTEN)
// und aufgesteckten DuPont-Kabeln.
//
//   TRAY: Pico rests on 4 standoffs with pegs; the DuPont plugs hang into the space
//         below the board; the wires leave through a slot in the end wall.
//   LID:  closes the top, presses the Pico down with 4 posts, window over BOOTSEL + LED.
//
// openscad -D 'part="tray"' -o pico_tray.stl pico_case.scad
// part = "assembly" | "tray" | "lid"
// =====================================================================

part = "assembly";

/* ---- Pico 2 (official mechanical drawing) ---- */
pico_l = 51;  pico_w = 21;  pico_t = 1.0;
hole_x = [2.0, 49.0];  hole_y = [4.8, 16.2];  hole_d = 2.1;   // mounting holes, from the USB end
usb_w_open = 12;   // opening for the plug (overmold), not just the socket
usb_h_open = 7.5;
usb_below  = 2.2;  // opening reaches this far below the board top (plug overmold is centred on the socket)
bootsel_x  = 12.5; // centre of the BOOTSEL window, from the USB end (button sits ~12-13 mm in)
bootsel_win = [12, 11];

/* ---- what hangs below the board: MEASURE (header plastic + DuPont plug + wire bend) ---- */
under_pcb = 24;    // 2.5 header plastic + 14 DuPont housing + ~7 mm for the wires to bend sideways
above_pcb = 4.5;   // space above the board top (USB socket ~3 mm)

/* ---- case ---- */
wall = 2.0;  floor_t = 1.6;  top_t = 1.6;
clr  = 0.4;        // around the board
extra_l = 10;      // extra room beyond the far end of the Pico (small parts, wire bends)
fit  = 0.2;        // lid rim play
lip_h = 3.0;       // lid rim depth

/* ---- wire exit ---- */
exit_end  = true;  // slot in the end wall opposite the USB socket
exit_side = false; // additional slot in one long side
exit_w = 20;  exit_h = 12;

// ---------------------------------------------------------------------
in_l = pico_l + 2 * clr + extra_l;
in_w = pico_w + 2 * clr;
out_l = in_l + 2 * wall;
out_w = in_w + 2 * wall;
z_pcb_bot = floor_t + under_pcb;
z_pcb_top = z_pcb_bot + pico_t;
z_lid     = z_pcb_top + above_pcb;          // lid underside = tray wall top
total_h   = z_lid + top_t;
bx = wall + clr;  by = wall + clr;          // board origin (USB end at x = bx)
echo(str("Pico case outer size: ", out_l, " x ", out_w, " x ", total_h, " mm"));

module tray() {
    difference() {
        cube([out_l, out_w, z_lid]);
        translate([wall, wall, floor_t]) cube([in_l, in_w, z_lid]);
        // USB opening (open towards the top, the lid closes it)
        translate([-1, by + pico_w / 2 - usb_w_open / 2, z_pcb_top - usb_below]) cube([wall + 2, usb_w_open, z_lid]);
        // wire exits at floor level
        if (exit_end)  translate([out_l - wall - 1, (out_w - exit_w) / 2, floor_t]) cube([wall + 2, exit_w, exit_h]);
        if (exit_side) translate([bx + pico_l / 2 - exit_w / 2, -1, floor_t]) cube([exit_w, wall + 2, exit_h]);
    }
    // standoffs with pegs into the mounting holes (thin enough to clear the DuPont plugs in the pin rows)
    for (x = hole_x, y = hole_y) translate([bx + x, by + y, 0]) {
        cylinder(d = 3.2, h = z_pcb_bot, $fn = 32);
        cylinder(d1 = 7, d2 = 3.2, h = 6, $fn = 32);          // wide foot below the DuPont plugs: stiffer, prints better
        cylinder(d = hole_d - 0.35, h = z_pcb_bot + pico_t + 0.8, $fn = 24);
    }
}

module lid() {
    difference() {
        union() {
            translate([0, 0, z_lid]) cube([out_l, out_w, top_t]);
            // rim inside the walls on three sides (the USB end stays open for the plug)
            difference() {
                translate([wall + fit, wall + fit, z_lid - lip_h]) cube([in_l - 2 * fit, in_w - 2 * fit, lip_h + 0.01]);
                translate([wall + fit - 1, wall + fit + 1.2, z_lid - lip_h - 1]) cube([in_l - 2 * fit - 0.2, in_w - 2 * fit - 2.4, lip_h + 2]);   // open at the USB end
            }
            // posts pressing the board onto the standoffs (hollow: the pegs poke into them)
            for (x = hole_x, y = hole_y) translate([bx + x, by + y, z_pcb_top + 0.1])
                cylinder(d = 3.2, h = z_lid - z_pcb_top, $fn = 32);
        }
        for (x = hole_x, y = hole_y) translate([bx + x, by + y, z_pcb_top - 1]) cylinder(d = hole_d + 0.3, h = 2.0, $fn = 24);
        // BOOTSEL / LED window
        translate([bx + bootsel_x - bootsel_win[0] / 2, by + pico_w / 2 - bootsel_win[1] / 2, z_lid - 1])
            cube([bootsel_win[0], bootsel_win[1], top_t + 2]);
    }
}

module pico_dummy() {
    color("#2e7d32") translate([bx, by, z_pcb_bot]) difference() {
        cube([pico_l, pico_w, pico_t]);
        for (x = hole_x, y = hole_y) translate([x, y, -1]) cylinder(d = hole_d, h = 3, $fn = 16);
    }
    color("#bbbbbb") translate([bx - 1.3, by + pico_w / 2 - 4, z_pcb_top]) cube([5.7, 8, 2.8]);   // USB socket
    for (i = [0:19], row = [1.61, pico_w - 1.61]) {                                              // header + DuPont plugs
        color("#222222") translate([bx + 1.37 + i * 2.54 - 1.27, by + row - 1.27, z_pcb_bot - 2.5]) cube([2.54, 2.54, 2.5]);
        color("#1565c0") translate([bx + 1.37 + i * 2.54 - 1.25, by + row - 1.25, z_pcb_bot - 2.5 - 14]) cube([2.5, 2.5, 14]);
    }
}

// print orientation: lid upside down (top face on the bed)
module print_lid() { translate([0, out_w, total_h]) rotate([180, 0, 0]) children(); }

if (part == "assembly") { color("#3a3a3a") tray(); pico_dummy(); color("#9e9e9e", 0.6) translate([0, 0, 15]) lid(); }
if (part == "tray") tray();
if (part == "lid")  print_lid() lid();
if (part == "check") { tray(); pico_dummy(); lid(); }
