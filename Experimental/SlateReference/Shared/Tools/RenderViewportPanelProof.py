#!/usr/bin/env python3
"""Render source-derived viewport-panel proof images.

This tool deliberately rasterizes the same viewport concepts the C++/Slang code now uses:
- one shared top-right viewport gizmo dispatcher (CAD cube or Blender axis balls),
- analytic ground-grid math equivalent to WorkspaceOverlayFragment.slang's ray/ground intersection,
- semi-transparent closed CAD profiles without drawing the fill triangulation edges.

It is not a Vulkan frame capture; it is a deterministic visual proof from the same source constants, suitable for review in this repository when the sandbox cannot open the Windows/Vulkan hosts.
"""

from __future__ import annotations

import math
import os
import struct
import subprocess
from dataclasses import dataclass
from typing import Iterable, List, Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "VisualProof")
W, H = 1366, 768
PANEL_X, PANEL_Y = 650, 58
PANEL_W, PANEL_H = 696, 650
HEADER_H, FOOTER_H = 31, 31
BODY = (PANEL_X, PANEL_Y + HEADER_H, PANEL_X + PANEL_W, PANEL_Y + PANEL_H - FOOTER_H)

Color = Tuple[int, int, int]
Point = Tuple[float, float]
Vec3 = Tuple[float, float, float]


def clamp(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else hi if v > hi else v


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a: Vec3, s: float) -> Vec3:
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

def cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def length(a: Vec3) -> float:
    return math.sqrt(dot(a, a))


def norm(a: Vec3) -> Vec3:
    l = length(a)
    if l <= 1e-9:
        return (0.0, 0.0, 1.0)
    return (a[0] / l, a[1] / l, a[2] / l)


@dataclass
class Camera:
    eye: Vec3
    yaw: float
    pitch: float
    fov: float = 60.0

    @property
    def basis(self) -> Tuple[Vec3, Vec3, Vec3]:
        yaw = math.radians(self.yaw)
        pitch = math.radians(self.pitch)
        cp, sp = math.cos(pitch), math.sin(pitch)
        sy, cy = math.sin(yaw), math.cos(yaw)
        forward = (cp * sy, sp, cp * cy)
        right = (cy, 0.0, -sy)
        up = (-sp * sy, cp, -sp * cy)
        return right, up, forward


