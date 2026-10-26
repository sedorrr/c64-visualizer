// =====================================================================
// Screwless monitor mount: two clips that slide onto the matrix case beside the middle clamp (e.g. the one with
// the light sensor), each with a clamp for the top edge of a monitor. The matrix stands upright on the monitor;
// in front only a 9 mm lip reaches over the bezel, behind it a jaw slides on a toothed rail and locks like a cable
// tie: push it against the monitor's back, it clicks; to release lift the tab. Monitors 10-40 mm thick.
// One clip (part "clip_pico") has a cradle for the Pico case at half the matrix height, the other ("clip") not.
// Schraubenlose Monitorhalterung: zwei Klemmen neben der mittleren Klammer, Rastschieber statt Schraube.
//
// openscad -D 'part="clip"' -o monitor_clip.stl monitor_clip.scad
// part = "clip" | "clip_pico" | "jaw" | "assembly" | "section"
// =====================================================================
use <clamp.scad>
part = "assembly";

L        = 30;      // clip length along the matrix
/* ---- monitor ---- */
mon_min  = 10;      // thinnest monitor (front to back, top edge)
mon_max  = 40;      // thickest
lip_down = 9;       // front lip below the monitor top: at most 10 mm (picture area)
lip_t    = 2.0;
saddle_t = 3.0;     // rail = the plate lying on the monitor
/* ---- ratchet ---- */
pitch    = 2.0;     // teeth on the rail
tooth_h  = 1.0;
j_len    = 24;      // jaw: sliding sleeve length
j_wall   = 2.5;
j_play   = 0.3;
hook_down = 30;     // jaw hook below the monitor top
hook_t   = 6;
tongue_w = 10;  tongue_l = 16;  tongue_t = 1.6;   // pawl tongue (thinner than the wall: springs, does not break)
/* ---- Pico case cradle (pico_case.scad: 65.8 x 25.8 x 32.7 mm) ---- */
pico_l = 65.8;  pico_w = 25.8;  pico_h = 32.7;
cr_up    = 40;      // cradle floor above the matrix's lower edge (the jaw's tab stays reachable below it)
cr_play  = 0.3;  cr_wall = 2.5;  cr_h = 12;  cr_crush = 0.3;
crush    = 0.3;     // crush ribs inside the clip (as clamp.scad)

G = clamp_geom();   // [x0, c_len, case width, c_play, c_side, c_bot, ih, c_hook_t]
cw = G[2]; cplay = G[3]; cside = G[4]; cbot = G[5]; cih = G[6]; chook = G[7];

/* clip coordinates as clamp.scad: x along the matrix, y across it (up when standing), z through it (+ = front) */
yb      = -cplay - cside;                 // lower cheek, outer face = lowest edge of the matrix
y_mon   = yb - saddle_t;                  // monitor top
z_front = cih + chook;
z_mon   = z_front - lip_t;                // monitor front
z_back  = z_mon - mon_max - j_len - 4;    // rail end
z_crf   = -cbot;
z_crb   = z_crf - 2 * cr_play - pico_w;
ys      = yb + cr_up;                     // cradle floor

$fn = 48;

module profile_2d(pico) {                 // everything that runs the clip's full length, in (y, z)
    translate([y_mon, z_back]) square([saddle_t + 0.01, z_front - z_back]);             // rail / saddle
    translate([y_mon - lip_down, z_mon]) square([lip_down + saddle_t + 0.01, lip_t]);   // front lip
    for (z = [z_back + 3 : pitch : -2 - pitch])                                          // teeth: ramp towards the front
        polygon([[yb - 0.01, z], [yb + tooth_h, z + pitch], [yb - 0.01, z + pitch]]);
    if (pico) {
        translate([ys, z_crb - cr_wall]) square([cr_wall, z_crf - z_crb + cr_wall + 0.01]);       // floor
        translate([ys, z_crb - cr_wall]) square([cr_wall + cr_h, cr_wall]);                       // back wall
        for (y = [ys + cr_wall + 3, ys + cr_wall + cr_h - 3])                                     // crush ribs
            polygon([[y - 0.7, z_crb - 0.01], [y + 0.7, z_crb - 0.01], [y, z_crb + cr_play + cr_crush]]);
        polygon([[ys + 0.01, z_crf + 0.01], [ys + 0.01, z_crf - 16], [ys - 16, z_crf + 0.01]]);   // gusset
    }
}

