// =====================================================================
// Clip ring for the Pico case: a closed rectangular band that is pushed over the
// assembled case from one end and holds the lid on. Print one (or two).
// Klemmring fuer das Pico-Gehaeuse: wird vom Ende her ueber das zusammengebaute
// Gehaeuse geschoben und haelt den Deckel. Einen (oder zwei) drucken.
//
// part = "clip" | "clip_assembly"
// =====================================================================
include <pico_case.scad>
part = "clip";

r_play  = 0.2;     // play around the case
r_wall  = 1.8;     // ring wall
r_width = 12;      // band width along the case
r_crush = 0.3;     // crush ribs: they deform a little and hold the ring in place
r_pos   = 26;      // assembly preview: distance of the ring from the USB end (BOOTSEL window ends at ~21 mm)

iw = out_w + 2 * r_play;      // inner width  (across the case)
ih = total_h + 2 * r_play;    // inner height (tray bottom to lid top)

// ring lying flat: band width = z, case cross-section in x/y
module clip_ring() {
    difference() {
        translate([-r_wall, -r_wall, 0]) offset_box(iw + 2 * r_wall, ih + 2 * r_wall, r_width, 1.0);
        translate([0, 0, -1]) cube([iw, ih, r_width + 2]);
    }
    // crush ribs on all four inner faces, running along the sliding direction
    for (f = [0 : 3], t = [0.3, 0.7]) {
        L = (f % 2 == 0) ? iw : ih;
        pos = L * t;
        if (f == 0) translate([pos, 0, 0])  rib(0);      // bottom face (y = 0), points +y
        if (f == 1) translate([iw, pos, 0]) rib(90);     // right face, points -x
        if (f == 2) translate([pos, ih, 0]) rib(180);    // top face, points -y
        if (f == 3) translate([0, pos, 0])  rib(270);    // left face, points +x
    }
}
module rib(a) {
    rotate([0, 0, a]) linear_extrude(r_width) polygon([[-0.7, -0.01], [0.7, -0.01], [0, r_crush + r_play]]);
}
module offset_box(x, y, z, r) {                  // box with rounded vertical edges
    translate([r, r, 0]) linear_extrude(z) offset(r = r) square([x - 2 * r, y - 2 * r]);
}

if (part == "clip") translate([r_wall, r_wall, 0]) clip_ring();     // print lying flat, no supports
if (part == "clip_assembly") {
    color("#3a3a3a") tray(); color("#9e9e9e") lid();
    // ring placed around the case: ring x -> case y, ring y -> case z, ring z -> case x
    color("#e65100") translate([r_pos, -r_play, -r_play]) multmatrix([[0,0,1,0],[1,0,0,0],[0,1,0,0],[0,0,0,1]]) clip_ring();
}
if (part == "ring_raw") translate([r_pos, -r_play, -r_play]) multmatrix([[0,0,1,0],[1,0,0,0],[0,1,0,0],[0,0,0,1]]) clip_ring();
