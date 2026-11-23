// =============================================================================
//                              SHAREDSCENE.JS
// =============================================================================
// The identical half of both probe-spawn prototypes: OBJ load, the radial
// Suzanne array, camera, software rasteriser, and the stats overlay.
//
// Both A (tile-election) and B (micro-raster) import this UNCHANGED so any
// difference you see on screen belongs to the spawn strategy, not the scene.
//
// Rendering is a plain CPU rasteriser into a 2D canvas. That is deliberate:
// the question under test is WHERE PROBES LAND, and a visibility buffer with
// per-pixel triangle IDs is exactly what both strategies need to read. No
// WebGPU, so it runs anywhere and the numbers are comparable.
// =============================================================================

// ---------------------------------------------------------------- OBJ loading
// Suzzane.obj is Blender-exported QUADS (f v/vt/vn x4, 507 verts / 500 faces).
// Fan-triangulate every face; ignore vt/vn (we want FLAT normals, same as
// SurfelSpawnRequest.comp's reconstruct, which crosses two triangle edges).
export async function LoadObj(Url)
{
    const Text = await (await fetch(Url)).text();
    const Positions = [];
    const Triangles = [];

    for (const RawLine of Text.split('\n'))
    {
        const Line = RawLine.trim();
        if (Line.startsWith('v '))
        {
            const P = Line.split(/\s+/);
            Positions.push([parseFloat(P[1]), parseFloat(P[2]), parseFloat(P[3])]);
        }
        else if (Line.startsWith('f '))
        {
            const Corners = Line.split(/\s+/).slice(1).map(C => parseInt(C.split('/')[0], 10) - 1);
            for (let I = 1; I + 1 < Corners.length; ++I)
                Triangles.push([Corners[0], Corners[I], Corners[I + 1]]);
        }
    }
    return { Positions, Triangles };
}

// ------------------------------------------------------------------ the scene
// A radial array of Suzannes, each a different hue. Distinct hues per instance
// make it obvious at a glance whether probes inherit the surface they sit on.
export const SUZANNE_COUNT  = 7;
export const SUZANNE_RADIUS = 3.4;

export function BuildScene(Mesh)
{
    const Instances = [];
    for (let I = 0; I < SUZANNE_COUNT; ++I)
    {
        const Angle = (I / SUZANNE_COUNT) * Math.PI * 2.0;
        const Hue   = (I / SUZANNE_COUNT) * 360.0;
        Instances.push({
            Index: I,
            Centre: [Math.cos(Angle) * SUZANNE_RADIUS, 0.0, Math.sin(Angle) * SUZANNE_RADIUS],
            YawAngle: -Angle,
            Colour: HslToRgb(Hue, 0.62, 0.56),
        });
    }

    // Flatten to world-space triangles once. Static geometry, so this is a
    // one-time cost and both prototypes pay it identically.
    const WorldTriangles = [];
    for (const Instance of Instances)
    {
        const Cos = Math.cos(Instance.YawAngle), Sin = Math.sin(Instance.YawAngle);
        for (const Tri of Mesh.Triangles)
        {
            const Corners = Tri.map(VertexIndex =>
            {
                const [X, Y, Z] = Mesh.Positions[VertexIndex];
                return [
                    Cos * X + Sin * Z + Instance.Centre[0],
                    Y                 + Instance.Centre[1],
                    -Sin * X + Cos * Z + Instance.Centre[2],
                ];
            });
            const Normal = TriangleNormal(Corners[0], Corners[1], Corners[2]);
            WorldTriangles.push({ Corners, Normal, Instance: Instance.Index, Colour: Instance.Colour });
        }
    }
    return { Instances, WorldTriangles };
}

function HslToRgb(H, S, L)
{
    const Chroma = (1 - Math.abs(2 * L - 1)) * S;
    const HPrime = H / 60.0;
    const Second = Chroma * (1 - Math.abs((HPrime % 2) - 1));
    let Rgb = [0, 0, 0];
    if      (HPrime < 1) Rgb = [Chroma, Second, 0];
    else if (HPrime < 2) Rgb = [Second, Chroma, 0];
    else if (HPrime < 3) Rgb = [0, Chroma, Second];
    else if (HPrime < 4) Rgb = [0, Second, Chroma];
    else if (HPrime < 5) Rgb = [Second, 0, Chroma];
    else                 Rgb = [Chroma, 0, Second];
    const Match = L - Chroma / 2;
    return [Rgb[0] + Match, Rgb[1] + Match, Rgb[2] + Match];
}

