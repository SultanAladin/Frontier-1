/*============================================================================================================================================
                                                         VEHICLEFACEDECODER.JS
============================================================================================================================================*/
// FCT1 binary stream decoder assembling the 25,068-triangle Controlled Integrator vehicle (Body + 4 Rims + 4 Tyres) in FrontierWorldZUp

import {
    ConvertWorldZUpToYUp,
    DegToRad,
    EvaluateCross,
    NormalizeAxis,
    RotateYUpVector,
} from "./CoordinateConversion.js";

const VehicleHeightRebase  = 0.11285;
const VehicleGroundLift    = 0.53000;
const RimWheelScale        = 0.34460 / 0.344818;
const BodyCreaseCosine     = Math.cos(38.0 * DegToRad);
const CornerNormalsPresent = 1;
const HeaderByteLength     = 48;

const CanonicalAxleMounts = [
    { Label: "FL", Mount: [ 1.7274,  1.0475, 0.0914], MirrorLateral: false },
    { Label: "FR", Mount: [ 1.7274, -1.0475, 0.0914], MirrorLateral: true  },
    { Label: "RL", Mount: [-1.6686,  1.0475, 0.0914], MirrorLateral: false },
    { Label: "RR", Mount: [-1.6686, -1.0475, 0.0914], MirrorLateral: true  },
];

export const VehiclePartCategory = Object.freeze({
    BodyCoat:       0,
    WheelCapAccent: 1,
    RimAlloy:       2,
    CanopyGloss:    3,
    VehicleGlass:   4,
    VehiclePlastic: 5,
    TyreRubber:     6,
    BrakeDisc:      7,
    VehicleTrim:    8,
    HeadlightLens:  9,
    TaillightLens:  10,
});

function ResolveBodySurfaceCategory(PartCode)
{
    switch (PartCode)
    {
        case 6:  return VehiclePartCategory.BodyCoat;
        case 7:  return VehiclePartCategory.VehicleGlass;
        case 8:  return VehiclePartCategory.VehiclePlastic;
        case 9:  return VehiclePartCategory.CanopyGloss;
        case 10: return VehiclePartCategory.WheelCapAccent;
        case 11: return VehiclePartCategory.BrakeDisc;
        case 12: return VehiclePartCategory.VehicleTrim;
        case 13: return VehiclePartCategory.HeadlightLens;
        case 14: return VehiclePartCategory.TaillightLens;
        default: return VehiclePartCategory.VehicleTrim;
    }
}

export function DecodeFaceStreamBytes(PayloadBuffer)
{
    const Bytes = PayloadBuffer instanceof ArrayBuffer
        ? PayloadBuffer
        : PayloadBuffer.buffer.slice(PayloadBuffer.byteOffset, PayloadBuffer.byteOffset + PayloadBuffer.byteLength);
    if (Bytes.byteLength < HeaderByteLength)
    {
        throw new Error("FCT1 stream shorter than 48-byte header");
    }
    const View  = new DataView(Bytes);
    const Magic = String.fromCharCode(View.getUint8(0), View.getUint8(1), View.getUint8(2), View.getUint8(3));
    if (Magic !== "FCT1")
    {
        throw new Error(`Invalid face stream magic '${Magic}'`);
    }

    const VertexCount = View.getUint32(4, true);
    const FaceCount   = View.getUint32(8, true);
    const HeaderBits  = View.getUint32(12, true);

    let Offset = HeaderByteLength;
    const Positions = new Float32Array(Bytes, Offset, VertexCount * 3).slice();
    Offset += VertexCount * 12;
    const Indices = new Uint32Array(Bytes, Offset, FaceCount * 3).slice();
    Offset += FaceCount * 12;
    const PartCodes = new Uint32Array(Bytes, Offset, FaceCount).slice();
    Offset += FaceCount * 4;

    let CornerNormals = null;
    if ((HeaderBits & CornerNormalsPresent) !== 0)
    {
        CornerNormals = new Float32Array(Bytes, Offset, FaceCount * 9).slice();
        Offset += FaceCount * 36;
    }
    else
    {
        CornerNormals = DeriveCreasedCornerNormals(VertexCount, FaceCount, Positions, Indices);
    }

    return { VertexCount, FaceCount, Positions, Indices, PartCodes, CornerNormals };
}

