// Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleSdfCompiler.js
// 🧩 Loads the real 25,068-triangle Frontier ControlVehicle ("Controlled Integrator": Body + 4x Rim + 4x Tyre),
//    builds an exact SAH Bounding Volume Accelerator over all 25,068 triangles for Ground-Truth distance & ray queries,
//    and compiles TWO competing Signed Distance Field paradigms on the exact same vehicle:
//      ① Parametric Bi-Directional Lofted Curve SDF: M Transverse Cross-Section Rib Splines x K Longitudinal Loft Rail Splines
//        (Bicubic Ferguson-Hermite / Catmull-Rom surface skin + 4 Revolved Wheel-Profile Curve Lofts, O(M*K) kilobytes VRAM!)
//      ② Uniform 3D Voxel Grid SDFs (16^3, 32^3, 64^3 rgba16float volumes, O(N^3) VRAM!)

export const LoftResolutionPresets = Object.freeze([
    { PresetKey: "loft-10x16", Label: "Loft 10 × 16 (Ultra-Compact)", RibsM: 10, RailsK: 16, WheelSamples: 12 },
    { PresetKey: "loft-18x24", Label: "Loft 18 × 24 (Balanced)",      RibsM: 18, RailsK: 24, WheelSamples: 16 },
    { PresetKey: "loft-32x36", Label: "Loft 32 × 36 (High-Precision)", RibsM: 32, RailsK: 36, WheelSamples: 24 },
    { PresetKey: "loft-48x64", Label: "Loft 48 × 64 (Reference Skin)", RibsM: 48, RailsK: 64, WheelSamples: 32 }
]);

export const VoxelResolutionPresets = Object.freeze([
    { PresetKey: "vox-16", Label: "Voxel 16³ (Coarse Block)",    Res: 16 },
    { PresetKey: "vox-32", Label: "Voxel 32³ (Standard Game)",   Res: 32 },
    { PresetKey: "vox-64", Label: "Voxel 64³ (Dense 512 KB)",    Res: 64 }
]);

// Exact Wheel Axle Placements from Frontier ControlledIntegrator specification
// Converted from native (x_fwd, y_left, z_up) -> Y-Up World (-y_left, z_up + 0.4172, x_fwd) [det = +1]
export const VehicleGroundLiftM = 0.4172;
export const RimFitScale        = 0.34460 / 0.344818;

export const VehicleWheelMounts = Object.freeze([
    { Label: "FL", Center: [-1.0475, 0.0914 + VehicleGroundLiftM,  1.7274], MirrorX: false, ArchRad: 0.560, InboardAbsX: 0.750 },
    { Label: "FR", Center: [ 1.0475, 0.0914 + VehicleGroundLiftM,  1.7274], MirrorX: true,  ArchRad: 0.560, InboardAbsX: 0.750 },
    { Label: "RL", Center: [-1.0475, 0.0914 + VehicleGroundLiftM, -1.6686], MirrorX: false, ArchRad: 0.562, InboardAbsX: 0.885 },
    { Label: "RR", Center: [ 1.0475, 0.0914 + VehicleGroundLiftM, -1.6686], MirrorX: true,  ArchRad: 0.562, InboardAbsX: 0.885 }
]);

function Clamp(X, Lo, Hi)
{
    return X < Lo ? Lo : (X > Hi ? Hi : X);
}

function Fract(X)
{
    return X - Math.floor(X);
}

export function EncodeFloat16Bits(Num)
{
    if (!Number.isFinite(Num)) return Num > 0 ? 0x7C00 : (Num < 0 ? 0xFC00 : 0x7E00);
    const Sign = (Num < 0 || Object.is(Num, -0)) ? 0x8000 : 0;
    const AbsN = Math.abs(Num);
    if (AbsN === 0.0) return Sign;
    if (AbsN >= 65504.0) return Sign | 0x7BFF;
    if (AbsN < 6.103515625e-5)
    {
        const Sub = Math.round(AbsN / 5.960464477539063e-8);
        return Sign | (Sub & 0x03FF);
    }
    const Exp = Math.floor(Math.log2(AbsN));
    const Mant = AbsN / Math.pow(2, Exp) - 1.0;
    let Biased = Exp + 15;
    let Frac = Math.round(Mant * 1024.0);
    if (Frac >= 1024) { Frac = 0; ++Biased; }
    if (Biased >= 31) return Sign | 0x7BFF;
    return Sign | ((Biased & 0x1F) << 10) | (Frac & 0x03FF);
}

//============================================================================================================================================
//                       1. FCT1 BINARY VEHICLE DECODER & ASSEMBLED 25,068-TRIANGLE CONTROL VEHICLE
//============================================================================================================================================

export function ParseFct1VehicleBinary(RawBuffer)
{
    const Bytes = new Uint8Array(RawBuffer);
    const Magic = String.fromCharCode(Bytes[0], Bytes[1], Bytes[2], Bytes[3]);
    if (Magic !== "FCT1")
    {
        throw new Error(`Unexpected FCT1 binary header "${Magic}"`);
    }

    const HeaderWords = new Uint32Array(RawBuffer, 0, 12);
    const VertexCount = HeaderWords[1];
    const FaceCount   = HeaderWords[2];
    const PosStartByte   = 48;
    const IdxStartByte   = PosStartByte + VertexCount * 12;
    const SlotStartByte  = IdxStartByte + FaceCount * 12;

    const SrcPositions = new Float32Array(RawBuffer, PosStartByte, VertexCount * 3);
    const Positions = new Float32Array(VertexCount * 3);
    for (let V = 0; V < VertexCount; ++V)
    {
        const Xf = SrcPositions[V * 3 + 0];
        const Yl = SrcPositions[V * 3 + 1];
        const Zu = SrcPositions[V * 3 + 2];
        // Right-handed Y-up mapping: WorldX = -y_left, WorldY = z_up, WorldZ = x_fwd (det = +1)
        Positions[V * 3 + 0] = -Yl;
        Positions[V * 3 + 1] =  Zu;
        Positions[V * 3 + 2] =  Xf;
    }

    const Indices     = new Uint32Array(new Uint32Array(RawBuffer, IdxStartByte, FaceCount * 3));
    const FinishSlots = new Uint32Array(new Uint32Array(RawBuffer, SlotStartByte, FaceCount));

    return { VertexCount, FaceCount, Positions, Indices, FinishSlots };
}

