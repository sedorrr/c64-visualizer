// =====================================================================
// Case for the VEML7700 light sensor module (17 x 17 mm, 5-pin header soldered in
// from below, pins pointing DOWN, DuPont plugs on the pins). The sensor looks UP
// (room / ceiling light), the DuPont plugs leave straight down through the floor.
// Gehaeuse fuer das VEML7700-Lichtsensor-Modul (17 x 17 mm, 5-polige Stiftleiste von unten
// eingeloetet, Pins nach UNTEN, DuPont-Stecker darauf). Der Sensor schaut nach OBEN,
// die Stecker gehen gerade nach unten durch den Boden.
//
//   BASE: the board rests on two posts with pegs through its mounting holes and on two
//         small pillars at the header edge; slot in the floor for the 5 DuPont plugs;
//         two countersunk M3 holes to screw it onto an edge (or use double-sided tape).
//   LID:  slip-on cap: a 4 mm skirt goes over the (thinner) upper part of the base walls and snaps
//         over a bead; an inner rim centres it; holds the board down, conical window over the sensor.
//
// openscad -D 'part="base"' -o sensor_base.stl sensor_case.scad
// part = "assembly" | "base" | "lid" | "section"
// =====================================================================

part = "assembly";

/* ---- the module: MEASURE yours and adjust / Modul nachmessen ---- */
board_w   = 17;     // across the header (x)
board_l   = 17;     // header edge -> hole edge (y)
board_t   = 1.6;
hole_dist = 11;     // mounting hole spacing, centre to centre (measured)
hole_d    = 3.0;    // mounting hole diameter (measured: about 3 mm)
hole_edge = 3.0;    // hole centres from the edge opposite the header
pins      = 5;      // VIN 3Vo GND SCL SDA
pitch     = 2.54;
hdr_y     = 1.3;    // pin row centre from the header edge
hdr_body  = 2.5;    // header plastic below the board
sensor_xy = [8.5, 8.5];   // sensor centre on the board (board origin = header edge, left)
above_pcb = 2.2;    // room above the board top (sensor 1 mm, solder joints of the header)

/* ---- case ---- */
wall    = 2.0;
floor_t = 2.0;
top_t   = 1.6;
play    = 0.7;      // around the board: the pegs set its position
peg_d   = hole_d - 0.7;   /* 2.3 mm: printed pegs come out thicker; 2.6 was very hard to push the board over */
post_d  = 5.0;
win_d   = 6.0;      // window over the sensor (bottom), widens 45 deg towards the top
win_skin = 0;       // 0 = open window; 0.6 = closed thin skin (print the lid in white/clear PLA)
lid_fit = 0.15;     // play of the inner lid rim inside the walls
lip_t   = 1.2;  lip_h = 1.6;
crush   = 0;        // crush ribs on the inner rim (0 = none; the snap holds the lid)
skirt_h = 4.0;      // outer skirt of the lid: how far it reaches down over the base
step    = 1.0;      // the base wall is this much thinner where the skirt sits (skirt flush outside)
skirt_fit = 0.1;    // play between skirt and base wall
snap    = 0.35;     // snap bead on the base walls (0 = none; more = firmer click)
snap_len = 0.6;     // bead length as a share of each side
screws  = true;     // two countersunk M3 holes in the floor
r_out   = 1.5;      // rounded vertical edges

// ---------------------------------------------------------------------
in_w  = board_w + 2 * play;
in_l  = board_l + 2 * play;
out_w = in_w + 2 * wall;
out_l = in_l + 2 * wall;
z_pcb   = floor_t + hdr_body + 0.5;       // board underside (0.5 mm above the header plastic)
z_rim   = z_pcb + board_t + above_pcb;    // wall top = lid underside
z_step  = z_rim - skirt_h;                // where the base wall gets thinner
z_bead  = z_step + skirt_h * 0.4;
total_h = z_rim + top_t;
bx = wall + play;  by = wall + play;      // board origin
holes = [[board_w / 2 - hole_dist / 2, board_l - hole_edge], [board_w / 2 + hole_dist / 2, board_l - hole_edge]];
echo(str("Sensor case outer size: ", out_w, " x ", out_l, " x ", total_h, " mm"));

