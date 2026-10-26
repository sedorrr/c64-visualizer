// =====================================================================
// Clamp for the matrix case: a C-profile that slides over the assembled case from one
// end and sits over the tray seam (dovetails) and the lid seam. It grips the floor,
// both side walls and the outer 1.4 mm of the lid (outside the pixel area).
// Klammer fuer das Matrix-Gehaeuse: C-Profil, wird vom Ende her ueber das zusammengebaute
// Gehaeuse geschoben und sitzt ueber der Naht (Schwalbenschwaenze) der Unterseite und der Deckelnaht.
//
// part = "clamp" | "clamp_wings" | "endcap" | "endcap_wings" | "assembly"
// clamp_wings: wide side wings with gussets and M3 holes, braces the case sideways and
//              lets you screw it to the back of the acrylic housing.
// =====================================================================
include <matrix_case.scad>
part = "assembly";

c_len    = 40;     // length along the case
c_play   = 0.2;    // play around the case
c_bot    = 1.6;    // material under the floor
c_side   = 2.0;    // side cheeks
c_hook   = 1.4;    // how far the hooks reach over the lid edge (pixel area starts 2 mm in)
c_hook_t = 1.2;    // hook thickness above the lid
wing_w   = 14;     // clamp_wings: wing width beyond the cheeks
wing_t   = 2.4;
screw_d  = 3.4;    // M3 clearance

xc = px0 + tray_split_x + dt_depth / 2;          // centre over the dovetails
x0 = xc - c_len / 2;
iw = out_w + 2 * c_play;                         // inner width
ih = total_h + c_play;                           // inner height

module clamp_profile_2d() {                      // in the YZ plane, y = 0 at the outer side of the left cheek
    difference() {
        union() {
            translate([0, -c_bot]) square([iw + 2 * c_side, c_bot + ih + c_hook_t]);
        }
        translate([c_side, 0]) square([iw, ih]);                                     // the case
        translate([c_side + c_hook, ih - 0.01]) square([iw - 2 * c_hook, c_hook_t + 1]);   // open front between the hooks
    }
}

module clamp(wings = false) {
    difference() {
        union() {
            translate([x0, -c_side - c_play, 0]) rotate([90, 0, 90]) linear_extrude(c_len) clamp_profile_2d();
            for (x = [x0 + 8, x0 + c_len / 2, x0 + c_len - 8], s = [0, 1])                       // crush ribs
                translate([x, s == 0 ? -c_play : out_w + c_play, 1]) rotate([0, 0, s == 0 ? 0 : 180])
                    linear_extrude(ih - 2) polygon([[-0.6, 0], [0.6, 0], [0, crush + c_play]]);
            if (wings) for (s = [0, 1]) {
                yw = s == 0 ? -c_side - c_play - wing_w : out_w + c_play + c_side;
                translate([x0, s == 0 ? yw : yw - 0.5, -c_bot]) cube([c_len, wing_w + 0.5, wing_t]);   // 0.5 mm overlap into the cheek
                // gussets at both ends of each wing
                for (xg = [x0, x0 + c_len - 2])
                    translate([xg, 0, 0]) rotate([90, 0, 90]) linear_extrude(2)
                        polygon(s == 0 ? [[yw, -c_bot + wing_t], [-c_side - c_play + 0.5, -c_bot + wing_t], [-c_side - c_play + 0.5, ih * 0.8]]
                                       : [[yw + wing_w, -c_bot + wing_t], [out_w + c_play + c_side - 0.5, -c_bot + wing_t], [out_w + c_play + c_side - 0.5, ih * 0.8]]);
            }
        }
        // keep the middle cable opening of the tray free (with 1 mm extra)
        if (mid_opening) {
            y0 = dt_y[1] + dt_head / 2 + dt_clr + mid_open_marg;
            y1 = dt_y[2] - dt_head / 2 - dt_clr - mid_open_marg;
            translate([px0 + tray_split_x - 1, y0 - 1, -c_bot - 1]) cube([mid_open_l + 2, y1 - y0 + 2, c_bot + 2]);
        }
        if (wings) for (s = [0, 1], xh = [x0 + 9, x0 + c_len - 9]) {
            yh = s == 0 ? -c_side - c_play - wing_w / 2 : out_w + c_play + c_side + wing_w / 2;
            translate([xh, yh, -c_bot - 1]) cylinder(d = screw_d, h = wing_t + 2, $fn = 24);
        }
    }
}

