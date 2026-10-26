#!/usr/bin/env python3
"""
Builds the screwless monitor clips (monitor_clip.scad) as solid CAD bodies and exports STEP files for Fusion,
FreeCAD, Onshape ... (same geometry and print position as monitor_clip.stl / monitor_clip_pico.stl / monitor_jaw.stl).
Needs: pip install cadquery
"""
import os
import cadquery as cq
import clamps_to_step as cl

L = 30
mon_max, lip_down, lip_t, saddle_t = 40, 9, 2.0, 3.0
pitch, tooth_h, j_len, j_wall, j_play, hook_down, hook_t = 2.0, 1.0, 24, 2.5, 0.3, 30, 6
tongue_w, tongue_l, tongue_t = 10, 16, 1.6
pico_w, cr_up, cr_play, cr_wall, cr_h, cr_crush, crush = 25.8, 40, 0.3, 2.5, 12, 0.3, 0.3

yb = -cl.c_play - cl.c_side
y_mon = yb - saddle_t
z_front = cl.ih + cl.c_hook_t
z_mon = z_front - lip_t
z_back = z_mon - mon_max - j_len - 4
z_crf = -cl.c_bot
z_crb = z_crf - 2 * cr_play - pico_w
ys = yb + cr_up


def prism(pts, x0=0, length=L):                        # polygon in (y, z), extruded along x
    return cq.Workplane("YZ").polyline(pts).close().extrude(length).translate((x0, 0, 0))


def rect(y0, z0, dy, dz, x0=0, length=L):
    return prism([(y0, z0), (y0 + dy, z0), (y0 + dy, z0 + dz), (y0, z0 + dz)], x0, length)


def box(x, y, z, dx, dy, dz):
    return cq.Workplane("XY").box(dx, dy, dz, centered=False).translate((x, y, z))


def clip(pico):
    b = cl.profile(0, L)                                                   # the clamp's C-profile
    for x in (6, L - 6):                                                   # crush ribs on the cheeks
        for side in (0, 1):
            y = -cl.c_play if side == 0 else cl.out_w + cl.c_play
            d = crush + cl.c_play if side == 0 else -(crush + cl.c_play)
            b = b.union(cq.Workplane("XY").workplane(offset=1).polyline([(x - 0.6, y), (x + 0.6, y), (x, y + d)]).close().extrude(cl.ih - 2))
    b = b.union(rect(y_mon, z_back, saddle_t + 0.01, z_front - z_back))
    b = b.union(rect(y_mon - lip_down, z_mon, lip_down + saddle_t + 0.01, lip_t))
    z = z_back + 3
    while z <= -2 - pitch + 1e-9:
        b = b.union(prism([(yb - 0.01, z), (yb + tooth_h, z + pitch), (yb - 0.01, z + pitch)]))
        z += pitch
    if pico:
        b = b.union(rect(ys, z_crb - cr_wall, cr_wall, z_crf - z_crb + cr_wall + 0.01))
        b = b.union(rect(ys, z_crb - cr_wall, cr_wall + cr_h, cr_wall))
        for y in (ys + cr_wall + 3, ys + cr_wall + cr_h - 3):
            b = b.union(prism([(y - 0.7, z_crb - 0.01), (y + 0.7, z_crb - 0.01), (y, z_crb + cr_play + cr_crush)]))
        b = b.union(prism([(ys + 0.01, z_crf + 0.01), (ys + 0.01, z_crf - 16), (ys - 16, z_crf + 0.01)]))
    return b


def jaw(zf=z_mon - 25):
    xa, xw = -j_play - j_wall, L + 2 * (j_play + j_wall)
    yl, yt = y_mon - j_play - j_wall, yb + tooth_h + j_play
    xc, zt = L / 2, zf - 5
    b = box(xa, yl, zf - j_len, xw, yt + j_wall - yl, j_len)
    b = b.union(box(xa, y_mon - hook_down, zf - hook_t, xw, yl - (y_mon - hook_down) + 0.01, hook_t))
    b = b.union(box(xc - tongue_w / 2, yt + j_wall - 0.01, zf - 5, tongue_w, 6, 2))
    b = b.cut(box(-j_play, y_mon - j_play, zf - j_len - 1, L + 2 * j_play, yt - y_mon + j_play, j_len + 2))
    for s in (-1, 1):
        b = b.cut(box(xc + s * (tongue_w / 2 + 0.5) - 0.5, yt - 1, zf - 3 - tongue_l, 1, j_wall + 2, tongue_l + 1))
    b = b.cut(box(xc - tongue_w / 2 - 1, yt - 1, zf - 3, tongue_w + 2, j_wall + 2, 1))
    b = b.cut(box(xc - tongue_w / 2 - 0.01, yt + tongue_t, zf - 3 - tongue_l, tongue_w + 0.02, j_wall, tongue_l - 4))
    return b.union(prism([(yt + 0.01, zt), (yb + 0.15, zt), (yt + 0.01, zt + pitch)], xc - tongue_w / 2, tongue_w))


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "step")
    up = lambda s: s.rotate((0, 0, 0), (0, 1, 0), -90)                     # print position as the STL files
    parts = (("monitor_clip", up(clip(False))), ("monitor_clip_pico", up(clip(True))),
             ("monitor_jaw", up(jaw().translate((j_play + j_wall, 0, 0)))))
    for name, body in parts:
        cq.exporters.export(body, os.path.join(out, name + ".step"))
        bb = body.val().BoundingBox()
        print(f"{name}: volume {body.val().Volume():.1f} mm3, box {bb.xmin:.1f}..{bb.xmax:.1f} x {bb.ymin:.1f}..{bb.ymax:.1f} x {bb.zmin:.1f}..{bb.zmax:.1f}")
