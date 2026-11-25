// Experimental/LoftedVehicleSdfWorkbench/VehicleShowroomBuilder.js
// 🧩 Constructs smooth crease-split triangle geometry for the 25,068-triangle ControlVehicle,
//    a circular architectural showroom floor + curved backdrop shadow-catcher, an interactive
//    SDF cross-section inspection quad, and 3D bi-directional Lofted Rib/Rail vs. Voxel cage lines.

import {
    VehicleWheelMounts,
    EvalBicubicLoftSurface
} from "./LoftedVehicleSdfCompiler.js";

export const ShowroomVertexFloats = 11;
export const ShowroomVertexBytes  = ShowroomVertexFloats * 4;

export const CageLineVertexFloats = 7;
export const CageLineVertexBytes  = CageLineVertexFloats * 4;

// Material Finish Lookup for ControlVehicle FinishSlots
function ResolveFinishAppearance(SlotId, FromBodyPart)
{
    switch (SlotId)
    {
        case 6: // Liquid Cobalt-Cyan Metallic Coachwork
            return { Color: [0.042, 0.255, 0.565], Roughness: 0.13, Tag: 1.0 };
        case 7: // Tinted Sapphire Canopy Glass
            return { Color: [0.022, 0.042, 0.072], Roughness: 0.05, Tag: 2.0 };
        case 8: // Satin Carbon Aero Skirts & Wheel Flares
            return { Color: [0.040, 0.044, 0.052], Roughness: 0.40, Tag: 3.0 };
        case 9: // Body Slot 9 = Gloss Obsidian Roof/Pillars; Wheel Slot 9 = Vulcanized Tyre Rubber
            return FromBodyPart
                ? { Color: [0.018, 0.022, 0.030], Roughness: 0.09, Tag: 1.0 }
                : { Color: [0.026, 0.028, 0.032], Roughness: 0.74, Tag: 3.0 };
        case 10: // Machined Titanium-Silver Alloy Rim
            return { Color: [0.680, 0.715, 0.765], Roughness: 0.19, Tag: 4.0 };
        case 12: // Dark Chrome Headlamp & Grille Surround
            return { Color: [0.115, 0.135, 0.165], Roughness: 0.23, Tag: 4.0 };
        case 13: // Daylight White Front LED Headlamp Strip
            return { Color: [2.500, 2.650, 2.950], Roughness: 0.05, Tag: 5.0 };
        case 14: // Crimson Rear LED Taillamp Strip
            return { Color: [2.850, 0.140, 0.110], Roughness: 0.05, Tag: 5.0 };
        case 15: // Rear Diffuser Center Tunnel
            return { Color: [0.034, 0.038, 0.045], Roughness: 0.46, Tag: 3.0 };
        default:
            return { Color: [0.220, 0.250, 0.290], Roughness: 0.32, Tag: 1.0 };
    }
}

