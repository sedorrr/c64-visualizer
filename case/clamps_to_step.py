#!/usr/bin/env python3
"""
Builds the matrix-case clamps as solid CAD bodies and exports STEP files for Autodesk Fusion,
FreeCAD, Onshape ... (same geometry as clamp.scad / the STL files).  Needs: pip install cadquery
"""
import cadquery as cq

# ---- dimensions (from matrix_case.scad / clamp.scad) ----
wall, clr = 1.6, 0.4
panel_l, panel_w = 320, 80
out_l, out_w = panel_l + 2 * (clr + wall), panel_w + 2 * (clr + wall)      # 324 x 84
total_h = 2.4 + 2.0 + 3.0 + 1.0                                          # floor + panel + gap + diffuser = 8.4
px0 = wall + clr
c_len, c_play, c_bot, c_side, c_hook, c_hook_t = 40, 0.2, 1.6, 2.0, 1.4, 1.2
wing_w, wing_t, screw_d, crush = 14, 2.4, 3.4, 0.3
cap_len, cap_end = 25, 2.0
iw, ih = out_w + 2 * c_play, total_h + c_play
x0 = px0 + 150 + 8 / 2 - c_len / 2                                       # clamp sits over the tray dovetails
mid_y0, mid_y1 = 42 + 6 + 0.15 + 2.5, 73.4 - 6 - 0.15 - 2.5               # middle cable opening of the tray
slot_w, slot_from_end = 44, 6

def box(x0_, y0_, z0_, dx, dy, dz):
    return cq.Workplane("XY").box(dx, dy, dz, centered=False).translate((x0_, y0_, z0_))

def profile(x_start, length):
    """C-profile: floor plate, two cheeks, hooks over the lid edges; extruded along x"""
    W = iw + 2 * c_side
    outer = (cq.Workplane("YZ").workplane(offset=x_start)
             .center(-c_side - c_play + W / 2, -c_bot + (c_bot + ih + c_hook_t) / 2)
             .rect(W, c_bot + ih + c_hook_t).extrude(length))
    case = box(x_start - 1, -c_play, 0, length + 2, iw, ih)
    front = box(x_start - 1, -c_play + c_hook, ih - 0.01, length + 2, iw - 2 * c_hook, c_hook_t + 1)
    return outer.cut(case).cut(front)

def ribs(xs):
    """crush ribs: 0.3 mm into the case walls, they deform when the clamp is pushed on"""
    s = None
    for x in xs:
        for side in (0, 1):
            y = -c_play if side == 0 else out_w + c_play
            d = crush + c_play if side == 0 else -(crush + c_play)
            r = (cq.Workplane("XY").workplane(offset=1)
                 .polyline([(x - 0.6, y), (x + 0.6, y), (x, y + d)]).close().extrude(ih - 2))
            s = r if s is None else s.union(r)
    return s

def wings(xa, xb):
    yl, yr = -c_side - c_play, -c_side - c_play + iw + 2 * c_side
    w = box(xa, yl - wing_w, -c_bot, xb - xa, wing_w + 0.5, wing_t).union(box(xa, yr - 0.5, -c_bot, xb - xa, wing_w + 0.5, wing_t))
    for xg in (xa, xb - 2):
        for pts in ([(yl - wing_w, -c_bot + wing_t), (yl + 0.5, -c_bot + wing_t), (yl + 0.5, ih * 0.8)],
                    [(yr + wing_w, -c_bot + wing_t), (yr - 0.5, -c_bot + wing_t), (yr - 0.5, ih * 0.8)]):
            g = cq.Workplane("YZ").workplane(offset=xg).polyline(pts).close().extrude(2)
            w = w.union(g)
    return w

def holes(xs):
    yl, yr = -c_side - c_play, -c_side - c_play + iw + 2 * c_side
    h = None
    for x in xs:
        for y in (yl - wing_w / 2, yr + wing_w / 2):
            c = cq.Workplane("XY").workplane(offset=-c_bot - 1).center(x, y).circle(screw_d / 2).extrude(wing_t + 2)
            h = c if h is None else h.union(c)
    return h

def clamp(with_wings):
    b = profile(x0, c_len).union(ribs([x0 + 8, x0 + c_len / 2, x0 + c_len - 8]))
    if with_wings: b = b.union(wings(x0, x0 + c_len))
    b = b.cut(box(px0 + 150 - 1, mid_y0 - 1, -c_bot - 1, 20 + 2, mid_y1 - mid_y0 + 2, c_bot + 2))
    if with_wings: b = b.cut(holes([x0 + 9, x0 + c_len - 9]))
    return b

def end_cap(with_wings):
    yl, W = -c_side - c_play, iw + 2 * c_side
    b = profile(0, cap_len)
    b = b.union(box(-cap_end, yl, -c_bot, cap_end, W, c_bot + ih + c_hook_t))      # end wall
    b = b.union(box(-0.01, yl, ih, c_hook + 0.01, W, c_hook_t))                     # hook over the lid end
    b = b.union(ribs([6, 13, 20]))
    if with_wings: b = b.union(wings(-cap_end, cap_len))
    b = b.cut(box(px0 + slot_from_end - 1, (out_w - slot_w) / 2 - 1, -c_bot - 1, cap_len, slot_w + 2, c_bot + 2))
    if with_wings: b = b.cut(holes([5, cap_len - 6]))
    return b

if __name__ == "__main__":
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    parts = {   # name: (body, translation into print position = same as the STL files)
        "clamp":        (clamp(False),   (-x0, c_side + c_play + 1 + wing_w, c_bot)),
        "clamp_wings":  (clamp(True),    (-x0, c_side + c_play + wing_w + 1, c_bot)),
        "endcap":       (end_cap(False), (cap_end, c_side + c_play + 1, c_bot)),
        "endcap_wings": (end_cap(True),  (cap_end, c_side + c_play + wing_w + 1, c_bot)),
    }
    for name, (body, t) in parts.items():
        cq.exporters.export(body.translate(t), os.path.join(here, "step", name + ".step"))
    # assembly in place: simplified case (outer box) + middle clamp + both end caps
    asm = cq.Assembly(name="matrix_case_with_clamps")
    asm.add(box(0, 0, 0, out_l, out_w, total_h), name="case_reference_simplified", color=cq.Color(0.25, 0.25, 0.28))
    asm.add(clamp(True), name="clamp_wings", color=cq.Color(0.9, 0.32, 0.0))
    asm.add(end_cap(True), name="endcap_left", color=cq.Color(0.9, 0.32, 0.0))
    asm.add(end_cap(True).rotate((0, 0, 0), (0, 0, 1), 180).translate((out_l, out_w, 0)), name="endcap_right", color=cq.Color(0.9, 0.32, 0.0))
    asm.save(os.path.join(here, "step", "assembly_case_with_clamps.step"))
    print("STEP files written to", os.path.join(here, "step"))