module clip(pico = false, x0 = 0) {
    translate([x0, yb, 0]) rotate([90, 0, 90]) linear_extrude(L) clamp_profile_2d();   // its origin: lower cheek, outer face
    for (x = [x0 + 6, x0 + L - 6], s = [0, 1])                                       // crush ribs on the cheeks
        translate([x, s == 0 ? -cplay : cw + cplay, 1]) rotate([0, 0, s == 0 ? 0 : 180])
            linear_extrude(cih - 2) polygon([[-0.6, 0], [0.6, 0], [0, crush + cplay]]);
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(L) profile_2d(pico);
}

/* jaw: a sleeve around the rail, the hook below it; zf = its pressing face (the monitor's back) */
module jaw(x0 = 0, zf = z_mon - 25) {
    xa = x0 - j_play - j_wall; xw = L + 2 * (j_play + j_wall);
    yl = y_mon - j_play - j_wall;  yt = yb + tooth_h + j_play;      // sleeve outside bottom / inside top
    xc = x0 + L / 2;  zt = zf - 5;                                   // pawl tooth position
    difference() {
        union() {
            translate([xa, yl, zf - j_len]) cube([xw, yt + j_wall - yl, j_len]);                   // sleeve
            translate([xa, y_mon - hook_down, zf - hook_t]) cube([xw, yl - (y_mon - hook_down) + 0.01, hook_t]);   // hook
            translate([xc - tongue_w / 2, yt + j_wall - 0.01, zf - 5]) cube([tongue_w, 6, 2]);     // release tab
        }
        translate([x0 - j_play, y_mon - j_play, zf - j_len - 1]) cube([L + 2 * j_play, yt - y_mon + j_play, j_len + 2]);   // channel
        // tongue: U-shaped cut, fixed at the back end, free at the front end
        for (s = [-1, 1]) translate([xc + s * (tongue_w / 2 + 0.5) - 0.5, yt - 1, zf - 3 - tongue_l]) cube([1, j_wall + 2, tongue_l + 1]);
        translate([xc - tongue_w / 2 - 1, yt - 1, zf - 3]) cube([tongue_w + 2, j_wall + 2, 1]);
        translate([xc - tongue_w / 2 - 0.01, yt + tongue_t, zf - 3 - tongue_l]) cube([tongue_w + 0.02, j_wall, tongue_l - 4]);   // thinner: springs
    }
    // pawl tooth under the tongue tip: upright face at the back (blocks), ramp at the front (slides on)
    translate([xc - tongue_w / 2, 0, 0]) rotate([90, 0, 90]) linear_extrude(tongue_w)
        polygon([[yt + 0.01, zt], [yb + 0.15, zt], [yt + 0.01, zt + pitch]]);
}

/* ---- previews ---- */
module monitor_dummy(t = 25) {
    color("#222831") translate([-30, y_mon - 150, z_mon - t]) cube([380, 150, t]);
    color("#4a5a70") translate([-20, y_mon - 140, z_mon]) cube([360, 130, 0.4]);
}
module pico_dummy(x0) { color("#2e7d32", 0.9) translate([x0 + L / 2 - pico_l / 2, ys + cr_wall, z_crb + cr_play]) cube([pico_l, pico_h, pico_w]); }
module upright() { multmatrix([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]]) children(); }
xa = 50;  xb = 324 - 50 - L;                 // clip positions in the preview (the middle clamp sits at ~136-176)
module scene(t = 25) {
    matrix_case_preview();
    color("#e65100") { clip(true, xa); clip(false, xb); }
    color("#ffb74d") { jaw(xa, z_mon - t); jaw(xb, z_mon - t); }
    pico_dummy(xa); monitor_dummy(t);
}
module cut(x) { difference() { children(); translate([x, -400, -400]) cube([400, 800, 800]); } }

if (part == "clip")      rotate([0, -90, 0]) clip(false);              // standing on its end face
if (part == "clip_pico") rotate([0, -90, 0]) clip(true);
if (part == "jaw")       rotate([0, -90, 0]) translate([j_play + j_wall, 0, 0]) jaw();   // on its side
if (part == "assembly")  upright() scene();
if (part == "section")   upright() {      // through the left clip's tongue
    cut(xa + L / 2) matrix_case_preview();
    color("#e65100") cut(xa + L / 2) clip(true, xa);
    color("#ffb74d") cut(xa + L / 2) jaw(xa, z_mon - 25);
    color("#2e7d32") cut(xa + L / 2) pico_dummy(xa);
    color("#222831") cut(xa + L / 2) monitor_dummy();
}