function DeriveCreasedCornerNormals(VertexCount, FaceCount, Positions, Indices)
{
    const FaceNormals = new Float32Array(FaceCount * 3);
    const Incidence   = Array.from({ length: VertexCount }, () => []);

    for (let F = 0; F < FaceCount; ++F)
    {
        const A = Indices[F * 3] * 3;
        const B = Indices[F * 3 + 1] * 3;
        const C = Indices[F * 3 + 2] * 3;
        const E1x = Positions[B] - Positions[A], E1y = Positions[B + 1] - Positions[A + 1], E1z = Positions[B + 2] - Positions[A + 2];
        const E2x = Positions[C] - Positions[A], E2y = Positions[C + 1] - Positions[A + 1], E2z = Positions[C + 2] - Positions[A + 2];
        FaceNormals[F * 3]     = E1y * E2z - E1z * E2y;
        FaceNormals[F * 3 + 1] = E1z * E2x - E1x * E2z;
        FaceNormals[F * 3 + 2] = E1x * E2y - E1y * E2x;
        Incidence[Indices[F * 3]].push(F);
        Incidence[Indices[F * 3 + 1]].push(F);
        Incidence[Indices[F * 3 + 2]].push(F);
    }

    const CornerNormals = new Float32Array(FaceCount * 9);
    for (let F = 0; F < FaceCount; ++F)
    {
        const Fx = FaceNormals[F * 3], Fy = FaceNormals[F * 3 + 1], Fz = FaceNormals[F * 3 + 2];
        const FLen = Math.hypot(Fx, Fy, Fz) || 1.0;
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            let Sx = 0.0, Sy = 0.0, Sz = 0.0;
            for (const Adj of Incidence[Indices[F * 3 + Corner]])
            {
                const Ax = FaceNormals[Adj * 3], Ay = FaceNormals[Adj * 3 + 1], Az = FaceNormals[Adj * 3 + 2];
                const ALen = Math.hypot(Ax, Ay, Az) || 1.0;
                if ((Ax * Fx + Ay * Fy + Az * Fz) / (ALen * FLen) < BodyCreaseCosine) continue;
                Sx += Ax; Sy += Ay; Sz += Az;
            }
            const SLen = Math.hypot(Sx, Sy, Sz) || 1.0;
            CornerNormals[F * 9 + Corner * 3]     = Sx / SLen;
            CornerNormals[F * 9 + Corner * 3 + 1] = Sy / SLen;
            CornerNormals[F * 9 + Corner * 3 + 2] = Sz / SLen;
        }
    }
    return CornerNormals;
}