// Spatial vertex welding + crease-angle normal smoothing (splits hard automotive creases > 46 deg)
function ComputeCreaseSplitVehicleBuffers(AssembledCar)
{
    const FaceCount     = AssembledCar.TotalFaces;
    const BodyFaceCount = AssembledCar.BodyPart.FaceCount;
    const SrcPos        = AssembledCar.Positions;
    const SrcIdx        = AssembledCar.Indices;
    const SrcSlots      = AssembledCar.FinishSlots;

    // 1. Compute area-weighted face normals and bucket corners by quantized 0.2 mm spatial key
    const FaceNormals = new Float32Array(FaceCount * 3);
    const SpatialBuckets = Object.create(null);

    for (let F = 0; F < FaceCount; ++F)
    {
        const I0 = SrcIdx[F * 3 + 0] * 3;
        const I1 = SrcIdx[F * 3 + 1] * 3;
        const I2 = SrcIdx[F * 3 + 2] * 3;

        const E1x = SrcPos[I1 + 0] - SrcPos[I0 + 0];
        const E1y = SrcPos[I1 + 1] - SrcPos[I0 + 1];
        const E1z = SrcPos[I1 + 2] - SrcPos[I0 + 2];
        const E2x = SrcPos[I2 + 0] - SrcPos[I0 + 0];
        const E2y = SrcPos[I2 + 1] - SrcPos[I0 + 1];
        const E2z = SrcPos[I2 + 2] - SrcPos[I0 + 2];

        const Nx = E1y * E2z - E1z * E2y;
        const Ny = E1z * E2x - E1x * E2z;
        const Nz = E1x * E2y - E1y * E2x;
        FaceNormals[F * 3 + 0] = Nx;
        FaceNormals[F * 3 + 1] = Ny;
        FaceNormals[F * 3 + 2] = Nz;

        for (let K = 0; K < 3; ++K)
        {
            const Vi = SrcIdx[F * 3 + K] * 3;
            const Qx = Math.round(SrcPos[Vi + 0] * 5000.0);
            const Qy = Math.round(SrcPos[Vi + 1] * 5000.0);
            const Qz = Math.round(SrcPos[Vi + 2] * 5000.0);
            const PartGroup = F < BodyFaceCount ? 0 : 1;
            const Key = `${PartGroup}:${Qx},${Qy},${Qz}`;
            let List = SpatialBuckets[Key];
            if (!List)
            {
                List = [];
                SpatialBuckets[Key] = List;
            }
            List.push(F);
        }
    }

    // 2. Build de-indexed crease-smoothed vertices (75,204 vertices -> indexed 1-to-1 for zero seam Bleeding across FinishSlots)
    const CosCrease = Math.cos((46.0 * Math.PI) / 180.0);
    const VertCount = FaceCount * 3;
    const Verts = new Float32Array(VertCount * ShowroomVertexFloats);
    const Indices = new Uint32Array(VertCount);

    for (let F = 0; F < FaceCount; ++F)
    {
        const Fnx = FaceNormals[F * 3 + 0];
        const Fny = FaceNormals[F * 3 + 1];
        const Fnz = FaceNormals[F * 3 + 2];
        const Flen = Math.hypot(Fnx, Fny, Fnz) || 1e-8;
        const Unx = Fnx / Flen, Uny = Fny / Flen, Unz = Fnz / Flen;

        const Mat = ResolveFinishAppearance(SrcSlots[F], F < BodyFaceCount);
        const PartGroup = F < BodyFaceCount ? 0 : 1;

        for (let K = 0; K < 3; ++K)
        {
            const Vi = SrcIdx[F * 3 + K] * 3;
            const Px = SrcPos[Vi + 0], Py = SrcPos[Vi + 1], Pz = SrcPos[Vi + 2];
            const Key = `${PartGroup}:${Math.round(Px * 5000.0)},${Math.round(Py * 5000.0)},${Math.round(Pz * 5000.0)}`;
            const Neighbors = SpatialBuckets[Key];

            let Sx = 0.0, Sy = 0.0, Sz = 0.0;
            for (let N = 0; N < Neighbors.length; ++N)
            {
                const Nf = Neighbors[N];
                const Nnx = FaceNormals[Nf * 3 + 0];
                const Nny = FaceNormals[Nf * 3 + 1];
                const Nnz = FaceNormals[Nf * 3 + 2];
                const Nlen = Math.hypot(Nnx, Nny, Nnz) || 1e-8;
                const Dot = (Nnx * Unx + Nny * Uny + Nnz * Unz) / Nlen;
                if (Dot >= CosCrease)
                {
                    Sx += Nnx;
                    Sy += Nny;
                    Sz += Nnz;
                }
            }

            const Slen = Math.hypot(Sx, Sy, Sz) || 1.0;
            const OutV = F * 3 + K;
            const Dst  = OutV * ShowroomVertexFloats;

            Verts[Dst + 0]  = Px;
            Verts[Dst + 1]  = Py;
            Verts[Dst + 2]  = Pz;
            Verts[Dst + 3]  = Sx / Slen;
            Verts[Dst + 4]  = Sy / Slen;
            Verts[Dst + 5]  = Sz / Slen;
            Verts[Dst + 6]  = Mat.Color[0];
            Verts[Dst + 7]  = Mat.Color[1];
            Verts[Dst + 8]  = Mat.Color[2];
            Verts[Dst + 9]  = Mat.Roughness;
            Verts[Dst + 10] = Mat.Tag;

            Indices[OutV] = OutV;
        }
    }

    return { Verts, Indices, VertCount };
}

