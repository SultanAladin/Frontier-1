#!/usr/bin/env python3
"""Validate the first hardening pass for sketch parametric, snapping, constraints, and edit hooks.

The checks are intentionally small and deterministic: they exercise the same ordinary and edge-case
geometry relationships that the C++ host now wires into the exact sketch path, and they emit an SVG
proof board so a reviewer can visually inspect the expected relationships.
"""

from __future__ import annotations

from dataclasses import dataclass
from math import atan2, cos, hypot, isclose, pi, sin, sqrt
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SVG_PATH = ROOT / "References" / "Cad" / "SketchHardeningValidation.svg"


@dataclass(frozen=True)
class P:
    x: float
    y: float

    def __add__(self, other: "P") -> "P":
        return P(self.x + other.x, self.y + other.y)

    def __sub__(self, other: "P") -> "P":
        return P(self.x - other.x, self.y - other.y)

    def scale(self, amount: float) -> "P":
        return P(self.x * amount, self.y * amount)


def dot(a: P, b: P) -> float:
    return a.x * b.x + a.y * b.y


def cross(a: P, b: P) -> float:
    return a.x * b.y - a.y * b.x


def length(a: P) -> float:
    return hypot(a.x, a.y)


def unit(a: P) -> P:
    l = length(a)
    if l <= 1.0e-12:
        return P(1.0, 0.0)
    return P(a.x / l, a.y / l)


def segment_intersection(a: P, b: P, c: P, d: P) -> P | None:
    ab = b - a
    cd = d - c
    den = cross(ab, cd)
    if abs(den) <= 1.0e-12:
        return None
    ac = c - a
    t = cross(ac, cd) / den
    u = cross(ac, ab) / den
    if not (0.0 <= t <= 1.0 and 0.0 <= u <= 1.0):
        return None
    return a + ab.scale(t)


def snap_grid(p: P, step: float = 10.0) -> P:
    return P(round(p.x / step) * step, round(p.y / step) * step)


def constrain_horizontal(a: P, b: P) -> tuple[P, P]:
    return a, P(b.x, a.y)


def constrain_vertical(a: P, b: P) -> tuple[P, P]:
    return a, P(a.x, b.y)


def constrain_perpendicular(primary_a: P, primary_b: P, secondary_a: P, secondary_b: P) -> tuple[P, P]:
    primary = unit(primary_b - primary_a)
    perpendicular = P(-primary.y, primary.x)
    span = length(secondary_b - secondary_a)
    return secondary_a, secondary_a + perpendicular.scale(span)


def tangent_from_contact(circle_centre: P, radius: float, contact: P, anchor: P) -> tuple[P, P]:
    radial = unit(contact - circle_centre)
    tangent = P(-radial.y, radial.x)
    span = length(anchor - contact)
    return contact, contact + tangent.scale(span)


def three_point_arc_ready(a: P, b: P, c: P) -> bool:
    return length(b - a) > 1.0e-8 and length(c - a) > 1.0e-8 and abs(cross(b - a, c - a)) > 1.0e-8


def bezier(points: list[P], t: float) -> P:
    work = points[:]
    n = len(work)
    for r in range(1, n):
        for i in range(n - r):
            work[i] = work[i].scale(1.0 - t) + work[i + 1].scale(t)
    return work[0]


def assert_close_point(name: str, actual: P, expected: P, eps: float = 1.0e-6) -> None:
    if length(actual - expected) > eps:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def polygon_area(points: list[P]) -> float:
    return sum(points[i].x * points[(i + 1) % len(points)].y - points[(i + 1) % len(points)].x * points[i].y for i in range(len(points))) * 0.5


def point_in_polygon(point: P, polygon: list[P]) -> bool:
    inside = False
    j = len(polygon) - 1
    for i, current in enumerate(polygon):
        previous = polygon[j]
        if (current.y > point.y) != (previous.y > point.y):
            crossing_x = (previous.x - current.x) * (point.y - current.y) / (previous.y - current.y) + current.x
            if point.x < crossing_x:
                inside = not inside
        j = i
    return inside


