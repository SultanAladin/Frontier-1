/*============================================================================================================================================
                                                     SCENEGEOMETRYBUILDER.JS
============================================================================================================================================*/
// 🧩 Decodes ControlVehicle FCT1 binary geometry (oriented +Z forward so X is the Left/Right headlamp axis), constructs procedural
//    showroom calibration receivers (glossy/matte floor, spheres, pillars, target walls), and builds 3D wireframe + solid debug
//    overlays for Raw Light Sources, the 8x4x4 3D Spatial Grid (active vs. empty center cells + sub-voxel optical anchors), and LOD rings.

import { GridDimX, GridDimY, GridDimZ, GridBakingMode } from "./LightGridBaker.js";

// Interleaved Surface Vertex Stride = 10 floats:
//   [PosX, PosY, PosZ, NormX, NormY, NormZ, AlbedoR, AlbedoG, AlbedoB, Roughness]
export const SurfaceVertexFloats = 10;

// Interleaved 3D Debug Line/Solid Vertex Stride = 7 floats:
//   [PosX, PosY, PosZ, ColorR, ColorG, ColorB, Alpha]
export const DebugVertexFloats = 7;

function DecodeOctSnorm8(XByte, YByte)
{
    let Fx = Math.max(-1.0, Math.min(1.0, XByte / 127.0));
    let Fy = Math.max(-1.0, Math.min(1.0, YByte / 127.0));
    const Fz = 1.0 - Math.abs(Fx) - Math.abs(Fy);
    if (Fz < 0.0)
    {
        const Ox = Fx;
        const Oy = Fy;
        Fx = (1.0 - Math.abs(Oy)) * (Ox >= 0.0 ? 1.0 : -1.0);
        Fy = (1.0 - Math.abs(Ox)) * (Oy >= 0.0 ? 1.0 : -1.0);
    }
    const Len = Math.hypot(Fx, Fy, Fz) || 1.0;
    return [Fx / Len, Fy / Len, Fz / Len];
}

// 📝 Parses an FCT1 binary ArrayBuffer into { Positions, Normals, Indices, VertexCount, IndexCount }
export function DecodeFct1Binary(RawBuffer)
{
    const View = new DataView(RawBuffer);
    const Magic = View.getUint32(0, true);
    if (Magic !== 0x31544346)
    {
        throw new Error("Invalid FCT1 geometry header");
    }
    const VertexCount = View.getUint32(4, true);
    const IndexCount = View.getUint32(8, true);
    const IndexBytes = View.getUint32(12, true);
    const QuantScale = View.getFloat32(16, true);

    let Cursor = 32;
    const Positions = new Float32Array(VertexCount * 3);
    for (let V = 0; V < VertexCount; ++V)
    {
        Positions[V * 3 + 0] = View.getInt16(Cursor + 0, true) * QuantScale;
        Positions[V * 3 + 1] = View.getInt16(Cursor + 2, true) * QuantScale;
        Positions[V * 3 + 2] = View.getInt16(Cursor + 4, true) * QuantScale;
        Cursor += 6;
    }
    if (Cursor % 4 !== 0) Cursor += 4 - (Cursor % 4);

    const Normals = new Float32Array(VertexCount * 3);
    for (let V = 0; V < VertexCount; ++V)
    {
        const NxByte = View.getInt8(Cursor + 0);
        const NyByte = View.getInt8(Cursor + 1);
        const [Nx, Ny, Nz] = DecodeOctSnorm8(NxByte, NyByte);
        Normals[V * 3 + 0] = Nx;
        Normals[V * 3 + 1] = Ny;
        Normals[V * 3 + 2] = Nz;
        Cursor += 4;
    }
    if (Cursor % 4 !== 0) Cursor += 4 - (Cursor % 4);

    const Indices = new Uint32Array(IndexCount);
    if (IndexBytes === 2)
    {
        for (let I = 0; I < IndexCount; ++I)
        {
            Indices[I] = View.getUint16(Cursor + I * 2, true);
        }
    }
    else
    {
        for (let I = 0; I < IndexCount; ++I)
        {
            Indices[I] = View.getUint32(Cursor + I * 4, true);
        }
    }

    return { Positions, Normals, Indices, VertexCount, IndexCount };
}