// Builds both the Showroom Environment (Floor Plinth + Curved Cyclorama Wall) and the 25,068-Triangle ControlVehicle
export function BuildShowroomSceneBuffers(AssembledCar)
{
    const EnvVerts = [];
    const EnvIdx   = [];

    function PushVertex(Px, Py, Pz, Nx, Ny, Nz, Cr, Cg, Cb, Rough, Tag)
    {
        const Id = EnvVerts.length / ShowroomVertexFloats;
        EnvVerts.push(Px, Py, Pz, Nx, Ny, Nz, Cr, Cg, Cb, Rough, Tag);
        return Id;
    }

    // 1. Circular Showroom Turntable Floor Disk (Y = 0.0, radius 11.0 m, 6 radial rings x 72 angular sectors)
    const Rings = 6;
    const Sectors = 72;
    const MaxRad = 11.0;
    const CenterId = PushVertex(0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.090, 0.102, 0.118, 0.16, 0.0);

    const RingStartIds = [];
    for (let R = 1; R <= Rings; ++R)
    {
        const Frac = R / Rings;
        const Rad = MaxRad * Math.pow(Frac, 1.25);
        const Start = EnvVerts.length / ShowroomVertexFloats;
        RingStartIds.push(Start);
        const Shade = 0.090 - 0.032 * Frac;
        for (let S = 0; S < Sectors; ++S)
        {
            const Ang = (S / Sectors) * Math.PI * 2.0;
            PushVertex(
                Math.cos(Ang) * Rad,
                0.0,
                Math.sin(Ang) * Rad,
                0.0, 1.0, 0.0,
                Shade, Shade * 1.10, Shade * 1.24,
                0.16, 0.0
            );
        }
    }

    // Inner fan
    const FirstRing = RingStartIds[0];
    for (let S = 0; S < Sectors; ++S)
    {
        const NextS = (S + 1) % Sectors;
        EnvIdx.push(CenterId, FirstRing + NextS, FirstRing + S);
    }
    // Outer concentric annuli
    for (let R = 0; R < Rings - 1; ++R)
    {
        const Inner = RingStartIds[R];
        const Outer = RingStartIds[R + 1];
        for (let S = 0; S < Sectors; ++S)
        {
            const NextS = (S + 1) % Sectors;
            EnvIdx.push(Inner + S, Inner + NextS, Outer + NextS);
            EnvIdx.push(Inner + S, Outer + NextS, Outer + S);
        }
    }

    // 2. Curved Architectural Shadow-Catcher Cyclorama Backdrop (behind & left of car so low-sun shadows land crisply!)
    const ArcCols = 40;
    const ArcRows = 12;
    const WallRad = 7.4;
    const AngStart = Math.PI * 0.62;
    const AngEnd   = Math.PI * 1.56;
    const WallHeight = 4.4;
    const WallStartId = EnvVerts.length / ShowroomVertexFloats;

    for (let Row = 0; Row <= ArcRows; ++Row)
    {
        const VFrac = Row / ArcRows;
        // Smooth showroom cove fillet near floor (Row 0..3)
        const CoveFrac = Math.min(1.0, VFrac / 0.22);
        const RadOffset = (1.0 - CoveFrac) * (1.0 - CoveFrac) * 0.65;
        const R = WallRad - RadOffset;
        const Y = WallHeight * Math.pow(VFrac, 1.08);

        for (let Col = 0; Col <= ArcCols; ++Col)
        {
            const UFrac = Col / ArcCols;
            const Ang = AngStart + (AngEnd - AngStart) * UFrac;
            const Px = Math.cos(Ang) * R;
            const Pz = Math.sin(Ang) * R;
            // Inward normal pointing toward origin + slight upward tilt in cove
            const InX = -Math.cos(Ang);
            const InZ = -Math.sin(Ang);
            const InY = (1.0 - CoveFrac) * 0.65;
            const NLen = Math.hypot(InX, InY, InZ);
            PushVertex(
                Px, Y, Pz,
                InX / NLen, InY / NLen, InZ / NLen,
                0.072, 0.082, 0.096,
                0.55, 6.0
            );
        }
    }

    const Stride = ArcCols + 1;
    for (let Row = 0; Row < ArcRows; ++Row)
    {
        for (let Col = 0; Col < ArcCols; ++Col)
        {
            const A = WallStartId + Row * Stride + Col;
            const B = A + 1;
            const C = A + Stride;
            const D = C + 1;
            EnvIdx.push(A, C, B);
            EnvIdx.push(B, C, D);
        }
    }

    // 3. Merge Showroom Environment + ControlVehicle Triangle Buffers
    const CarBuffers = ComputeCreaseSplitVehicleBuffers(AssembledCar);
    const EnvVertCount = EnvVerts.length / ShowroomVertexFloats;
    const TotalVertCount = EnvVertCount + CarBuffers.VertCount;

    const CombinedVerts = new Float32Array(TotalVertCount * ShowroomVertexFloats);
    CombinedVerts.set(EnvVerts, 0);
    CombinedVerts.set(CarBuffers.Verts, EnvVerts.length);

    const CombinedIndices = new Uint32Array(EnvIdx.length + CarBuffers.Indices.length);
    CombinedIndices.set(EnvIdx, 0);
    for (let I = 0; I < CarBuffers.Indices.length; ++I)
    {
        CombinedIndices[EnvIdx.length + I] = EnvVertCount + CarBuffers.Indices[I];
    }

    return {
        Vertices: CombinedVerts,
        Indices: CombinedIndices,
        EnvIndexStart: 0,
        EnvIndexCount: EnvIdx.length,
        VehicleIndexStart: EnvIdx.length,
        VehicleIndexCount: CarBuffers.Indices.length,
        VehicleTriangleCount: AssembledCar.TotalFaces,
        VehicleVertexCount: AssembledCar.TotalVerts
    };
}

