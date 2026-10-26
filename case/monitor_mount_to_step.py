#!/usr/bin/env python3
"""
Builds the monitor mount (monitor_mount.scad) as solid CAD bodies and exports STEP files for Fusion, FreeCAD,
Onshape ... (same geometry and print position as monitor_mount.stl / monitor_knob.stl / monitor_pad.stl).
Reuses clamps_to_step.py for the clamp.  Needs: pip install cadquery
"""
import math
import os
import cadquery as cq
import clamps_to_step as cl

# ---- from monitor_mount.scad ----
mon_max, lip_down, lip_t, saddle_t, hook_down, hook_t, screw_y = 40, 9, 2.0, 3.0, 32, 6.0, 14
screw_d, nut_af, nut_t = 4.4, 7.3, 3.6
pico_w = 25.8
cr_play, cr_wall, cr_h, cr_crush = 0.3, 2.5, 12, 0.3

x0, clen = cl.x0, cl.c_len
yb = -cl.c_play - cl.c_side
y_mon = yb - saddle_t
z_front = cl.ih + cl.c_hook_t
z_mon = z_front - lip_t
z_hin = z_mon - mon_max - 1
z_hout = z_hin - hook_t
z_crb = -cl.c_bot - 2 * cr_play - pico_w
y_screw = y_mon - screw_y
xc = x0 + clen / 2


def prism(pts):                                    # a polygon in (y, z), extruded along the clamp
    return cq.Workplane("YZ").polyline(pts).close().extrude(clen).translate((x0, 0, 0))


def rect(y0, z0, dy, dz):
    return prism([(y0, z0), (y0 + dy, z0), (y0 + dy, z0 + dz), (y0, z0 + dz)])


def mount():
    b = cl.clamp(False)
    for r in (rect(y_mon, z_hout, saddle_t + 0.01, z_front - z_hout),                    # saddle
              rect(y_mon - lip_down, z_mon, lip_down + saddle_t + 0.01, lip_t),           # front lip
              rect(y_mon - hook_down, z_hout, hook_down + saddle_t + 0.01, hook_t),       # back hook
              prism([(y_mon + 0.01, z_hin - 0.01), (y_mon - 3, z_hin - 0.01), (y_mon + 0.01, z_hin + 3)]),
              rect(yb - 0.01, z_crb - cr_wall, cr_h + 0.01, cr_wall)):                    # cradle back wall
        b = b.union(r)
    for y in (yb + 4, yb + cr_h - 3):                                                       # crush ribs
        b = b.union(prism([(y - 0.7, z_crb - 0.01), (y + 0.7, z_crb - 0.01), (y, z_crb + cr_play + cr_crush)]))
    hole = cq.Workplane("XY").workplane(offset=z_hout - 1).center(xc, y_screw).circle(screw_d / 2).extrude(hook_t + 2)
    r = nut_af / math.cos(math.radians(30)) / 2
    hexpts = [(xc + r * math.cos(math.radians(a)), y_screw + r * math.sin(math.radians(a))) for a in range(0, 360, 60)]
    zn = z_hout + (hook_t - nut_t) / 2
    nut = cq.Workplane("XY").workplane(offset=zn).polyline(hexpts).close().extrude(nut_t)
    slot = cq.Workplane("XY").box(x0 + clen + 1 - xc, nut_af, nut_t, centered=False).translate((xc, y_screw - nut_af / 2, zn))
    return b.cut(hole).cut(nut.union(slot))


def knob():
    k = cq.Workplane("XY").circle(10).extrude(8)
    for a in range(0, 360, 30):
        k = k.union(cq.Workplane("XY").center(10 * math.cos(math.radians(a)), 10 * math.sin(math.radians(a))).circle(1.5).extrude(8))
    k = k.cut(cq.Workplane("XY").workplane(offset=8 - 3.2).polygon(6, 7.25 / math.cos(math.radians(30))).extrude(4))
    return k.cut(cq.Workplane("XY").workplane(offset=-1).circle(screw_d / 2).extrude(10))


def pad():
    return cq.Workplane("XY").circle(7).extrude(5).cut(cq.Workplane("XY").workplane(offset=1.5).circle(1.7).extrude(4))


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "step")
    m = mount().translate((-x0, 0, 0)).rotate((0, 0, 0), (0, 1, 0), -90)        # as monitor_mount.stl: on its end face
    for name, body in (("monitor_mount", m), ("monitor_knob", knob()), ("monitor_pad", pad())):
        cq.exporters.export(body, os.path.join(out, name + ".step"))
        bb = body.val().BoundingBox()
        print(f"{name}: volume {body.val().Volume():.1f} mm3, box {bb.xmin:.1f}..{bb.xmax:.1f} x {bb.ymin:.1f}..{bb.ymax:.1f} x {bb.zmin:.1f}..{bb.zmax:.1f}")