class SurfaceTriangleCollector
{
    constructor()
    {
        this.Verts = [];
        this.Indices = [];
        this.VertexCount = 0;
    }

    PushVertex(Px, Py, Pz, Nx, Ny, Nz, Cr, Cg, Cb, Roughness)
    {
        this.Verts.push(Px, Py, Pz, Nx, Ny, Nz, Cr, Cg, Cb, Roughness);
        return this.VertexCount++;
    }

    AddQuad(P0, P1, P2, P3, Normal, Color, Roughness)
    {
        const I0 = this.PushVertex(P0[0], P0[1], P0[2], Normal[0], Normal[1], Normal[2], Color[0], Color[1], Color[2], Roughness);
        const I1 = this.PushVertex(P1[0], P1[1], P1[2], Normal[0], Normal[1], Normal[2], Color[0], Color[1], Color[2], Roughness);
        const I2 = this.PushVertex(P2[0], P2[1], P2[2], Normal[0], Normal[1], Normal[2], Color[0], Color[1], Color[2], Roughness);
        const I3 = this.PushVertex(P3[0], P3[1], P3[2], Normal[0], Normal[1], Normal[2], Color[0], Color[1], Color[2], Roughness);
        this.Indices.push(I0, I1, I2, I0, I2, I3);
    }

    AddBox(MinPt, MaxPt, Color, Roughness)
    {
        const [X0, Y0, Z0] = MinPt;
        const [X1, Y1, Z1] = MaxPt;
        // +Y top
        this.AddQuad([X0, Y1, Z1], [X1, Y1, Z1], [X1, Y1, Z0], [X0, Y1, Z0], [0, 1, 0], Color, Roughness);
        // -Y bottom
        this.AddQuad([X0, Y0, Z0], [X1, Y0, Z0], [X1, Y0, Z1], [X0, Y0, Z1], [0, -1, 0], Color, Roughness);
        // +Z front
        this.AddQuad([X0, Y0, Z1], [X1, Y0, Z1], [X1, Y1, Z1], [X0, Y1, Z1], [0, 0, 1], Color, Roughness);
        // -Z back
        this.AddQuad([X1, Y0, Z0], [X0, Y0, Z0], [X0, Y1, Z0], [X1, Y1, Z0], [0, 0, -1], Color, Roughness);
        // +X right
        this.AddQuad([X1, Y0, Z1], [X1, Y0, Z0], [X1, Y1, Z0], [X1, Y1, Z1], [1, 0, 0], Color, Roughness);
        // -X left
        this.AddQuad([X0, Y0, Z0], [X0, Y0, Z1], [X0, Y1, Z1], [X0, Y1, Z0], [-1, 0, 0], Color, Roughness);
    }

    AddSphere(Center, Radius, Meridians, Stacks, Color, Roughness)
    {
        const StartV = this.VertexCount;
        for (let St = 0; St <= Stacks; ++St)
        {
            const V = St / Stacks;
            const Phi = V * Math.PI;
            const SinP = Math.sin(Phi);
            const CosP = Math.cos(Phi);
            for (let Sl = 0; Sl <= Meridians; ++Sl)
            {
                const U = Sl / Meridians;
                const Theta = U * Math.PI * 2.0;
                const Nx = Math.cos(Theta) * SinP;
                const Ny = CosP;
                const Nz = Math.sin(Theta) * SinP;
                this.PushVertex(
                    Center[0] + Nx * Radius,
                    Center[1] + Ny * Radius,
                    Center[2] + Nz * Radius,
                    Nx, Ny, Nz,
                    Color[0], Color[1], Color[2], Roughness
                );
            }
        }
        const Row = Meridians + 1;
        for (let St = 0; St < Stacks; ++St)
        {
            for (let Sl = 0; Sl < Meridians; ++Sl)
            {
                const A = StartV + St * Row + Sl;
                const B = A + 1;
                const C = A + Row;
                const D = C + 1;
                this.Indices.push(A, C, B, B, C, D);
            }
        }
    }

