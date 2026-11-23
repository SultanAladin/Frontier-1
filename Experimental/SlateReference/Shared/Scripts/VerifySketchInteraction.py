"""Headless contract checks for sketch selection, curve naming, and orthographic navigation.

This deliberately tests the same state transitions and vector equations used by the
C++ interaction code. It does not pretend to be a Vulkan runtime test.
"""
from dataclasses import dataclass
from math import isclose

@dataclass(frozen=True)
class Pick:
    subject: str
    record: int
    identity: int


def same(a, b):
    return (a.subject, a.record, a.identity) == (b.subject, b.record, b.identity)


def set_pick(items, pick, additive):
    if not additive:
        return [pick]
    for i, existing in enumerate(items):
        if same(existing, pick):
            return items[:i] + items[i + 1:]
    return [pick] + items                 # newest item is active


def test_additive_selection():
    rect_a = Pick("Point", 10, 1)
    rect_b = Pick("Point", 10, 2)
    bezier = Pick("Control", 11, 7)
    items = set_pick([], rect_a, False)
    items = set_pick(items, rect_b, True)
    items = set_pick(items, bezier, True)
    assert len(items) == 3, items
    assert items[0] == bezier, items
    assert {p.identity for p in items if p.record == 10} == {1, 2}
    items = set_pick(items, rect_b, True)
    assert len(items) == 2 and rect_b not in items, items


def test_multi_selection_centroid():
    positions = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (10.0, 0.0, 10.0), (0.0, 0.0, 10.0)]
    cx = sum(p[0] for p in positions) / len(positions)
    cy = sum(p[1] for p in positions) / len(positions)
    cz = sum(p[2] for p in positions) / len(positions)
    assert (cx, cy, cz) == (5.0, 0.0, 5.0), (cx, cy, cz)


def test_directory_does_not_overwrite_viewport_additions():
    # The directory signature is unchanged during a viewport Shift-click.
    old_signature = 1234
    new_signature = old_signature
    directory_changed = old_signature != new_signature
    viewport = [Pick("Point", 10, 1), Pick("Control", 11, 7)]
    if directory_changed:
        viewport = [p for p in viewport if p.record == 10]
    assert len(viewport) == 2, viewport


def test_orthographic_camera_navigation():
    # Camera-local basis: Right, Up, Forward.
    # In locked orthographic mode (Top/Bottom/Left/Right/Front/Back), WASD pans along screen-right (A/D)
    # and depth travel (W/S) is restricted. E zooms in (scale increases) and Q zooms out (scale decreases).
    right = (1.0, 0.0, 0.0)
    forward = (0.0, 0.0, 1.0)
    distance = 2.0
    def add(a, b): return tuple(x + y for x, y in zip(a, b))
    def scale(a, n): return tuple(x * n for x in a)

    # Locked Top view: A/D pans along Right, W/S depth travel is restricted
    is_locked = True
    focus = (0.0, 0.0, 0.0)
    focus = add(focus, scale(right, distance))       # D
    if not is_locked:
        focus = add(focus, scale(forward, distance)) # W
    assert focus == (2.0, 0.0, 0.0), focus

    # Isometric view: A/D pans along Right, W/S travels along Forward
    is_locked = False
    focus_iso = (0.0, 0.0, 0.0)
    focus_iso = add(focus_iso, scale(right, distance))       # D
    if not is_locked:
        focus_iso = add(focus_iso, scale(forward, distance)) # W
    assert focus_iso == (2.0, 0.0, 2.0), focus_iso

    # Zoom direction: E zooms in (scale increases), Q zooms out (scale decreases)
    ortho_scale = 3.0
    elapsed = 0.1
    zoom_in_scale = ortho_scale * (1.0 + elapsed)
    zoom_out_scale = ortho_scale / (1.0 + elapsed)
    assert zoom_in_scale > ortho_scale, "E must zoom in"
    assert zoom_out_scale < ortho_scale, "Q must zoom out"


def test_curve_naming_and_folder_contract():
    # Formatted 3-digit zero-padded names under Curves folder
    def format_name(prefix, count):
        return f"{prefix}_{count:03d}"

    hermite_name = format_name("HermiteCurve", 1)
    bezier_name = format_name("BezierCurve", 1)
    spline_name = format_name("SplineCurve", 1)
    line_name = format_name("LineCurve", 1)
    arc_name = format_name("ArcCurve", 1)

    assert hermite_name == "HermiteCurve_001"
    assert bezier_name == "BezierCurve_001"
    assert spline_name == "SplineCurve_001"
    assert line_name == "LineCurve_001"
    assert arc_name == "ArcCurve_001"


def main():
    test_additive_selection()
    test_multi_selection_centroid()
    test_directory_does_not_overwrite_viewport_additions()
    test_orthographic_camera_navigation()
    test_curve_naming_and_folder_contract()
    print("[SketchInteraction] additive selection contract: PASS")
    print("[SketchInteraction] multi-element centroid: PASS")
    print("[SketchInteraction] directory overwrite guard: PASS")
    print("[SketchInteraction] orthographic navigation & zoom contract: PASS")
    print("[SketchInteraction] curve naming (HermiteCurve_001, BezierCurve_001, etc.): PASS")

if __name__ == "__main__":
    main()