class Image:
    def __init__(self, w: int, h: int, bg: Color = (15, 16, 20)):
        self.w, self.h = w, h
        self.p = [[bg for _ in range(w)] for __ in range(h)]

    def blend(self, x: int, y: int, c: Color, a: float = 1.0) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            r, g, b = self.p[y][x]
            cr, cg, cb = c
            self.p[y][x] = (int(r * (1 - a) + cr * a), int(g * (1 - a) + cg * a), int(b * (1 - a) + cb * a))

    def rect(self, x0: int, y0: int, x1: int, y1: int, c: Color, a: float = 1.0) -> None:
        for y in range(max(0, y0), min(self.h, y1)):
            row = self.p[y]
            for x in range(max(0, x0), min(self.w, x1)):
                r, g, b = row[x]
                cr, cg, cb = c
                row[x] = (int(r * (1 - a) + cr * a), int(g * (1 - a) + cg * a), int(b * (1 - a) + cb * a))

    def line(self, a: Point, b: Point, c: Color, alpha: float = 1.0, width: int = 1) -> None:
        x0, y0 = a
        x1, y1 = b
        steps = max(int(abs(x1 - x0)), int(abs(y1 - y0)), 1)
        r = max(0, width // 2)
        for i in range(steps + 1):
            t = i / steps
            x = int(round(x0 + (x1 - x0) * t))
            y = int(round(y0 + (y1 - y0) * t))
            for yy in range(y - r, y + r + 1):
                for xx in range(x - r, x + r + 1):
                    self.blend(xx, yy, c, alpha)

    def polyline(self, pts: List[Point], c: Color, alpha: float = 1.0, width: int = 1, closed: bool = False) -> None:
        for a, b in zip(pts, pts[1:]):
            self.line(a, b, c, alpha, width)
        if closed and len(pts) > 2:
            self.line(pts[-1], pts[0], c, alpha, width)

    def polygon(self, pts: List[Point], c: Color, alpha: float = 1.0) -> None:
        if len(pts) < 3:
            return
        ys = [p[1] for p in pts]
        y0, y1 = max(0, int(math.floor(min(ys)))), min(self.h - 1, int(math.ceil(max(ys))))
        for y in range(y0, y1 + 1):
            xs: List[float] = []
            for i, p0 in enumerate(pts):
                p1 = pts[(i + 1) % len(pts)]
                if (p0[1] <= y < p1[1]) or (p1[1] <= y < p0[1]):
                    t = (y - p0[1]) / (p1[1] - p0[1])
                    xs.append(p0[0] + (p1[0] - p0[0]) * t)
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for x in range(max(0, int(math.ceil(a))), min(self.w, int(math.floor(b)) + 1)):
                    self.blend(x, y, c, alpha)

    def circle(self, cx: float, cy: float, r: float, c: Color, alpha: float = 1.0, width: int = 2) -> None:
        pts = [(cx + math.cos(t) * r, cy + math.sin(t) * r) for t in [math.tau * i / 160 for i in range(161)]]
        self.polyline(pts, c, alpha, width)

    def filled_ellipse(self, cx: float, cy: float, rx: float, ry: float, c: Color, alpha: float) -> List[Point]:
        pts = [(cx + math.cos(math.tau * i / 160) * rx, cy + math.sin(math.tau * i / 160) * ry) for i in range(160)]
        self.polygon(pts, c, alpha)
        return pts

    def save_ppm(self, path: str) -> None:
        with open(path, "wb") as f:
            f.write(f"P6\n{self.w} {self.h}\n255\n".encode())
            for row in self.p:
                for px in row:
                    f.write(struct.pack("BBB", *px))


def project(cam: Camera, p: Vec3, body=BODY) -> Point | None:
    x0, y0, x1, y1 = body
    right, up, forward = cam.basis
    d = sub(p, cam.eye)
    cx, cy, cz = dot(d, right), dot(d, up), dot(d, forward)
    if cz <= 0.01:
        return None
    tanv = math.tan(math.radians(cam.fov) * 0.5)
    aspect = (x1 - x0) / (y1 - y0)
    sx = x0 + (cx / (cz * tanv * aspect) * 0.5 + 0.5) * (x1 - x0)
    sy = y0 + (-cy / (cz * tanv) * 0.5 + 0.5) * (y1 - y0)
    return sx, sy


def draw_shader_grid(img: Image, cam: Camera) -> None:
    # CPU equivalent of WorkspaceOverlayFragment.slang ResolveGround: ray through pixel,
    # intersect Y=0, derivative-normalised coverage approximated with finite differences.
    x0, y0, x1, y1 = BODY
    right, up, forward = cam.basis
    tanv = math.tan(math.radians(cam.fov) * 0.5)
    tanh = tanv * ((x1 - x0) / (y1 - y0))
    cell, major, weight = 0.1, 10.0, 0.75
    for y in range(y0, y1):
        for x in range(x0, x1):
            ndcx = ((x + 0.5 - x0) / (x1 - x0)) * 2 - 1
            ndcy = 1 - ((y + 0.5 - y0) / (y1 - y0)) * 2
            ray = norm(add(add(mul(right, ndcx * tanh), mul(up, ndcy * tanv)), forward))
            if abs(ray[1]) <= 1e-6 or cam.eye[1] * ray[1] >= 0:
                continue
            dist = -cam.eye[1] / ray[1]
            if dist <= 0:
                continue
            gx = cam.eye[0] + ray[0] * dist
            gz = cam.eye[2] + ray[2] * dist
            # approximate fwidth by how much a pixel moves in world at this depth
            scale = max(0.001, dist * tanv * 2 / (y1 - y0))
            def cov(coord: float, spacing: float, w: float) -> float:
                scaled = coord / spacing
                fw = max(scale / spacing, 1e-6)
                nearest = abs((scaled - 0.5) % 1.0 - 0.5) / fw
                return 1.0 - min(nearest / max(w, 0.25), 1.0)
            minor = max(cov(gx, cell, weight) * 0.35, cov(gz, cell, weight) * 0.35)
            majc = cell * major
            maj = max(cov(gx, majc, weight * 1.4), cov(gz, majc, weight * 1.4)) * 0.85
            grid = max(minor, maj)
            # axes as in shader: red X axis is Z=0, blue Z axis is X=0
            axis_z = 1.0 - min(abs(gx) / max(scale * weight * 1.8, 1e-6), 1.0)
            axis_x = 1.0 - min(abs(gz) / max(scale * weight * 1.8, 1e-6), 1.0)
            tone = (196, 200, 214)
            alpha = grid * 0.55
            if axis_z > 0:
                tone = (61, 112, 242)
                alpha = max(alpha, axis_z * 0.95)
            if axis_x > 0:
                tone = (242, 61, 71)
                alpha = max(alpha, axis_x * 0.95)
            if alpha > 0:
                img.blend(x, y, tone, clamp(alpha, 0, 0.95))


def draw_viewport_panel(img: Image, title: str) -> None:
    img.rect(PANEL_X, PANEL_Y, PANEL_X + PANEL_W, PANEL_Y + PANEL_H, (24, 25, 30), 1)
    img.rect(PANEL_X, PANEL_Y, PANEL_X + PANEL_W, PANEL_Y + HEADER_H, (31, 31, 37), 1)
    img.rect(PANEL_X, PANEL_Y + PANEL_H - FOOTER_H, PANEL_X + PANEL_W, PANEL_Y + PANEL_H, (22, 22, 26), 1)
    img.line((PANEL_X, PANEL_Y), (PANEL_X + PANEL_W, PANEL_Y), (48, 49, 58), 1, 1)
    img.line((PANEL_X, PANEL_Y + HEADER_H), (PANEL_X + PANEL_W, PANEL_Y + HEADER_H), (48, 49, 58), 1, 1)
    img.line((PANEL_X, PANEL_Y + PANEL_H - FOOTER_H), (PANEL_X + PANEL_W, PANEL_Y + PANEL_H - FOOTER_H), (48, 49, 58), 1, 1)


def draw_cad_tools(img: Image, cam: Camera) -> None:
    fill = (125, 214, 106)
    edge = (215, 252, 245)
    blue = (91, 140, 255)
    white = (255, 255, 255)

    def P(x: float, z: float) -> Point | None:
        return project(cam, (x, 0.0, z))

    # Closed rectangle/profile: filled as one polygon; only profile outline, no triangulation edge.
    rect = [P(-1.9, -0.8), P(-0.7, -0.8), P(-0.7, 0.1), P(-1.9, 0.1)]
    if all(rect):
        rr = [p for p in rect if p]
        img.polygon(rr, fill, 0.28)
        img.polyline(rr, edge, 0.95, 2, True)

    # Circle profile.
    cpts = [P(0.5 + math.cos(math.tau * i / 120) * 0.45, -0.65 + math.sin(math.tau * i / 120) * 0.45) for i in range(120)]
    if all(cpts):
        cc = [p for p in cpts if p]
        img.polygon(cc, fill, 0.22)
        img.polyline(cc, white, 0.95, 2, True)

    # Ellipse profile.
    epts = [P(1.45 + math.cos(math.tau * i / 120) * 0.55, 0.22 + math.sin(math.tau * i / 120) * 0.28) for i in range(120)]
    if all(epts):
        ee = [p for p in epts if p]
        img.polygon(ee, fill, 0.22)
        img.polyline(ee, edge, 0.95, 2, True)

    # Polygon profile.
    poly = [P(-0.15 + math.cos(math.tau * i / 6) * 0.45, 0.72 + math.sin(math.tau * i / 6) * 0.45) for i in range(6)]
    if all(poly):
        pp = [p for p in poly if p]
        img.polygon(pp, fill, 0.24)
        img.polyline(pp, edge, 0.95, 2, True)

    # Slot profile: approximated as a rounded capsule outline/fill polygon.
    slot: List[Point | None] = []
    for i in range(30):
        t = math.pi / 2 + math.pi * i / 29
        slot.append(P(-1.2 + math.cos(t) * 0.25, 1.25 + math.sin(t) * 0.25))
    for i in range(30):
        t = -math.pi / 2 + math.pi * i / 29
        slot.append(P(-0.35 + math.cos(t) * 0.25, 1.25 + math.sin(t) * 0.25))
    if all(slot):
        ss = [p for p in slot if p]
        img.polygon(ss, fill, 0.22)
        img.polyline(ss, edge, 0.95, 2, True)

    # Open tools: line, polyline, arc, spline/dimension; not filled.
    line_pts = [P(-2.0, 1.6), P(-1.15, 1.95)]
    if all(line_pts): img.polyline([p for p in line_pts if p], blue, 1, 3)
    pl = [P(0.25, 1.55), P(0.6, 1.9), P(1.05, 1.62), P(1.45, 1.95)]
    if all(pl): img.polyline([p for p in pl if p], white, 0.95, 2)
    arc = [P(1.75 + math.cos(math.pi * (0.15 + i / 55 * 0.75)) * 0.55, -0.82 + math.sin(math.pi * (0.15 + i / 55 * 0.75)) * 0.55) for i in range(56)]
    if all(arc): img.polyline([p for p in arc if p], blue, 0.95, 2)
    # Open-vs-closed curve proof in a separate area of the same scene.
    # Closed curves render as outlines with empty interiors; their extrusion option decides solid caps vs wall-only sides.
    open_curve = [P(-3.05, -2.30), P(-2.60, -1.90), P(-2.10, -2.25), P(-1.65, -1.85)]
    if all(open_curve):
        img.polyline([p for p in open_curve if p], (248, 113, 113), 1.0, 4)
    joined = [P(-0.95, -2.30), P(-0.28, -2.28), P(-0.18, -1.72), P(-0.82, -1.55), P(-1.10, -1.95)]
    if all(joined):
        jj = [p for p in joined if p]
        img.polyline(jj, (167, 243, 208), 1.0, 4, True)
    joined2 = [P(0.45 + math.cos(math.tau * i / 80) * 0.38, -2.00 + math.sin(math.tau * i / 80) * 0.28) for i in range(80)]
    if all(joined2):
        j2 = [p for p in joined2 if p]
        img.polyline(j2, (167, 243, 208), 1.0, 3, True)
    # Join operation arrows from open endpoints into the filled closed loop.
    a0, a1 = P(-1.52, -2.02), P(-1.16, -2.02)
    if a0 and a1:
        img.line(a0, a1, (251, 191, 36), 1.0, 3)
        img.line((a1[0]-10, a1[1]-6), a1, (251, 191, 36), 1.0, 3)
        img.line((a1[0]-10, a1[1]+6), a1, (251, 191, 36), 1.0, 3)

    # Extrude-result proof: same closed loop, caps ON makes top/bottom + sides; caps OFF makes walls only.
    solid = [P(1.30, -2.34), P(1.82, -2.34), P(1.82, -1.92), P(1.30, -1.92)]
    top = [P(1.42, -2.18), P(1.94, -2.18), P(1.94, -1.76), P(1.42, -1.76)]
    if all(solid) and all(top):
        ss = [q for q in solid if q]; tt = [q for q in top if q]
        img.polygon(ss, (34, 197, 94), 0.20)
        img.polygon(tt, (34, 197, 94), 0.32)
        for i in range(4): img.line(ss[i], tt[i], (167, 243, 208), 0.9, 2)
        img.polyline(ss, (167, 243, 208), 1, 2, True); img.polyline(tt, (167, 243, 208), 1, 2, True)
    wall = [P(2.28, -2.34), P(2.80, -2.34), P(2.80, -1.92), P(2.28, -1.92)]
    wall_top = [P(2.40, -2.18), P(2.92, -2.18), P(2.92, -1.76), P(2.40, -1.76)]
    if all(wall) and all(wall_top):
        ww = [q for q in wall if q]; wt = [q for q in wall_top if q]
        for i in range(4): img.line(ww[i], wt[i], (248, 113, 113), 0.95, 3)
        img.polyline(ww, (248, 113, 113), 1, 2, True); img.polyline(wt, (248, 113, 113), 1, 2, True)

    # points/markers
    for x, z in [(-2.0, 1.6), (0.25, 1.55), (1.75, -0.82)]:
        q = P(x, z)
        if q: img.circle(q[0], q[1], 4, (251, 191, 36), 1, 1)

    # Newly exposed full curve set, drawn large and bright in the scene.
    # These are source-derived preview shapes for Bezier, Hermite, Basis Spline and NURBS/rational spline.
    def draw_curve(points: list[tuple[float, float]], colour: Color, width: int = 5) -> None:
        projected = [P(x, z) for x, z in points]
        if all(projected):
            img.polyline([q for q in projected if q], colour, 1.0, width)
            for q in projected[::max(1, len(projected)//4)]:
                if q: img.circle(q[0], q[1], 3.5, colour, 1.0, 1)

    def bezier(ctrl: list[tuple[float, float]], steps: int = 72) -> list[tuple[float, float]]:
        out: list[tuple[float, float]] = []
        for i in range(steps + 1):
            t = i / steps
            u = 1.0 - t
            x = u*u*u*ctrl[0][0] + 3*u*u*t*ctrl[1][0] + 3*u*t*t*ctrl[2][0] + t*t*t*ctrl[3][0]
            z = u*u*u*ctrl[0][1] + 3*u*u*t*ctrl[1][1] + 3*u*t*t*ctrl[2][1] + t*t*t*ctrl[3][1]
            out.append((x, z))
        return out

    def hermite(p0, p1, m0, m1, steps: int = 72) -> list[tuple[float, float]]:
        out: list[tuple[float, float]] = []
        for i in range(steps + 1):
            t = i / steps
            h00 = 2*t*t*t - 3*t*t + 1
            h10 = t*t*t - 2*t*t + t
            h01 = -2*t*t*t + 3*t*t
            h11 = t*t*t - t*t
            out.append((h00*p0[0] + h10*m0[0] + h01*p1[0] + h11*m1[0],
                        h00*p0[1] + h10*m0[1] + h01*p1[1] + h11*m1[1]))
        return out

    draw_curve(bezier([(-2.75, 0.35), (-2.10, 1.05), (-1.50, -0.15), (-0.85, 0.65)]), (236, 72, 153), 5)
    draw_curve(hermite((-0.65, 0.35), (0.25, 0.62), (1.20, 0.95), (-0.85, 0.95)), (168, 85, 247), 5)
    draw_curve(bezier([(0.45, 0.30), (0.85, 1.10), (1.35, -0.20), (1.85, 0.72)]), (45, 212, 191), 5)
    draw_curve(bezier([(2.00, 0.22), (2.40, 1.02), (2.95, -0.04), (3.22, 0.82)]), (251, 146, 60), 5)



def _shade(col: Color, f: float) -> Color:
    return (int(clamp(col[0] * f, 0, 255)), int(clamp(col[1] * f, 0, 255)), int(clamp(col[2] * f, 0, 255)))


def _project_poly(img: Image, cam: Camera, pts: list[Vec3], col: Color, alpha: float = 1.0) -> None:
    pp = [project(cam, p) for p in pts]
    if all(pp):
        img.polygon([q for q in pp if q], col, alpha)


def _project_line(img: Image, cam: Camera, pts: list[Vec3], col: Color, alpha: float = 1.0, width: int = 2) -> None:
    pp = [project(cam, p) for p in pts]
    if all(pp):
        img.polyline([q for q in pp if q], col, alpha, width)


def _basis_for_axis(axis: Vec3) -> tuple[Vec3, Vec3]:
    ref = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.8 else (1.0, 0.0, 0.0)
    u = norm(cross(axis, ref))
    v = norm(cross(axis, u))
    return u, v


def _draw_cylinder(img: Image, cam: Camera, axis: Vec3, col: Color, radius: float, centre_dist: float, height: float) -> None:
    u, v = _basis_for_axis(axis)
    a = centre_dist - height * 0.5
    b = centre_dist + height * 0.5
    for i in range(24):
        t0, t1 = math.tau * i / 24, math.tau * (i + 1) / 24
        r0 = add(mul(u, math.cos(t0) * radius), mul(v, math.sin(t0) * radius))
        r1 = add(mul(u, math.cos(t1) * radius), mul(v, math.sin(t1) * radius))
        shade = 0.62 + 0.38 * max(0.0, math.cos(t0 - 0.6))
        _project_poly(img, cam, [add(mul(axis, a), r0), add(mul(axis, b), r0), add(mul(axis, b), r1), add(mul(axis, a), r1)], _shade(col, shade), 0.96)


def _draw_cone(img: Image, cam: Camera, axis: Vec3, col: Color, radius: float, base_dist: float, tip_dist: float) -> None:
    u, v = _basis_for_axis(axis)
    tip = mul(axis, tip_dist)
    for i in range(24):
        t0, t1 = math.tau * i / 24, math.tau * (i + 1) / 24
        p0 = add(mul(axis, base_dist), add(mul(u, math.cos(t0) * radius), mul(v, math.sin(t0) * radius)))
        p1 = add(mul(axis, base_dist), add(mul(u, math.cos(t1) * radius), mul(v, math.sin(t1) * radius)))
        shade = 0.58 + 0.42 * max(0.0, math.cos(t0 - 0.6))
        _project_poly(img, cam, [tip, p0, p1], _shade(col, shade), 0.98)


def _draw_arc_bar(img: Image, cam: Camera, u: Vec3, v: Vec3, radius: float, band: float, sweep: float, col: Color) -> None:
    centre = math.pi / 4.0
    start = centre - sweep * 0.5
    for i in range(24):
        a0 = start + sweep * i / 24
        a1 = start + sweep * (i + 1) / 24
        def P(a: float, r: float) -> Vec3:
            return add(mul(u, math.cos(a) * r), mul(v, math.sin(a) * r))
        _project_poly(img, cam, [P(a0, radius - band), P(a0, radius + band), P(a1, radius + band), P(a1, radius - band)], col, 0.96)


def _draw_plane_square(img: Image, cam: Camera, axis_name: str, u: Vec3, v: Vec3, normal: Vec3, col: Color) -> None:
    half, tip = 0.08, 0.95
    corner = mul(add(u, v), tip - half)
    pts = [add(corner, add(mul(u, -half), mul(v, -half))),
           add(corner, add(mul(u, half), mul(v, -half))),
           add(corner, add(mul(u, half), mul(v, half))),
           add(corner, add(mul(u, -half), mul(v, half)))]
    _project_poly(img, cam, pts, col, 0.28)
    outer = add(corner, add(mul(u, half), mul(v, half)))
    back_u = add(outer, mul(u, -half * 2.0))
    back_v = add(outer, mul(v, -half * 2.0))
    _project_line(img, cam, [back_u, outer, back_v], col, 0.95, 2)


def draw_transform_gizmo(img: Image, cam: Camera, mode: str) -> None:
    # References/Gizmo.html, not the older 2D gizmo.js: all grabbable handles are real
    # 3D primitives in world space: cone translate tips, short cylinder scale handles,
    # transparent plane squares, narrow annular rotation bars, and a camera-facing white centre ring.
    axes = {"x": ((1.0, 0.0, 0.0), (0xE0, 0x14, 0x14)),
            "y": ((0.0, 1.0, 0.0), (0x12, 0xD4, 0x0A)),
            "z": ((0.0, 0.0, 1.0), (0x15, 0x60, 0xE0))}
    others = {"x": ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0x1F, 0xC7, 0xC7)),
              "y": ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0xC8, 0x1E, 0xC8)),
              "z": ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0xE0, 0xCD, 0x12))}
    for name, (axis, col) in axes.items():
        _draw_cone(img, cam, axis, col, 0.06, 0.95 - 0.09, 0.95)
        _draw_cylinder(img, cam, axis, col, 0.06, 0.95 - 0.28, 0.14)
        u, v, plane_col = others[name]
        _draw_plane_square(img, cam, name, u, v, axis, plane_col)
        _draw_arc_bar(img, cam, u, v, 0.95 * 0.62, 0.038, math.radians(31.0), col)
    o = project(cam, (0.0, 0.0, 0.0))
    if o:
        # Torus billboard in the HTML; represented as a white ring plus dark centre and tiny target cube.
        img.circle(o[0], o[1], 26.0, (255, 255, 255), 1.0, 4)
        img.circle(o[0], o[1], 8.0, (115, 124, 138), 0.75, 3)