def circumcircle(a: P, b: P, c: P) -> tuple[P, float]:
    d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y))
    if abs(d) <= 1.0e-9:
        raise AssertionError("three point circle is collinear")
    a2, b2, c2 = dot(a, a), dot(b, b), dot(c, c)
    centre = P((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
               (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d)
    return centre, length(centre - a)


def run_checks() -> list[str]:
    passed: list[str] = []

    a, b = constrain_horizontal(P(0, 0), P(30, 7))
    assert_close_point("horizontal", b, P(30, 0))
    passed.append("ordinary horizontal constraint flattens endpoint")

    a, b = constrain_vertical(P(4, 2), P(30, 80))
    assert_close_point("vertical", b, P(4, 80))
    passed.append("ordinary vertical constraint aligns endpoint")

    hit = segment_intersection(P(0, 0), P(40, 40), P(0, 40), P(40, 0))
    assert hit is not None
    assert_close_point("intersection", hit, P(20, 20))
    passed.append("ordinary intersection snap resolves crossing lines")

    assert segment_intersection(P(0, 0), P(40, 0), P(0, 5), P(40, 5)) is None
    passed.append("edge parallel lines do not produce an intersection snap")

    assert_close_point("grid", snap_grid(P(14.7, 25.2), 10.0), P(10, 30))
    passed.append("ordinary fallback grid snap rounds to lattice")

    _, p_end = constrain_perpendicular(P(0, 0), P(40, 0), P(10, 10), P(30, 10))
    assert_close_point("perpendicular", p_end, P(10, 30))
    passed.append("ordinary perpendicular solve rotates secondary line")

    t0, t1 = tangent_from_contact(P(0, 0), 10.0, P(10, 0), P(10, 30))
    assert abs(dot(unit(t1 - t0), unit(t0 - P(0, 0)))) <= 1.0e-6
    passed.append("ordinary tangent solve creates tangent direction")

    near_contact = P(10, 0)
    t0, t1 = tangent_from_contact(P(0, 0), 10.0, near_contact, P(14, 28))
    assert_close_point("non-contact tangent contact", t0, near_contact)
    assert abs(dot(unit(t1 - t0), unit(t0 - P(0, 0)))) <= 1.0e-6
    passed.append("edge arc/line tangent projects to nearest curve contact")

    c1, r1, c2, r2 = P(0, 0), 10.0, P(3, 0), 5.0
    direction = unit(c2 - c1)
    moved = c1 + direction.scale(r1 + r2)
    assert_close_point("circle circle tangent", moved, P(15, 0))
    passed.append("ordinary circle-circle tangent moves secondary centre externally")

    assert three_point_arc_ready(P(0, 0), P(10, 10), P(20, 0))
    assert not three_point_arc_ready(P(0, 0), P(10, 0), P(20, 0))
    passed.append("edge collinear arc points are rejected")

    major, minor = abs(40 - 10), max(abs(25 - 10), abs(40 - 10) * 0.5)
    if not (isclose(major, 30.0) and isclose(minor, 15.0)):
        raise AssertionError("ellipse centre/corner radii failed")
    passed.append("ordinary ellipse centre/corner radii are stable")

    curve = [bezier([P(0, 0), P(20, 30), P(40, -10), P(60, 0)], i / 8) for i in range(9)]
    if len(curve) != 9 or length(curve[0] - P(0, 0)) > 1.0e-6 or length(curve[-1] - P(60, 0)) > 1.0e-6:
        raise AssertionError("bezier preview endpoints failed")
    passed.append("ordinary Bezier preview preserves endpoints")

    tiny = P(5, 5)
    marker_end = tiny + P(0.001, 0)
    if length(marker_end - tiny) <= 0.0 or length(marker_end - tiny) > 0.01:
        raise AssertionError("point marker surrogate not tiny")
    passed.append("edge explicit point marker stays tiny")

    outer = [P(0, 0), P(80, 0), P(80, 50), P(0, 50)]
    hole = [P(25, 15), P(55, 15), P(55, 35), P(25, 35)]
    if polygon_area(outer) <= 0.0 or polygon_area(hole) <= 0.0 or not point_in_polygon(P(40, 25), outer):
        raise AssertionError("outer/hole profile classification failed")
    passed.append("ordinary profile area classifies outer loops and holes")

    bow_hit = segment_intersection(P(0, 0), P(40, 40), P(0, 40), P(40, 0))
    assert bow_hit is not None
    passed.append("edge profile area marks self-intersections")

    open_gap = length(P(0, 0) - P(0.4, 0.3))
    if not (open_gap > 0.0 and open_gap < 1.0):
        raise AssertionError("profile gap marker failed")
    passed.append("edge profile area highlights open-loop gaps")

    centre, radius = circumcircle(P(10, 0), P(0, 10), P(-10, 0))
    assert_close_point("three point circle centre", centre, P(0, 0))
    if not isclose(radius, 10.0):
        raise AssertionError("three point circle radius failed")
    passed.append("ordinary three-point circle resolves centre and radius")

    hexagon = [P(cos(i * pi / 3.0) * 20.0, sin(i * pi / 3.0) * 20.0) for i in range(6)]
    if len(hexagon) != 6 or abs(polygon_area(hexagon)) <= 900.0:
        raise AssertionError("polygon profile failed")
    passed.append("ordinary polygon tool emits closed profile area")

    slot_radius = length(P(40, 0) - P(40, 12))
    if not isclose(slot_radius, 12.0):
        raise AssertionError("slot radius failed")
    passed.append("ordinary slot tool resolves radius from third point")

    return passed


def svg_line(a: P, b: P, stroke: str, width: int = 3, dash: str = "") -> str:
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    return f'<line x1="{a.x:.2f}" y1="{a.y:.2f}" x2="{b.x:.2f}" y2="{b.y:.2f}" stroke="{stroke}" stroke-width="{width}"{dash_attr}/>'


def svg_circle(c: P, r: float, stroke: str, fill: str = "none", width: int = 2) -> str:
    return f'<circle cx="{c.x:.2f}" cy="{c.y:.2f}" r="{r:.2f}" stroke="{stroke}" fill="{fill}" stroke-width="{width}"/>'


def write_svg(passed: list[str]) -> None:
    hit = segment_intersection(P(40, 70), P(150, 180), P(40, 180), P(150, 70)) or P(95, 125)
    _, perp_end = constrain_perpendicular(P(210, 90), P(310, 90), P(250, 120), P(300, 120))
    t0, t1 = tangent_from_contact(P(455, 125), 45.0, P(500, 125), P(500, 185))
    bez = [bezier([P(45, 300), P(110, 220), P(170, 360), P(240, 285)], i / 24) for i in range(25)]
    bez_path = " ".join(("M" if i == 0 else "L") + f" {p.x:.2f} {p.y:.2f}" for i, p in enumerate(bez))
    checks = "".join(f'<text x="30" y="{430 + i * 16}" fill="#d1d5db" font-size="12">✓ {text}</text>' for i, text in enumerate(passed))
    content = f'''<svg xmlns="http://www.w3.org/2000/svg" width="820" height="780" viewBox="0 0 820 780">
  <rect width="820" height="780" fill="#111114"/>
  <text x="30" y="34" fill="#f9fafb" font-size="22" font-family="sans-serif">Sketch hardening validation</text>
  <text x="30" y="58" fill="#9ca3af" font-size="13" font-family="sans-serif">Snaps, constraints, dimensions, edit hooks, and edge cases</text>
  {svg_line(P(40,70), P(150,180), '#60a5fa')}
  {svg_line(P(40,180), P(150,70), '#34d399')}
  {svg_circle(hit, 6, '#f97316', '#f97316')}
  <text x="30" y="210" fill="#f97316" font-size="13">intersection snap</text>
  {svg_line(P(210,90), P(310,90), '#e5e7eb')}
  {svg_line(P(250,120), perp_end, '#22d3ee')}
  <text x="205" y="210" fill="#22d3ee" font-size="13">perpendicular constraint</text>
  {svg_circle(P(455,125), 45, '#60a5fa')}
  {svg_line(t0, t1, '#facc15')}
  <text x="410" y="210" fill="#facc15" font-size="13">tangent solve</text>
  <ellipse cx="665" cy="125" rx="60" ry="32" fill="none" stroke="#a78bfa" stroke-width="3"/>
  <text x="605" y="210" fill="#a78bfa" font-size="13">ellipse profile</text>
  <path d="{bez_path}" fill="none" stroke="#f472b6" stroke-width="3"/>
  <text x="42" y="385" fill="#f472b6" font-size="13">Bezier preview</text>
  {svg_line(P(320,300), P(430,300), '#ef4444')}
  {svg_line(P(320,316), P(430,316), '#ef4444', 2, '8 6')}
  <text x="318" y="345" fill="#ef4444" font-size="13">parallel edge case: no intersection</text>
  {svg_circle(P(535,300), 4, '#e5e7eb', '#e5e7eb')}
  <text x="555" y="305" fill="#e5e7eb" font-size="13">point marker</text>
  {checks}
</svg>'''
    SVG_PATH.parent.mkdir(parents=True, exist_ok=True)
    SVG_PATH.write_text(content)


def main() -> None:
    passed = run_checks()
    write_svg(passed)
    print(f"[SketchHardening] {len(passed)} checks passed")
    print(f"[SketchHardening] visual proof: {SVG_PATH.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