$fn = 48;
module rbox(x, y, z, r = r_out) { translate([r, r, 0]) linear_extrude(z) offset(r = r) square([x - 2 * r, y - 2 * r]); }
module at_board(p) { translate([bx + p[0], by + p[1], 0]) children(); }
module rrect(inset, z0, h) {   // outline of the case, shrunk by inset (rounded corners follow)
    r = max(r_out - inset, 0.2);
    translate([inset + r, inset + r, z0]) linear_extrude(h) offset(r = r) square([out_w - 2 * inset - 2 * r, out_l - 2 * inset - 2 * r]);
}
module ridges(d, half) {       // triangular ridge (depth d, height 2*half) on the four faces of the thinned wall
    lx = (out_w - 2 * step) * snap_len;  ly = (out_l - 2 * step) * snap_len;
    x0 = (out_w - lx) / 2;  y0 = (out_l - ly) / 2;
    hull() { translate([x0, step, z_bead - half]) cube([lx, 0.01, 2 * half]); translate([x0, step - d, z_bead - 0.005]) cube([lx, d, 0.01]); }
    hull() { translate([x0, out_l - step - 0.01, z_bead - half]) cube([lx, 0.01, 2 * half]); translate([x0, out_l - step, z_bead - 0.005]) cube([lx, d, 0.01]); }
    hull() { translate([step, y0, z_bead - half]) cube([0.01, ly, 2 * half]); translate([step - d, y0, z_bead - 0.005]) cube([d, ly, 0.01]); }
    hull() { translate([out_w - step - 0.01, y0, z_bead - half]) cube([0.01, ly, 2 * half]); translate([out_w - step, y0, z_bead - 0.005]) cube([d, ly, 0.01]); }
}

module base() {
    if (snap > 0) ridges(snap, 0.4);                                                     // snap bead (after the thinning)
    difference() {
        union() {
            difference() {
                rbox(out_w, out_l, z_rim);
                translate([wall, wall, floor_t]) cube([in_w, in_l, z_rim]);
            }
            for (h = holes) at_board(h) {
                cylinder(d = post_d, h = z_pcb);                                        // post
                translate([0, 0, z_pcb - 0.01]) cylinder(d = peg_d, h = board_t + 0.6); // peg through the hole
                translate([0, 0, z_pcb + board_t + 0.59]) cylinder(d1 = peg_d, d2 = peg_d - 1.0, h = 0.6);   // lead-in
            }
            for (x = [0, board_w - 1.5]) at_board([x, 0]) cube([1.5, 1.6, z_pcb]);      // pillars beside the header
        }
        // upper part of the walls thinner: the lid skirt sits there, flush with the outside
        difference() {
            translate([-1, -1, z_step]) cube([out_w + 2, out_l + 2, skirt_h + 1]);
            rrect(step, z_step - 1, skirt_h + 3);
        }
        // slot for the DuPont plugs (they hang straight down through the floor)
        at_board([board_w / 2 - pins * pitch / 2 - 0.6, hdr_y - pitch / 2 - 0.5]) translate([0, 0, -1])
            cube([pins * pitch + 1.2, pitch + 1.0, floor_t + 2]);
        // countersunk M3 holes, head inside the case (screw it on before the board goes in)
        if (screws) for (h = holes) at_board([h[0], board_l / 2 - 0.5]) {
            translate([0, 0, -1]) cylinder(d = 3.4, h = floor_t + 2);
            translate([0, 0, floor_t - 1.6]) cylinder(d1 = 3.4, d2 = 6.6, h = 1.61);
        }
    }
}

