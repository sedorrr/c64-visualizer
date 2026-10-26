// =====================================================================
// Middle clamp of the matrix case (plain, no wings) with the VEML7700 sensor case built on
// at one long side. The board lies parallel to the matrix, the sensor looks the same way as
// the LEDs (into the room); the DuPont plugs leave straight out through the back.
// The sensor case is exactly as tall as the clamp: its lid sits flush with the clamp hooks.
// The lid is the same part as sensor_lid.stl (sensor_case.scad).
// Mittlere Klammer des Matrix-Gehaeuses (ohne Fluegel) mit angebautem Lichtsensor-Gehaeuse
// an einer langen Seite. Deckel = sensor_lid.stl.
//
// openscad -D 'part="clamp_sensor"' -o clamp_sensor.stl sensor_clamp.scad
// part = "clamp_sensor" | "assembly" | "section"
// pod_side = 1: sensor on the long side with the middle cable opening of the tray, 0: other long side
// =====================================================================
use <clamp.scad>
include <sensor_case.scad>
part = "assembly";
pod_side = 1;

pod_gap = 0.6;      // gap between clamp cheek and sensor case where the lid skirt sits (bridged below)
screws  = false;    // no screw holes: the clamp holds it

G = clamp_geom();   // [x0, c_len, case width, c_play, c_side, c_bot, ih, c_hook_t]
cx0 = G[0]; clen = G[1]; cw = G[2]; cplay = G[3]; cside = G[4]; cbot = G[5]; cih = G[6]; chook = G[7];
// thicker floor: sensor case as tall as the clamp (11.4 mm). A reassigned variable takes the place of
// its first assignment in sensor_case.scad, so it can't be computed from the clamp values here - checked below.
floor_t = 3.0;
assert(abs(total_h - (cbot + cih + chook)) < 0.01, str("sensor case height ", total_h, " != clamp height ", cbot + cih + chook));
xc = cx0 + clen / 2;

// sensor-case coordinates -> case coordinates. The header edge (pod y = 0, plugs + finger notch)
// points away from the matrix, the hole edge (pod y = out_l) faces the clamp cheek.
module pod_place(side = pod_side) {
    if (side == 1) translate([xc + out_w / 2, cw + cplay + cside + pod_gap + out_l, -cbot]) rotate([0, 0, 180]) children();
    else           translate([xc - out_w / 2, -cplay - cside - pod_gap - out_l, -cbot]) children();
}

module bridge() {   // joins the sensor case to the cheek below the lid skirt (pod coordinates)
    translate([r_out, out_l - wall / 2, 0]) cube([out_w - 2 * r_out, wall / 2 + pod_gap + cside / 2, z_step - 0.3]);
}

module clamp_sensor(side = pod_side) {
    clamp(false);
    pod_place(side) { base(); bridge(); }
}

if (part == "clamp_sensor")   // print lying on its back like clamp.stl: clamp plate and sensor floor on the bed
    translate([-cx0, pod_side == 1 ? cside + cplay + 1 : cside + cplay + pod_gap + out_l + 1, cbot]) clamp_sensor();
if (part == "assembly") {
    matrix_case_preview();
    color("#e65100") clamp_sensor();
    pod_place() { board(); color("#9e9e9e", 0.9) translate([0, 0, 6]) lid(); }
}
module cut() { difference() { children(); translate([xc - 200, -100, -40]) cube([200, 300, 80]); } }
if (part == "section") {   // cut across the case through the sensor window (sensor sits in the middle)
    cut() matrix_case_preview();
    color("#e65100") cut() clamp_sensor();
    color("#9e9e9e") cut() pod_place() lid();
    cut() pod_place() board();
}