export function DecodeControlVehicleTemplate(BodyBuffer, RimBuffer, TyreBuffer)
{
    const Body = DecodeFaceStreamBytes(BodyBuffer);
    const Rim  = DecodeFaceStreamBytes(RimBuffer);
    const Tyre = DecodeFaceStreamBytes(TyreBuffer);

    const TotalTriangles = Body.FaceCount + 4 * (Rim.FaceCount + Tyre.FaceCount);
    const Positions      = new Float32Array(TotalTriangles * 9);
    const Normals        = new Float32Array(TotalTriangles * 9);
    const PartCategories = new Uint8Array(TotalTriangles);
    let   Cursor         = 0;

    const AppendPart = (Stream, Mount, MirrorLateral, Scale, CategoryFn) =>
    {
        const Order = MirrorLateral ? [0, 2, 1] : [0, 1, 2];
        const Sign  = MirrorLateral ? -1.0 : 1.0;
        for (let F = 0; F < Stream.FaceCount; ++F)
        {
            const PBase = Cursor * 9;
            for (let K = 0; K < 3; ++K)
            {
                const C    = Order[K];
                const VIdx = Stream.Indices[F * 3 + C] * 3;
                const Px   = Mount[0] + Stream.Positions[VIdx] * Scale;
                const Py   = Mount[1] + Stream.Positions[VIdx + 1] * Scale * Sign;
                const Pz   = Mount[2] + Stream.Positions[VIdx + 2] * Scale - VehicleHeightRebase + VehicleGroundLift;

                Positions[PBase + K * 3]     = Px;
                Positions[PBase + K * 3 + 1] = Py;
                Positions[PBase + K * 3 + 2] = Pz;

                const NIdx = F * 9 + C * 3;
                const Nrm  = NormalizeAxis([
                    Stream.CornerNormals[NIdx],
                    Stream.CornerNormals[NIdx + 1] * Sign,
                    Stream.CornerNormals[NIdx + 2],
                ], [0.0, 0.0, 1.0]);
                Normals[PBase + K * 3]     = Nrm[0];
                Normals[PBase + K * 3 + 1] = Nrm[1];
                Normals[PBase + K * 3 + 2] = Nrm[2];
            }
            PartCategories[Cursor] = CategoryFn(Stream.PartCodes[F]);
            ++Cursor;
        }
    };

    AppendPart(Body, [0.0, 0.0, 0.0], false, 1.0, ResolveBodySurfaceCategory);
    for (const Axle of CanonicalAxleMounts)
    {
        AppendPart(Tyre, Axle.Mount, Axle.MirrorLateral, 1.0, () => VehiclePartCategory.TyreRubber);
        AppendPart(Rim, Axle.Mount, Axle.MirrorLateral, RimWheelScale, (Code) =>
            Code === 10 ? VehiclePartCategory.WheelCapAccent : VehiclePartCategory.RimAlloy);
    }

    return {
        TriangleCount: Cursor,
        Positions,
        Normals,
        PartCategories,
    };
}

export async function LoadControlVehicleTemplateFromUrls(BasePath = "./Resources/ControlVehicle/")
{
    const Names = ["ControlVehicleBody.face", "ControlVehicleRim.face", "ControlVehicleTyre.face"];
    const Buffers = [];
    for (const Name of Names)
    {
        const Resp = await fetch(`${BasePath}${Name}`);
        if (!Resp.ok) throw new Error(`Failed to fetch ${BasePath}${Name} (${Resp.status})`);
        Buffers.push(await Resp.arrayBuffer());
    }
    return DecodeControlVehicleTemplate(Buffers[0], Buffers[1], Buffers[2]);
}