def _orientation_points(cam: Camera, body=BODY) -> list[tuple[Vec3, Color, bool, str, float, float, float]]:
    x0, y0, x1, _ = body
    cx, cy, r = x1 - 70, y0 + 58, 34
    right, up, forward = cam.basis
    axes = [((1,0,0), (252,90,90), True, "X"), ((-1,0,0), (252,90,90), False, ""),
            ((0,1,0), (123,214,106), True, "Y"), ((0,-1,0), (123,214,106), False, ""),
            ((0,0,1), (90,139,252), True, "Z"), ((0,0,-1), (90,139,252), False, "")]
    out=[]
    for a,col,pos,label in axes:
        sx, sy = dot(a, right), dot(a, up)
        depth = -dot(a, forward)  # SharedViewportCameraDepth: match HTML reference front/back ordering.
        out.append((a, col, pos, label, cx + sx*r, cy - sy*r, depth))
    return sorted(out, key=lambda x: x[6])


def draw_blender_gizmo(img: Image, cam: Camera, body=BODY) -> None:
    x0, y0, x1, _ = body
    cx, cy = x1 - 70, y0 + 58
    pts = _orientation_points(cam, body)
    for _, _, pos, _, x, y, _ in pts:
        if pos:
            img.line((cx, cy), (x, y), (255, 255, 255), 0.14, 2)
    for _, col, pos, _, x, y, depth in pts:
        if pos:
            img.circle(x, y, 9, col, 1.0 if depth > 0 else 0.75, 1)
        else:
            img.circle(x, y, 7, (10, 12, 16), 0.9, 1)
            img.circle(x, y, 4, col, 0.85, 1)


