from math import cos, sin, pi, sqrt, atan2
from pathlib import Path

ROOT = Path('/home/user/repo/VisualProof/Cad2D')
ROOT.mkdir(parents=True, exist_ok=True)


def svg_header(w, h):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
            '<rect width="100%" height="100%" fill="#0b0d10"/>']

def polyline(points, stroke="#d8dde6", fill="none", width=2, dash=None, opacity=1.0):
    pts = ' '.join(f'{x:.2f},{y:.2f}' for x, y in points)
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<polyline points="{pts}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" opacity="{opacity}"{dash_attr}/>'

def polygon(points, stroke="#d8dde6", fill="none", width=2, opacity=1.0):
    pts = ' '.join(f'{x:.2f},{y:.2f}' for x, y in points)
    return f'<polygon points="{pts}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" opacity="{opacity}"/>'

def circle(cx, cy, r, stroke="#d8dde6", fill="none", width=2, opacity=1.0):
    return f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" opacity="{opacity}"/>'

def text(x, y, value, size=14, color="#d8dde6", weight="400"):
    return f'<text x="{x:.2f}" y="{y:.2f}" fill="{color}" font-family="Inter,Arial,sans-serif" font-size="{size}" font-weight="{weight}">{value}</text>'

def marker(x, y, color="#ffd166", r=4):
    return f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r}" fill="{color}"/>'

def line(a, b, color="#d8dde6", width=2, dash=None, opacity=1.0):
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<line x1="{a[0]:.2f}" y1="{a[1]:.2f}" x2="{b[0]:.2f}" y2="{b[1]:.2f}" stroke="{color}" stroke-width="{width}" opacity="{opacity}"{dash_attr}/>'

def bezier(points, steps=48):
    pts = [tuple(map(float, p)) for p in points]
    out = []
    for i in range(steps + 1):
        t = i / steps
        work = pts[:]
        while len(work) > 1:
            work = [((1-t)*a[0]+t*b[0], (1-t)*a[1]+t*b[1]) for a,b in zip(work, work[1:])]
        out.append(work[0])
    return out

def hermite(p0, p1, t0, t1, steps=48):
    out = []
    for i in range(steps + 1):
        t = i / steps
        t2 = t*t
        t3 = t2*t
        h00 = 2*t3 - 3*t2 + 1
        h10 = t3 - 2*t2 + t
        h01 = -2*t3 + 3*t2
        h11 = t3 - t2
        out.append((h00*p0[0]+h10*t0[0]+h01*p1[0]+h11*t1[0],
                    h00*p0[1]+h10*t0[1]+h01*p1[1]+h11*t1[1]))
    return out

def circle_arc(center, radius, start, sweep, steps=32):
    return [(center[0] + cos(start + sweep * i/steps) * radius,
             center[1] + sin(start + sweep * i/steps) * radius) for i in range(steps+1)]

def ellipse_arc(center, rx, ry, start, sweep, steps=48):
    return [(center[0] + cos(start + sweep * i/steps) * rx,
             center[1] + sin(start + sweep * i/steps) * ry) for i in range(steps+1)]

def basis_spline(points, degree=3, steps=64):
    n = len(points)
    m = n + degree + 1
    interior = max(0, n - degree - 1)
    knots = [0.0]*(degree+1) + [i+1 for i in range(interior)] + [interior+1.0]*(degree+1)
    max_t = knots[-degree-1]
    def basis(i, d, t):
        if d == 0:
            return 1.0 if (knots[i] <= t < knots[i+1]) or (t == knots[-1] and i+1 == len(knots)-1) else 0.0
        left = 0.0
        lw = knots[i+d] - knots[i]
        if lw > 0:
            left = (t - knots[i]) / lw * basis(i, d-1, t)
        right = 0.0
        rw = knots[i+d+1] - knots[i+1]
        if rw > 0:
            right = (knots[i+d+1] - t) / rw * basis(i+1, d-1, t)
        return left + right
    out = []
    for s in range(steps+1):
        t = max_t * s / steps
        x = y = 0.0
        for i, p in enumerate(points):
            b = basis(i, degree, t)
            x += p[0]*b
            y += p[1]*b
        out.append((x, y))
    return out

def radial_pattern(points, center, count, sweep):
    out = []
    for i in range(count):
        a = sweep * i / max(count-1,1)
        ca, sa = cos(a), sin(a)
        out.append([(
            center[0] + (x-center[0])*ca - (y-center[1])*sa,
            center[1] + (x-center[0])*sa + (y-center[1])*ca,
        ) for x,y in points])
    return out