export function InstantiateVehicleSurface(Template, MaterialSlots, BodyPaintSlot, BrakeCaliperSlot, BakeTransform, WorldTranslation)
{
    const TriCount  = Template.TriangleCount;
    const Positions = new Float32Array(TriCount * 9);
    const Normals   = new Float32Array(TriCount * 9);
    const Slots     = new Uint16Array(TriCount);

    const Scale    = BakeTransform?.UniformScale ?? 1.0;
    const PitchRad = (BakeTransform?.PitchDegrees ?? 0.0) * DegToRad;
    const YawRad   = (BakeTransform?.YawDegrees ?? 0.0) * DegToRad;
    const RollRad  = (BakeTransform?.RollDegrees ?? 0.0) * DegToRad;

    let MinZ = Infinity;
    for (let T = 0; T < TriCount; ++T)
    {
        const Base = T * 9;
        for (let C = 0; C < 3; ++C)
        {
            const O = Base + C * 3;
            const ZupScaled = [
                Template.Positions[O] * Scale,
                Template.Positions[O + 1] * Scale,
                Template.Positions[O + 2] * Scale,
            ];
            const Yup    = ConvertWorldZUpToYUp(ZupScaled);
            const RotYup = RotateYUpVector(Yup, PitchRad, YawRad, RollRad);
            const Wx     = RotYup[0];
            const Wy     = -RotYup[2];
            const Wz     = RotYup[1];
            Positions[O]     = Wx;
            Positions[O + 1] = Wy;
            Positions[O + 2] = Wz;
            if (Wz < MinZ) MinZ = Wz;

            const NYup    = ConvertWorldZUpToYUp([Template.Normals[O], Template.Normals[O + 1], Template.Normals[O + 2]]);
            const NRotYup = RotateYUpVector(NYup, PitchRad, YawRad, RollRad);
            const WNorm    = NormalizeAxis([NRotYup[0], -NRotYup[2], NRotYup[1]], [0.0, 0.0, 1.0]);
            Normals[O]     = WNorm[0];
            Normals[O + 1] = WNorm[1];
            Normals[O + 2] = WNorm[2];
        }

        const Cat = Template.PartCategories[T];
        switch (Cat)
        {
            case VehiclePartCategory.BodyCoat:       Slots[T] = BodyPaintSlot; break;
            case VehiclePartCategory.WheelCapAccent: Slots[T] = BrakeCaliperSlot || MaterialSlots.VehicleBrakeCaliper; break;
            case VehiclePartCategory.RimAlloy:       Slots[T] = MaterialSlots.VehicleRimAlloy; break;
            case VehiclePartCategory.CanopyGloss:    Slots[T] = MaterialSlots.VehicleGlossCanopy; break;
            case VehiclePartCategory.VehicleGlass:   Slots[T] = MaterialSlots.VehicleGlass; break;
            case VehiclePartCategory.VehiclePlastic: Slots[T] = MaterialSlots.VehicleIntakePolymer; break;
            case VehiclePartCategory.TyreRubber:     Slots[T] = MaterialSlots.VehicleTireRubber; break;
            case VehiclePartCategory.BrakeDisc:
            case VehiclePartCategory.VehicleTrim:    Slots[T] = MaterialSlots.VehicleTrimMetal; break;
            case VehiclePartCategory.HeadlightLens:  Slots[T] = MaterialSlots.VehicleHeadlightOptic; break;
            case VehiclePartCategory.TaillightLens:  Slots[T] = MaterialSlots.VehicleTaillightOptic; break;
            default:                                 Slots[T] = BodyPaintSlot; break;
        }
    }

    const RebaseZ = Number.isFinite(MinZ) && Math.abs(MinZ) > 1e-5 ? MinZ : 0.0;
    const Tx = WorldTranslation[0], Ty = WorldTranslation[1], Tz = WorldTranslation[2] - RebaseZ;
    for (let I = 0; I < TriCount * 3; ++I)
    {
        Positions[I * 3]     += Tx;
        Positions[I * 3 + 1] += Ty;
        Positions[I * 3 + 2] += Tz;
    }

    // Double-check geometric winding matches normal hemisphere
    for (let T = 0; T < TriCount; ++T)
    {
        const B = T * 9;
        const E1 = [Positions[B + 3] - Positions[B], Positions[B + 4] - Positions[B + 1], Positions[B + 5] - Positions[B + 2]];
        const E2 = [Positions[B + 6] - Positions[B], Positions[B + 7] - Positions[B + 1], Positions[B + 8] - Positions[B + 2]];
        const Cross = EvaluateCross(E1, E2);
        const AvgN = [Normals[B] + Normals[B + 3] + Normals[B + 6], Normals[B + 1] + Normals[B + 4] + Normals[B + 7], Normals[B + 2] + Normals[B + 5] + Normals[B + 8]];
        if (Cross[0] * AvgN[0] + Cross[1] * AvgN[1] + Cross[2] * AvgN[2] < 0.0)
        {
            // Swap corner 1 and corner 2 to guarantee outward CCW winding
            for (let K = 0; K < 3; ++K)
            {
                const TmpP = Positions[B + 3 + K]; Positions[B + 3 + K] = Positions[B + 6 + K]; Positions[B + 6 + K] = TmpP;
                const TmpN = Normals[B + 3 + K];   Normals[B + 3 + K]   = Normals[B + 6 + K];   Normals[B + 6 + K]   = TmpN;
            }
        }
    }

    return { TriangleCount: TriCount, Positions, Normals, Slots };
}