/* ---- end caps ---- */
cap_len  = 25;     // how far the cap reaches over the case
cap_end  = 2.0;    // thickness of the end wall
crush    = 0.3;    // crush ribs on the inside of the cheeks: they deform a little and hold the cap firmly

// end cap for the case end at x = 0 (the other end uses the same part, turned round)
module end_cap(wings = false) {
    yl = -c_side - c_play;                       // outer side of the left cheek
    wy = iw + 2 * c_side;                        // outer width
    difference() {
        union() {
            translate([0, yl, 0]) rotate([90, 0, 90]) linear_extrude(cap_len) clamp_profile_2d();
            translate([-cap_end, yl, -c_bot]) cube([cap_end, wy, c_bot + ih + c_hook_t]);          // end wall
            translate([-0.01, yl, ih]) cube([c_hook + 0.01, wy, c_hook_t]);                        // hook over the lid end
            for (x = [6, 13, 20], s = [0, 1])                                                       // crush ribs
                translate([x, s == 0 ? -c_play : out_w + c_play, 1]) rotate([0, 0, s == 0 ? 0 : 180])
                    linear_extrude(ih - 2) polygon([[-0.6, 0], [0.6, 0], [0, crush + c_play]]);
            if (wings) for (s = [0, 1]) {
                yw = s == 0 ? yl - wing_w : yl + wy - 0.5;
                translate([-cap_end, yw, -c_bot]) cube([cap_len + cap_end, wing_w + 0.5, wing_t]);
                for (xg = [-cap_end, cap_len - 2])
                    translate([xg, 0, 0]) rotate([90, 0, 90]) linear_extrude(2)
                        polygon(s == 0 ? [[yl - wing_w, -c_bot + wing_t], [yl + 0.5, -c_bot + wing_t], [yl + 0.5, ih * 0.8]]
                                       : [[yl + wy + wing_w, -c_bot + wing_t], [yl + wy - 0.5, -c_bot + wing_t], [yl + wy - 0.5, ih * 0.8]]);
            }
        }
        // keep the cable opening of the tray floor at this end free
        translate([px0 + slot_from_end - 1, (out_w - slot_w) / 2 - 1, -c_bot - 1]) cube([cap_len, slot_w + 2, c_bot + 2]);
        if (wings) for (s = [0, 1], xh = [5, cap_len - 6]) {
            yh = s == 0 ? yl - wing_w / 2 : yl + wy + wing_w / 2;
            translate([xh, yh, -c_bot - 1]) cylinder(d = screw_d, h = wing_t + 2, $fn = 24);
        }
    }
}
module end_cap_right(wings = false) { translate([out_l, out_w, 0]) rotate([0, 0, 180]) end_cap(wings); }

// for sensor_clamp.scad: "use" imports functions and modules, not variables
function clamp_geom() = [x0, c_len, out_w, c_play, c_side, c_bot, ih, c_hook_t];
module matrix_case_preview() { color("#3a3a3a") tray(); color("#eeeeea", 0.9) lid(); }

// print orientation: lying on its back (bottom plate on the bed)
module print_clamp() { translate([-x0, c_side + c_play + wing_w + 1, c_bot]) children(); }

if (part == "assembly") { color("#3a3a3a") tray(); color("#eeeeea") lid(); color("#e65100") { clamp(true); end_cap(true); end_cap_right(true); } }
if (part == "endcap")       translate([cap_end, c_side + c_play + 1, c_bot]) end_cap(false);
if (part == "endcap_wings") translate([cap_end, c_side + c_play + wing_w + 1, c_bot]) end_cap(true);
if (part == "endcaps_raw")  { end_cap(true); end_cap_right(true); }
if (part == "clamp")       print_clamp() clamp(false);
if (part == "clamp_wings") print_clamp() clamp(true);
if (part == "clamp_raw")   clamp(true);
