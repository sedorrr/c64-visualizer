// =====================================================================
// Monitor mount: the middle clamp of the matrix case with a clamp for the top edge of a monitor and a cradle
// for the Pico case. The matrix stands upright on the monitor (LEDs towards the viewer); in front only a 9 mm lip
// reaches down over the monitor's top bezel, behind it a hook with a thumb screw clamps monitors 10-40 mm thick.
// The Pico case lies in the cradle behind the matrix (invisible from the front), its ends stay free (USB, wires).
// Monitorhalterung: mittlere Klammer des Matrix-Gehaeuses + Klemme fuer die Monitoroberkante + Mulde fuer das
// Pico-Gehaeuse.
//
// Everything is one profile along the clamp (40 mm): printed standing on one end face, no supports.
// Hardware: M4x40 hex screw + M4 nut (nut slides into the slot from the top during assembly).
//
// openscad -D 'part="mount"' -o monitor_mount.stl monitor_mount.scad
// part = "mount" | "knob" | "pad" | "assembly" | "section"
// =====================================================================
use <clamp.scad>
part = "assembly";

/* ---- monitor ---- */
mon_min   = 10;     // thinnest monitor edge (front to back, where the screw presses)
mon_max   = 40;     // thickest
lip_down  = 9;      // front lip below the monitor top: at most 10 mm (picture area)
lip_t     = 2.0;    // front lip thickness
saddle_t  = 3.0;    // plate lying on the monitor top
hook_down = 32;     // back hook below the monitor top
hook_t    = 6.0;
screw_y   = 14;     // the screw presses this far below the monitor top
screw_d   = 4.4;    // M4 clearance
nut_af    = 7.3;    // M4 nut across flats + play
nut_t     = 3.6;

/* ---- Pico case (pico_case.scad: 65.8 x 25.8 x 32.7 mm) ---- */
pico_l = 65.8;  pico_w = 25.8;  pico_h = 32.7;
cr_play  = 0.3;     // play around the case in the cradle
cr_wall  = 2.5;
cr_h     = 12;      // height of the cradle's back wall
cr_crush = 0.3;     // crush ribs: hold the case without glue

G = clamp_geom();   // [x0, c_len, case width, c_play, c_side, c_bot, ih, c_hook_t]
cx0 = G[0]; clen = G[1]; cw = G[2]; cplay = G[3]; cside = G[4]; cbot = G[5]; cih = G[6]; chook = G[7];

/* positions in the clamp's coordinates: x along the matrix, y across it (up when standing), z through it (+ = front) */
yb      = -cplay - cside;                 // outer face of the lower cheek = the matrix's lowest edge
y_mon   = yb - saddle_t;                  // monitor top surface
z_front = cih + chook;                    // front face of the clamp hooks
z_mon   = z_front - lip_t;                // monitor front face
z_hin   = z_mon - mon_max - 1;            // back hook, inner face
z_hout  = z_hin - hook_t;
z_crf   = -cbot;                          // cradle front = the clamp's back plate
z_crb   = z_crf - 2 * cr_play - pico_w;   // cradle back wall, inner face
y_screw = y_mon - screw_y;
xc      = cx0 + clen / 2;

$fn = 48;

module mount_2d() {                      // the profile in (y, z)
    // saddle on the monitor top
    translate([y_mon, z_hout]) square([saddle_t + 0.01, z_front - z_hout]);
    // front lip over the bezel
    translate([y_mon - lip_down, z_mon]) square([lip_down + saddle_t + 0.01, lip_t]);
    // back hook
    translate([y_mon - hook_down, z_hout]) square([hook_down + saddle_t + 0.01, hook_t]);
    // small fillet in the inner corner saddle/hook (stiffer; monitors are rounded there anyway)
    polygon([[y_mon + 0.01, z_hin - 0.01], [y_mon - 3, z_hin - 0.01], [y_mon + 0.01, z_hin + 3]]);
    // cradle back wall + crush ribs
    translate([yb - 0.01, z_crb - cr_wall]) square([cr_h + 0.01, cr_wall]);
    for (y = [yb + 4, yb + cr_h - 3])
        polygon([[y - 0.7, z_crb - 0.01], [y + 0.7, z_crb - 0.01], [y, z_crb + cr_play + cr_crush]]);
}

module mount() {
    difference() {
        union() {
            clamp(false);
            translate([cx0, 0, 0]) rotate([90, 0, 90]) linear_extrude(clen) mount_2d();
        }
        // thumb screw through the back hook, nut slot open towards +x (the top when printing)
        translate([xc, y_screw, z_hout - 1]) cylinder(d = screw_d, h = hook_t + 2);
        translate([xc, y_screw, z_hout + (hook_t - nut_t) / 2]) hull() {
            cylinder(d = nut_af / cos(30), h = nut_t, $fn = 6);
            translate([clen, 0, 0]) cylinder(d = nut_af / cos(30), h = nut_t, $fn = 6);
        }
    }
}

/* thumb knob for the M4 hex head (pressed on) and the pad that presses on the monitor's back (turned onto the tip) */
module knob() {
    difference() {
        union() {
            cylinder(d = 20, h = 8);
            for (a = [0 : 30 : 359]) rotate(a) translate([10, 0, 0]) cylinder(d = 3, h = 8, $fn = 16);   // grip
        }
        translate([0, 0, 8 - 3.2]) cylinder(d = 7.25 / cos(30), h = 4, $fn = 6);   // hex head M4 (7 mm), press fit
        translate([0, 0, -1]) cylinder(d = screw_d, h = 10);
    }
}
module pad() {
    difference() {
        cylinder(d = 14, h = 5);
        translate([0, 0, 1.5]) cylinder(d = 3.4, h = 4);                         // the screw cuts its thread into it
    }
}

module monitor_dummy(t = 25) {
    color("#222831") translate([cx0 - 120, y_mon - 150, z_mon - t]) cube([clen + 240, 150, t]);
    color("#4a5a70") translate([cx0 - 110, y_mon - 150, z_mon]) cube([clen + 220, 140, 0.4]);   // picture area
}
module pico_dummy() {
    color("#2e7d32", 0.9) translate([xc - pico_l / 2, yb, z_crb + cr_play]) cube([pico_l, pico_h, pico_w]);
}
module screw_dummy(t = 25) {                 // M4x40: tip 3.5 mm in the pad, the pad on the monitor's back
    tip = z_mon - t - 5 + 3.5;
    color("#bbbbbb") translate([xc, y_screw, tip - 40]) cylinder(d = 4, h = 40);
    color("#e65100") translate([xc, y_screw, z_mon - t - 5]) pad();
    color("#e65100") translate([xc, y_screw, tip - 40 - 5]) knob();
}

if (part == "mount")    rotate([0, -90, 0]) translate([-cx0, 0, 0]) mount();   // standing on its end face, nut slot on top
if (part == "mount_raw") mount();
if (part == "knob")     knob();
if (part == "pad")      pad();
/* previews upright as on the monitor: x along, y (across the matrix) up, z (front) towards the viewer (-Y) */
module upright() { multmatrix([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]]) children(); }
module scene() { matrix_case_preview(); color("#e65100") mount(); pico_dummy(); monitor_dummy(); screw_dummy(); }
if (part == "assembly") upright() scene();
module cut() { difference() { children(); translate([xc, -400, -400]) cube([400, 800, 800]); } }
if (part == "section") upright() {        // cut through the screw, seen from the side
    cut() matrix_case_preview();
    color("#e65100") cut() mount();
    color("#2e7d32") cut() pico_dummy();
    color("#222831") cut() monitor_dummy();
    color("#9e9e9e") cut() screw_dummy();
}