module lid() {   // modelled in place (as on the base); part = "lid" turns it over for printing
    difference() {
        union() {
            translate([0, 0, z_rim]) rbox(out_w, out_l, top_t);
            // outer skirt over the thinned base wall (0.2 mm short, so the lid sits on the wall top)
            difference() {
                translate([0, 0, z_step + 0.2]) rbox(out_w, out_l, skirt_h - 0.2 + 0.01);
                rrect(step - skirt_fit, z_step, skirt_h + 1);
            }
            // rim that plugs into the base
            translate([0, 0, z_rim - lip_h]) difference() {
                translate([wall + lid_fit, wall + lid_fit, 0]) cube([in_w - 2 * lid_fit, in_l - 2 * lid_fit, lip_h + 0.01]);
                translate([wall + lid_fit + lip_t, wall + lid_fit + lip_t, -1]) cube([in_w - 2 * lid_fit - 2 * lip_t, in_l - 2 * lid_fit - 2 * lip_t, lip_h + 2]);
            }
            // crush ribs on the outside of the rim
            for (t = [0.3, 0.7]) {
                for (s = [0, 1]) {
                    translate([wall + lid_fit + (in_w - 2 * lid_fit) * t, s == 0 ? wall + lid_fit : wall + in_l - lid_fit, z_rim - lip_h])
                        rotate([0, 0, s == 0 ? 0 : 180]) linear_extrude(lip_h) polygon([[-0.6, 0.01], [0.6, 0.01], [0, -lid_fit - crush]]);
                    translate([s == 0 ? wall + lid_fit : wall + in_w - lid_fit, wall + lid_fit + (in_l - 2 * lid_fit) * t, z_rim - lip_h])
                        rotate([0, 0, s == 0 ? 90 : 270]) linear_extrude(lip_h) polygon([[-0.6, -0.01], [0.6, -0.01], [0, lid_fit + crush]]);
                }
            }
            // press tubes around the pegs and two posts at the header corners: hold the board down
            for (h = holes) at_board(h) translate([0, 0, z_pcb + board_t]) difference() {
                cylinder(d = post_d, h = above_pcb + 0.01);
                translate([0, 0, -1]) cylinder(d = peg_d + 0.8, h = above_pcb + 2);
            }
            for (x = [0.1, board_w - 1.5]) at_board([x, 0.1]) translate([0, 0, z_pcb + board_t]) cube([1.4, 1.4, above_pcb + 0.01]);
        }
        if (snap > 0) ridges(snap + 0.1, 0.5);                                           // groove for the bead
        // finger notch in the skirt (lift the lid with a fingernail)
        translate([out_w / 2 - 3, -1, z_step + 0.19]) cube([6, step + 1, 0.8]);
        // window over the sensor: cone (45 deg) so the sensor sees a wide angle
        at_board(sensor_xy) {
            translate([0, 0, z_rim - 0.01]) cylinder(d1 = win_d, d2 = win_d + 2 * (top_t - win_skin), h = top_t - win_skin + 0.01);
        }
    }
}

module board() {   // reference only
    color("#1b4f9c") difference() {
        at_board([0, 0]) translate([0, 0, z_pcb]) cube([board_w, board_l, board_t]);
        for (h = holes) at_board(h) translate([0, 0, z_pcb - 1]) cylinder(d = hole_d, h = board_t + 2);
    }
    color("#f2f2f2") at_board(sensor_xy) translate([-3.4, -1.2, z_pcb + board_t]) cube([6.8, 2.35, 1.0]);
    for (i = [0 : pins - 1]) at_board([board_w / 2 - (pins - 1) * pitch / 2 + i * pitch, hdr_y]) {
        color("#222") translate([-pitch / 2, -1.25, z_pcb - hdr_body]) cube([pitch, 2.5, hdr_body]);
        color("#c9a227") translate([-0.32, -0.32, z_pcb - hdr_body - 6]) cube([0.64, 0.64, 6 + hdr_body + board_t + 1]);
        color(["#c62828", "#ff9800", "#212121", "#1565c0", "#2e7d32"][i % 5])      // DuPont plugs + wires
            translate([-1.25, -1.25, z_pcb - hdr_body - 14]) cube([2.5, 2.5, 14]);
        color(["#c62828", "#ff9800", "#212121", "#1565c0", "#2e7d32"][i % 5])
            translate([0, 0, z_pcb - hdr_body - 30]) cylinder(d = 1.4, h = 16, $fn = 12);
    }
}

if (part == "base") base();
if (part == "lid") translate([0, out_l, total_h]) rotate([180, 0, 0]) lid();   // print top face down
if (part == "assembly") { color("#3a3a3a") base(); board(); color("#9e9e9e", 0.9) translate([0, 0, 12]) lid(); }
if (part == "section") difference() {   // cut through the sensor and the pegs
    union() { color("#3a3a3a") base(); board(); color("#9e9e9e") lid(); }
    translate([-1, by + sensor_xy[1], -40]) cube([out_w + 2, out_l, 80]);
}