// ------------------------------------------------------------- vector helpers
export const Sub   = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
export const Add   = (A, B) => [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
export const Scale = (A, S) => [A[0] * S, A[1] * S, A[2] * S];
export const Dot   = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
export const Cross = (A, B) => [A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0]];
export const Length = A => Math.sqrt(Dot(A, A));
export function Normalise(A) { const L = Length(A); return L > 1e-9 ? Scale(A, 1 / L) : [0, 0, 1]; }

function TriangleNormal(A, B, C) { return Normalise(Cross(Sub(B, A), Sub(C, A))); }

// -------------------------------------------------------------------- camera
// Orbit camera. Both prototypes share the orbit state so the two windows can
// be compared frame-for-frame at the same viewpoint.
export function CreateCamera()
{
    return { Yaw: 0.62, Pitch: 0.42, Distance: 11.0, Target: [0, 0, 0], Fov: 50 * Math.PI / 180 };
}

export function CameraBasis(Camera)
{
    const CosPitch = Math.cos(Camera.Pitch);
    const Eye = Add(Camera.Target, [
        Math.cos(Camera.Yaw) * CosPitch * Camera.Distance,
        Math.sin(Camera.Pitch)          * Camera.Distance,
        Math.sin(Camera.Yaw) * CosPitch * Camera.Distance,
    ]);
    const Forward = Normalise(Sub(Camera.Target, Eye));
    const Right   = Normalise(Cross(Forward, [0, 1, 0]));
    const Up      = Cross(Right, Forward);
    return { Eye, Forward, Right, Up };
}

// Project a world point to pixel coords. Returns null behind the eye.
export function ProjectPoint(World, Basis, Camera, Width, Height)
{
    const Offset = Sub(World, Basis.Eye);
    const Depth  = Dot(Offset, Basis.Forward);
    if (Depth <= 0.02) return null;
    const HalfHeight = Math.tan(Camera.Fov * 0.5);
    const HalfWidth  = HalfHeight * (Width / Height);
    const XNdc = Dot(Offset, Basis.Right) / (Depth * HalfWidth);
    const YNdc = Dot(Offset, Basis.Up)    / (Depth * HalfHeight);
    return { X: (XNdc * 0.5 + 0.5) * Width, Y: (0.5 - YNdc * 0.5) * Height, Depth };
}

// ------------------------------------------------------- visibility rasteriser
// Renders the scene into a visibility buffer: per pixel a triangle ID + depth,
// which is precisely the input SurfelSpawnRequest.comp reads (it reconstructs
// world position from a packed identity rather than sampling a G-buffer).
export class VisibilityBuffer
{
    constructor(Width, Height)
    {
        this.Width  = Width;
        this.Height = Height;
        this.Identity = new Int32Array(Width * Height).fill(-1);
        this.Depth    = new Float32Array(Width * Height).fill(Infinity);
        this.Shade    = new Float32Array(Width * Height);
    }

    Clear()
    {
        this.Identity.fill(-1);
        this.Depth.fill(Infinity);
        this.Shade.fill(0);
    }