    // Appends decoded FCT1 vehicle part rotated -90 deg around +Y so native +X nose points along +Z (det = +1 preserves CCW winding!)
    AddVehiclePartOrientedZ(DecodedPart, WorldOrigin, MirrorZSide, Albedo, Roughness)
    {
        const StartV = this.VertexCount;
        const Pos = DecodedPart.Positions;
        const Nrm = DecodedPart.Normals;
        const FlipSign = MirrorZSide ? -1.0 : 1.0;

        for (let V = 0; V < DecodedPart.VertexCount; ++V)
        {
            const LocalX = Pos[V * 3 + 0];
            const LocalY = Pos[V * 3 + 1];
            const LocalZ = Pos[V * 3 + 2] * FlipSign;

            const LocalNx = Nrm[V * 3 + 0];
            const LocalNy = Nrm[V * 3 + 1];
            const LocalNz = Nrm[V * 3 + 2] * FlipSign;

            // Rotate +X -> +Z, +Y -> +Y, +Z -> -X (det = +1 when FlipSign = +1)
            const Wx = WorldOrigin[0] - LocalZ;
            const Wy = WorldOrigin[1] + LocalY;
            const Wz = WorldOrigin[2] + LocalX;

            const WNx = -LocalNz;
            const WNy = LocalNy;
            const WNz = LocalNx;

            this.PushVertex(Wx, Wy, Wz, WNx, WNy, WNz, Albedo[0], Albedo[1], Albedo[2], Roughness);
        }

        const SrcIdx = DecodedPart.Indices;
        for (let T = 0; T < DecodedPart.IndexCount; T += 3)
        {
            if (MirrorZSide)
            {
                this.Indices.push(StartV + SrcIdx[T], StartV + SrcIdx[T + 2], StartV + SrcIdx[T + 1]);
            }
            else
            {
                this.Indices.push(StartV + SrcIdx[T], StartV + SrcIdx[T + 1], StartV + SrcIdx[T + 2]);
            }
        }
    }

    AddCompleteVehicle(VehicleParts, CarCenter, BodyPaintRgb, BodyRoughness = 0.16)
    {
        if (!VehicleParts || !VehicleParts.Body) return;
        this.AddVehiclePartOrientedZ(VehicleParts.Body, CarCenter, false, BodyPaintRgb, BodyRoughness);

        // Native wheel offsets: front x=+1.37, rear x=-1.39, lateral z=+-0.81, height y=0.338
        const WheelMounts = [
            { Dx: -0.815, Dy: 0.338, Dz:  1.375, Mirror: false },
            { Dx:  0.815, Dy: 0.338, Dz:  1.375, Mirror: true  },
            { Dx: -0.825, Dy: 0.338, Dz: -1.395, Mirror: false },
            { Dx:  0.825, Dy: 0.338, Dz: -1.395, Mirror: true  }
        ];
        for (const W of WheelMounts)
        {
            const AxlePos = [CarCenter[0] + W.Dx, CarCenter[1] + W.Dy, CarCenter[2] + W.Dz];
            if (VehicleParts.Rim)
            {
                this.AddVehiclePartOrientedZ(VehicleParts.Rim, AxlePos, W.Mirror, [0.80, 0.83, 0.88], 0.14);
            }
            if (VehicleParts.Tyre)
            {
                this.AddVehiclePartOrientedZ(VehicleParts.Tyre, AxlePos, W.Mirror, [0.11, 0.11, 0.12], 0.68);
            }
        }
    }

    Finalize()
    {
        return {
            InterleavedVertices: new Float32Array(this.Verts),
            Indices: new Uint32Array(this.Indices),
            VertexCount: this.VertexCount,
            TriangleCount: this.Indices.length / 3
        };
    }
}

