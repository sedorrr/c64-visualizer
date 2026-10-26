#!/usr/bin/env python3
"""
Builds the middle clamp with the light sensor case built on (sensor_clamp.scad) as a solid CAD body and exports
STEP files for Autodesk Fusion, FreeCAD, Onshape ... (same geometry and print position as clamp_sensor.stl /
clamp_sensor_other_side.stl). Reuses clamps_to_step.py and sensor_case_to_step.py.  Needs: pip install cadquery
"""
import os
import cadquery as cq
import clamps_to_step as cl
import sensor_case_to_step as sc

pod_gap = 0.6
# sensor case as tall as the clamp: thicker floor, no screw holes (as in sensor_clamp.scad)
sc.floor_t, sc.screws = 3.0, False
sc.z_pcb = sc.floor_t + sc.hdr_body + 0.5
sc.z_rim = sc.z_pcb + sc.board_t + sc.above_pcb
sc.total_h = sc.z_rim + sc.top_t
sc.z_step = sc.z_rim - sc.skirt_h
sc.z_bead = sc.z_step + sc.skirt_h * 0.4
assert abs(sc.total_h - (cl.c_bot + cl.ih + cl.c_hook_t)) < 0.01
xc = cl.x0 + cl.c_len / 2


def pod_place(body, side):
    """sensor-case coordinates -> case coordinates; header edge away from the matrix"""
    if side == 1:
        return body.rotate((0, 0, 0), (0, 0, 1), 180).translate(
            (xc + sc.out_w / 2, cl.out_w + cl.c_play + cl.c_side + pod_gap + sc.out_l, -cl.c_bot))
    return body.translate((xc - sc.out_w / 2, -cl.c_play - cl.c_side - pod_gap - sc.out_l, -cl.c_bot))


def clamp_sensor(side):
    bridge = sc.box(sc.r_out, sc.out_l - sc.wall / 2, 0, sc.out_w - 2 * sc.r_out, sc.wall / 2 + pod_gap + cl.c_side / 2, sc.z_step - 0.3)
    return cl.clamp(False).union(pod_place(sc.base().union(bridge), side))


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "step")
    for side, name in ((1, "clamp_sensor"), (0, "clamp_sensor_other_side")):
        dy = cl.c_side + cl.c_play + 1 if side == 1 else cl.c_side + cl.c_play + pod_gap + sc.out_l + 1
        body = clamp_sensor(side)
        cq.exporters.export(body.translate((-cl.x0, dy, cl.c_bot)), os.path.join(out, name + ".step"))
        bb = body.val().BoundingBox()
        print(f"{name}: volume {body.val().Volume():.1f} mm3, size {bb.xlen:.2f} x {bb.ylen:.2f} x {bb.zlen:.2f}")
    asm = cq.Assembly(name="matrix_case_with_sensor_clamp")
    asm.add(cl.box(0, 0, 0, cl.out_l, cl.out_w, cl.total_h), name="case_reference_simplified", color=cq.Color(0.25, 0.25, 0.28))
    asm.add(clamp_sensor(1), name="clamp_sensor", color=cq.Color(0.9, 0.32, 0.0))
    asm.add(pod_place(sc.lid_in_place(), 1), name="sensor_lid", color=cq.Color(0.62, 0.62, 0.62))
    asm.save(os.path.join(out, "assembly_case_with_sensor_clamp.step"))