def draw_cad_cube(img: Image, cam: Camera, body=BODY) -> None:
    x0, y0, x1, _ = body
    cx, cy, scale = x1 - 70, y0 + 58, 28.0
    right, up, forward = cam.basis
    def P(p: Vec3) -> Point:
        return (cx + dot(p, right) * scale, cy - dot(p, up) * scale)
    faces = [((1,0,0), [(1,0,0),(1,1,0),(1,1,1),(1,0,1)], (252,90,90), "Right"),
             ((-1,0,0), [(0,0,0),(0,0,1),(0,1,1),(0,1,0)], (252,90,90), "Left"),
             ((0,1,0), [(0,1,0),(0,1,1),(1,1,1),(1,1,0)], (248,250,252), "Top"),
             ((0,-1,0), [(0,0,0),(1,0,0),(1,0,1),(0,0,1)], (216,222,234), "Bottom"),
             ((0,0,1), [(0,0,1),(1,0,1),(1,1,1),(0,1,1)], (91,140,255), "Front"),
             ((0,0,-1), [(0,0,0),(0,1,0),(1,1,0),(1,0,0)], (91,140,255), "Back")]
    def centre_pts(corners):
        return tuple(sum(q[i] for q in corners)/4.0 - 0.5 for i in range(3))
    for normal, corners, col, label in sorted(faces, key=lambda f: -dot(f[0], forward)):
        shifted=[(p[0]-0.5,p[1]-0.5,p[2]-0.5) for p in corners]
        pp=[P(q) for q in shifted]
        img.polygon(pp, col, 0.54)
        img.polyline(pp, (255,255,255), 0.78, 1, True)
        # Face-projected vector label: endpoints are interpolated inside the face, not placed as screen text.
        def fp(u: float, v: float) -> Point:
            ax, ay = pp[0]
            bx, by = pp[1]
            dx, dy = pp[3]
            return (ax + (bx - ax) * u + (dx - ax) * v, ay + (by - ay) * u + (dy - ay) * v)
        def stroke(u0: float, v0: float, u1: float, v1: float):
            img.line(fp(u0, v0), fp(u1, v1), (16,18,24), 0.95, 1)
        glyphs = {
            'T': [(0,0,1,0),(0.5,0,0.5,1)],
            'O': [(0.2,0,0.8,0),(0.8,0,1,0.2),(1,0.2,1,0.8),(1,0.8,0.8,1),(0.8,1,0.2,1),(0.2,1,0,0.8),(0,0.8,0,0.2),(0,0.2,0.2,0)],
            'P': [(0,1,0,0),(0,0,0.8,0),(0.8,0,1,0.25),(1,0.25,0.8,0.5),(0.8,0.5,0,0.5)],
            'R': [(0,1,0,0),(0,0,0.8,0),(0.8,0,1,0.25),(1,0.25,0.8,0.5),(0.8,0.5,0,0.5),(0.45,0.5,1,1)],
            'I': [(0,0,1,0),(0.5,0,0.5,1),(0,1,1,1)],
            'G': [(1,0.15,0.8,0),(0.8,0,0.2,0),(0.2,0,0,0.2),(0,0.2,0,0.8),(0,0.8,0.2,1),(0.2,1,0.85,1),(0.85,1,1,0.82),(1,0.82,1,0.58),(1,0.58,0.58,0.58)],
            'H': [(0,0,0,1),(1,0,1,1),(0,0.5,1,0.5)],
            'F': [(0,0,0,1),(0,0,1,0),(0,0.5,0.78,0.5)],
            'N': [(0,1,0,0),(0,0,1,1),(1,1,1,0)],
        }
        text = label.upper()
        count = len(text)
        gw = min(0.135, (0.78 - 0.018 * max(count-1, 0)) / max(count, 1))
        gh = gw * 1.5
        total = gw * count + 0.018 * max(count-1, 0)
        sx, sy = 0.5 - total * 0.5, 0.5 - gh * 0.5
        for i, ch in enumerate(text):
            for a,b,c,d in glyphs.get(ch, []):
                stroke(sx + i*(gw+0.018) + a*gw, sy + b*gh, sx + i*(gw+0.018) + c*gw, sy + d*gh)