// 📝 Builds the complete 3D receiver scene for the active PresetId ("dual-headlamps", "five-emissives", or "nanite-hall")
export function BuildPresetSceneSurfaces(PresetId, VehicleParts)
{
    const Builder = new SurfaceTriangleCollector();

    // Tiled Showroom Floor with alternating low-roughness (0.14) and satin (0.42) calibration tiles
    // so BOTH crisp diffuse cutoffs AND dual-headlamp GGX specular streaks are immediately visible!
    const XExtent = PresetId === "nanite-hall" ? 10.0 : 8.0;
    const ZMin = PresetId === "nanite-hall" ? -38.0 : -8.0;
    const ZMax = PresetId === "dual-headlamps" ? 16.0 : 12.0;
    const TileStep = 1.0;

    for (let Z = ZMin; Z < ZMax; Z += TileStep)
    {
        for (let X = -XExtent; X < XExtent; X += TileStep)
        {
            const Checker = (Math.floor(X + 100.0) + Math.floor(Z + 100.0)) % 2 === 0;
            const Albedo = Checker ? [0.64, 0.66, 0.70] : [0.46, 0.48, 0.52];
            const Rough = Checker ? 0.15 : 0.38;
            Builder.AddQuad(
                [X, 0.0, Z + TileStep],
                [X + TileStep, 0.0, Z + TileStep],
                [X + TileStep, 0.0, Z],
                [X, 0.0, Z],
                [0.0, 1.0, 0.0],
                Albedo,
                Rough
            );
        }
    }

    if (PresetId === "dual-headlamps")
    {
        // Hero ControlVehicle facing +Z: its front bumper & headlamps sit directly inside the 8x4x4 Grid at Z = +1.98!
        Builder.AddCompleteVehicle(VehicleParts, [0.0, 0.0, 0.0], [0.72, 0.76, 0.82], 0.14);

        // Photometric Calibration Wall ahead at Z = +9.2 m (reveals crisp ECE R112 dual low-beam pattern vs. center-merged cyclops blob!)
        Builder.AddBox([-6.5, 0.0, 9.2], [6.5, 3.2, 9.45], [0.82, 0.83, 0.86], 0.45);

        // Left & Right Side Walls at X = +-6.5 m (capture the +-48 deg Amber Corner Markers & Cyan Wheel-Arch Side Wash!)
        Builder.AddBox([-6.75, 0.0, -3.5], [-6.5, 2.8, 9.45], [0.76, 0.78, 0.81], 0.42);
        Builder.AddBox([ 6.5,  0.0, -3.5], [ 6.75, 2.8, 9.45], [0.76, 0.78, 0.81], 0.42);

        // Near-Field Calibration Spheres across X = [-1.8, -0.76, 0.0, +0.76, +1.8] at Z = +4.1 m!
        // Notice: the center sphere at X = 0.0 sits right in the gap between the 2 headlights!
        // Naive center-averaging blasts the X = 0.0 sphere head-on, whereas Ground Truth & 8x4x4 Spatial Grid illuminate it from +-0.76 m!
        const SphereXs = [-1.95, -0.78, 0.0, 0.78, 1.95];
        for (let I = 0; I < SphereXs.length; ++I)
        {
            const Sx = SphereXs[I];
            const Rough = I % 2 === 0 ? 0.12 : 0.30;
            Builder.AddBox([Sx - 0.24, 0.0, 3.96], [Sx + 0.24, 0.22, 4.44], [0.35, 0.37, 0.40], 0.40);
            Builder.AddSphere([Sx, 0.58, 4.20], 0.36, 22, 16, [0.86, 0.87, 0.90], Rough);
        }
    }
    else if (PresetId === "five-emissives")
    {
        // Suspended Architectural Luminaire Housing at (0, 1.85, 0) + 4 Directional Calibration Walls & Spheres + Center Plinth
        Builder.AddBox([-0.52, 1.72, -0.52], [0.52, 2.02, 0.52], [0.22, 0.24, 0.28], 0.28);
        Builder.AddBox([-0.08, 2.02, -0.08], [0.08, 3.40, 0.08], [0.30, 0.32, 0.36], 0.35);

        // Center calibration sphere & plinth directly beneath the -Y overhead downlight
        Builder.AddBox([-0.55, 0.0, -0.55], [0.55, 0.26, 0.55], [0.58, 0.60, 0.64], 0.32);
        Builder.AddSphere([0.0, 0.68, 0.0], 0.42, 24, 18, [0.88, 0.89, 0.92], 0.14);

        // 4 Radial Calibration Spheres along +Z (Cyan), +X (Gold), -Z (Ruby), -X (Emerald) at R = 2.65 m
        const RingDirs = [
            [ 0.0,  2.65], [ 2.65,  0.0], [ 0.0, -2.65], [-2.65,  0.0],
            [ 1.95, 1.95], [-1.95, 1.95], [ 1.95,-1.95], [-1.95,-1.95]
        ];
        for (let I = 0; I < RingDirs.length; ++I)
        {
            const [Rx, Rz] = RingDirs[I];
            Builder.AddBox([Rx - 0.28, 0.0, Rz - 0.28], [Rx + 0.28, 0.24, Rz + 0.28], [0.42, 0.44, 0.48], 0.38);
            Builder.AddSphere([Rx, 0.64, Rz], 0.40, 22, 16, [0.86, 0.87, 0.90], I < 4 ? 0.14 : 0.34);
        }

        // 4 Low Perimeter Receiver Baffles at +-5.2 m to catch each cardinal beam cleanly
        Builder.AddBox([-4.8, 0.0,  5.2], [ 4.8, 2.5,  5.45], [0.80, 0.81, 0.84], 0.44);
        Builder.AddBox([-4.8, 0.0, -5.45],[ 4.8, 2.5, -5.2],  [0.80, 0.81, 0.84], 0.44);
        Builder.AddBox([ 5.2, 0.0, -4.8], [ 5.45,2.5,  4.8],  [0.80, 0.81, 0.84], 0.44);
        Builder.AddBox([-5.45,0.0, -4.8], [-5.2, 2.5,  4.8],  [0.80, 0.81, 0.84], 0.44);
    }
    else
    {
        // "nanite-hall": 3 ControlVehicles + 9 Ceiling Multi-Panel Rigs spanning Z = +4 m down to Z = -35 m!
        Builder.AddCompleteVehicle(VehicleParts, [ 0.0, 0.0,  0.00], [0.76, 0.79, 0.84], 0.14);
        Builder.AddCompleteVehicle(VehicleParts, [-4.6, 0.0, -6.18], [0.72, 0.22, 0.20], 0.16);
        Builder.AddCompleteVehicle(VehicleParts, [ 4.6, 0.0, -6.18], [0.20, 0.48, 0.78], 0.16);

        // Architectural Columns & Calibration Spheres down the hall from Z = +3 m to Z = -32 m
        for (let BayZ = 2.0; BayZ >= -34.0; BayZ -= 6.0)
        {
            Builder.AddBox([-8.6, 0.0, BayZ - 0.35], [-8.0, 3.8, BayZ + 0.35], [0.68, 0.70, 0.74], 0.40);
            Builder.AddBox([ 8.0, 0.0, BayZ - 0.35], [ 8.6, 3.8, BayZ + 0.35], [0.68, 0.70, 0.74], 0.40);
            Builder.AddSphere([-2.2, 0.50, BayZ - 1.5], 0.50, 18, 14, [0.85, 0.86, 0.89], 0.15);
            Builder.AddSphere([ 2.2, 0.50, BayZ - 1.5], 0.50, 18, 14, [0.85, 0.86, 0.89], 0.34);
        }
        // Far end wall & front target wall
        Builder.AddBox([-9.5, 0.0, -36.5], [9.5, 4.2, -36.1], [0.76, 0.78, 0.82], 0.45);
        Builder.AddBox([-6.5, 0.0,   9.4], [6.5, 2.6,   9.7], [0.80, 0.82, 0.85], 0.45);
    }

    return Builder.Finalize();
}