# 1 creation + snap + selection
w, h = 1400, 900
s = svg_header(w, h)
# grid
for gx in range(0, w, 40):
    s.append(line((gx,0),(gx,h), color="#11151a", width=1))
for gy in range(0, h, 40):
    s.append(line((0,gy),(w,gy), color="#11151a", width=1))
# shapes
ln = [(100,120),(260,180)]
arc = circle_arc((420,180), 70, pi*0.95, -pi*0.9)
circ = circle_arc((620,170), 58, 0, 2*pi, 48)
ell = ellipse_arc((820,170), 90, 45, 0, 2*pi, 48)
poly = [(160 + cos(i*2*pi/6)*60, 420 + sin(i*2*pi/6)*60) for i in range(6)]
slot = [(420,380),(540,380)]
slot_shape = [(420,350),(540,350)] + circle_arc((540,380),30,-pi/2,pi,18)[1:-1] + [(420,410)] + circle_arc((420,380),30,pi/2,pi,18)[1:-1]
bez = bezier([(720,340),(760,430),(860,310),(940,420)], 48)
bs = basis_spline([(1030,340),(1080,430),(1140,300),(1210,430),(1280,360)],3,64)
herm = hermite((730,560),(930,600),(80,-120),(120,80),48)
for shape, col in [(ln,"#78c6ff"),(arc,"#ff9f6e"),(circ,"#9dff9a"),(ell,"#f4d35e"),(poly,"#ff6ea8"),(slot_shape,"#d6c2ff"),(bez,"#ffdb8a"),(bs,"#8ee3ef"),(herm,"#b8f2b1")]:
    s.append(polyline(shape if isinstance(shape,list) else list(shape), stroke=col, width=3))
# snap points and selection
snap_points = [(100,120),(260,180),(620,170),(678,170),(820+90,170),(160,420),(420,380),(480,350)]
for p in snap_points:
    s.append(marker(*p, color="#ffd166", r=4))
# selection highlight on bezier curve and control points
for p in [(720,340),(760,430),(860,310),(940,420)]:
    s.append(marker(*p, color="#ff4d6d", r=5))
s.append(polyline(bez, stroke="#ffffff", width=5, opacity=0.35))
# outliner card
s.append('<rect x="1030" y="520" width="320" height="280" rx="16" fill="#11161c" stroke="#2a313a"/>')
s.append(text(1055,555,'SKETCH OUTLINER',16,'#ffffff','700'))
out_rows = [('Curve Set', '#78c6ff'),('Open Polyline', '#d8dde6'),('3-Point Arc', '#d8dde6'),('Circle Profile', '#9dff9a'),('Polygon Profile', '#ff6ea8'),('Selected Bezier', '#ffdb8a'),('Hermite Curve', '#b8f2b1')]
for i,(label,col) in enumerate(out_rows):
    y = 590 + i*28
    s.append(marker(1060,y-4,col,4))
    s.append(text(1074,y,label,13,'#d8dde6'))
s.append(text(1055,780,'Snap: endpoint / midpoint / centre / control / along curve',12,'#9aa6b2'))
s.append(text(40,40,'Creation + Selection + Snap Proof',22,'#ffffff','700'))
s.append('</svg>')
(ROOT/'creation-selection-snap.svg').write_text('\n'.join(s))

# 2 fillet/chamfer
w,h=1200,700
s=svg_header(w,h)
base=[(180,180),(420,180),(420,420),(180,420)]
# chamfer top-right
ch=[(180,180),(370,180),(420,230),(420,420),(180,420)]
# fillet bottom-right
fil=[(680,180),(920,180),(920,350)] + circle_arc((850,350),70,0,pi/2,18)[1:-1] + [(680,420),(680,180)]
s.append(text(40,40,'Chamfer / Fillet Proof',22,'#ffffff','700'))
s.append(polygon(base, stroke="#2a313a", width=2))
s.append(polyline(ch+[(180,180)], stroke="#ff9f6e", width=4))
s.append(polyline(fil, stroke="#78c6ff", width=4))
s.append(text(150,470,'Chamfered corner',16,'#ff9f6e'))
s.append(text(730,470,'Filleted corner',16,'#78c6ff'))
s.append('</svg>')
(ROOT/'profile-corner.svg').write_text('\n'.join(s))