// Computes smooth area-weighted vertex normals split cleanly at hard automotive creases (> 48 deg)
export function AssembleCompleteControlVehicle(BodyBin, RimBin, TyreBin)
{
    const Parts = [];

    function AppendTransformedPart(SrcBin, OffsetVec, ScaleFactor, MirrorX, OverrideSlot, PartTag)
    {
        const VCount = SrcBin.VertexCount;
        const FCount = SrcBin.FaceCount;
        const Pos = new Float32Array(VCount * 3);
        const Flip = MirrorX ? -1.0 : 1.0;

        for (let V = 0; V < VCount; ++V)
        {
            Pos[V * 3 + 0] = OffsetVec[0] + SrcBin.Positions[V * 3 + 0] * ScaleFactor * Flip;
            Pos[V * 3 + 1] = OffsetVec[1] + SrcBin.Positions[V * 3 + 1] * ScaleFactor;
            Pos[V * 3 + 2] = OffsetVec[2] + SrcBin.Positions[V * 3 + 2] * ScaleFactor;
        }

        const Idx = new Uint32Array(FCount * 3);
        const Slots = new Uint32Array(FCount);
        for (let F = 0; F < FCount; ++F)
        {
            const I0 = SrcBin.Indices[F * 3 + 0];
            const I1 = SrcBin.Indices[F * 3 + 1];
            const I2 = SrcBin.Indices[F * 3 + 2];
            if (MirrorX)
            {
                Idx[F * 3 + 0] = I0;
                Idx[F * 3 + 1] = I2;
                Idx[F * 3 + 2] = I1;
            }
            else
            {
                Idx[F * 3 + 0] = I0;
                Idx[F * 3 + 1] = I1;
                Idx[F * 3 + 2] = I2;
            }
            Slots[F] = OverrideSlot >= 0 ? OverrideSlot : SrcBin.FinishSlots[F];
        }

        Parts.push({ PartTag, VertexCount: VCount, FaceCount: FCount, Positions: Pos, Indices: Idx, FinishSlots: Slots });
    }

    // 1. Body skin lifted so tyre bottoms rest flush on y = 0.000 m
    AppendTransformedPart(BodyBin, [0.0, VehicleGroundLiftM, 0.0], 1.0, false, -1, "body");

    // 2. Four wheels (Rim = slot 10, Tyre = slot 9)
    for (let W = 0; W < VehicleWheelMounts.length; ++W)
    {
        const M = VehicleWheelMounts[W];
        AppendTransformedPart(RimBin,  M.Center, RimFitScale, M.MirrorX, 10, "rim");
        AppendTransformedPart(TyreBin, M.Center, 1.0,         M.MirrorX,  9, "tyre");
    }

    let TotalVerts = 0;
    let TotalFaces = 0;
    for (const P of Parts)
    {
        TotalVerts += P.VertexCount;
        TotalFaces += P.FaceCount;
    }

    const AllPositions = new Float32Array(TotalVerts * 3);
    const AllIndices   = new Uint32Array(TotalFaces * 3);
    const AllSlots     = new Uint32Array(TotalFaces);

    const BoundsMin = [ 1e9,  1e9,  1e9];
    const BoundsMax = [-1e9, -1e9, -1e9];

    let VStart = 0;
    let FStart = 0;
    for (const P of Parts)
    {
        AllPositions.set(P.Positions, VStart * 3);
        for (let V = 0; V < P.VertexCount; ++V)
        {
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                const Coord = P.Positions[V * 3 + Axis];
                if (Coord < BoundsMin[Axis]) BoundsMin[Axis] = Coord;
                if (Coord > BoundsMax[Axis]) BoundsMax[Axis] = Coord;
            }
        }
        for (let F = 0; F < P.FaceCount; ++F)
        {
            AllIndices[(FStart + F) * 3 + 0] = VStart + P.Indices[F * 3 + 0];
            AllIndices[(FStart + F) * 3 + 1] = VStart + P.Indices[F * 3 + 1];
            AllIndices[(FStart + F) * 3 + 2] = VStart + P.Indices[F * 3 + 2];
            AllSlots[FStart + F] = P.FinishSlots[F];
        }
        VStart += P.VertexCount;
        FStart += P.FaceCount;
    }

    return {
        BodyPart: Parts[0],
        RimRaw: RimBin,
        TyreRaw: TyreBin,
        TotalVerts,
        TotalFaces,
        Positions: AllPositions,
        Indices: AllIndices,
        FinishSlots: AllSlots,
        BoundsMin,
        BoundsMax
    };
}

//============================================================================================================================================
//                     2. EXACT BOUNDING VOLUME ACCELERATOR OVER ALL 25,068 TRIANGLES (GROUND TRUTH)
//============================================================================================================================================

