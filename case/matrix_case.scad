// =====================================================================
// Flat case for a WS2812B 8x32 flexible LED matrix (320 x 80 mm, 5050 LEDs)
// Flaches Gehaeuse fuer eine WS2812B-8x32-Flex-Matrix
//
// Two parts / Zwei Teile:
//   LID  (Oberseite): one closed 1 mm diffuser surface + light grid underneath
//   TRAY (Unterseite): holds the panel, cable openings in the floor
// Both parts can be split into two halves for printers smaller than ~330 mm.
//
// Render one part:   openscad -D 'part="lid_left"' -o lid_left.stl matrix_case.scad
// part = "assembly" | "lid" | "tray" | "lid_left" | "lid_right" | "tray_left" | "tray_right"
// =====================================================================

part = "assembly";

/* ---- your panel: MEASURE IT and adjust / Panel ausmessen und anpassen ---- */
panel_l   = 320;    // length of the flexible PCB (mm)
panel_w   = 80;     // width
panel_t   = 2.0;    // total thickness incl. LEDs
led_h     = 1.6;    // height of the 5050 LEDs above the PCB
pitch     = 10;     // LED pitch
cols      = 32;
rows      = 8;

/* ---- case ---- */
clr       = 0.4;    // play around the panel (each side)
wall      = 1.6;    // side walls of the tray
floor_t   = 2.4;    // tray floor = height of the dovetail joint (was 1.2; 2.4 doubles it)
top_t     = 1.0;    // diffuser thickness (light shines through) / Streuscheibe
gap       = 3.0;    // air between LED tops and diffuser: bigger = more even pixels, 0 = thinnest
grid      = true;   // light grid between the pixels (sharp square pixels)
rib       = 0.8;    // grid rib thickness
lip_w     = 1.6;    // lid rim that presses the panel border down (panel border is 2.5 mm wide)
fit       = 0.2;    // play between lid rim and tray wall

/* ---- cable openings in the tray floor (wires are soldered on the back at both ends) ---- */
slot_both_ends = true;
slot_l    = 40;     // along the panel
slot_w    = 44;     // across the panel
slot_from_end = 6;  // distance from the panel end

/* ---- splitting for smaller printers ---- */
lid_split_x  = 160;  // lid seam sits on the grid line between column 16 and 17
seam_rib     = 2.0;  // double-thick rib at the lid seam = glue surface
tray_split_x = 150;  // tray seam (offset from the lid seam for stiffness), dovetail joint
dt_neck = 8; dt_head = 12; dt_depth = 8; dt_clr = 0.15;
dt_y = [wall + 9, (panel_w + 2 * clr + 2 * wall) / 2, panel_w + 2 * clr + wall - 9];   // tab positions across the width

/* ---- extra cable opening at the tray seam, between the middle and the upper dovetail (middle power wires) ---- */
mid_opening   = true;
mid_open_l    = 20;    // length along the panel, starting at the seam towards the right half
mid_open_marg = 2.5;   // material left next to the dovetail sockets

// ---------------------------------------------------------------------
in_l  = panel_l + 2 * clr;
in_w  = panel_w + 2 * clr;
out_l = in_l + 2 * wall;
out_w = in_w + 2 * wall;
z_pcb = floor_t + (panel_t - led_h);     // top of the PCB
z_led = floor_t + panel_t;               // top of the LEDs
z_lid = z_led + gap;                     // underside of the diffuser
total_h = z_lid + top_t;
px0 = wall + clr;                        // panel origin
py0 = wall + clr;
echo(str("Outer size: ", out_l, " x ", out_w, " x ", total_h, " mm"));

module tray() {
    difference() {
        cube([out_l, out_w, z_lid]);
        translate([wall, wall, floor_t]) cube([in_l, in_w, z_lid]);
        for (end = slot_both_ends ? [0, 1] : [0]) {
            sx = end == 0 ? px0 + slot_from_end : px0 + panel_l - slot_from_end - slot_l;
            translate([sx, (out_w - slot_w) / 2, -1]) cube([slot_l, slot_w, floor_t + 2]);
        }
        // opening for the wires in the middle of the panel: between the middle and the upper dovetail,
        // open towards the seam (the straight edge of the left half closes it when glued)
        if (mid_opening) {
            y0 = dt_y[1] + dt_head / 2 + dt_clr + mid_open_marg;
            y1 = dt_y[2] - dt_head / 2 - dt_clr - mid_open_marg;
            translate([px0 + tray_split_x, y0, -1]) cube([mid_open_l, y1 - y0, floor_t + 2]);
        }
    }
}

module lid() {
    // diffuser plate over the full footprint
    translate([0, 0, z_lid]) cube([out_l, out_w, top_t]);
    // rim: fits inside the tray walls, presses the panel border onto the floor
    difference() {
        translate([wall + fit, wall + fit, z_pcb]) cube([in_l - 2 * fit, in_w - 2 * fit, z_lid - z_pcb + 0.01]);
        translate([px0 + lip_w, py0 + lip_w, z_pcb - 1]) cube([panel_l - 2 * lip_w, panel_w - 2 * lip_w, z_lid]);
    }
    // light grid in the air gap above the LEDs
    if (grid && gap > 0.6) {
        gz = z_led + 0.2;
        for (i = [1 : cols - 1]) {
            t = (i * pitch == lid_split_x) ? seam_rib : rib;
            translate([px0 + i * pitch - t / 2, py0 + lip_w, gz]) cube([t, panel_w - 2 * lip_w, z_lid - gz + 0.01]);
        }
        for (j = [1 : rows - 1])
            translate([px0 + lip_w, py0 + j * pitch - rib / 2, gz]) cube([panel_l - 2 * lip_w, rib, z_lid - gz + 0.01]);
    }
}

// left region of the tray split with dovetail tabs (2D, extends beyond the part)
module tray_left_2d(grow = 0) {
    sx = px0 + tray_split_x;
    offset(delta = grow) union() {
        translate([-10, -10]) square([sx + 10, out_w + 20]);
        for (y = dt_y)
            polygon([[sx - 0.01, y - dt_neck / 2], [sx + dt_depth, y - dt_head / 2],
                     [sx + dt_depth, y + dt_head / 2], [sx - 0.01, y + dt_neck / 2]]);
    }
}

module half_x(xcut, keep_left) {
    if (keep_left) translate([-10, -10, -10]) cube([xcut + 10, out_w + 20, 50]);
    else           translate([xcut, -10, -10]) cube([out_l, out_w + 20, 50]);
}

// print orientation: tray floor down, lid diffuser face down (smooth top surface, no supports)
module print_lid()  { translate([0, out_w, total_h]) rotate([180, 0, 0]) children(); }

if (part == "assembly") {
    color("#303030") tray();
    color("#f4f4f0", 0.85) translate([0, 0, 12]) lid();
    %translate([px0, py0, floor_t]) cube([panel_l, panel_w, panel_t - led_h]);
}
if (part == "tray")       tray();
if (part == "lid")        print_lid() lid();
if (part == "tray_left")  intersection() { tray(); linear_extrude(50, center = true) tray_left_2d(0); }
if (part == "tray_right") difference()   { tray(); linear_extrude(60, center = true) tray_left_2d(dt_clr); }
if (part == "lid_left")   print_lid() intersection() { lid(); half_x(px0 + lid_split_x, true); }
if (part == "lid_right")  print_lid() intersection() { lid(); half_x(px0 + lid_split_x, false); }
