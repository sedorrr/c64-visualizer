#!/usr/bin/env python3
"""
Builds the clip ring of the Pico case as a solid CAD body and exports STEP files for
Autodesk Fusion, FreeCAD, Onshape ... (same geometry as pico_clip.scad / pico_clip.stl).
Needs: pip install cadquery
"""
import os
import cadquery as cq

# ---- dimensions (from pico_case.scad / pico_clip.scad) ----
out_w   = 21 + 2 * 0.4 + 2 * 2.0                 # Pico case width: board + play + walls = 25.8
total_h = 1.6 + 24 + 1.0 + 4.5 + 1.6             # floor + under board + board + above + lid = 32.7
out_l   = 51 + 2 * 0.4 + 10 + 2 * 2.0            # Pico case length = 65.8 (only for the reference)
r_play, r_wall, r_width, r_crush, r_round = 0.2, 1.8, 12, 0.3, 1.0
iw, ih = out_w + 2 * r_play, total_h + 2 * r_play

def ring():
    outer = (cq.Workplane("XY").center(-r_wall + (iw + 2 * r_wall) / 2, -r_wall + (ih + 2 * r_wall) / 2)
             .rect(iw + 2 * r_wall, ih + 2 * r_wall).extrude(r_width).edges("|Z").fillet(r_round))
    body = outer.cut(cq.Workplane("XY").box(iw, ih, r_width + 2, centered=False).translate((0, 0, -1)))
    h = r_crush + r_play                                   # crush ribs reach 0.3 mm into the case
    for t in (0.3, 0.7):
        for pts in ([(iw * t - 0.7, -0.01), (iw * t + 0.7, -0.01), (iw * t, h)],               # floor side
                    [(iw * t - 0.7, ih + 0.01), (iw * t + 0.7, ih + 0.01), (iw * t, ih - h)],  # lid side
                    [(-0.01, ih * t - 0.7), (-0.01, ih * t + 0.7), (h, ih * t)],               # left wall
                    [(iw + 0.01, ih * t - 0.7), (iw + 0.01, ih * t + 0.7), (iw - h, ih * t)]): # right wall
            body = body.union(cq.Workplane("XY").polyline(pts).close().extrude(r_width))
    return body

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(here, "step"), exist_ok=True)
    r = ring()
    cq.exporters.export(r.translate((r_wall, r_wall, 0)), os.path.join(here, "step", "pico_clip.step"))
    # assembly: simplified Pico case (outer box) with the ring 26 mm from the USB end
    placed = (ring().rotate((0, 0, 0), (1, 0, 0), 90).rotate((0, 0, 0), (0, 0, 1), 90)
              .translate((26, -r_play, -r_play)))
    asm = cq.Assembly(name="pico_case_with_clip")
    asm.add(cq.Workplane("XY").box(out_l, out_w, total_h, centered=False), name="pico_case_reference_simplified",
            color=cq.Color(0.25, 0.25, 0.28))
    asm.add(placed, name="pico_clip", color=cq.Color(0.9, 0.32, 0.0))
    asm.save(os.path.join(here, "step", "assembly_pico_case_with_clip.step"))
    print("written: step/pico_clip.step, step/assembly_pico_case_with_clip.step")
