#!/usr/bin/env python3
"""
Builds the VEML7700 sensor case (base + lid) as solid CAD bodies and exports STEP files for
Autodesk Fusion, FreeCAD, Onshape ... (same geometry and print position as sensor_case.scad / the STL files).
Needs: pip install cadquery
"""
import os
import cadquery as cq

# ---- dimensions (from sensor_case.scad) ----
board_w, board_l, board_t = 17, 17, 1.6
hole_dist, hole_d, hole_edge = 11, 3.0, 3.0
pins, pitch, hdr_y, hdr_body = 5, 2.54, 1.3, 2.5
sensor_xy = (8.5, 8.5)
above_pcb = 2.2
wall, floor_t, top_t, play = 2.0, 2.0, 1.6, 0.7
peg_d, post_d, win_d = hole_d - 0.7, 5.0, 6.0
lid_fit, lip_t, lip_h, crush, r_out = 0.15, 1.2, 1.6, 0, 1.5
skirt_h, step, skirt_fit, snap, snap_len = 4.0, 1.0, 0.1, 0.35, 0.6
screws = True                                 # countersunk M3 holes in the floor

in_w, in_l = board_w + 2 * play, board_l + 2 * play
out_w, out_l = in_w + 2 * wall, in_l + 2 * wall
z_pcb = floor_t + hdr_body + 0.5
z_rim = z_pcb + board_t + above_pcb
total_h = z_rim + top_t
z_step = z_rim - skirt_h
z_bead = z_step + skirt_h * 0.4
bx = by = wall + play
holes = [(board_w / 2 - hole_dist / 2, board_l - hole_edge), (board_w / 2 + hole_dist / 2, board_l - hole_edge)]


def box(x0, y0, z0, x, y, z):
    return cq.Workplane("XY").box(x, y, z, centered=False).translate((x0, y0, z0))


def cyl(cx, cy, z0, d, h):
    return cq.Workplane("XY").circle(d / 2).extrude(h).translate((cx, cy, z0))


def cone(cx, cy, z0, d1, d2, h):
    return cq.Solid.makeCone(d1 / 2, d2 / 2, h, pnt=cq.Vector(cx, cy, z0))


def rbox(z0, h):
    return box(0, 0, z0, out_w, out_l, h).edges("|Z").fillet(r_out)


def rrect(inset, z0, h):           # outline of the case, shrunk by inset
    r = max(r_out - inset, 0.2)
    return box(inset, inset, z0, out_w - 2 * inset, out_l - 2 * inset, h).edges("|Z").fillet(r)


def ridges(d, half):               # triangular ridge on the four faces of the thinned wall
    lx, ly = (out_w - 2 * step) * snap_len, (out_l - 2 * step) * snap_len
    x0, y0 = (out_w - lx) / 2, (out_l - ly) / 2
    a, b, c = z_bead - half, z_bead, z_bead + half
    shapes = [
        cq.Workplane("YZ").polyline([(step, a), (step - d, b), (step, c)]).close().extrude(lx).translate((x0, 0, 0)),
        cq.Workplane("YZ").polyline([(out_l - step, a), (out_l - step + d, b), (out_l - step, c)]).close().extrude(lx).translate((x0, 0, 0)),
        cq.Workplane("XZ").polyline([(step, a), (step - d, b), (step, c)]).close().extrude(-ly).translate((0, y0, 0)),
        cq.Workplane("XZ").polyline([(out_w - step, a), (out_w - step + d, b), (out_w - step, c)]).close().extrude(-ly).translate((0, y0, 0)),
    ]
    out = shapes[0]
    for sh in shapes[1:]:
        out = out.union(sh)
    return out


def base():
    body = rbox(0, z_rim).cut(box(wall, wall, floor_t, in_w, in_l, z_rim))
    for hx, hy in holes:
        x, y = bx + hx, by + hy
        body = body.union(cyl(x, y, 0, post_d, z_pcb))
        body = body.union(cyl(x, y, z_pcb - 0.01, peg_d, board_t + 0.6))
        body = body.union(cq.Workplane("XY").add(cone(x, y, z_pcb + board_t + 0.59, peg_d, peg_d - 1.0, 0.6)))
    for x in (0, board_w - 1.5):                          # pillars beside the header
        body = body.union(box(bx + x, by, 0, 1.5, 1.6, z_pcb))
    # slot for the DuPont plugs
    body = body.cut(box(bx + board_w / 2 - pins * pitch / 2 - 0.6, by + hdr_y - pitch / 2 - 0.5, -1,
                        pins * pitch + 1.2, pitch + 1.0, floor_t + 2))
    # countersunk M3 holes
    for hx, _hy in (holes if screws else []):
        x, y = bx + hx, by + board_l / 2 - 0.5
        body = body.cut(cyl(x, y, -1, 3.4, floor_t + 2))
        body = body.cut(cq.Workplane("XY").add(cone(x, y, floor_t - 1.6, 3.4, 6.6, 1.61)))
    # upper part of the walls thinner (the lid skirt sits there), then the snap bead
    body = body.cut(box(-1, -1, z_step, out_w + 2, out_l + 2, skirt_h + 1).cut(rrect(step, z_step - 1, skirt_h + 3)))
    if snap > 0:
        body = body.union(ridges(snap, 0.4))
    return body