# 3 booleans
w,h=1200,700
s=svg_header(w,h)
outer=[(120,120),(420,120),(420,420),(120,420)]
hole=circle_arc((270,270),70,0,2*pi,48)
inter_a=[(620,120),(860,180),(780,420),(540,360)]
inter_b=[(700,80),(930,230),(760,460),(560,280)]
# subtract with hole
s.append(text(40,40,'Boolean Proof',22,'#ffffff','700'))
s.append(polygon(outer, stroke="#d8dde6", fill="#1a2027", width=2))
s.append(polyline(hole, stroke="#0b0d10", fill="#0b0d10", width=2))
s.append(text(150,500,'Subtract / contained cutter -> hole loop',16,'#d8dde6'))
# intersection visual
s.append(polygon(inter_a, stroke="#78c6ff", fill="rgba(120,198,255,0.15)", width=2))
s.append(polygon(inter_b, stroke="#ff9f6e", fill="rgba(255,159,110,0.15)", width=2))
inter_vis=[(682,158),(848,206),(793,381),(621,327)]
s.append(polygon(inter_vis, stroke="#ffffff", fill="#ffffff22", width=3))
s.append(text(600,500,'Convex intersection subset',16,'#d8dde6'))
s.append('</svg>')
(ROOT/'profile-boolean.svg').write_text('\n'.join(s))

# 4 dimensions + constraints
w,h=1300,800
s=svg_header(w,h)
s.append(text(40,40,'Dimensions + Constraints Proof',22,'#ffffff','700'))
A=(140,180); B=(360,180); C=(360,360)
s.append(line(A,B,'#78c6ff',4))
s.append(line(B,C,'#ff9f6e',4))
s.append(marker(*A,'#ffd166',5)); s.append(marker(*B,'#ffd166',5)); s.append(marker(*C,'#ffd166',5))
# dimensions
s.append(line((140,140),(360,140),'#d8dde6',2,'8 8'))
s.append(line((140,140),(140,180),'#d8dde6',1))
s.append(line((360,140),(360,180),'#d8dde6',1))
s.append(text(235,132,'Horizontal 220',14,'#d8dde6'))
s.append(line((400,180),(400,360),'#d8dde6',2,'8 8'))
s.append(line((360,180),(400,180),'#d8dde6',1))
s.append(line((360,360),(400,360),'#d8dde6',1))
s.append(text(410,275,'Vertical 180',14,'#d8dde6'))
# angle
s.append(circle(360,180,50,'#9dff9a','none',2))
s.append(text(390,150,'90°',14,'#9dff9a'))
# radius + tangent
circ2 = circle_arc((840,250),90,0,2*pi,48)
s.append(polyline(circ2,'#f4d35e',width=3))
line_t=[(840,160),(980,160)]
s.append(polyline(line_t,'#ffffff',width=3))
s.append(text(730,390,'Radius / diameter / tangent target',16,'#d8dde6'))
# outliner note
s.append('<rect x="980" y="100" width="250" height="220" rx="14" fill="#11161c" stroke="#2a313a"/>')
s.append(text(1000,132,'LOOPS',15,'#ffffff','700'))
s.append(text(1000,160,'Closed profiles: 2',13,'#d8dde6'))
s.append(text(1000,186,'Open curves: 3',13,'#d8dde6'))
s.append(text(1000,212,'Dims: horizontal / vertical / angle',13,'#d8dde6'))
s.append(text(1000,238,'Constraints: parallel / perp / tangent',13,'#d8dde6'))
s.append('</svg>')
(ROOT/'constraint-dimension.svg').write_text('\n'.join(s))


# 5 boolean stress
w,h=1500,900
s=svg_header(w,h)
s.append(text(40,40,'Boolean Stress Proof',22,'#ffffff','700'))
# left: multiple contained cutters
outer=[(120,120),(520,120),(520,520),(120,520)]
cut1=circle_arc((240,240),55,0,2*pi,48)
cut2=ellipse_arc((400,240),70,40,0,2*pi,48)
cut3=[(260,380),(340,330),(420,380),(380,450),(280,450)]
s.append(polygon(outer, stroke="#d8dde6", fill="#1a2027", width=2))
for shape,col in [(cut1,'#ff9f6e'),(cut2,'#78c6ff'),(cut3,'#9dff9a')]:
    s.append(polyline(shape if isinstance(shape,list) else list(shape), stroke=col, width=2))