// Builds the double-sided translucent SDF cross-section inspection plane (Tag = 7.0)
// AxisMode 0: Transverse Rib Cut (Z = CutCoord); AxisMode 1: Longitudinal Profile Cut (X = CutCoord)
export function BuildCutawayPlaneBuffers(AxisMode, CutCoord)
{
    const Verts = new Float32Array(4 * ShowroomVertexFloats);
    const Indices = new Uint32Array([0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2]);

    let Corners;
    let Normal;
    if (AxisMode === 0)
    {
        // Z = CutCoord transverse cross-section plane
        Corners = [
            [-1.95, 0.002, CutCoord],
            [ 1.95, 0.002, CutCoord],
            [ 1.95, 2.350, CutCoord],
            [-1.95, 2.350, CutCoord]
        ];
        Normal = [0.0, 0.0, 1.0];
    }
    else
    {
        // X = CutCoord longitudinal cross-section plane
        Corners = [
            [CutCoord, 0.002, -3.65],
            [CutCoord, 0.002,  3.65],
            [CutCoord, 2.350,  3.65],
            [CutCoord, 2.350, -3.65]
        ];
        Normal = [1.0, 0.0, 0.0];
    }

    for (let I = 0; I < 4; ++I)
    {
        const Dst = I * ShowroomVertexFloats;
        Verts[Dst + 0]  = Corners[I][0];
        Verts[Dst + 1]  = Corners[I][1];
        Verts[Dst + 2]  = Corners[I][2];
        Verts[Dst + 3]  = Normal[0];
        Verts[Dst + 4]  = Normal[1];
        Verts[Dst + 5]  = Normal[2];
        Verts[Dst + 6]  = 0.15;
        Verts[Dst + 7]  = 0.55;
        Verts[Dst + 8]  = 0.85;
        Verts[Dst + 9]  = 0.25;
        Verts[Dst + 10] = 7.0;
    }

    return { Vertices: Verts, Indices, IndexCount: Indices.length };
}

function InsideCarvedWheelArch(Px, Py, Pz)
{
    for (let W = 0; W < 4; ++W)
    {
        const Mount = VehicleWheelMounts[W];
        const SideSign = Mount.MirrorX ? 1.0 : -1.0;
        const LatX = Px * SideSign;
        if (LatX > Mount.InboardAbsX + 0.01)
        {
            const Rho = Math.hypot(Py - Mount.Center[1], Pz - Mount.Center[2]);
            if (Rho < Mount.ArchRad - 0.006)
            {
                return true;
            }
        }
    }
    return false;
}