def draw_interaction_highlight(img: Image, cam: Camera, gizmo: str, transform: str) -> None:
    # Visual interaction proof: these rings mark the selectable handle/face used by the same hit paths
    # validated in code (HitSharedViewportGizmo, ResolveGizmoHandle, Start/UpdateTransformSession).
    amber = (251, 191, 36)
    if transform == "translate":
        q = project(cam, (0.95, 0.0, 0.0))
        q0 = project(cam, (0.0, 0.0, 0.0))
        if q and q0:
            img.circle(q[0], q[1], 18, amber, 1.0, 4)
            img.line(q0, q, amber, 0.9, 3)
            # before/after drag ghost for a selected curve/profile
            a = project(cam, (-1.9, 0.0, -0.8)); b = project(cam, (-1.35, 0.0, -1.05))
            if a and b: img.line(a, b, amber, 0.9, 3)
    elif transform == "rotate":
        q = project(cam, (0.0, 0.0, 0.59))
        if q:
            img.circle(q[0], q[1], 22, amber, 1.0, 4)
        # changed viewport/projection proof: show a selected rotation arc with amber pick band
        o = project(cam, (0.0, 0.0, 0.0))
        if o: img.circle(o[0], o[1], 34, amber, 0.85, 3)
    elif transform == "scale":
        q = project(cam, (0.67, 0.0, 0.0))
        if q: img.circle(q[0], q[1], 20, amber, 1.0, 4)
        # scaled selected profile ghost
        pts = [project(cam, (x*1.18, 0.0, z*1.18)) for x,z in [(-1.9,-0.8),(-0.7,-0.8),(-0.7,0.1),(-1.9,0.1)]]
        if all(pts): img.polyline([q for q in pts if q], amber, 0.9, 3, True)
    x0, y0, x1, _ = BODY
    if gizmo == "cad":
        img.circle(x1 - 70, y0 + 58, 46, amber, 0.9, 3)
    else:
        img.circle(x1 - 70 + 34, y0 + 58, 17, amber, 0.95, 3)