    Render(Triangles, Basis, Camera)
    {
        this.Clear();
        const { Width, Height } = this;
        let Drawn = 0;

        for (let T = 0; T < Triangles.length; ++T)
        {
            const Tri = Triangles[T];

            // Backface cull against the view vector, matching the reconstruct's
            // normal flip (it points the normal back toward the eye).
            const ToEye = Sub(Basis.Eye, Tri.Corners[0]);
            if (Dot(Tri.Normal, ToEye) <= 0) continue;

            const P0 = ProjectPoint(Tri.Corners[0], Basis, Camera, Width, Height);
            const P1 = ProjectPoint(Tri.Corners[1], Basis, Camera, Width, Height);
            const P2 = ProjectPoint(Tri.Corners[2], Basis, Camera, Width, Height);
            if (!P0 || !P1 || !P2) continue;

            const MinX = Math.max(0, Math.floor(Math.min(P0.X, P1.X, P2.X)));
            const MaxX = Math.min(Width  - 1, Math.ceil(Math.max(P0.X, P1.X, P2.X)));
            const MinY = Math.max(0, Math.floor(Math.min(P0.Y, P1.Y, P2.Y)));
            const MaxY = Math.min(Height - 1, Math.ceil(Math.max(P0.Y, P1.Y, P2.Y)));
            if (MinX > MaxX || MinY > MaxY) continue;

            const Area = (P1.X - P0.X) * (P2.Y - P0.Y) - (P2.X - P0.X) * (P1.Y - P0.Y);
            if (Math.abs(Area) < 1e-9) continue;
            const InverseArea = 1.0 / Area;

            // A soft headlight term so the forms read as solid. Shading is
            // cosmetic here; only Identity/Depth feed the spawn strategies.
            const Lambert = Math.max(0.12, Dot(Tri.Normal, Normalise(ToEye)));
            Drawn++;

            for (let Y = MinY; Y <= MaxY; ++Y)
            for (let X = MinX; X <= MaxX; ++X)
            {
                const Px = X + 0.5, Py = Y + 0.5;
                const W0 = ((P1.X - Px) * (P2.Y - Py) - (P2.X - Px) * (P1.Y - Py)) * InverseArea;
                const W1 = ((P2.X - Px) * (P0.Y - Py) - (P0.X - Px) * (P2.Y - Py)) * InverseArea;
                const W2 = 1.0 - W0 - W1;
                if (W0 < 0 || W1 < 0 || W2 < 0) continue;

                const Depth = W0 * P0.Depth + W1 * P1.Depth + W2 * P2.Depth;
                const Slot  = Y * Width + X;
                if (Depth >= this.Depth[Slot]) continue;
                this.Depth[Slot]    = Depth;
                this.Identity[Slot] = T;
                this.Shade[Slot]    = Lambert;
            }
        }
        return Drawn;
    }

    // Reconstruct a pixel's world position + flat normal from the identity,
    // the same way SurfelSpawnRequest.comp does (ray/triangle, not a G-buffer).
    Reconstruct(X, Y, Triangles, Basis, Camera)
    {
        const Slot = Y * this.Width + X;
        const Id = this.Identity[Slot];
        if (Id < 0) return null;
        const Tri = Triangles[Id];

        const HalfHeight = Math.tan(Camera.Fov * 0.5);
        const HalfWidth  = HalfHeight * (this.Width / this.Height);
        const XNdc = ((X + 0.5) / this.Width)  * 2 - 1;
        const YNdc = 1 - ((Y + 0.5) / this.Height) * 2;
        const Direction = Normalise(Add(Basis.Forward,
            Add(Scale(Basis.Right, XNdc * HalfWidth), Scale(Basis.Up, YNdc * HalfHeight))));

        const Distance = this.Depth[Slot] / Dot(Direction, Basis.Forward);
        return {
            World: Add(Basis.Eye, Scale(Direction, Distance)),
            Normal: Tri.Normal,
            Instance: Tri.Instance,
            Colour: Tri.Colour,
            TriangleId: Id,
        };
    }
}

// ------------------------------------------------------------- stats overlay
// Top-left overlay: FPS, frame time, estimated VRAM/footprint, probe counts,
// and whatever extra rows the host strategy wants to publish.
export class StatsOverlay
{
    constructor(Element)
    {
        this.Element = Element;
        this.Samples = [];
        this.LastStamp = performance.now();
        this.Extra = {};
    }

    BeginFrame()
    {
        const Now = performance.now();
        this.FrameMs = Now - this.LastStamp;
        this.LastStamp = Now;
        this.Samples.push(this.FrameMs);
        if (this.Samples.length > 60) this.Samples.shift();
        return Now;
    }

    Publish(Rows)
    {
        const Mean = this.Samples.reduce((A, B) => A + B, 0) / Math.max(1, this.Samples.length);
        const Fps  = 1000.0 / Math.max(0.001, Mean);

        // "VRAM" here is the real byte footprint of the buffers this prototype
        // allocates. On a CPU rasteriser nothing lives in video memory, so this
        // is the honest analogue: what the equivalent GPU buffers would cost.
        const Lines = [
            ['FPS', Fps.toFixed(1)],
            ['Frame', Mean.toFixed(2) + ' ms'],
            ...Object.entries(Rows),
        ];
        this.Element.innerHTML = Lines
            .map(([Key, Value]) => `<span class="k">${Key}</span><span class="v">${Value}</span>`)
            .join('');
    }
}

export function FormatBytes(Bytes)
{
    if (Bytes < 1024) return Bytes + ' B';
    if (Bytes < 1024 * 1024) return (Bytes / 1024).toFixed(1) + ' KiB';
    return (Bytes / (1024 * 1024)).toFixed(2) + ' MiB';
}