// Builds the 3D Bi-Directional Lofted Rib & Rail Curve Wireframe Overlay:
//   - Transverse Cross-Section Rib Curves (Cyan)
//   - Longitudinal Loft Rail Curves (Solar Gold)
//   - Wheel-Arch Cutout Lips + 4 Revolved Wheel Profile Curves (Emerald Mint)
export function BuildLoftCurveCageBuffers(LoftSdf)
{
    const Lines = [];

    function PushSeg(Ax, Ay, Az, Bx, By, Bz, Cr, Cg, Cb, Alpha)
    {
        Lines.push(
            Ax, Ay, Az, Cr, Cg, Cb, Alpha,
            Bx, By, Bz, Cr, Cg, Cb, Alpha
        );
    }

    function EvalOffsetLoftPoint(U, V, LiftM)
    {
        const S = EvalBicubicLoftSurface(LoftSdf, U, V);
        // Outward surface normal = Sv x Su (or Su x Sv checked against radial vector from spine)
        const Nx = S.Su[1] * S.Sv[2] - S.Su[2] * S.Sv[1];
        const Ny = S.Su[2] * S.Sv[0] - S.Su[0] * S.Sv[2];
        const Nz = S.Su[0] * S.Sv[1] - S.Su[1] * S.Sv[0];
        const Rx = S.Pos[0];
        const Ry = S.Pos[1] - S.SpineY;
        const Flip = (Nx * Rx + Ny * Ry) < 0.0 ? -1.0 : 1.0;
        const Len = Math.hypot(Nx, Ny, Nz) || 1.0;
        return [
            S.Pos[0] + (Nx / Len) * Flip * LiftM,
            S.Pos[1] + (Ny / Len) * Flip * LiftM,
            S.Pos[2] + (Nz / Len) * Flip * LiftM
        ];
    }

    // 1. M Transverse Cross-Section Rib Splines (Electric Cyan)
    const RibAngSteps = 84;
    for (let M = 0; M < LoftSdf.RibsM; ++M)
    {
        const U = M / (LoftSdf.RibsM - 1);
        let Prev = EvalOffsetLoftPoint(U, 0.0, 0.0045);
        let PrevCarved = InsideCarvedWheelArch(Prev[0], Prev[1], Prev[2]);
        for (let S = 1; S <= RibAngSteps; ++S)
        {
            const V = (S % RibAngSteps) / RibAngSteps;
            const Curr = EvalOffsetLoftPoint(U, V, 0.0045);
            const CurrCarved = InsideCarvedWheelArch(Curr[0], Curr[1], Curr[2]);
            if (!PrevCarved && !CurrCarved)
            {
                PushSeg(Prev[0], Prev[1], Prev[2], Curr[0], Curr[1], Curr[2], 0.24, 0.86, 0.98, 0.92);
            }
            Prev = Curr;
            PrevCarved = CurrCarved;
        }
    }

    // 2. K Longitudinal Loft Rail Splines (Solar Gold)
    const RailAxialSteps = 112;
    for (let K = 0; K < LoftSdf.RailsK; ++K)
    {
        const V = K / LoftSdf.RailsK;
        let Prev = EvalOffsetLoftPoint(0.0, V, 0.0052);
        let PrevCarved = InsideCarvedWheelArch(Prev[0], Prev[1], Prev[2]);
        for (let S = 1; S <= RailAxialSteps; ++S)
        {
            const U = S / RailAxialSteps;
            const Curr = EvalOffsetLoftPoint(U, V, 0.0052);
            const CurrCarved = InsideCarvedWheelArch(Curr[0], Curr[1], Curr[2]);
            if (!PrevCarved && !CurrCarved)
            {
                PushSeg(Prev[0], Prev[1], Prev[2], Curr[0], Curr[1], Curr[2], 0.98, 0.74, 0.25, 0.88);
            }
            Prev = Curr;
            PrevCarved = CurrCarved;
        }
    }

    // 3. 4 Revolved Wheel Profile Curves (Emerald Mint)
    const WheelRingSteps = 48;
    const Hw = LoftSdf.WheelHalfWidth;
    for (let W = 0; W < 4; ++W)
    {
        const Mount = VehicleWheelMounts[W];
        const Flip = Mount.MirrorX ? -1.0 : 1.0;

        // Generate 5 axial profile rings across each tyre/rim width
        const RingCount = Math.min(6, LoftSdf.WheelSamples);
        for (let R = 0; R < RingCount; ++R)
        {
            const Frac = R / (RingCount - 1);
            const SampleIdx = Math.min(LoftSdf.WheelSamples - 1, Math.round(Frac * (LoftSdf.WheelSamples - 1)));
            const Rad = LoftSdf.WheelOuterProfile[SampleIdx] + 0.004;
            const LocalX = (-Hw + 2.0 * Hw * Frac) * Flip;
            const Wx = Mount.Center[0] + LocalX;

            for (let S = 0; S < WheelRingSteps; ++S)
            {
                const A0 = (S / WheelRingSteps) * Math.PI * 2.0;
                const A1 = ((S + 1) / WheelRingSteps) * Math.PI * 2.0;
                PushSeg(
                    Wx, Mount.Center[1] + Math.sin(A0) * Rad, Mount.Center[2] + Math.cos(A0) * Rad,
                    Wx, Mount.Center[1] + Math.sin(A1) * Rad, Mount.Center[2] + Math.cos(A1) * Rad,
                    0.28, 0.92, 0.62, 0.90
                );
            }
        }
    }

    const Arr = new Float32Array(Lines);
    return {
        Vertices: Arr,
        VertexCount: Arr.length / CageLineVertexFloats
    };
}