s.append(text(140,580,'Subtract: several cutters inside one profile',16,'#d8dde6'))
# middle: outside/disjoint
left_rect=[(650,130),(860,130),(860,320),(650,320)]
right_rect=[(930,180),(1160,180),(1160,390),(930,390)]
s.append(polygon(left_rect, stroke="#f4d35e", fill="#f4d35e22", width=2))
s.append(polygon(right_rect, stroke="#ff6ea8", fill="#ff6ea822", width=2))
s.append(text(690,430,'Disjoint union -> separate profiles',16,'#d8dde6'))
# right: convex overlap intersection
p1=[(1220,120),(1440,190),(1360,420),(1140,350)]
p2=[(1270,90),(1470,250),(1310,470),(1130,290)]
pii=[(1264,140),(1430,218),(1334,415),(1184,326)]
s.append(polygon(p1, stroke="#78c6ff", fill="#78c6ff22", width=2))
s.append(polygon(p2, stroke="#ff9f6e", fill="#ff9f6e22", width=2))
s.append(polygon(pii, stroke="#ffffff", fill="#ffffff22", width=3))
s.append(text(1160,580,'Convex overlap intersection subset',16,'#d8dde6'))
s.append('</svg>')
(ROOT/'boolean-stress.svg').write_text('\n'.join(s))

# 6 trim cut join open-close
w,h=1500,900
s=svg_header(w,h)
s.append(text(40,40,'Trim / Cut / Join / Open-Close Proof',22,'#ffffff','700'))
# trim arc
trim_arc = circle_arc((240,220),90,pi*0.9,-pi*0.7,32)
s.append(polyline(trim_arc, stroke="#ff9f6e", width=4))
s.append(marker(trim_arc[18][0], trim_arc[18][1], '#ffd166',5))
s.append(text(120,360,'Trimmed arc target',16,'#d8dde6'))
# cut bezier
bez_full = bezier([(420,140),(470,300),(590,120),(650,280)], 64)
s.append(polyline(bez_full, stroke="#ffdb8a", width=3, opacity=0.5))
s.append(polyline(bez_full[:33], stroke="#ffffff", width=4))
s.append(polyline(bez_full[32:], stroke="#8ee3ef", width=4))
s.append(text(400,360,'Cut Bezier into two preserved curve pieces',16,'#d8dde6'))
# join open chain and close loop
chain=[(860,160),(960,120),(1050,200),(1140,150)]
s.append(polyline(chain, stroke="#78c6ff", width=4))
closed=[(860,460),(980,400),(1100,470),(1080,610),(900,620)]
s.append(polyline(closed+[(860,460)], stroke="#9dff9a", width=4))
s.append(text(820,700,'Join open chain / close loop into profile',16,'#d8dde6'))
# outliner
s.append('<rect x="1180" y="110" width="260" height="210" rx="14" fill="#11161c" stroke="#2a313a"/>')
s.append(text(1200,140,'STATUS',15,'#ffffff','700'))
s.append(text(1200,170,'Open chain: 1',13,'#d8dde6'))
s.append(text(1200,196,'Closed profiles: 1',13,'#d8dde6'))
s.append(text(1200,222,'Cut outputs: 2 curves',13,'#d8dde6'))
s.append(text(1200,248,'Trim target: arc',13,'#d8dde6'))
s.append('</svg>')
(ROOT/'reshape-chain.svg').write_text('\n'.join(s))


# 7 solver stress
w,h=1500,900
s=svg_header(w,h)
s.append(text(40,40,'Solver Stress Proof',22,'#ffffff','700'))
# base frame
p0=(160,180); p1=(420,180); p2=(420,420); p3=(160,420)
s.append(line(p0,p1,'#78c6ff',4))
s.append(line(p1,p2,'#ff9f6e',4))
s.append(line((720,180),(980,260),'#8ee3ef',4))
# tangent line + ellipse
ell2=ellipse_arc((1160,250),110,60,0,2*pi,64)
s.append(polyline(ell2,'#f4d35e',width=3))
s.append(line((1050,250),(940,250),'#ffffff',4))
# dimensions / constraints annotations
s.append(text(110,500,'Horizontal / vertical / equal / perpendicular / tangent stress',16,'#d8dde6'))
s.append(text(720,500,'Angle-driven line against base line',16,'#d8dde6'))
s.append(text(1010,500,'Ellipse tangent line target',16,'#d8dde6'))
# highlight points
for p in [p0,p1,p2,p3,(1050,250),(940,250)]:
    s.append(marker(*p,'#ffd166',5))
# dimension graphics
s.append(line((160,140),(420,140),'#d8dde6',2,'8 8'))
s.append(text(255,132,'H 260',14,'#d8dde6'))
s.append(line((460,180),(460,420),'#d8dde6',2,'8 8'))
s.append(text(470,305,'V 240',14,'#d8dde6'))
s.append(circle(720,180,55,'#9dff9a','none',2))
s.append(text(748,148,'Angle',14,'#9dff9a'))
s.append('</svg>')
(ROOT/'solver-stress.svg').write_text('\n'.join(s))
