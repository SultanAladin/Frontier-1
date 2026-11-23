// Swept point collision against the analytic course, static world boxes and convex debris.
// Particles have a 3 mm skin; unlike endpoint tests, thin walls cannot be skipped.
import { ResolveCourseHeight, ResolveCourseNormal } from './CourseStructure.js';
const Skin = 0.003;
const Dot = (A, B) => A.reduce((Sum, Value, Axis) => Sum + Value * B[Axis], 0);
export function SweepConvex(From, To, Planes) {
    let Enter = 0, Exit = 1, Normal = null, Inside = true, Nearest = -Infinity, ExitNormal = null;
    for (let Offset = 0; Offset < Planes.length; Offset += 4) {
        const N = Planes.slice(Offset, Offset + 3), D = Planes[Offset + 3] + Skin;
        const A = Dot(N, From) - D, B = Dot(N, To) - D;
        if (A > 0) Inside = false;
        if (A > Nearest) { Nearest = A; ExitNormal = N; }
        if (A > 0 && B >= A) return null;
        if (A <= 0 && B <= 0) continue;
        const T = A / (A - B);
        if (A > B) { if (T >= Enter) { Enter = T; Normal = N; } }
        else Exit = Math.min(Exit, T);
        if (Enter > Exit) return null;
    }
    if (Inside) return { Time: 0, Normal: ExitNormal, Push: -Nearest + Skin };
    return Normal && Enter <= 1 ? { Time: Enter, Normal, Push: Skin } : null;
}
export function BoxPlanes({ Minimum: A, Maximum: B }) {
    return [1,0,0,B[0], -1,0,0,-A[0], 0,1,0,B[1], 0,-1,0,-A[1], 0,0,1,B[2], 0,0,-1,-A[2]];
}
export function SweepSpark(From, To, Boxes = [], Bodies = []) {
    let Hit = null;
    const Take = Candidate => { if (Candidate && (!Hit || Candidate.Time < Hit.Time)) Hit = Candidate; };
    const Overlaps = (Minimum, Maximum) => Minimum.every((Low, Axis) =>
        Math.max(From[Axis], To[Axis]) + Skin >= Low && Math.min(From[Axis], To[Axis]) - Skin <= Maximum[Axis]);
    for (const Box of Boxes) if (Overlaps(Box.Minimum, Box.Maximum)) Take(SweepConvex(From, To, Box.Planes || BoxPlanes(Box)));
    for (const Body of Bodies) {
        const Min = Body.Position.map(Value => Value - Body.Radius), Max = Body.Position.map(Value => Value + Body.Radius);
        if (Overlaps(Min, Max)) Take(SweepConvex(From, To, Body.WorldPlanes));
    }
    // Resolve the course in <= 2 cm horizontal intervals, then bisect the first crossing.
    // This also catches the ramp and raised kerbs when both endpoints lie above the flat pad.
    const Count = Math.max(1, Math.ceil(Math.hypot(To[0] - From[0], To[1] - From[1]) / 0.02));
    const At = T => From.map((Value, Axis) => Value + (To[Axis] - Value) * T);
    const Height = P => P[2] - ResolveCourseHeight(P[0], P[1]) - Skin;
    for (let Index = 0; Index <= Count; ++Index) {
        const T = Index / Count;
        if (Hit && T > Hit.Time) break;
        const Point = At(T);
        if (Height(Point) > 0) continue;
        let Low = Math.max(0, (Index - 1) / Count), High = T;
        for (let Step = 0; Step < 10; ++Step) {
            const Mid = (Low + High) / 2;
            if (Height(At(Mid)) > 0) Low = Mid; else High = Mid;
        }
        const Contact = At(High), Normal = ResolveCourseNormal(Contact[0], Contact[1]);
        Take({ Time: High, Normal, Push: Math.max(Skin, -Height(Contact) + Skin) });
        break;
    }
    return Hit;
}
export function BounceSpark(Velocity, Normal) {
    const Approach = Dot(Velocity, Normal);
    return Velocity.map((Value, Axis) => (Value - Math.min(0, Approach) * Normal[Axis]) * 0.55
        - Math.min(0, Approach) * Normal[Axis] * 0.28);
}