// Builds the 3D Rectilinear Voxel Lattice Cage around surface-straddling voxels (|d| <= 0.55 * VoxelDiag)
export function BuildVoxelCageBuffers(VoxelSdf)
{
    const Lines = [];
    function PushSeg(Ax, Ay, Az, Bx, By, Bz, Cr, Cg, Cb, Alpha)
    {
        Lines.push(
            Ax, Ay, Az, Cr, Cg, Cb, Alpha,
            Bx, By, Bz, Cr, Cg, Cb, Alpha
        );
    }

    function PushBox(X0, Y0, Z0, X1, Y1, Z1, Cr, Cg, Cb, Alpha)
    {
        PushSeg(X0, Y0, Z0, X1, Y0, Z0, Cr, Cg, Cb, Alpha);
        PushSeg(X1, Y0, Z0, X1, Y0, Z1, Cr, Cg, Cb, Alpha);
        PushSeg(X1, Y0, Z1, X0, Y0, Z1, Cr, Cg, Cb, Alpha);
        PushSeg(X0, Y0, Z1, X0, Y0, Z0, Cr, Cg, Cb, Alpha);

        PushSeg(X0, Y1, Z0, X1, Y1, Z0, Cr, Cg, Cb, Alpha);
        PushSeg(X1, Y1, Z0, X1, Y1, Z1, Cr, Cg, Cb, Alpha);
        PushSeg(X1, Y1, Z1, X0, Y1, Z1, Cr, Cg, Cb, Alpha);
        PushSeg(X0, Y1, Z1, X0, Y1, Z0, Cr, Cg, Cb, Alpha);

        PushSeg(X0, Y0, Z0, X0, Y1, Z0, Cr, Cg, Cb, Alpha);
        PushSeg(X1, Y0, Z0, X1, Y1, Z0, Cr, Cg, Cb, Alpha);
        PushSeg(X1, Y0, Z1, X1, Y1, Z1, Cr, Cg, Cb, Alpha);
        PushSeg(X0, Y0, Z1, X0, Y1, Z1, Cr, Cg, Cb, Alpha);
    }

    const BMin  = VoxelSdf.BoundsMin;
    const BMax  = VoxelSdf.BoundsMax;
    const Pitch = VoxelSdf.VoxelPitch;
    const Res   = VoxelSdf.Res;
    const Vol   = VoxelSdf.FloatVol;

    // Outer 3D Grid Volume Box
    PushBox(BMin[0], BMin[1], BMin[2], BMax[0], BMax[1], BMax[2], 0.96, 0.56, 0.30, 0.45);

    // Surface-straddling voxel cells (stride 2 on 64^3 so wireframe remains crisp and readable)
    const Stride = Res >= 64 ? 2 : 1;
    const Band = VoxelSdf.VoxelDiag * 0.48;

    for (let Z = 0; Z < Res; Z += Stride)
    {
        for (let Y = 0; Y < Res; Y += Stride)
        {
            for (let X = 0; X < Res; X += Stride)
            {
                const Sd = Vol[(Z * Res + Y) * Res + X];
                if (Math.abs(Sd) <= Band)
                {
                    const X0 = BMin[0] + X * Pitch[0];
                    const Y0 = BMin[1] + Y * Pitch[1];
                    const Z0 = BMin[2] + Z * Pitch[2];
                    PushBox(
                        X0, Y0, Z0,
                        X0 + Pitch[0] * Stride, Y0 + Pitch[1] * Stride, Z0 + Pitch[2] * Stride,
                        0.96, 0.42, 0.34, 0.52
                    );
                }
            }
        }
    }

    const Arr = new Float32Array(Lines);
    return {
        Vertices: Arr,
        VertexCount: Arr.length / CageLineVertexFloats
    };
}