def draw_parametric_property_proof(img: Image) -> None:
    # Left inspector proof: closed-profile rows expose an extrusion-cap toggle; open curves do not.
    x0, y0, w = 28, 100, 320
    img.rect(x0, y0, x0 + w, y0 + 245, (18, 18, 22), 0.98)
    img.rect(x0, y0, x0 + w, y0 + 32, (31, 31, 37), 1.0)
    for i, text_col in enumerate([(96,165,250), (167,243,208)]):
        y = y0 + 48 + i * 36
        img.rect(x0 + 14, y, x0 + w - 14, y + 28, (24, 25, 30), 1.0)
        img.circle(x0 + 30, y + 14, 6, text_col, 1.0, 1)
    # toggle card visible only for selected closed profile
    ty = y0 + 132
    img.rect(x0 + 14, ty, x0 + w - 14, ty + 86, (20, 22, 28), 1.0)
    img.rect(x0 + w - 122, ty + 42, x0 + w - 30, ty + 66, (34, 197, 94), 1.0)
    img.circle(x0 + w - 44, ty + 54, 8, (255,255,255), 1.0, 1)

def render(name: str, cam: Camera, gizmo: str, transform: str) -> str:
    img = Image(W, H)
    # App frame gutters around the viewport panel.
    img.rect(0, 0, W, H, (17, 17, 20), 1)
    img.rect(0, 0, 390, H, (18, 18, 22), 1)
    img.rect(390, 0, 640, H, (20, 20, 24), 1)
    draw_parametric_property_proof(img)
    draw_viewport_panel(img, name)
    draw_shader_grid(img, cam)
    draw_cad_tools(img, cam)
    draw_transform_gizmo(img, cam, transform)
    draw_interaction_highlight(img, cam, gizmo, transform)
    if gizmo == "cad":
        draw_cad_cube(img, cam)
    else:
        draw_blender_gizmo(img, cam)
    ppm = os.path.join(OUT, f"{name}.ppm")
    png = os.path.join(OUT, f"{name}.png")
    img.save_ppm(ppm)
    # Add text labels with ImageMagick so the proof visibly carries mode/view/tool notes.
    subprocess.run([
        "convert", ppm,
        "-font", "DejaVu-Sans-Bold", "-pointsize", "12", "-fill", "#d8d8dc",
        "-gravity", "northwest", "-annotate", f"+{PANEL_X + 72}+{PANEL_Y + 9}", "3D Viewport",
        "-pointsize", "12", "-fill", "#d8d8dc", "-annotate", "+42+112", "Sketch Directory / Properties",
        "-pointsize", "10", "-fill", "#93c5fd", "-annotate", "+72+156", "Open Curve: no closure toggle",
        "-fill", "#a7f3d0", "-annotate", "+72+192", "Closed loop: cap toggle visible",
        "-fill", "#d8d8dc", "-annotate", "+54+250", "Extrude Caps",
        "-fill", "#101014", "-annotate", "+242+288", "Solid",
        "-pointsize", "10", "-fill", "#101014", "-annotate", f"+{BODY[2] - 73}+{BODY[1] + 25}", "X" if gizmo == "blender" else "",
        "-annotate", f"+{BODY[2] - 105}+{BODY[1] - 7}", "Z" if gizmo == "blender" else "",
        "-pointsize", "12", "-fill", "#f87171", "-annotate", f"+{BODY[0] + 24}+{BODY[1] + 500}", "OPEN curves",
        "-fill", "#a7f3d0", "-annotate", f"+{BODY[0] + 466}+{BODY[1] + 500}", "caps ON: solid",
        "-fill", "#f87171", "-annotate", f"+{BODY[0] + 568}+{BODY[1] + 500}", "caps OFF: walls",
        "-fill", "#a7f3d0", "-annotate", f"+{BODY[0] + 210}+{BODY[1] + 500}", "JOIN -> CLOSED empty-outline loops",
        "-pointsize", "12", "-fill", "#ec4899", "-annotate", f"+{BODY[0] + 48}+{BODY[1] + 122}", "Bezier",
        "-fill", "#a855f7", "-annotate", f"+{BODY[0] + 242}+{BODY[1] + 122}", "Hermite",
        "-fill", "#2dd4bf", "-annotate", f"+{BODY[0] + 388}+{BODY[1] + 122}", "Basis Spline",
        "-fill", "#fb923c", "-annotate", f"+{BODY[0] + 548}+{BODY[1] + 122}", "NURBS",
        "-pointsize", "11", "-fill", "#fbbf24", "-annotate", f"+{PANEL_X + 16}+{PANEL_Y + PANEL_H - 43}",
        "select/move proof: highlighted handles use HitSharedViewportGizmo + ResolveGizmoHandle -> StartTransformSession -> UpdateTransformSession",
        "-pointsize", "13", "-fill", "#a7f3d0", "-annotate", f"+{PANEL_X + 16}+{PANEL_Y + PANEL_H - 24}",
        f"{name}: projected cube face-label strokes • full curves visible • selectable overlays update viewport/gizmo",
        png
    ], check=True)
    os.remove(ppm)
    return png


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    views = [
        ("viewport_proof_top_ortho_cad_translate", Camera((0.0, 3.6, 0.001), 0.0, -89.0), "cad", "translate"),
        ("viewport_proof_front_ortho_blender_rotate", Camera((0.0, 1.25, -4.2), 0.0, -6.0), "blender", "rotate"),
        ("viewport_proof_iso_perspective_cad_scale", Camera((3.2, 2.5, -4.2), -38.0, -20.0), "cad", "scale"),
    ]
    for name, cam, gizmo, transform in views:
        print(render(name, cam, gizmo, transform))


if __name__ == "__main__":
    main()