def tri_prism(pts, z0, h):
    return cq.Workplane("XY").polyline(pts).close().extrude(h).translate((0, 0, z0))


def lid_in_place():
    body = rbox(z_rim, top_t)
    body = body.union(rbox(z_step + 0.2, skirt_h - 0.2 + 0.01).cut(rrect(step - skirt_fit, z_step, skirt_h + 1)))   # outer skirt
    rim = box(wall + lid_fit, wall + lid_fit, z_rim - lip_h, in_w - 2 * lid_fit, in_l - 2 * lid_fit, lip_h + 0.01)
    rim = rim.cut(box(wall + lid_fit + lip_t, wall + lid_fit + lip_t, z_rim - lip_h - 1,
                      in_w - 2 * lid_fit - 2 * lip_t, in_l - 2 * lid_fit - 2 * lip_t, lip_h + 2))
    body = body.union(rim)
    z0, r = z_rim - lip_h, lid_fit + crush
    for t in (0.3, 0.7):                                  # crush ribs on the outside of the rim
        x = wall + lid_fit + (in_w - 2 * lid_fit) * t
        y = wall + lid_fit + (in_l - 2 * lid_fit) * t
        ya, yb = wall + lid_fit, wall + in_l - lid_fit
        xa, xb = wall + lid_fit, wall + in_w - lid_fit
        for pts in ([(x - 0.6, ya + 0.01), (x + 0.6, ya + 0.01), (x, ya - r)],
                    [(x - 0.6, yb - 0.01), (x + 0.6, yb - 0.01), (x, yb + r)],
                    [(xa + 0.01, y - 0.6), (xa + 0.01, y + 0.6), (xa - r, y)],
                    [(xb - 0.01, y - 0.6), (xb - 0.01, y + 0.6), (xb + r, y)]):
            body = body.union(tri_prism(pts, z0, lip_h))
    for hx, hy in holes:                                  # press tubes around the pegs
        x, y = bx + hx, by + hy
        tube = cyl(x, y, z_pcb + board_t, post_d, above_pcb + 0.01).cut(cyl(x, y, z_pcb + board_t - 1, peg_d + 0.8, above_pcb + 2))
        body = body.union(tube)
    for x in (0.1, board_w - 1.5):                        # posts at the header corners
        body = body.union(box(bx + x, by + 0.1, z_pcb + board_t, 1.4, 1.4, above_pcb + 0.01))
    if snap > 0:
        body = body.cut(ridges(snap + 0.1, 0.5))                                           # groove for the bead
    body = body.cut(box(out_w / 2 - 3, -1, z_step + 0.19, 6, step + 1, 0.8))              # finger notch
    # conical window over the sensor
    body = body.cut(cq.Workplane("XY").add(cone(bx + sensor_xy[0], by + sensor_xy[1], z_rim - 0.01, win_d, win_d + 2 * top_t, top_t + 0.01)))
    return body


def lid_print():   # top face down, as part = "lid" in the .scad
    return lid_in_place().rotate((0, 0, 0), (1, 0, 0), 180).translate((0, out_l, total_h))


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "step")
    os.makedirs(out, exist_ok=True)
    b, lp = base(), lid_print()
    cq.exporters.export(b, os.path.join(out, "sensor_base.step"))
    cq.exporters.export(lp, os.path.join(out, "sensor_lid.step"))
    asm = cq.Assembly(name="sensor_case")
    asm.add(b, name="sensor_base", color=cq.Color(0.23, 0.23, 0.23))
    asm.add(lid_in_place(), name="sensor_lid", color=cq.Color(0.62, 0.62, 0.62))
    asm.add(box(bx, by, z_pcb, board_w, board_l, board_t).cut(cyl(bx + holes[0][0], by + holes[0][1], z_pcb - 1, hole_d, 4))
            .cut(cyl(bx + holes[1][0], by + holes[1][1], z_pcb - 1, hole_d, 4)),
            name="veml7700_board_reference", color=cq.Color(0.1, 0.31, 0.61))
    asm.export(os.path.join(out, "assembly_sensor_case.step"))
    for name, s in (("base", b), ("lid", lp)):
        bb = s.val().BoundingBox()
        print(f"{name}: volume {s.val().Volume():.1f} mm3, box {bb.xmin:.2f}..{bb.xmax:.2f} x {bb.ymin:.2f}..{bb.ymax:.2f} x {bb.zmin:.2f}..{bb.zmax:.2f}")