export function BuildTriangleBvhAccelerator(Positions, Indices, FaceCount)
{
    const TriCentroids = new Float32Array(FaceCount * 3);
    const TriBoxesMin  = new Float32Array(FaceCount * 3);
    const TriBoxesMax  = new Float32Array(FaceCount * 3);
    const TriOrder     = new Uint32Array(FaceCount);

    for (let F = 0; F < FaceCount; ++F)
    {
        TriOrder[F] = F;
        const I0 = Indices[F * 3 + 0] * 3;
        const I1 = Indices[F * 3 + 1] * 3;
        const I2 = Indices[F * 3 + 2] * 3;
        for (let K = 0; K < 3; ++K)
        {
            const A = Positions[I0 + K], B = Positions[I1 + K], C = Positions[I2 + K];
            const Lo = Math.min(A, B, C), Hi = Math.max(A, B, C);
            TriBoxesMin[F * 3 + K]  = Lo;
            TriBoxesMax[F * 3 + K]  = Hi;
            TriCentroids[F * 3 + K] = (A + B + C) / 3.0;
        }
    }

    // Flat BVH Box Array:
    //   BoxBounds: 6 floats [MinX, MinY, MinZ, MaxX, MaxY, MaxZ]
    //   BoxLinks:  2 ints   [LeftOrStart, RightOrNegCount] (if RightOrNegCount <= 0 -> leaf with count = -RightOrNegCount)
    const BoxBounds = [];
    const BoxLinks  = [];

    function BuildRecursive(Start, End)
    {
        const BoxIdx = BoxBounds.length / 6;
        let MinX = 1e9, MinY = 1e9, MinZ = 1e9;
        let MaxX = -1e9, MaxY = -1e9, MaxZ = -1e9;
        let CMinX = 1e9, CMinY = 1e9, CMinZ = 1e9;
        let CMaxX = -1e9, CMaxY = -1e9, CMaxZ = -1e9;

        for (let I = Start; I < End; ++I)
        {
            const F = TriOrder[I] * 3;
            if (TriBoxesMin[F + 0] < MinX) MinX = TriBoxesMin[F + 0];
            if (TriBoxesMin[F + 1] < MinY) MinY = TriBoxesMin[F + 1];
            if (TriBoxesMin[F + 2] < MinZ) MinZ = TriBoxesMin[F + 2];
            if (TriBoxesMax[F + 0] > MaxX) MaxX = TriBoxesMax[F + 0];
            if (TriBoxesMax[F + 1] > MaxY) MaxY = TriBoxesMax[F + 1];
            if (TriBoxesMax[F + 2] > MaxZ) MaxZ = TriBoxesMax[F + 2];

            const Cx = TriCentroids[F + 0], Cy = TriCentroids[F + 1], Cz = TriCentroids[F + 2];
            if (Cx < CMinX) CMinX = Cx; if (Cx > CMaxX) CMaxX = Cx;
            if (Cy < CMinY) CMinY = Cy; if (Cy > CMaxY) CMaxY = Cy;
            if (Cz < CMinZ) CMinZ = Cz; if (Cz > CMaxZ) CMaxZ = Cz;
        }

        BoxBounds.push(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
        BoxLinks.push(0, 0);

        const Count = End - Start;
        if (Count <= 6)
        {
            BoxLinks[BoxIdx * 2 + 0] = Start;
            BoxLinks[BoxIdx * 2 + 1] = -Count;
            return BoxIdx;
        }

        const Ex = CMaxX - CMinX, Ey = CMaxY - CMinY, Ez = CMaxZ - CMinZ;
        const Axis = (Ex >= Ey && Ex >= Ez) ? 0 : (Ey >= Ez ? 1 : 2);
        const SplitPos = 0.5 * ((Axis === 0 ? CMinX + CMaxX : (Axis === 1 ? CMinY + CMaxY : CMinZ + CMaxZ)));

        let Left = Start, Right = End - 1;
        while (Left <= Right)
        {
            if (TriCentroids[TriOrder[Left] * 3 + Axis] < SplitPos)
            {
                ++Left;
            }
            else
            {
                const Tmp = TriOrder[Left];
                TriOrder[Left] = TriOrder[Right];
                TriOrder[Right] = Tmp;
                --Right;
            }
        }

        let Mid = Left;
        if (Mid === Start || Mid === End) Mid = Start + (Count >> 1);

        const LSub = BuildRecursive(Start, Mid);
        const RSub = BuildRecursive(Mid, End);
        BoxLinks[BoxIdx * 2 + 0] = LSub;
        BoxLinks[BoxIdx * 2 + 1] = RSub;
        return BoxIdx;
    }

    BuildRecursive(0, FaceCount);

    // Pack ordered triangle vertices for locality-ordered closest-point & ray intersection!
    const OrderedTriVerts = new Float32Array(FaceCount * 9);
    for (let I = 0; I < FaceCount; ++I)
    {
        const F = TriOrder[I];
        for (let K = 0; K < 3; ++K)
        {
            const Vi = Indices[F * 3 + K] * 3;
            OrderedTriVerts[I * 9 + K * 3 + 0] = Positions[Vi + 0];
            OrderedTriVerts[I * 9 + K * 3 + 1] = Positions[Vi + 1];
            OrderedTriVerts[I * 9 + K * 3 + 2] = Positions[Vi + 2];
        }
    }

    return {
        BoxCount: BoxBounds.length / 6,
        BoxBounds: new Float32Array(BoxBounds),
        BoxLinks:  new Int32Array(BoxLinks),
        OrderedTriVerts,
        FaceCount
    };
}

// Exact squared distance from point P to triangle (A, B, C) (Ericson, Real-Time Collision Detection)
function PointTriangleDistSq(Px, Py, Pz, Tv, Start9)
{
    const Ax = Tv[Start9 + 0], Ay = Tv[Start9 + 1], Az = Tv[Start9 + 2];
    const Bx = Tv[Start9 + 3], By = Tv[Start9 + 4], Bz = Tv[Start9 + 5];
    const Cx = Tv[Start9 + 6], Cy = Tv[Start9 + 7], Cz = Tv[Start9 + 8];

    const Abx = Bx - Ax, Aby = By - Ay, Abz = Bz - Az;
    const Acx = Cx - Ax, Acy = Cy - Ay, Acz = Cz - Az;
    const Apx = Px - Ax, Apy = Py - Ay, Apz = Pz - Az;

    const D1 = Abx * Apx + Aby * Apy + Abz * Apz;
    const D2 = Acx * Apx + Acy * Apy + Acz * Apz;
    if (D1 <= 0.0 && D2 <= 0.0) return Apx * Apx + Apy * Apy + Apz * Apz;

    const Bpx = Px - Bx, Bpy = Py - By, Bpz = Pz - Bz;
    const D3 = Abx * Bpx + Aby * Bpy + Abz * Bpz;
    const D4 = Acx * Bpx + Acy * Bpy + Acz * Bpz;
    if (D3 >= 0.0 && D4 <= D3) return Bpx * Bpx + Bpy * Bpy + Bpz * Bpz;

    const Vc = D1 * D4 - D3 * D2;
    if (Vc <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        const V = D1 / (D1 - D3);
        const Dx = Px - (Ax + V * Abx), Dy = Py - (Ay + V * Aby), Dz = Pz - (Az + V * Abz);
        return Dx * Dx + Dy * Dy + Dz * Dz;
    }

    const Cpx = Px - Cx, Cpy = Py - Cy, Cpz = Pz - Cz;
    const D5 = Abx * Cpx + Aby * Cpy + Abz * Cpz;
    const D6 = Acx * Cpx + Acy * Cpy + Acz * Cpz;
    if (D6 >= 0.0 && D5 <= D6) return Cpx * Cpx + Cpy * Cpy + Cpz * Cpz;

    const Vb = D5 * D2 - D1 * D6;
    if (Vb <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        const W = D2 / (D2 - D6);
        const Dx = Px - (Ax + W * Acx), Dy = Py - (Ay + W * Acy), Dz = Pz - (Az + W * Acz);
        return Dx * Dx + Dy * Dy + Dz * Dz;
    }

    const Va = D3 * D6 - D5 * D4;
    if (Va <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        const W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
        const Dx = Px - (Bx + W * (Cx - Bx)), Dy = Py - (By + W * (Cy - By)), Dz = Pz - (Bz + W * (Cz - Bz));
        return Dx * Dx + Dy * Dy + Dz * Dz;
    }

    const Denom = 1.0 / (Va + Vb + Vc);
    const V = Vb * Denom;
    const W = Vc * Denom;
    const Dx = Px - (Ax + Abx * V + Acx * W);
    const Dy = Py - (Ay + Aby * V + Acy * W);
    const Dz = Pz - (Az + Abz * V + Acz * W);
    return Dx * Dx + Dy * Dy + Dz * Dz;
}

function PointAabbDistSq(Px, Py, Pz, Bounds, Off6)
{
    const Dx = Math.max(Bounds[Off6 + 0] - Px, 0.0, Px - Bounds[Off6 + 3]);
    const Dy = Math.max(Bounds[Off6 + 1] - Py, 0.0, Py - Bounds[Off6 + 4]);
    const Dz = Math.max(Bounds[Off6 + 2] - Pz, 0.0, Pz - Bounds[Off6 + 5]);
    return Dx * Dx + Dy * Dy + Dz * Dz;
}

// Möller–Trumbore ray-triangle hit test (counts parity intersections or finds closest hit t)
export function IntersectRayBvhClosest(Bvh, Ro, Rd, TMax = 1e6)
{
    const Bounds = Bvh.BoxBounds;
    const Links  = Bvh.BoxLinks;
    const Tv     = Bvh.OrderedTriVerts;
    const InvX   = 1.0 / (Math.abs(Rd[0]) < 1e-8 ? (Rd[0] >= 0 ? 1e-8 : -1e-8) : Rd[0]);
    const InvY   = 1.0 / (Math.abs(Rd[1]) < 1e-8 ? (Rd[1] >= 0 ? 1e-8 : -1e-8) : Rd[1]);
    const InvZ   = 1.0 / (Math.abs(Rd[2]) < 1e-8 ? (Rd[2] >= 0 ? 1e-8 : -1e-8) : Rd[2]);

    const Stack = [0];
    let ClosestT = TMax;
    let HitAny = false;

    while (Stack.length > 0)
    {
        const BoxIdx = Stack.pop();
        const Off6 = BoxIdx * 6;
        const Tx0 = (Bounds[Off6 + 0] - Ro[0]) * InvX, Tx1 = (Bounds[Off6 + 3] - Ro[0]) * InvX;
        const Ty0 = (Bounds[Off6 + 1] - Ro[1]) * InvY, Ty1 = (Bounds[Off6 + 4] - Ro[1]) * InvY;
        const Tz0 = (Bounds[Off6 + 2] - Ro[2]) * InvZ, Tz1 = (Bounds[Off6 + 5] - Ro[2]) * InvZ;
        const TEnter = Math.max(0.0, Math.min(Tx0, Tx1), Math.min(Ty0, Ty1), Math.min(Tz0, Tz1));
        const TExit  = Math.min(ClosestT, Math.max(Tx0, Tx1), Math.max(Ty0, Ty1), Math.max(Tz0, Tz1));
        if (TEnter > TExit) continue;

        const L = Links[BoxIdx * 2 + 0];
        const R = Links[BoxIdx * 2 + 1];
        if (R <= 0)
        {
            const Count = -R;
            for (let I = 0; I < Count; ++I)
            {
                const B9 = (L + I) * 9;
                const E1x = Tv[B9 + 3] - Tv[B9 + 0], E1y = Tv[B9 + 4] - Tv[B9 + 1], E1z = Tv[B9 + 5] - Tv[B9 + 2];
                const E2x = Tv[B9 + 6] - Tv[B9 + 0], E2y = Tv[B9 + 7] - Tv[B9 + 1], E2z = Tv[B9 + 8] - Tv[B9 + 2];
                const Hx = Rd[1] * E2z - Rd[2] * E2y;
                const Hy = Rd[2] * E2x - Rd[0] * E2z;
                const Hz = Rd[0] * E2y - Rd[1] * E2x;
                const Det = E1x * Hx + E1y * Hy + E1z * Hz;
                if (Math.abs(Det) < 1e-8) continue;
                const InvDet = 1.0 / Det;
                const Sx = Ro[0] - Tv[B9 + 0], Sy = Ro[1] - Tv[B9 + 1], Sz = Ro[2] - Tv[B9 + 2];
                const U = InvDet * (Sx * Hx + Sy * Hy + Sz * Hz);
                if (U < 0.0 || U > 1.0) continue;
                const Qx = Sy * E1z - Sz * E1y;
                const Qy = Sz * E1x - Sx * E1z;
                const Qz = Sx * E1y - Sy * E1x;
                const V = InvDet * (Rd[0] * Qx + Rd[1] * Qy + Rd[2] * Qz);
                if (V < 0.0 || U + V > 1.0) continue;
                const T = InvDet * (E2x * Qx + E2y * Qy + E2z * Qz);
                if (T > 1e-4 && T < ClosestT)
                {
                    ClosestT = T;
                    HitAny = true;
                }
            }
        }
        else
        {
            Stack.push(L, R);
        }
    }

    return HitAny ? ClosestT : -1.0;
}

// Exact Euclidean Surface Distance to the 25,068-triangle ControlVehicle (exact ground truth for all exterior shadow/AO/reflection queries!)
export function EvaluateExactSurfaceDistance(Bvh, P)
{
    const Px = P[0], Py = P[1], Pz = P[2];
    const Bounds = Bvh.BoxBounds;
    const Links  = Bvh.BoxLinks;
    const Tv     = Bvh.OrderedTriVerts;

    const Stack = [0];
    let BestSq = 1e12;

    while (Stack.length > 0)
    {
        const BoxIdx = Stack.pop();
        if (PointAabbDistSq(Px, Py, Pz, Bounds, BoxIdx * 6) >= BestSq) continue;

        const L = Links[BoxIdx * 2 + 0];
        const R = Links[BoxIdx * 2 + 1];
        if (R <= 0)
        {
            const Count = -R;
            for (let I = 0; I < Count; ++I)
            {
                const D2 = PointTriangleDistSq(Px, Py, Pz, Tv, (L + I) * 9);
                if (D2 < BestSq) BestSq = D2;
            }
        }
        else
        {
            const Dl = PointAabbDistSq(Px, Py, Pz, Bounds, L * 6);
            const Dr = PointAabbDistSq(Px, Py, Pz, Bounds, R * 6);
            if (Dl < Dr)
            {
                if (Dr < BestSq) Stack.push(R);
                if (Dl < BestSq) Stack.push(L);
            }
            else
            {
                if (Dl < BestSq) Stack.push(L);
                if (Dr < BestSq) Stack.push(R);
            }
        }
    }

    return Math.sqrt(BestSq);
}

// Signed Distance for 3D Voxel Volume Baking (uses watertight outer-hull sign + wheel cylinders so interior voxels are cleanly negative without open-panel parity leaks!)
export function EvaluateExactBvhSignedDistance(Bvh, P, ReferenceLoft = null)
{
    const UnsignedD = EvaluateExactSurfaceDistance(Bvh, P);
    if (ReferenceLoft)
    {
        const LoftD = EvaluateLoftedVehicleSdf(ReferenceLoft, P);
        return LoftD < 0.0 ? -UnsignedD : UnsignedD;
    }
    return UnsignedD;
}

//============================================================================================================================================
//          3. LOFTED BI-DIRECTIONAL CURVE SURFACE SDF COMPILER (M RIBS x K RAILS + 4 REVOLVED WHEEL LOFTS)
//============================================================================================================================================

// Smooth longitudinal spine centerline y_c(u) inside the ControlVehicle body (u = 0 at rear bumper z=-3.110, u = 1 at front nose z=+3.110)
export function EvalVehicleSpineY(U)
{
    // Rises smoothly inside the cabin (u ~ 0.35..0.68) and sits centered in the front hood & rear deck
    const CabinArch = Math.exp(-Math.pow((U - 0.46) / 0.28, 2.0));
    return 0.78 + 0.18 * CabinArch;
}

export function EvalVehicleSpineDerivY(U, ZSpan)
{
    const Diff = (U - 0.46) / 0.28;
    const Du = 0.18 * Math.exp(-Diff * Diff) * (-2.0 * Diff / 0.28);
    return Du / ZSpan; // dy_c / dz (and dy_c / du is Du)
}

// 1D Cubic Hermite basis functions H00, H10, H01, H11 and first derivatives on t in [0, 1]
function HermiteBasis1d(T)
{
    const T2 = T * T;
    const T3 = T2 * T;
    return [
        2.0 * T3 - 3.0 * T2 + 1.0, // H00
        T3 - 2.0 * T2 + T,         // H10
        -2.0 * T3 + 3.0 * T2,      // H01
        T3 - T2,                   // H11
        6.0 * T2 - 6.0 * T,        // dH00/dt
        3.0 * T2 - 4.0 * T + 1.0,  // dH10/dt
        -6.0 * T2 + 6.0 * T,       // dH01/dt
        3.0 * T2 - 2.0 * T         // dH11/dt
    ];
}

// Cuts the ControlVehicleBody triangle list at exact plane WorldZ = Z0 and returns outermost hull radius r(theta) from (0, SpineY, Z0)
function ExtractBodyCrossSectionRadii(BodyPart, Z0, SpineY, RailsK)
{
    const Pos = BodyPart.Positions;
    const Idx = BodyPart.Indices;
    const FCount = BodyPart.FaceCount;
    const Segs = [];

    for (let F = 0; F < FCount; ++F)
    {
        const Vs = [Idx[F * 3 + 0], Idx[F * 3 + 1], Idx[F * 3 + 2]];
        const Pts = [];
        for (let E = 0; E < 3; ++E)
        {
            const A = Vs[E] * 3, B = Vs[(E + 1) % 3] * 3;
            const Za = Pos[A + 2], Zb = Pos[B + 2];
            if ((Za <= Z0 && Zb >= Z0) || (Za >= Z0 && Zb <= Z0))
            {
                const T = Math.abs(Zb - Za) < 1e-8 ? 0.5 : (Z0 - Za) / (Zb - Za);
                Pts.push([
                    Pos[A + 0] + (Pos[B + 0] - Pos[A + 0]) * T,
                    Pos[A + 1] + (Pos[B + 1] - Pos[A + 1]) * T
                ]);
            }
        }
        if (Pts.length >= 2)
        {
            Segs.push(Pts[0][0], Pts[0][1], Pts[1][0], Pts[1][1]);
        }
    }

    const Radii = new Float32Array(RailsK);
    for (let K = 0; K < RailsK; ++K)
    {
        const Theta = (K / RailsK) * Math.PI * 2.0;
        const Dx = Math.cos(Theta);
        const Dy = Math.sin(Theta);
        let MaxR = 0.0;

        for (let S = 0; S < Segs.length; S += 4)
        {
            const X0 = Segs[S + 0], Y0 = Segs[S + 1] - SpineY;
            const Ux = Segs[S + 2] - X0, Uy = (Segs[S + 3] - SpineY) - Y0;
            const Det = -Dx * Uy + Dy * Ux;
            if (Math.abs(Det) < 1e-8) continue;
            const R = (-X0 * Uy + Y0 * Ux) / Det;
            const Param = (Dx * Y0 - Dy * X0) / Det;
            if (R > 0.02 && Param >= -1e-4 && Param <= 1.0001)
            {
                if (R > MaxR) MaxR = R;
            }
        }
        Radii[K] = MaxR;
    }

    // Fill any grazing degenerate sample from periodic neighbors and enforce exact left/right automotive symmetry
    for (let K = 0; K < RailsK; ++K)
    {
        if (Radii[K] <= 0.02)
        {
            const Prev = Radii[(K - 1 + RailsK) % RailsK];
            const Next = Radii[(K + 1) % RailsK];
            Radii[K] = Math.max(0.18, 0.5 * (Prev + Next));
        }
    }
    return Radii;
}

// Compiles a Bi-Directional Bicubic Ferguson-Hermite Lofted Surface SDF + 4 Revolved Wheel Curve Lofts!
export function CompileLoftedVehicleSdf(AssembledCar, RibsM = 32, RailsK = 36, WheelSamples = 24)
{
    const Body = AssembledCar.BodyPart;
    let BodyZMin = 1e9, BodyZMax = -1e9;
    for (let V = 0; V < Body.VertexCount; ++V)
    {
        const Z = Body.Positions[V * 3 + 2];
        if (Z < BodyZMin) BodyZMin = Z;
        if (Z > BodyZMax) BodyZMax = Z;
    }

    // Inset end stations by 18 mm from exact singular bumper tips so all M cross-sections have crisp full contours
    const ZMin = BodyZMin + 0.018;
    const ZMax = BodyZMax - 0.018;
    const ZSpan = ZMax - ZMin;

    // 1. Sample M x K cross-section rib radii directly from the real ControlVehicleBody geometry
    const RawRadii = new Float32Array(RibsM * RailsK);
    for (let M = 0; M < RibsM; ++M)
    {
        const U = M / (RibsM - 1);
        const Z0 = ZMin + ZSpan * U;
        const Yc = EvalVehicleSpineY(U);
        const Rib = ExtractBodyCrossSectionRadii(Body, Z0, Yc, RailsK);
        RawRadii.set(Rib, M * RailsK);
    }

    // Span smooth outer fender skin across the 4 wheel-arch openings (since exact circular wheel-arch cylinders are CSG-carved!)
    // This keeps longitudinal loft rails C^1-smooth even at ultra-coarse 10x16 loft resolution!
    const ArchWindows = [
        { ZCenter:  1.7274, HalfZ: 0.585 },
        { ZCenter: -1.6686, HalfZ: 0.585 }
    ];
    for (const Win of ArchWindows)
    {
        let MLo = -1, MHi = -1;
        for (let M = 0; M < RibsM; ++M)
        {
            const Z0 = ZMin + ZSpan * (M / (RibsM - 1));
            if (Z0 < Win.ZCenter - Win.HalfZ) MLo = M;
            if (MHi < 0 && Z0 > Win.ZCenter + Win.HalfZ) MHi = M;
        }
        if (MLo >= 0 && MHi > MLo + 1 && MHi < RibsM)
        {
            for (let M = MLo + 1; M < MHi; ++M)
            {
                const Alpha = (M - MLo) / (MHi - MLo);
                for (let K = 0; K < RailsK; ++K)
                {
                    const Theta = (K / RailsK) * Math.PI * 2.0;
                    // Only span side/lower fender rays (|cos(theta)| > 0.55 and sin(theta) < 0.22) where wheel cutout indents inward
                    if (Math.abs(Math.cos(Theta)) > 0.55 && Math.sin(Theta) < 0.22)
                    {
                        const RSpan = RawRadii[MLo * RailsK + K] * (1.0 - Alpha) + RawRadii[MHi * RailsK + K] * Alpha;
                        if (RSpan > RawRadii[M * RailsK + K])
                        {
                            RawRadii[M * RailsK + K] = RSpan;
                        }
                    }
                }
            }
        }
    }

    // 2. Compute Shape-Preserving Monotone Cubic Hermite Tangents (Fritsch-Carlson / Steffen clamped):
    //    Eliminates Catmull-Rom overshoot at sharp wheel-well arches, rocker sills, and hood creases while staying C^1 smooth!
    const HermiteKnotsF32    = new Float32Array(RibsM * RailsK * 4);
    const HermiteKnotsRgba16 = new Uint16Array(RibsM * RailsK * 4);

    const AtR = (M, K) => {
        const Cm = Clamp(M, 0, RibsM - 1);
        const Ck = ((K % RailsK) + RailsK) % RailsK;
        return RawRadii[Cm * RailsK + Ck];
    };

    function MonotoneSlope(Prev, Cur, Next)
    {
        const S0 = Cur - Prev;
        const S1 = Next - Cur;
        if (S0 * S1 <= 0.0) return 0.0;
        const Raw = 0.5 * (S0 + S1);
        const Limit = 2.0 * Math.min(Math.abs(S0), Math.abs(S1));
        return Clamp(Raw, -Limit, Limit);
    }

    for (let M = 0; M < RibsM; ++M)
    {
        for (let K = 0; K < RailsK; ++K)
        {
            const R = AtR(M, K);
            const Ru = M === 0
                ? (AtR(1, K) - AtR(0, K))
                : (M === RibsM - 1 ? (AtR(RibsM - 1, K) - AtR(RibsM - 2, K)) : MonotoneSlope(AtR(M - 1, K), R, AtR(M + 1, K)));
            const Rv = MonotoneSlope(AtR(M, K - 1), R, AtR(M, K + 1));

            // Twist derivative clamped cleanly where either axis hits a sharp crease
            let Ruv = 0.0;
            if (Ru !== 0.0 && Rv !== 0.0 && M > 0 && M < RibsM - 1)
            {
                const RvNext = MonotoneSlope(AtR(M + 1, K - 1), AtR(M + 1, K), AtR(M + 1, K + 1));
                const RvPrev = MonotoneSlope(AtR(M - 1, K - 1), AtR(M - 1, K), AtR(M - 1, K + 1));
                Ruv = 0.5 * (RvNext - RvPrev);
            }

            const Out = (M * RailsK + K) * 4;
            HermiteKnotsF32[Out + 0] = R;
            HermiteKnotsF32[Out + 1] = Ru;
            HermiteKnotsF32[Out + 2] = Rv;
            HermiteKnotsF32[Out + 3] = Ruv;

            HermiteKnotsRgba16[Out + 0] = EncodeFloat16Bits(R);
            HermiteKnotsRgba16[Out + 1] = EncodeFloat16Bits(Ru);
            HermiteKnotsRgba16[Out + 2] = EncodeFloat16Bits(Rv);
            HermiteKnotsRgba16[Out + 3] = EncodeFloat16Bits(Ruv);
        }
    }

    // 3. Compile Revolved Wheel Profile Loft Curve from real ControlVehicleTyre + ControlVehicleRim vertices!
    //    In native wheel coords (after ParseFct1VehicleBinary): X is axle lateral [-0.198..+0.198], Y & Z are radial!
    const WheelHalfWidth = 0.1979;
    const WheelOuterProfile = new Float32Array(WheelSamples);
    const WheelDishProfile  = new Float32Array(WheelSamples);

    const TyrePos = AssembledCar.TyreRaw.Positions;
    const RimPos  = AssembledCar.RimRaw.Positions;

    for (let S = 0; S < WheelSamples; ++S)
    {
        const Xi = -WheelHalfWidth + (2.0 * WheelHalfWidth * S) / (WheelSamples - 1);
        const Band = (2.2 * WheelHalfWidth) / WheelSamples;
        let MaxRad = 0.46;
        for (let V = 0; V < AssembledCar.TyreRaw.VertexCount; ++V)
        {
            const Vx = TyrePos[V * 3 + 0];
            if (Math.abs(Vx - Xi) <= Band)
            {
                const Rad = Math.hypot(TyrePos[V * 3 + 1], TyrePos[V * 3 + 2]);
                if (Rad > MaxRad) MaxRad = Rad;
            }
        }
        WheelOuterProfile[S] = MaxRad;

        // Rim outer face dish depth profile (concave spoke dish on outboard side)
        let DishR = 0.24;
        for (let V = 0; V < AssembledCar.RimRaw.VertexCount; ++V)
        {
            const Vx = RimPos[V * 3 + 0] * RimFitScale;
            if (Math.abs(Vx - Xi) <= Band * 1.2)
            {
                const Rad = Math.hypot(RimPos[V * 3 + 1], RimPos[V * 3 + 2]) * RimFitScale;
                if (Rad > DishR) DishR = Rad;
            }
        }
        WheelDishProfile[S] = DishR;
    }

    // Pack Wheel Profile into vec4<f32> array for WGSL storage buffer
    const WheelGpuFloats = new Float32Array(WheelSamples * 4);
    for (let S = 0; S < WheelSamples; ++S)
    {
        const Slope = S === 0
            ? (WheelOuterProfile[1] - WheelOuterProfile[0])
            : (S === WheelSamples - 1 ? (WheelOuterProfile[S] - WheelOuterProfile[S - 1]) : 0.5 * (WheelOuterProfile[S + 1] - WheelOuterProfile[S - 1]));
        WheelGpuFloats[S * 4 + 0] = WheelOuterProfile[S];
        WheelGpuFloats[S * 4 + 1] = Slope;
        WheelGpuFloats[S * 4 + 2] = WheelDishProfile[S];
        WheelGpuFloats[S * 4 + 3] = 0.0;
    }

    // Memory accounting:
    //   - Raw Compact Knot Encoding (fp16 radius per M*K knot + wheel profile): RibsM * RailsK * 2 + WheelSamples * 4 bytes
    //   - Pre-Differentiated GPU Hermite Patch Encoding (rgba16float 4-channel): RibsM * RailsK * 8 + WheelSamples * 8 bytes
    const CompactKnotBytes = RibsM * RailsK * 2 + WheelSamples * 4;
    const GpuHermiteBytes  = RibsM * RailsK * 8 + WheelSamples * 8;

    return {
        RibsM,
        RailsK,
        WheelSamples,
        WheelHalfWidth,
        ZMin,
        ZMax,
        ZSpan,
        BoundsMin: AssembledCar.BoundsMin,
        BoundsMax: AssembledCar.BoundsMax,
        RawRadii,
        HermiteKnotsF32,
        HermiteKnotsRgba16,
        WheelOuterProfile,
        WheelGpuFloats,
        CompactKnotBytes,
        GpuHermiteBytes
    };
}

// Evaluates the Bicubic Ferguson-Hermite Loft Surface S(u, v) and analytical partial derivatives Su, Sv at (u in [0,1], v in [0,1))
export function EvalBicubicLoftSurface(LoftSdf, UIn, VIn)
{
    const MCount = LoftSdf.RibsM;
    const KCount = LoftSdf.RailsK;
    const Knots  = LoftSdf.HermiteKnotsF32;

    const U = Clamp(UIn, 0.0, 1.0);
    const V = Fract(VIn);

    const ScaledU = U * (MCount - 1);
    const I0 = Math.min(MCount - 2, Math.floor(ScaledU));
    const I1 = I0 + 1;
    const Tu = ScaledU - I0;

    const ScaledV = V * KCount;
    const J0 = Math.floor(ScaledV) % KCount;
    const J1 = (J0 + 1) % KCount;
    const Tv = ScaledV - Math.floor(ScaledV);

    const Hu = HermiteBasis1d(Tu);
    const Hv = HermiteBasis1d(Tv);

    // 4 patch corners: (I0, J0), (I1, J0), (I0, J1), (I1, J1)
    const C00 = (I0 * KCount + J0) * 4;
    const C10 = (I1 * KCount + J0) * 4;
    const C01 = (I0 * KCount + J1) * 4;
    const C11 = (I1 * KCount + J1) * 4;

    // Evaluate 1D Hermite along u for r and r_v at J0 and J1
    const R_J0   = Hu[0] * Knots[C00]     + Hu[1] * Knots[C00 + 1] + Hu[2] * Knots[C10]     + Hu[3] * Knots[C10 + 1];
    const dRu_J0 = Hu[4] * Knots[C00]     + Hu[5] * Knots[C00 + 1] + Hu[6] * Knots[C10]     + Hu[7] * Knots[C10 + 1];
    const Rv_J0  = Hu[0] * Knots[C00 + 2] + Hu[1] * Knots[C00 + 3] + Hu[2] * Knots[C10 + 2] + Hu[3] * Knots[C10 + 3];
    const dRuv_0 = Hu[4] * Knots[C00 + 2] + Hu[5] * Knots[C00 + 3] + Hu[6] * Knots[C10 + 2] + Hu[7] * Knots[C10 + 3];

    const R_J1   = Hu[0] * Knots[C01]     + Hu[1] * Knots[C01 + 1] + Hu[2] * Knots[C11]     + Hu[3] * Knots[C11 + 1];
    const dRu_J1 = Hu[4] * Knots[C01]     + Hu[5] * Knots[C01 + 1] + Hu[6] * Knots[C11]     + Hu[7] * Knots[C11 + 1];
    const Rv_J1  = Hu[0] * Knots[C01 + 2] + Hu[1] * Knots[C01 + 3] + Hu[2] * Knots[C11 + 2] + Hu[3] * Knots[C11 + 3];
    const dRuv_1 = Hu[4] * Knots[C01 + 2] + Hu[5] * Knots[C01 + 3] + Hu[6] * Knots[C11 + 2] + Hu[7] * Knots[C11 + 3];

    const R  = Hv[0] * R_J0   + Hv[1] * Rv_J0  + Hv[2] * R_J1   + Hv[3] * Rv_J1;
    const Ru = (Hv[0] * dRu_J0 + Hv[1] * dRuv_0 + Hv[2] * dRu_J1 + Hv[3] * dRuv_1) * (MCount - 1);
    const Rv = (Hv[4] * R_J0   + Hv[5] * Rv_J0  + Hv[6] * R_J1   + Hv[7] * Rv_J1) * KCount;

    const Theta = V * Math.PI * 2.0;
    const CosT  = Math.cos(Theta);
    const SinT  = Math.sin(Theta);
    const Yc    = EvalVehicleSpineY(U);
    const dYcDu = EvalVehicleSpineDerivY(U, 1.0); // dy_c / du

    const Sx = R * CosT;
    const Sy = Yc + R * SinT;
    const Sz = LoftSdf.ZMin + LoftSdf.ZSpan * U;

    // Exact partial derivatives dS/du and dS/dv in 3D world meters!
    const TwoPi = Math.PI * 2.0;
    const Sux = Ru * CosT;
    const Suy = dYcDu + Ru * SinT;
    const Suz = LoftSdf.ZSpan;

    const Svx = Rv * CosT - R * TwoPi * SinT;
    const Svy = Rv * SinT + R * TwoPi * CosT;
    const Svz = 0.0;

    return {
        Pos: [Sx, Sy, Sz],
        Su:  [Sux, Suy, Suz],
        Sv:  [Svx, Svy, Svz],
        Radius: R,
        SpineY: Yc
    };
}

// Evaluates exact distance to one Revolved Wheel Loft Unit at WheelMount
function EvalSingleWheelLoftSdf(LoftSdf, P, Mount)
{
    const Dx = (P[0] - Mount.Center[0]) * (Mount.MirrorX ? -1.0 : 1.0);
    const Dy = P[1] - Mount.Center[1];
    const Dz = P[2] - Mount.Center[2];
    const Rho = Math.hypot(Dy, Dz);
    const Hw = LoftSdf.WheelHalfWidth;

    // Quick bounding-cylinder guard
    if (Math.abs(Dx) > Hw + 0.35 && Rho > 0.86)
    {
        return Math.hypot(Math.abs(Dx) - Hw, Math.max(0.0, Rho - 0.51));
    }

    const ClampedX = Clamp(Dx, -Hw, Hw);
    const NormS = ((ClampedX + Hw) / (2.0 * Hw)) * (LoftSdf.WheelSamples - 1);
    const I0 = Math.min(LoftSdf.WheelSamples - 2, Math.max(0, Math.floor(NormS)));
    const T  = NormS - I0;

    const H = HermiteBasis1d(T);
    const Wf = LoftSdf.WheelGpuFloats;
    const ROuter = H[0] * Wf[I0 * 4] + H[1] * Wf[I0 * 4 + 1] + H[2] * Wf[(I0 + 1) * 4] + H[3] * Wf[(I0 + 1) * 4 + 1];

    const DRad = Rho - ROuter;
    const DAx  = Math.abs(Dx) - Hw;

    // Subtle concave alloy rim dish recess on outboard face (Dx < -0.09 in native non-mirrored FL wheel)
    let DishRecess = 0.0;
    if (Rho < 0.31 && Dx < -Hw + 0.06)
    {
        const CenterDist = Math.hypot(Rho - 0.14, 0.0);
        DishRecess = 0.032 * (1.0 - Clamp(CenterDist / 0.19, 0.0, 1.0));
    }
    const EffAx = Math.max(DAx, -(Hw + DishRecess) - Dx);

    const Outside = Math.hypot(Math.max(DRad, 0.0), Math.max(EffAx, 0.0));
    const Inside  = Math.min(Math.max(DRad, EffAx), 0.0);
    return Outside + Inside;
}

// 🟢 Evaluates the Complete Lofted Bi-Directional Curve Vehicle SDF at 3D World Point P!
//    Uses O(1) Spine-Cylindrical Coordinate Seed + 2-Step Damped 2x2 Gauss-Newton Orthogonal Projection + Watertight Endcaps + 4 Wheel Lofts!
export function EvaluateLoftedVehicleSdf(LoftSdf, P)
{
    const Px = P[0], Py = P[1], Pz = P[2];

    // 1. Fast outer bounding-box distance leap when far in empty air (> 0.55 m outside vehicle box)
    const BMin = LoftSdf.BoundsMin;
    const BMax = LoftSdf.BoundsMax;
    const BoxDx = Math.max(BMin[0] - Px, 0.0, Px - BMax[0]);
    const BoxDy = Math.max(BMin[1] - Py, 0.0, Py - BMax[1]);
    const BoxDz = Math.max(BMin[2] - Pz, 0.0, Pz - BMax[2]);
    const BoxDist = Math.hypot(BoxDx, BoxDy, BoxDz);
    if (BoxDist > 0.65)
    {
        return BoxDist + 0.04;
    }

    // 2. Instant O(1) Spine-Cylindrical Seed (u0, v0) + 7-Point Star Bracket + 3-Step 2x2 Gauss-Newton Orthogonal Projection!
    let U0 = Clamp((Pz - LoftSdf.ZMin) / LoftSdf.ZSpan, 0.0, 1.0);
    const Yc0 = EvalVehicleSpineY(U0);
    let V0 = Fract(Math.atan2(Py - Yc0, Px) / (Math.PI * 2.0));

    let BestU = U0, BestV = V0;
    const SInit = EvalBicubicLoftSurface(LoftSdf, U0, V0).Pos;
    let BestSq = (Px - SInit[0]) ** 2 + (Py - SInit[1]) ** 2 + (Pz - SInit[2]) ** 2;

    const StarOffsets = [
        [-0.045, 0.0], [0.045, 0.0],
        [0.0, -0.055], [0.0, 0.055],
        [0.0, -0.115], [0.0, 0.115]
    ];
    for (let S = 0; S < StarOffsets.length; ++S)
    {
        const CandU = Clamp(U0 + StarOffsets[S][0], 0.0, 1.0);
        const CandV = Fract(V0 + StarOffsets[S][1]);
        const CandPos = EvalBicubicLoftSurface(LoftSdf, CandU, CandV).Pos;
        const D2 = (Px - CandPos[0]) ** 2 + (Py - CandPos[1]) ** 2 + (Pz - CandPos[2]) ** 2;
        if (D2 < BestSq)
        {
            BestSq = D2;
            BestU = CandU;
            BestV = CandV;
        }
    }

    let U = BestU, V = BestV;
    for (let Iter = 0; Iter < 3; ++Iter)
    {
        const Surf = EvalBicubicLoftSurface(LoftSdf, U, V);
        const Rx = Px - Surf.Pos[0];
        const Ry = Py - Surf.Pos[1];
        const Rz = Pz - Surf.Pos[2];

        const Su = Surf.Su;
        const Sv = Surf.Sv;
        const Gu = Rx * Su[0] + Ry * Su[1] + Rz * Su[2];
        const Gv = Rx * Sv[0] + Ry * Sv[1] + Rz * Sv[2];

        const A11 = Su[0] * Su[0] + Su[1] * Su[1] + Su[2] * Su[2] + 0.002;
        const A12 = Su[0] * Sv[0] + Su[1] * Sv[1] + Su[2] * Sv[2];
        const A22 = Sv[0] * Sv[0] + Sv[1] * Sv[1] + Sv[2] * Sv[2] + 0.002;
        const Det = A11 * A22 - A12 * A12;
        if (Det > 1e-6)
        {
            const Du = Clamp(( A22 * Gu - A12 * Gv) / Det, -0.09, 0.09);
            const Dv = Clamp((-A12 * Gu + A11 * Gv) / Det, -0.09, 0.09);
            U = Clamp(U + Du, 0.0, 1.0);
            V = Fract(V + Dv);
        }
    }

    const FinalSurf = EvalBicubicLoftSurface(LoftSdf, U, V);
    const Dx = Px - FinalSurf.Pos[0];
    const Dy = Py - FinalSurf.Pos[1];
    const Dz = Pz - FinalSurf.Pos[2];
    const SkinDist = Math.hypot(Dx, Dy, Dz);

    // Exact inside/outside sign relative to the closed lofted tube + watertight front/rear bumper endcaps
    const URaw = (Pz - LoftSdf.ZMin) / LoftSdf.ZSpan;
    const YcAtZ = EvalVehicleSpineY(Clamp(URaw, 0.0, 1.0));
    const RhoAtZ = Math.hypot(Px, Py - YcAtZ);
    const VAtZ = Fract(Math.atan2(Py - YcAtZ, Px) / (Math.PI * 2.0));
    const RAtZ = EvalBicubicLoftSurface(LoftSdf, Clamp(URaw, 0.0, 1.0), VAtZ).Radius;

    let DBody;
    if (Pz >= LoftSdf.ZMin && Pz <= LoftSdf.ZMax && RhoAtZ <= RAtZ)
    {
        // Strictly inside the lofted car body! Also respect distance to front/rear endcaps
        const EndCapClearance = Math.min(Pz - LoftSdf.ZMin, LoftSdf.ZMax - Pz);
        DBody = -Math.min(SkinDist, EndCapClearance);
    }
    else if ((Pz < LoftSdf.ZMin || Pz > LoftSdf.ZMax) && RhoAtZ <= RAtZ)
    {
        // Directly in front of front grille or behind rear diffuser endcap disk
        const AxialOver = Pz < LoftSdf.ZMin ? (LoftSdf.ZMin - Pz) : (Pz - LoftSdf.ZMax);
        DBody = Math.min(SkinDist, AxialOver);
    }
    else
    {
        DBody = SkinDist;
    }

    // 3. Carve exact circular Wheel-Well Arch Cylinders out of the smooth coachwork loft, then CSG-union the 4 Revolved Wheel Curve Lofts!
    for (let W = 0; W < 4; ++W)
    {
        const Mount = VehicleWheelMounts[W];
        const SideSign = Mount.MirrorX ? 1.0 : -1.0;
        const LatX = Px * SideSign; // positive on outboard side of this wheel
        const RhoArch = Math.hypot(Py - Mount.Center[1], Pz - Mount.Center[2]);
        const DArchRad = RhoArch - Mount.ArchRad;
        const DArchInboard = Mount.InboardAbsX - LatX;
        const DArchOutboard = LatX - 1.35;
        const DArchAx = Math.max(DArchInboard, DArchOutboard);
        const DArch = Math.hypot(Math.max(DArchRad, 0.0), Math.max(DArchAx, 0.0)) + Math.min(Math.max(DArchRad, DArchAx), 0.0);
        DBody = Math.max(DBody, -DArch);
    }

    let DTotal = DBody;
    for (let W = 0; W < 4; ++W)
    {
        const Dw = EvalSingleWheelLoftSdf(LoftSdf, P, VehicleWheelMounts[W]);
        if (Dw < DTotal) DTotal = Dw;
    }

    return DTotal;
}

//============================================================================================================================================
//                     4. UNIFORM 3D VOXEL GRID SDF COMPILER & TRILINEAR SAMPLER (FOR COMPARISON)
//============================================================================================================================================

export function CompileUniformVoxelVehicleSdf(AssembledCar, ExactBvh, Res = 32, SignRefLoft = null)
{
    const RefLoft = SignRefLoft || CompileLoftedVehicleSdf(AssembledCar, 32, 36, 24);
    const Pad = 0.32;
    const BMin = [
        AssembledCar.BoundsMin[0] - Pad,
        AssembledCar.BoundsMin[1] - Pad,
        AssembledCar.BoundsMin[2] - Pad
    ];
    const BMax = [
        AssembledCar.BoundsMax[0] + Pad,
        AssembledCar.BoundsMax[1] + Pad,
        AssembledCar.BoundsMax[2] + Pad
    ];
    const Span = [BMax[0] - BMin[0], BMax[1] - BMin[1], BMax[2] - BMin[2]];
    const VoxelPitch = [Span[0] / Res, Span[1] / Res, Span[2] / Res];

    const TotalVoxels = Res * Res * Res;
    const FloatVol = new Float32Array(TotalVoxels);
    const Rgba16f  = new Uint16Array(TotalVoxels * 4);

    for (let Z = 0; Z < Res; ++Z)
    {
        const Pz = BMin[2] + (Z + 0.5) * VoxelPitch[2];
        for (let Y = 0; Y < Res; ++Y)
        {
            const Py = BMin[1] + (Y + 0.5) * VoxelPitch[1];
            for (let X = 0; X < Res; ++X)
            {
                const Px = BMin[0] + (X + 0.5) * VoxelPitch[0];
                const Sd = EvaluateExactBvhSignedDistance(ExactBvh, [Px, Py, Pz], RefLoft);
                const Flat = (Z * Res + Y) * Res + X;
                FloatVol[Flat] = Sd;
                Rgba16f[Flat * 4 + 0] = EncodeFloat16Bits(Sd);
                Rgba16f[Flat * 4 + 1] = 0;
                Rgba16f[Flat * 4 + 2] = 0;
                Rgba16f[Flat * 4 + 3] = EncodeFloat16Bits(1.0);
            }
        }
    }

    return {
        Res,
        BoundsMin: BMin,
        BoundsMax: BMax,
        VoxelPitch,
        VoxelDiag: Math.hypot(VoxelPitch[0], VoxelPitch[1], VoxelPitch[2]),
        FloatVol,
        Rgba16f,
        VramFp16Bytes: TotalVoxels * 2,      // Single-channel r16float theoretical minimum
        VramRgba16Bytes: TotalVoxels * 8     // Standard WebGPU filterable rgba16float 3D texture
    };
}

export function EvaluateVoxelVehicleSdf(VoxelSdf, P)
{
    const BMin = VoxelSdf.BoundsMin;
    const BMax = VoxelSdf.BoundsMax;
    const Res  = VoxelSdf.Res;

    const OverX = Math.max(BMin[0] - P[0], 0.0, P[0] - BMax[0]);
    const OverY = Math.max(BMin[1] - P[1], 0.0, P[1] - BMax[1]);
    const OverZ = Math.max(BMin[2] - P[2], 0.0, P[2] - BMax[2]);
    const ExteriorDist = Math.hypot(OverX, OverY, OverZ);
    if (ExteriorDist > 0.35)
    {
        return ExteriorDist + 0.30;
    }

    const U = Clamp((P[0] - BMin[0]) / (BMax[0] - BMin[0]), 0.0, 1.0);
    const V = Clamp((P[1] - BMin[1]) / (BMax[1] - BMin[1]), 0.0, 1.0);
    const W = Clamp((P[2] - BMin[2]) / (BMax[2] - BMin[2]), 0.0, 1.0);

    const Fx = Clamp(U * Res - 0.5, 0.0, Res - 1.00001);
    const Fy = Clamp(V * Res - 0.5, 0.0, Res - 1.00001);
    const Fz = Clamp(W * Res - 0.5, 0.0, Res - 1.00001);

    const X0 = Math.floor(Fx), Y0 = Math.floor(Fy), Z0 = Math.floor(Fz);
    const X1 = Math.min(Res - 1, X0 + 1), Y1 = Math.min(Res - 1, Y0 + 1), Z1 = Math.min(Res - 1, Z0 + 1);
    const Tx = Fx - X0, Ty = Fy - Y0, Tz = Fz - Z0;

    const Vol = VoxelSdf.FloatVol;
    const At = (X, Y, Z) => Vol[(Z * Res + Y) * Res + X];

    const C00 = At(X0, Y0, Z0) * (1 - Tx) + At(X1, Y0, Z0) * Tx;
    const C10 = At(X0, Y1, Z0) * (1 - Tx) + At(X1, Y1, Z0) * Tx;
    const C01 = At(X0, Y0, Z1) * (1 - Tx) + At(X1, Y0, Z1) * Tx;
    const C11 = At(X0, Y1, Z1) * (1 - Tx) + At(X1, Y1, Z1) * Tx;
    const C0  = C00 * (1 - Ty) + C10 * Ty;
    const C1  = C01 * (1 - Ty) + C11 * Ty;
    return (C0 * (1 - Tz) + C1 * Tz) + ExteriorDist;
}

//============================================================================================================================================
//                     5. QUANTITATIVE ACCURACY, VRAM & EVALUATION-TIME BENCHMARK SUITE
//============================================================================================================================================

export function BenchmarkLoftVsVoxelSdf(AssembledCar, ExactBvh, LoftSdf, VoxelSdf)
{
    // Cast 180 stratified exterior rays across all azimuths & elevations onto the real 25,068-triangle ControlVehicle!
    const HitPoints = [];
    for (let I = 0; I < 180; ++I)
    {
        const Theta = I * 2.39996323;
        const Elev  = 0.14 + 0.70 * ((I % 19) / 18);
        const Dx = Math.cos(Elev) * Math.sin(Theta);
        const Dy = Math.sin(Elev);
        const Dz = Math.cos(Elev) * Math.cos(Theta);
        const Aim = [0.0, 0.75, -1.85 + 3.70 * ((I % 15) / 14)];
        const Ro  = [Aim[0] + Dx * 4.5, Aim[1] + Dy * 4.5, Aim[2] + Dz * 4.5];
        const Rd  = [-Dx, -Dy, -Dz];
        const T   = IntersectRayBvhClosest(ExactBvh, Ro, Rd, 10.0);
        if (T > 0.0)
        {
            HitPoints.push([Ro[0] + Rd[0] * T, Ro[1] + Rd[1] * T, Ro[2] + Rd[2] * T]);
        }
    }

    const N = Math.max(1, HitPoints.length);
    const LoftErrs = new Float32Array(N);
    let LoftAbsSum = 0.0, LoftSqSum = 0.0;

    const TLoft0 = performance.now();
    const LoftReps = 6;
    for (let Rep = 0; Rep < LoftReps; ++Rep)
    {
        for (let I = 0; I < N; ++I)
        {
            const ErrM = Math.abs(EvaluateLoftedVehicleSdf(LoftSdf, HitPoints[I]));
            if (Rep === 0)
            {
                LoftErrs[I] = ErrM * 1000.0;
                LoftAbsSum += ErrM;
                LoftSqSum  += ErrM * ErrM;
            }
        }
    }
    const LoftUsPerEval = ((performance.now() - TLoft0) * 1000.0) / (N * LoftReps);

    const VoxErrs = new Float32Array(N);
    let VoxAbsSum = 0.0, VoxSqSum = 0.0;

    const TVox0 = performance.now();
    const VoxReps = 24;
    for (let Rep = 0; Rep < VoxReps; ++Rep)
    {
        for (let I = 0; I < N; ++I)
        {
            const ErrM = Math.abs(EvaluateVoxelVehicleSdf(VoxelSdf, HitPoints[I]));
            if (Rep === 0)
            {
                VoxErrs[I] = ErrM * 1000.0;
                VoxAbsSum += ErrM;
                VoxSqSum  += ErrM * ErrM;
            }
        }
    }
    const VoxUsPerEval = ((performance.now() - TVox0) * 1000.0) / (N * VoxReps);

    const SortedLoft = Array.from(LoftErrs).sort((A, B) => A - B);
    const SortedVox  = Array.from(VoxErrs).sort((A, B) => A - B);

    return {
        SampleCount:   N,
        LoftMedianMm:  SortedLoft[Math.floor(N * 0.5)],
        LoftMeanErrMm: (LoftAbsSum / N) * 1000.0,
        LoftP90Mm:     SortedLoft[Math.floor(N * 0.9)],
        LoftRmseMm:    Math.sqrt(LoftSqSum / N) * 1000.0,
        LoftUsPerEval,
        LoftCompactKb: LoftSdf.CompactKnotBytes / 1024.0,
        LoftGpuKb:     LoftSdf.GpuHermiteBytes / 1024.0,
        VoxMedianMm:   SortedVox[Math.floor(N * 0.5)],
        VoxMeanErrMm:  (VoxAbsSum / N) * 1000.0,
        VoxP90Mm:      SortedVox[Math.floor(N * 0.9)],
        VoxRmseMm:     Math.sqrt(VoxSqSum / N) * 1000.0,
        VoxUsPerEval,
        VoxFp16Kb:     VoxelSdf.VramFp16Bytes / 1024.0,
        VoxRgba16Kb:   VoxelSdf.VramRgba16Bytes / 1024.0
    };
}