// 📝 Builds 3D Debug Wireframe Lines & Solid Markers so the user directly sees:
//    ① Exact 3D positions & aiming cones of every Raw Unmerged Light Source (ShowRawLights)
//    ② The 3D 8x4x4 Spatial Light Grid itself: outer bounds, faint empty interior cells (showing the empty gap between headlights!),
//       highlighted Active Occupied Cells, Naive Center-Averaged collapse warning, and Sub-Voxel Optical Anchor pins (Show3dGrid)
//    ③ Continuous Nanite Distance-LOD & Influence Cutoff ground rings around each cluster (ShowLodRings)
export function BuildDebugOverlayGeometry(CompiledClusters, BakingMode, DebugToggles)
{
    const LineVerts = [];
    const TriVerts = [];

    function PushLine(P0, P1, Color, Alpha = 1.0)
    {
        LineVerts.push(
            P0[0], P0[1], P0[2], Color[0], Color[1], Color[2], Alpha,
            P1[0], P1[1], P1[2], Color[0], Color[1], Color[2], Alpha
        );
    }

    function PushWireBox(MinPt, MaxPt, Color, Alpha = 1.0)
    {
        const [X0, Y0, Z0] = MinPt;
        const [X1, Y1, Z1] = MaxPt;
        const C000 = [X0, Y0, Z0], C100 = [X1, Y0, Z0], C010 = [X0, Y1, Z0], C110 = [X1, Y1, Z0];
        const C001 = [X0, Y0, Z1], C101 = [X1, Y0, Z1], C011 = [X0, Y1, Z1], C111 = [X1, Y1, Z1];
        PushLine(C000, C100, Color, Alpha); PushLine(C010, C110, Color, Alpha);
        PushLine(C001, C101, Color, Alpha); PushLine(C011, C111, Color, Alpha);
        PushLine(C000, C010, Color, Alpha); PushLine(C100, C110, Color, Alpha);
        PushLine(C001, C011, Color, Alpha); PushLine(C101, C111, Color, Alpha);
        PushLine(C000, C001, Color, Alpha); PushLine(C100, C101, Color, Alpha);
        PushLine(C010, C011, Color, Alpha); PushLine(C110, C111, Color, Alpha);
    }

    function PushGroundCircle(Center, Radius, Segments, Color, Alpha = 0.85, YHeight = 0.025)
    {
        for (let S = 0; S < Segments; ++S)
        {
            const A0 = (S / Segments) * Math.PI * 2.0;
            const A1 = ((S + 1) / Segments) * Math.PI * 2.0;
            PushLine(
                [Center[0] + Math.cos(A0) * Radius, YHeight, Center[2] + Math.sin(A0) * Radius],
                [Center[0] + Math.cos(A1) * Radius, YHeight, Center[2] + Math.sin(A1) * Radius],
                Color,
                Alpha
            );
        }
    }

    function PushSolidDiamond(Center, Radius, Color, Alpha = 0.95)
    {
        const [Cx, Cy, Cz] = Center;
        const Top = [Cx, Cy + Radius, Cz];
        const Bot = [Cx, Cy - Radius, Cz];
        const Pts = [
            [Cx + Radius, Cy, Cz],
            [Cx, Cy, Cz + Radius],
            [Cx - Radius, Cy, Cz],
            [Cx, Cy, Cz - Radius]
        ];
        for (let K = 0; K < 4; ++K)
        {
            const P0 = Pts[K];
            const P1 = Pts[(K + 1) % 4];
            TriVerts.push(
                Top[0], Top[1], Top[2], Color[0], Color[1], Color[2], Alpha,
                P1[0],  P1[1],  P1[2],  Color[0], Color[1], Color[2], Alpha,
                P0[0],  P0[1],  P0[2],  Color[0], Color[1], Color[2], Alpha,

                Bot[0], Bot[1], Bot[2], Color[0], Color[1], Color[2], Alpha,
                P0[0],  P0[1],  P0[2],  Color[0], Color[1], Color[2], Alpha,
                P1[0],  P1[1],  P1[2],  Color[0], Color[1], Color[2], Alpha
            );
        }
    }

    for (let CIdx = 0; CIdx < CompiledClusters.length; ++CIdx)
    {
        const CG = CompiledClusters[CIdx];
        const BMin = CG.Cluster.BoundsMin;
        const BMax = CG.Cluster.BoundsMax;
        const Cs = CG.CellSize;

        // ① 3D Spatial Grid Structure (8x4x4 Voxel Lattice + Occupied Cells + Sub-Voxel Optical Anchors)
        if (DebugToggles.Show3dGrid)
        {
            // Outer 3D Grid Bounding Box
            PushWireBox(BMin, BMax, [0.16, 0.78, 0.96], 0.92);

            // Interior 8x4x4 Subdivision Lattice Lines on bottom/top/front faces so all 8 lateral X columns
            // (including the EMPTY center columns ix = 2, 3, 4, 5!) are unmistakably visible in 3D!
            for (let Ix = 1; Ix < GridDimX; ++Ix)
            {
                const X = BMin[0] + Ix * Cs[0];
                PushLine([X, BMin[1], BMin[2]], [X, BMin[1], BMax[2]], [0.22, 0.56, 0.76], 0.34);
                PushLine([X, BMax[1], BMin[2]], [X, BMax[1], BMax[2]], [0.22, 0.56, 0.76], 0.34);
                PushLine([X, BMin[1], BMax[2]], [X, BMax[1], BMax[2]], [0.22, 0.56, 0.76], 0.30);
            }
            for (let Iy = 1; Iy < GridDimY; ++Iy)
            {
                const Y = BMin[1] + Iy * Cs[1];
                PushLine([BMin[0], Y, BMax[2]], [BMax[0], Y, BMax[2]], [0.22, 0.56, 0.76], 0.25);
                PushLine([BMin[0], Y, BMin[2]], [BMax[0], Y, BMin[2]], [0.22, 0.56, 0.76], 0.20);
            }
            for (let Iz = 1; Iz < GridDimZ; ++Iz)
            {
                const Z = BMin[2] + Iz * Cs[2];
                PushLine([BMin[0], BMin[1], Z], [BMax[0], BMin[1], Z], [0.22, 0.56, 0.76], 0.30);
                PushLine([BMin[0], BMax[1], Z], [BMax[0], BMax[1], Z], [0.22, 0.56, 0.76], 0.25);
            }

            if (BakingMode === GridBakingMode.NaiveCenterAveraged)
            {
                // 🔴 Visually expose Naive Center-Averaging: red collapse lines from every true lamp into BoxCenter!
                const C = CG.BoxCenter;
                PushSolidDiamond(C, 0.11, [1.0, 0.20, 0.22], 0.95);
                PushWireBox([C[0] - 0.16, C[1] - 0.16, C[2] - 0.16], [C[0] + 0.16, C[1] + 0.16, C[2] + 0.16], [1.0, 0.22, 0.24], 0.95);
                for (const S of CG.Cluster.Sources)
                {
                    PushLine(S.Position, C, [1.0, 0.24, 0.24], 0.85);
                }
            }
            else
            {
                const UseOpt = BakingMode === GridBakingMode.ErrorOptimizedMip;
                const CellColor = UseOpt ? [0.16, 0.96, 0.58] : [1.0, 0.76, 0.18];

                for (const Cell of CG.ActiveCellsLod0)
                {
                    const [Ix, Iy, Iz] = Cell.GridIndex;
                    const CellMin = [BMin[0] + Ix * Cs[0], BMin[1] + Iy * Cs[1], BMin[2] + Iz * Cs[2]];
                    const CellMax = [CellMin[0] + Cs[0], CellMin[1] + Cs[1], CellMin[2] + Cs[2]];
                    PushWireBox(CellMin, CellMax, CellColor, 0.96);

                    const Anchor = UseOpt
                        ? [
                            Cell.VoxelCenter[0] + Cell.SubOffset[0],
                            Cell.VoxelCenter[1] + Cell.SubOffset[1],
                            Cell.VoxelCenter[2] + Cell.SubOffset[2]
                        ]
                        : Cell.VoxelCenter;

                    // Small cross at raw VoxelCenter + solid emerald/gold pin at active optical Anchor + displacement vector
                    const Vc = Cell.VoxelCenter;
                    PushLine([Vc[0] - 0.04, Vc[1], Vc[2]], [Vc[0] + 0.04, Vc[1], Vc[2]], [0.75, 0.82, 0.90], 0.70);
                    PushLine([Vc[0], Vc[1] - 0.04, Vc[2]], [Vc[0], Vc[1] + 0.04, Vc[2]], [0.75, 0.82, 0.90], 0.70);
                    if (UseOpt)
                    {
                        PushLine(Vc, Anchor, [1.0, 0.92, 0.28], 0.95);
                    }
                    PushSolidDiamond(Anchor, 0.048, CellColor, 0.95);

                    // Dominant baked lobe direction vector from optical anchor
                    const D = Cell.DominantDir;
                    PushLine(Anchor, [Anchor[0] + D[0] * 0.42, Anchor[1] + D[1] * 0.42, Anchor[2] + D[2] * 0.42], CellColor, 0.80);
                }
            }
        }

        // ② Raw Individual Light Source Gizmos & Aiming Cones (shows exact ground-truth lamp positions & directions!)
        if (DebugToggles.ShowRawLights)
        {
            for (const Src of CG.Cluster.Sources)
            {
                const P = Src.Position;
                const Ax = Src.Axis;
                const Col = [
                    Math.min(1.0, Src.ColorRgb[0] * 0.85 + 0.15),
                    Math.min(1.0, Src.ColorRgb[1] * 0.85 + 0.15),
                    Math.min(1.0, Src.ColorRgb[2] * 0.85 + 0.15)
                ];

                // Glowing solid diamond at exact physical light position
                PushSolidDiamond(P, 0.062, Col, 0.98);

                // Aiming cone & central optical axis ray
                const ConeLen = 0.58;
                const TipCenter = [P[0] + Ax[0] * ConeLen, P[1] + Ax[1] * ConeLen, P[2] + Ax[2] * ConeLen];
                PushLine(P, TipCenter, Col, 0.95);

                // Build orthonormal tangent basis for aiming cone rim
                const RefUp = Math.abs(Ax[1]) > 0.9 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0];
                let Tx = RefUp[1] * Ax[2] - RefUp[2] * Ax[1];
                let Ty = RefUp[2] * Ax[0] - RefUp[0] * Ax[2];
                let Tz = RefUp[0] * Ax[1] - RefUp[1] * Ax[0];
                const TLen = Math.hypot(Tx, Ty, Tz) || 1.0;
                Tx /= TLen; Ty /= TLen; Tz /= TLen;
                const Bx = Ax[1] * Tz - Ax[2] * Ty;
                const By = Ax[2] * Tx - Ax[0] * Tz;
                const Bz = Ax[0] * Ty - Ax[1] * Tx;

                const ConeRad = Math.min(0.28, Math.max(0.09, ConeLen * Math.sqrt(Math.max(0.02, 1.0 - Src.CutoffCos)) * 0.34));
                const Segs = 12;
                for (let S = 0; S < Segs; ++S)
                {
                    const A0 = (S / Segs) * Math.PI * 2.0;
                    const A1 = ((S + 1) / Segs) * Math.PI * 2.0;
                    const R0 = [
                        TipCenter[0] + (Math.cos(A0) * Tx + Math.sin(A0) * Bx) * ConeRad,
                        TipCenter[1] + (Math.cos(A0) * Ty + Math.sin(A0) * By) * ConeRad,
                        TipCenter[2] + (Math.cos(A0) * Tz + Math.sin(A0) * Bz) * ConeRad
                    ];
                    const R1 = [
                        TipCenter[0] + (Math.cos(A1) * Tx + Math.sin(A1) * Bx) * ConeRad,
                        TipCenter[1] + (Math.cos(A1) * Ty + Math.sin(A1) * By) * ConeRad,
                        TipCenter[2] + (Math.cos(A1) * Tz + Math.sin(A1) * Bz) * ConeRad
                    ];
                    PushLine(R0, R1, Col, 0.72);
                    if (S % 3 === 0) PushLine(P, R0, Col, 0.58);
                }
            }
        }

        // ③ Nanite Distance-LOD Transition Rings & Perceptual Influence Cutoff Ring on Ground Plane
        if (DebugToggles.ShowLodRings)
        {
            const NearRef = Math.max(CG.Diameter * 2.2, 1.8);
            const R_Lod1 = NearRef * Math.pow(2.0, 1.0 / 1.15); // LOD 0 -> LOD 1 boundary
            const R_Lod2 = NearRef * Math.pow(2.0, 2.0 / 1.15); // LOD 1 -> LOD 2 (1-sample!) boundary
            const R_Inf  = CG.Cluster.InfluenceRadius;

            PushGroundCircle(CG.BoxCenter, NearRef, 48, [0.16, 0.95, 0.56], 0.72, 0.022); // Green: Full 8x4x4 LOD 0 zone
            PushGroundCircle(CG.BoxCenter, R_Lod1,  56, [0.22, 0.76, 1.00], 0.65, 0.022); // Cyan: Bimodal LOD 1 zone
            PushGroundCircle(CG.BoxCenter, R_Lod2,  64, [0.98, 0.74, 0.20], 0.60, 0.022); // Amber: 1-Sample Far Grid LOD 2
            PushGroundCircle(CG.BoxCenter, R_Inf,   72, [0.95, 0.32, 0.42], 0.45, 0.022); // Rose: Influence Culling Cutoff
        }
    }

    return {
        LineVertices: new Float32Array(LineVerts),
        LineVertexCount: LineVerts.length / DebugVertexFloats,
        SolidVertices: new Float32Array(TriVerts),
        SolidVertexCount: TriVerts.length / DebugVertexFloats
    };
}
