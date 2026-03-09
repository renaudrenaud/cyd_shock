# FreeCAD macro — CYD Shock front & back plates
# ESP32-2432S028 ("Cheap Yellow Display")
# Run from FreeCAD: Macro menu → Execute macro

import FreeCAD
import Part

doc = FreeCAD.newDocument("CYD_Case")

# ============================================================
#  Dimensions (mm) — adjust if your board differs
# ============================================================
PCB_W       = 86.0   # board width  (landscape)
PCB_H       = 60.0   # board height (landscape)

SCREEN_W    = 70.0   # display cutout width  (landscape)
SCREEN_H    = 50.0   # display cutout height (landscape)

BORDER      =  0.0   # border around the PCB
THICKNESS   =  3.5   # plate thickness

HOLE_D      =  3.2   # M3 mounting hole diameter
HOLE_INSET  =  4.0   # hole centre distance from PCB edge

# ============================================================
#  Derived values
# ============================================================
W = PCB_W + 2 * BORDER   # outer plate width  = 96
H = PCB_H + 2 * BORDER   # outer plate height = 60

# Screen cutout — centred on the plate
SX = (W - SCREEN_W) / 2
SY = (H - SCREEN_H) / 2

# Mounting hole centres (relative to plate origin)
holes = [
    (BORDER + HOLE_INSET,         BORDER + HOLE_INSET),
    (BORDER + PCB_W - HOLE_INSET, BORDER + HOLE_INSET),
    (BORDER + HOLE_INSET,         BORDER + PCB_H - HOLE_INSET),
    (BORDER + PCB_W - HOLE_INSET, BORDER + PCB_H - HOLE_INSET),
]

def make_holes(shape):
    for (hx, hy) in holes:
        cyl = Part.makeCylinder(HOLE_D / 2, THICKNESS, FreeCAD.Vector(hx, hy, 0))
        shape = shape.cut(cyl)
    return shape

# ============================================================
#  Front plate — frame with screen cutout
# ============================================================
front = Part.makeBox(W, H, THICKNESS)

screen_cut = Part.makeBox(SCREEN_W, SCREEN_H, THICKNESS,
                          FreeCAD.Vector(SX, SY, 0))
front = front.cut(screen_cut)
front = make_holes(front)

front_obj = doc.addObject("Part::Feature", "Front")
front_obj.Shape = front
front_obj.ViewObject.ShapeColor = (0.2, 0.5, 0.9)

# ============================================================
#  Back plate — flat
# ============================================================
back = Part.makeBox(W, H, THICKNESS)
back = make_holes(back)

back_obj = doc.addObject("Part::Feature", "Back")
back_obj.Shape = back
back_obj.ViewObject.ShapeColor = (0.2, 0.5, 0.9)

# Place back plate next to front for visibility
back_obj.Placement.Base = FreeCAD.Vector(0, H + 10, 0)

# ============================================================
doc.recompute()
FreeCAD.Gui.activeDocument().activeView().viewAxometric()
FreeCAD.Gui.SendMsgToActiveView("ViewFit")

print(f"Front plate : {W} x {H} x {THICKNESS} mm  —  screen cutout : {SCREEN_W} x {SCREEN_H} mm")
print(f"Back plate  : {W} x {H} x {THICKNESS} mm")
print("Adjust BORDER, THICKNESS and hole positions to match your board.")
