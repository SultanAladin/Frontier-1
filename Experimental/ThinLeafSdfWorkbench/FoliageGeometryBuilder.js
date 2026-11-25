// Experimental/ThinLeafSdfWorkbench/FoliageGeometryBuilder.js
// 🧩 Builds interleaved 3D triangle & wireframe geometry buffers for WebGPU (and fallback WebGL2):
//    - Sunlit Limestone Courtyard + Vertical Stucco Shadow-Catcher Wall + Alabaster Catcher Plinths
//    - Smooth Tapered Wooden Branch Capsules (trunk, boughs, twigs)
//    - Curved 3D Botanical Leaf Blades (parabolic camber y = cx*u^2 + cz*w^2, alpha-cut by the exact 64x64 2D Leaf SDF!)
//    - 3D Visual Debug Overlays: Occupied 16^3 Canopy Brick AABBs, 5-Archetype Leaf OBBs, and Live Shadow-Ray March Steps

import {
    CanopyBrickRes,
    Sample3dVolumeTrilinear,
    MacroVoxelRes,
    DefaultBarkNoiseAmp,
    DefaultBarkNoiseFreq,
    BarkDispMaxFrac,
    EvalBarkFbm3d,
    EvalRayCapsuleShadow,
    EvalThinLeafRaySegmentShadow
} from "./ThinLeafSdfCompiler.js";

// Interleaved Surface Vertex Layout (16 floats = 64 bytes = 4 x vec4<f32>, 16-byte aligned for WebGPU):
//   @location(0) PosMat:   vec4<f32>  (Position.xyz, MaterialTag: 0 = Stone/Wall, 1 = Bark Wood, 2 = Thin Leaf Blade)
//   @location(1) NormArch: vec4<f32>  (Normal.xyz, ArchetypeId: 0..4 for leaves, -1 for receivers/bark)
//   @location(2) AlbedoAo: vec4<f32>  (Albedo.rgb, AmbientOcc)
//   @location(3) LeafUv:   vec4<f32>  (LocalU, LocalW, Scale, CamberMag)
export const SurfaceVertexFloats = 16;
export const SurfaceVertexBytes  = 64;

// Debug Line Vertex Layout: 8 floats = 32 bytes = 2 x vec4<f32>:
//   @location(0) PosTag: vec4<f32> (Position.xyz, LayerTag: 0 = CanopyBrick, 1 = LeafSheetOutline, 2 = RayMarchStep)
//   @location(1) Color:  vec4<f32> (RGBA)
export const DebugLineVertexFloats = 8;
export const DebugLineVertexBytes  = 32;

const ArchetypeBotanicalAlbedo = [
    [0.24, 0.50, 0.20], // 0 · Ovate Beech (rich emerald woodland green)
    [0.31, 0.56, 0.21], // 1 · Heart Linden (sunlit lime-chartreuse green)
    [0.20, 0.43, 0.18], // 2 · Lobed Oak (deep temperate forest green)
    [0.58, 0.42, 0.16], // 3 · 5-Point Maple (warm golden-amber botanical accent)
    [0.29, 0.51, 0.34]  // 4 · Slender Willow (cool silvery-sage green)
];

const ArchetypeDebugColors = [
    [0.26, 0.86, 0.48, 0.88], // 0 · Beech
    [0.64, 0.90, 0.28, 0.88], // 1 · Linden
    [0.24, 0.75, 0.96, 0.88], // 2 · Oak
    [0.98, 0.68, 0.24, 0.92], // 3 · Maple
    [0.78, 0.52, 0.98, 0.88]  // 4 · Willow
];

function Normalize3(V)
{
    const L = Math.hypot(V[0], V[1], V[2]) || 1.0;
    return [V[0] / L, V[1] / L, V[2] / L];
}

function Cross3(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}

class GeometryAccumulator
{
    constructor()
    {
        this.Verts = [];
        this.Indices = [];
        this.VertexCount = 0;
    }

    PushVertex(Px, Py, Pz, MatTag, Nx, Ny, Nz, ArchId, Cr, Cg, Cb, Ao, U = 0.0, W = 0.0, Scale = 1.0, Camber = 0.0)
    {
        this.Verts.push(
            Px, Py, Pz, MatTag,
            Nx, Ny, Nz, ArchId,
            Cr, Cg, Cb, Ao,
            U,  W,  Scale, Camber
        );
        return this.VertexCount++;
    }

    PushTri(I0, I1, I2)
    {
        this.Indices.push(I0, I1, I2);
    }

    PushQuad(I0, I1, I2, I3)
    {
        this.Indices.push(I0, I1, I2, I0, I2, I3);
    }

    Finalize()
    {
        return {
            VerticesF32: new Float32Array(this.Verts),
            IndicesU32:  new Uint32Array(this.Indices),
            IndexCount:  this.Indices.length,
            VertexCount: this.VertexCount
        };
    }
}

function AppendAxisBox(Acc, Min, Max, Albedo, AoBottom = 0.78, AoTop = 1.0)
{
    const [X0, Y0, Z0] = Min;
    const [X1, Y1, Z1] = Max;
    const [Cr, Cg, Cb] = Albedo;

    // +Y top
    let V0 = Acc.PushVertex(X0, Y1, Z1, 0, 0, 1, 0, -1, Cr, Cg, Cb, AoTop);
    let V1 = Acc.PushVertex(X1, Y1, Z1, 0, 0, 1, 0, -1, Cr, Cg, Cb, AoTop);
    let V2 = Acc.PushVertex(X1, Y1, Z0, 0, 0, 1, 0, -1, Cr, Cg, Cb, AoTop);
    let V3 = Acc.PushVertex(X0, Y1, Z0, 0, 0, 1, 0, -1, Cr, Cg, Cb, AoTop);
    Acc.PushQuad(V0, V1, V2, V3);

    // +Z front
    V0 = Acc.PushVertex(X0, Y0, Z1, 0, 0, 0, 1, -1, Cr, Cg, Cb, AoBottom);
    V1 = Acc.PushVertex(X1, Y0, Z1, 0, 0, 0, 1, -1, Cr, Cg, Cb, AoBottom);
    V2 = Acc.PushVertex(X1, Y1, Z1, 0, 0, 0, 1, -1, Cr, Cg, Cb, AoTop);
    V3 = Acc.PushVertex(X0, Y1, Z1, 0, 0, 0, 1, -1, Cr, Cg, Cb, AoTop);
    Acc.PushQuad(V0, V1, V2, V3);

    // -Z back
    V0 = Acc.PushVertex(X1, Y0, Z0, 0, 0, 0, -1, -1, Cr, Cg, Cb, AoBottom);
    V1 = Acc.PushVertex(X0, Y0, Z0, 0, 0, 0, -1, -1, Cr, Cg, Cb, AoBottom);
    V2 = Acc.PushVertex(X0, Y1, Z0, 0, 0, 0, -1, -1, Cr, Cg, Cb, AoTop);
    V3 = Acc.PushVertex(X1, Y1, Z0, 0, 0, 0, -1, -1, Cr, Cg, Cb, AoTop);
    Acc.PushQuad(V0, V1, V2, V3);

    // +X right
    V0 = Acc.PushVertex(X1, Y0, Z1, 0, 1, 0, 0, -1, Cr, Cg, Cb, AoBottom);
    V1 = Acc.PushVertex(X1, Y0, Z0, 0, 1, 0, 0, -1, Cr, Cg, Cb, AoBottom);
    V2 = Acc.PushVertex(X1, Y1, Z0, 0, 1, 0, 0, -1, Cr, Cg, Cb, AoTop);
    V3 = Acc.PushVertex(X1, Y1, Z1, 0, 1, 0, 0, -1, Cr, Cg, Cb, AoTop);
    Acc.PushQuad(V0, V1, V2, V3);

    // -X left
    V0 = Acc.PushVertex(X0, Y0, Z0, 0, -1, 0, 0, -1, Cr, Cg, Cb, AoBottom);
    V1 = Acc.PushVertex(X0, Y0, Z1, 0, -1, 0, 0, -1, Cr, Cg, Cb, AoBottom);
    V2 = Acc.PushVertex(X0, Y1, Z1, 0, -1, 0, 0, -1, Cr, Cg, Cb, AoTop);
    V3 = Acc.PushVertex(X0, Y1, Z0, 0, -1, 0, 0, -1, Cr, Cg, Cb, AoTop);
    Acc.PushQuad(V0, V1, V2, V3);
}

function AppendReceiverSphere(Acc, Center, Radius, Albedo, Rings = 14, Sectors = 22)
{
    const [Cx, Cy, Cz] = Center;
    const [Cr, Cg, Cb] = Albedo;
    const StartVert = Acc.VertexCount;

    for (let R = 0; R <= Rings; ++R)
    {
        const V = R / Rings;
        const Phi = V * Math.PI;
        const SinP = Math.sin(Phi), CosP = Math.cos(Phi);
        for (let S = 0; S <= Sectors; ++S)
        {
            const U = S / Sectors;
            const Theta = U * Math.PI * 2.0;
            const Nx = SinP * Math.cos(Theta);
            const Ny = CosP;
            const Nz = SinP * Math.sin(Theta);
            const Ao = 0.65 + 0.35 * (0.5 * Ny + 0.5);
            Acc.PushVertex(
                Cx + Radius * Nx, Cy + Radius * Ny, Cz + Radius * Nz, 0,
                Nx, Ny, Nz, -1,
                Cr, Cg, Cb, Ao
            );
        }
    }

    const Stride = Sectors + 1;
    for (let R = 0; R < Rings; ++R)
    {
        for (let S = 0; S < Sectors; ++S)
        {
            const A = StartVert + R * Stride + S;
            const B = A + Stride;
            Acc.PushQuad(A, B, B + 1, A + 1);
        }
    }
}

// Builds a 100% closed, watertight Tapered Capsule (Start Hemisphere Cap + Tangential Tapered Mantle + End Hemisphere Cap)
// displaced radially by 3D Procedural Bark & Burl FBM Noise so adjacent capsule ball-joints weld seamlessly with ZERO gaps!
function AppendWatertightWoodCapsule(Acc, Cap, BarkNoiseAmp, BarkNoiseFreq, RadialSides = 16)
{
    const { P0, R0, P1, R1 } = Cap;
    const Axis = [P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2]];
    const SegLen = Math.hypot(Axis[0], Axis[1], Axis[2]) || 1e-4;
    const Dir = [Axis[0] / SegLen, Axis[1] / SegLen, Axis[2] / SegLen];
    const UpRef = Math.abs(Dir[1]) > 0.92 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0];
    const Right = Normalize3(Cross3(UpRef, Dir));
    const Fwd   = Normalize3(Cross3(Dir, Right));

    // Exact cone taper slope sin(alpha) = (R0 - R1) / SegLen for smooth mantle normals
    const SinCone = Math.max(-0.82, Math.min(0.82, (R0 - R1) / SegLen));
    const CosCone = Math.sqrt(Math.max(0.05, 1.0 - SinCone * SinCone));

    const BarkCrevice = [0.24, 0.16, 0.10];
    const BarkRidge   = [0.46, 0.33, 0.22];

    // Profile stations: 4 start-hemisphere rings + 6 axial mantle rings + 4 end-hemisphere rings = 15 closed rings
    const CapRings    = 4;
    const MantleRings = 6;
    const Stations    = [];

    for (let H = 0; H < CapRings; ++H)
    {
        const Phi = -0.5 * Math.PI * (1.0 - H / CapRings); // -pi/2 (pole) -> < 0
        Stations.push({
            Center: P0,
            RingScale: Math.cos(Phi),
            AxialShift: Math.sin(Phi),
            SphereRadius: R0,
            NormalAxial: Math.sin(Phi),
            NormalRadial: Math.cos(Phi),
            UseHemisphere: true
        });
    }

    for (let K = 0; K <= MantleRings; ++K)
    {
        const T = K / MantleRings;
        Stations.push({
            Center: [P0[0] + Axis[0] * T, P0[1] + Axis[1] * T, P0[2] + Axis[2] * T],
            RingScale: 1.0,
            AxialShift: 0.0,
            SphereRadius: R0 + (R1 - R0) * T,
            NormalAxial: SinCone,
            NormalRadial: CosCone,
            UseHemisphere: false
        });
    }

    for (let H = 1; H <= CapRings; ++H)
    {
        const Phi = 0.5 * Math.PI * (H / CapRings); // > 0 -> +pi/2 (pole)
        Stations.push({
            Center: P1,
            RingScale: Math.cos(Phi),
            AxialShift: Math.sin(Phi),
            SphereRadius: R1,
            NormalAxial: Math.sin(Phi),
            NormalRadial: Math.cos(Phi),
            UseHemisphere: true
        });
    }

    const StartVert = Acc.VertexCount;
    const RingCount = Stations.length;

    for (let RIdx = 0; RIdx < RingCount; ++RIdx)
    {
        const St = Stations[RIdx];
        const SmoothRad = St.SphereRadius;

        for (let S = 0; S <= RadialSides; ++S)
        {
            const Ang = (S / RadialSides) * Math.PI * 2.0;
            const CosA = Math.cos(Ang), SinA = Math.sin(Ang);
            const RadDirX = Right[0] * CosA + Fwd[0] * SinA;
            const RadDirY = Right[1] * CosA + Fwd[1] * SinA;
            const RadDirZ = Right[2] * CosA + Fwd[2] * SinA;

            // Outward unit direction on the smooth sphere cap or mantle cross-section
            const UnitX = St.RingScale * RadDirX + St.AxialShift * Dir[0];
            const UnitY = St.RingScale * RadDirY + St.AxialShift * Dir[1];
            const UnitZ = St.RingScale * RadDirZ + St.AxialShift * Dir[2];

            const SkinX = St.Center[0] + SmoothRad * UnitX;
            const SkinY = St.Center[1] + SmoothRad * UnitY;
            const SkinZ = St.Center[2] + SmoothRad * UnitZ;

            let BarkFbm = 0.0;
            let NoisyRad = SmoothRad;
            let OutNorm = Normalize3([
                St.NormalRadial * RadDirX + St.NormalAxial * Dir[0],
                St.NormalRadial * RadDirY + St.NormalAxial * Dir[1],
                St.NormalRadial * RadDirZ + St.NormalAxial * Dir[2]
            ]);

            if (BarkNoiseAmp > 1e-4)
            {
                BarkFbm  = EvalBarkFbm3d(SkinX, SkinY, SkinZ, BarkNoiseFreq);
                NoisyRad = SmoothRad * (1.0 + BarkDispMaxFrac * BarkNoiseAmp * BarkFbm);

                // Tangent-plane finite difference of 3D bark noise so lighting catches every bark furrow & woody burl!
                const TangCircum = [-Right[0] * SinA + Fwd[0] * CosA, -Right[1] * SinA + Fwd[1] * CosA, -Right[2] * SinA + Fwd[2] * CosA];
                const Eps = Math.max(0.006, SmoothRad * 0.14);
                const BPlusU = EvalBarkFbm3d(SkinX + TangCircum[0] * Eps, SkinY + TangCircum[1] * Eps, SkinZ + TangCircum[2] * Eps, BarkNoiseFreq);
                const BPlusV = EvalBarkFbm3d(SkinX + Dir[0] * Eps,        SkinY + Dir[1] * Eps,        SkinZ + Dir[2] * Eps,        BarkNoiseFreq);
                const SlopeScale = (SmoothRad * BarkDispMaxFrac * BarkNoiseAmp) / Eps;
                const Du = (BPlusU - BarkFbm) * SlopeScale * 0.65;
                const Dv = (BPlusV - BarkFbm) * SlopeScale * 0.65;
                OutNorm = Normalize3([
                    OutNorm[0] - TangCircum[0] * Du - Dir[0] * Dv,
                    OutNorm[1] - TangCircum[1] * Du - Dir[1] * Dv,
                    OutNorm[2] - TangCircum[2] * Du - Dir[2] * Dv
                ]);
            }

            const Px = St.Center[0] + NoisyRad * UnitX;
            const Py = St.Center[1] + NoisyRad * UnitY;
            const Pz = St.Center[2] + NoisyRad * UnitZ;

            const RidgeFactor = Math.max(0.0, Math.min(1.0, 0.52 + 0.48 * BarkFbm));
            const Cr = BarkCrevice[0] + (BarkRidge[0] - BarkCrevice[0]) * RidgeFactor;
            const Cg = BarkCrevice[1] + (BarkRidge[1] - BarkCrevice[1]) * RidgeFactor;
            const Cb = BarkCrevice[2] + (BarkRidge[2] - BarkCrevice[2]) * RidgeFactor;
            const Ao = 0.74 + 0.22 * RidgeFactor;

            Acc.PushVertex(
                Px, Py, Pz, 1,
                OutNorm[0], OutNorm[1], OutNorm[2], -1,
                Cr, Cg, Cb, Ao
            );
        }
    }

    const Stride = RadialSides + 1;
    for (let RIdx = 0; RIdx < RingCount - 1; ++RIdx)
    {
        for (let S = 0; S < RadialSides; ++S)
        {
            const A = StartVert + RIdx * Stride + S;
            const B = A + Stride;
            Acc.PushQuad(A, A + 1, B + 1, B);
        }
    }
}

function AppendCurvedLeafInstance(Acc, Leaf, SubdivU = 6, SubdivW = 8)
{
    const { Center, XAxis, YAxis, ZAxis, Scale, ArchetypeId, CamberX, CamberZ } = Leaf;
    const ChlorophyllTint = ArchetypeBotanicalAlbedo[ArchetypeId % 5];
    const CamberMag = Math.hypot(CamberX, CamberZ);
    const StartVert = Acc.VertexCount;

    for (let Iw = 0; Iw <= SubdivW; ++Iw)
    {
        const W = -1.0 + 2.0 * (Iw / SubdivW);
        for (let Iu = 0; Iu <= SubdivU; ++Iu)
        {
            const U = -1.0 + 2.0 * (Iu / SubdivU);
            const YLocal = CamberX * U * U + CamberZ * W * W;

            // Local surface tangent slopes dy/du = 2*CamberX*U, dy/dw = 2*CamberZ*W -> exact curved leaf normal!
            const SlopeU = -2.0 * CamberX * U;
            const SlopeW = -2.0 * CamberZ * W;
            const NLocal = Normalize3([SlopeU, 1.0, SlopeW]);

            const Nx = XAxis[0] * NLocal[0] + YAxis[0] * NLocal[1] + ZAxis[0] * NLocal[2];
            const Ny = XAxis[1] * NLocal[0] + YAxis[1] * NLocal[1] + ZAxis[1] * NLocal[2];
            const Nz = XAxis[2] * NLocal[0] + YAxis[2] * NLocal[1] + ZAxis[2] * NLocal[2];

            const Px = Center[0] + Scale * (XAxis[0] * U + YAxis[0] * YLocal + ZAxis[0] * W);
            const Py = Center[1] + Scale * (XAxis[1] * U + YAxis[1] * YLocal + ZAxis[1] * W);
            const Pz = Center[2] + Scale * (XAxis[2] * U + YAxis[2] * YLocal + ZAxis[2] * W);

            // Subtle midrib brightening & tip warmth
            const TipFactor = 0.5 + 0.5 * W;
            const Cr = ChlorophyllTint[0] * (0.90 + 0.24 * TipFactor);
            const Cg = ChlorophyllTint[1] * (0.92 + 0.18 * TipFactor);
            const Cb = ChlorophyllTint[2] * (0.94 + 0.12 * TipFactor);

            Acc.PushVertex(
                Px, Py, Pz, 2,
                Nx, Ny, Nz, ArchetypeId,
                Cr, Cg, Cb, 0.95,
                U, W, Scale, CamberMag
            );
        }
    }

    const Stride = SubdivU + 1;
    for (let Iw = 0; Iw < SubdivW; ++Iw)
    {
        for (let Iu = 0; Iu < SubdivU; ++Iu)
        {
            const A = StartVert + Iw * Stride + Iu;
            const B = A + Stride;
            Acc.PushQuad(A, B, B + 1, A + 1);
        }
    }
}

// 📝 Builds the complete 3D scene surface geometry (receivers + bark capsules + curved instanced leaves)
export function BuildFoliageSceneGeometry(SceneSpec)
{
    const Acc = new GeometryAccumulator();
    const PresetId = SceneSpec.PresetId;

    // 1. Warm Limestone Courtyard Floor (large sunlit shadow-catcher plane y = 0)
    const FloorHalfX = 16.0;
    const FloorMinZ  = PresetId === "avenue-lod" ? -21.0 : -12.0;
    const FloorMaxZ  = 11.0;
    const StoneColor = [0.84, 0.81, 0.75];
    const PlinthColor = [0.89, 0.87, 0.83];
    const WallColor  = [0.87, 0.84, 0.78];

    const F0 = Acc.PushVertex(-FloorHalfX, 0.0, FloorMaxZ, 0, 0, 1, 0, -1, StoneColor[0], StoneColor[1], StoneColor[2], 1.0);
    const F1 = Acc.PushVertex( FloorHalfX, 0.0, FloorMaxZ, 0, 0, 1, 0, -1, StoneColor[0], StoneColor[1], StoneColor[2], 1.0);
    const F2 = Acc.PushVertex( FloorHalfX, 0.0, FloorMinZ, 0, 0, 1, 0, -1, StoneColor[0], StoneColor[1], StoneColor[2], 1.0);
    const F3 = Acc.PushVertex(-FloorHalfX, 0.0, FloorMinZ, 0, 0, 1, 0, -1, StoneColor[0], StoneColor[1], StoneColor[2], 1.0);
    Acc.PushQuad(F0, F1, F2, F3);

    // 2. Sunlit Architectural Shadow-Catcher Wall & Curved Alabaster Catcher Spheres
    if (PresetId === "bough-closeup")
    {
        // Low sunlit garden wall & catcher sphere right under the bough's cast shadow!
        AppendAxisBox(Acc, [-2.8, 0.0, -2.45], [2.8, 2.35, -2.25], WallColor, 0.86, 1.0);
        AppendAxisBox(Acc, [-0.55, 0.0, -0.85], [0.15, 0.24, -0.15], PlinthColor, 0.84, 0.98);
        AppendReceiverSphere(Acc, [-0.20, 0.50, -0.50], 0.26, [0.91, 0.89, 0.85]);
    }
    else if (PresetId === "full-arbor")
    {
        AppendAxisBox(Acc, [-4.5, 0.0, -3.65], [4.5, 3.60, -3.40], WallColor, 0.85, 1.0);
        AppendAxisBox(Acc, [-1.45, 0.0, -1.35], [-0.65, 0.32, -0.55], PlinthColor, 0.84, 0.98);
        AppendReceiverSphere(Acc, [-1.05, 0.66, -0.95], 0.34, [0.91, 0.89, 0.85]);
    }
    else
    {
        // Avenue of 3 arbors: long colonnade garden wall along -X to catch near, mid, and far canopy shadows!
        AppendAxisBox(Acc, [-4.6, 0.0, -18.5], [-4.3, 4.2, 4.5], WallColor, 0.85, 1.0);
    }

    // 3. Watertight Tapered Wooden Trunk & Branch Capsules (+ 3D Procedural Bark & Burl FBM Displacement)
    const BarkAmp  = SceneSpec.BarkNoiseAmp  !== undefined ? SceneSpec.BarkNoiseAmp  : DefaultBarkNoiseAmp;
    const BarkFreq = SceneSpec.BarkNoiseFreq !== undefined ? SceneSpec.BarkNoiseFreq : DefaultBarkNoiseFreq;
    for (let C = 0; C < SceneSpec.Capsules.length; ++C)
    {
        AppendWatertightWoodCapsule(Acc, SceneSpec.Capsules[C], BarkAmp, BarkFreq, 16);
    }

    // 4. Instanced Curved Botanical Leaf Blades (alpha-clipped in fragment shader by the exact 64x64 2D Leaf Archetype SDF!)
    for (let L = 0; L < SceneSpec.LeafInstances.length; ++L)
    {
        AppendCurvedLeafInstance(Acc, SceneSpec.LeafInstances[L], 6, 8);
    }

    return Acc.Finalize();
}

// 🔍 Builds 3D Debug Wireframe Line Buffers (Occupied 16^3 Canopy Bricks + 5-Archetype Curved Leaf Outlines + Live Sun Shadow Ray Steps!)
export function BuildDebugWireframeGeometry(CompiledVol, CompiledArchetypes, SunDir, RayOriginGround)
{
    const Verts = [];
    let VertexCount = 0;

    function PushLine(Ax, Ay, Az, Bx, By, Bz, LayerTag, Rgba)
    {
        Verts.push(
            Ax, Ay, Az, LayerTag, Rgba[0], Rgba[1], Rgba[2], Rgba[3],
            Bx, By, Bz, LayerTag, Rgba[0], Rgba[1], Rgba[2], Rgba[3]
        );
        VertexCount += 2;
    }

    function PushWireAabb(Min, Max, LayerTag, Rgba)
    {
        const [X0, Y0, Z0] = Min;
        const [X1, Y1, Z1] = Max;
        // Bottom
        PushLine(X0, Y0, Z0, X1, Y0, Z0, LayerTag, Rgba);
        PushLine(X1, Y0, Z0, X1, Y0, Z1, LayerTag, Rgba);
        PushLine(X1, Y0, Z1, X0, Y0, Z1, LayerTag, Rgba);
        PushLine(X0, Y0, Z1, X0, Y0, Z0, LayerTag, Rgba);
        // Top
        PushLine(X0, Y1, Z0, X1, Y1, Z0, LayerTag, Rgba);
        PushLine(X1, Y1, Z0, X1, Y1, Z1, LayerTag, Rgba);
        PushLine(X1, Y1, Z1, X0, Y1, Z1, LayerTag, Rgba);
        PushLine(X0, Y1, Z1, X0, Y1, Z0, LayerTag, Rgba);
        // Pillars
        PushLine(X0, Y0, Z0, X0, Y1, Z0, LayerTag, Rgba);
        PushLine(X1, Y0, Z0, X1, Y1, Z0, LayerTag, Rgba);
        PushLine(X1, Y0, Z1, X1, Y1, Z1, LayerTag, Rgba);
        PushLine(X0, Y0, Z1, X0, Y1, Z1, LayerTag, Rgba);
    }

    // Layer 0: Occupied 16x16x16 Canopy Bricks (color-graded cyan -> emerald by optical extinction sigma)
    const Bricks = CompiledVol.OccupiedBrickBounds;
    for (let B = 0; B < Bricks.length; ++B)
    {
        const Ob = Bricks[B];
        const Density = Math.min(1.0, Ob.Density * 0.25);
        const Rgba = [0.16 + 0.25 * Density, 0.72 + 0.22 * Density, 0.92 - 0.30 * Density, 0.42];
        PushWireAabb(Ob.Min, Ob.Max, 0.0, Rgba);
    }

    // Layer 1: Instanced Thin-Leaf Curved Sheet Outlines (color-coded by Archetype 0..4)
    const Leaves = CompiledVol.SceneSpec.LeafInstances;
    const Stride = Leaves.length > 350 ? 2 : 1;
    for (let I = 0; I < Leaves.length; I += Stride)
    {
        const L = Leaves[I];
        const Col = ArchetypeDebugColors[L.ArchetypeId % 5];
        const S = L.Scale;

        function LeafPt(U, W)
        {
            const Yc = L.CamberX * U * U + L.CamberZ * W * W;
            return [
                L.Center[0] + S * (L.XAxis[0] * U + L.YAxis[0] * Yc + L.ZAxis[0] * W),
                L.Center[1] + S * (L.XAxis[1] * U + L.YAxis[1] * Yc + L.ZAxis[1] * W),
                L.Center[2] + S * (L.XAxis[2] * U + L.YAxis[2] * Yc + L.ZAxis[2] * W)
            ];
        }

        // Curved perimeter + central midrib + sheet-normal tick
        const C0 = LeafPt(-0.72, -0.82);
        const C1 = LeafPt( 0.72, -0.82);
        const C2 = LeafPt( 0.72,  0.92);
        const C3 = LeafPt(-0.72,  0.92);
        PushLine(C0[0], C0[1], C0[2], C1[0], C1[1], C1[2], 1.0, Col);
        PushLine(C1[0], C1[1], C1[2], C2[0], C2[1], C2[2], 1.0, Col);
        PushLine(C2[0], C2[1], C2[2], C3[0], C3[1], C3[2], 1.0, Col);
        PushLine(C3[0], C3[1], C3[2], C0[0], C0[1], C0[2], 1.0, Col);

        const Mid0 = LeafPt(0.0, -0.88);
        const MidM = LeafPt(0.0,  0.00);
        const Mid1 = LeafPt(0.0,  0.94);
        PushLine(Mid0[0], Mid0[1], Mid0[2], MidM[0], MidM[1], MidM[2], 1.0, Col);
        PushLine(MidM[0], MidM[1], MidM[2], Mid1[0], Mid1[1], Mid1[2], 1.0, Col);

        // Signed-sheet normal vector tick (+YAxis)
        PushLine(
            MidM[0], MidM[1], MidM[2],
            MidM[0] + L.YAxis[0] * S * 0.32,
            MidM[1] + L.YAxis[1] * S * 0.32,
            MidM[2] + L.YAxis[2] * S * 0.32,
            1.0, [1.0, 0.95, 0.55, 0.90]
        );
    }

    // Layer 2: Live Diagnostic Shadow-Ray March from Ground Receiver toward SunDir!
    // Plots a fan of 3 parallel sun shadow rays with sphere-tracing step crosses & exact thin-leaf hit diamonds!
    const Offsets = [[-0.22, 0.0], [0.0, 0.0], [0.22, 0.0]];
    const BMin = CompiledVol.BoundsMin;
    const BMax = CompiledVol.BoundsMax;
    const Span = [BMax[0] - BMin[0], BMax[1] - BMin[1], BMax[2] - BMin[2]];
    const BrickSz = CompiledVol.BrickSize;

    for (let RIdx = 0; RIdx < Offsets.length; ++RIdx)
    {
        const Ro = [
            RayOriginGround[0] + Offsets[RIdx][0],
            0.015,
            RayOriginGround[2] + Offsets[RIdx][1]
        ];

        // Ground origin crosshair
        const Cr = 0.07;
        PushLine(Ro[0] - Cr, Ro[1], Ro[2], Ro[0] + Cr, Ro[1], Ro[2], 2.0, [1.0, 0.78, 0.22, 1.0]);
        PushLine(Ro[0], Ro[1], Ro[2] - Cr, Ro[0], Ro[1], Ro[2] + Cr, 2.0, [1.0, 0.78, 0.22, 1.0]);

        let T = 0.02;
        const TMax = 7.5;
        let HitT = TMax;
        let HitLeaf = false;
        let HitWood = false;
        const Caps = CompiledVol.SceneSpec.Capsules;
        const BarkAmp  = CompiledVol.SceneSpec.BarkNoiseAmp  !== undefined ? CompiledVol.SceneSpec.BarkNoiseAmp  : DefaultBarkNoiseAmp;
        const BarkFreq = CompiledVol.SceneSpec.BarkNoiseFreq !== undefined ? CompiledVol.SceneSpec.BarkNoiseFreq : DefaultBarkNoiseFreq;

        // First check exact intersection with the continuous Tapered-Capsule Wood Skeleton!
        for (let C = 0; C < Caps.length; ++C)
        {
            const Wv = EvalRayCapsuleShadow(Ro, SunDir, 0.02, TMax, 0.006, Caps[C], BarkAmp, BarkFreq);
            if (Wv < 0.35)
            {
                const MidCap = [
                    0.5 * (Caps[C].P0[0] + Caps[C].P1[0]) - Ro[0],
                    0.5 * (Caps[C].P0[1] + Caps[C].P1[1]) - Ro[1],
                    0.5 * (Caps[C].P0[2] + Caps[C].P1[2]) - Ro[2]
                ];
                const ApproxT = Math.max(0.05, MidCap[0] * SunDir[0] + MidCap[1] * SunDir[1] + MidCap[2] * SunDir[2]);
                if (ApproxT < HitT)
                {
                    HitT = ApproxT;
                    HitWood = true;
                }
            }
        }

        for (let Step = 0; Step < 36 && T < HitT; ++Step)
        {
            const P = [Ro[0] + SunDir[0] * T, Ro[1] + SunDir[1] * T, Ro[2] + SunDir[2] * T];
            if (P[0] < BMin[0] || P[0] > BMax[0] || P[1] < BMin[1] || P[1] > BMax[1] || P[2] < BMin[2] || P[2] > BMax[2])
            {
                T += 0.28;
                continue;
            }

            // Plot step marker cross along the ray!
            const Tick = 0.032;
            PushLine(P[0] - Tick, P[1], P[2], P[0] + Tick, P[1], P[2], 2.0, [1.0, 0.84, 0.26, 0.95]);
            PushLine(P[0], P[1] - Tick, P[2], P[0], P[1] + Tick, P[2], 2.0, [1.0, 0.84, 0.26, 0.95]);

            const DHull = Sample3dVolumeTrilinear(CompiledVol.MacroFloat3d, MacroVoxelRes, BMin, BMax, P, 1);
            if (DHull > 0.02)
            {
                T += Math.max(DHull * 0.88, 0.04);
                continue;
            }

            const Bx = Math.max(0, Math.min(CanopyBrickRes - 1, Math.floor(((P[0] - BMin[0]) / Span[0]) * CanopyBrickRes)));
            const By = Math.max(0, Math.min(CanopyBrickRes - 1, Math.floor(((P[1] - BMin[1]) / Span[1]) * CanopyBrickRes)));
            const Bz = Math.max(0, Math.min(CanopyBrickRes - 1, Math.floor(((P[2] - BMin[2]) / Span[2]) * CanopyBrickRes)));

            let TBrickOut = HitT;
            const CellX = BMin[0] + (SunDir[0] >= 0 ? Bx + 1 : Bx) * BrickSz[0];
            const CellY = BMin[1] + (SunDir[1] >= 0 ? By + 1 : By) * BrickSz[1];
            const CellZ = BMin[2] + (SunDir[2] >= 0 ? Bz + 1 : Bz) * BrickSz[2];
            if (Math.abs(SunDir[0]) > 1e-6) TBrickOut = Math.min(TBrickOut, (CellX - Ro[0]) / SunDir[0]);
            if (Math.abs(SunDir[1]) > 1e-6) TBrickOut = Math.min(TBrickOut, (CellY - Ro[1]) / SunDir[1]);
            if (Math.abs(SunDir[2]) > 1e-6) TBrickOut = Math.min(TBrickOut, (CellZ - Ro[2]) / SunDir[2]);
            TBrickOut = Math.max(T + 0.02, Math.min(HitT, TBrickOut));

            const Flat = (Bz * CanopyBrickRes + By) * CanopyBrickRes + Bx;
            const StartOff = CompiledVol.BrickOffsetsU32[Flat * 4 + 0];
            const Count    = CompiledVol.BrickOffsetsU32[Flat * 4 + 1];
            for (let K = 0; K < Count; ++K)
            {
                const LeafIdx = CompiledVol.BrickLeafRefsU32[StartOff + K];
                const Lv = EvalThinLeafRaySegmentShadow(Ro, SunDir, T - 0.03, TBrickOut + 0.03, 0.006, Leaves[LeafIdx], CompiledArchetypes.FloatGrids);
                if (Lv < 0.35)
                {
                    HitT = 0.5 * (T + TBrickOut);
                    HitLeaf = true;
                    HitWood = false;
                    break;
                }
            }
            if (HitLeaf) break;
            T = TBrickOut + 0.005;
        }

        const EndPt = [Ro[0] + SunDir[0] * HitT, Ro[1] + SunDir[1] * HitT, Ro[2] + SunDir[2] * HitT];
        const AnyHit = HitLeaf || HitWood;
        const RayCol = HitWood ? [0.98, 0.74, 0.32, 0.96] : (HitLeaf ? [0.28, 0.98, 0.56, 0.96] : [1.0, 0.62, 0.22, 0.72]);
        PushLine(Ro[0], Ro[1], Ro[2], EndPt[0], EndPt[1], EndPt[2], 2.0, RayCol);

        if (AnyHit)
        {
            const D = 0.065;
            const HitCol = HitWood ? [1.0, 0.82, 0.38, 1.0] : [0.28, 1.0, 0.56, 1.0];
            PushLine(EndPt[0] - D, EndPt[1], EndPt[2], EndPt[0], EndPt[1] + D, EndPt[2], 2.0, HitCol);
            PushLine(EndPt[0], EndPt[1] + D, EndPt[2], EndPt[0] + D, EndPt[1], EndPt[2], 2.0, HitCol);
            PushLine(EndPt[0] + D, EndPt[1], EndPt[2], EndPt[0], EndPt[1] - D, EndPt[2], 2.0, HitCol);
            PushLine(EndPt[0], EndPt[1] - D, EndPt[2], EndPt[0] - D, EndPt[1], EndPt[2], 2.0, HitCol);
        }
    }

    // Layer 3: Analytic Tapered-Capsule Skeleton + Ball-Joint Rings (warm amber/gold wireframe cage)
    const Caps = CompiledVol.SceneSpec.Capsules;
    const CapCol  = [0.98, 0.74, 0.30, 0.86];
    const AxisCol = [1.00, 0.92, 0.58, 0.94];
    for (let C = 0; C < Caps.length; ++C)
    {
        const { P0, R0, P1, R1 } = Caps[C];
        const Lift0 = R0 * 1.04 + 0.003;
        const Lift1 = R1 * 1.04 + 0.003;
        const Axis = [P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2]];
        const Dir  = Normalize3(Axis);
        const UpRef = Math.abs(Dir[1]) > 0.92 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0];
        const Right = Normalize3(Cross3(UpRef, Dir));
        const Fwd   = Normalize3(Cross3(Dir, Right));

        // Centerline spine segment
        PushLine(P0[0], P0[1], P0[2], P1[0], P1[1], P1[2], 3.0, AxisCol);

        // Joint rings at P0 and P1 + 4 longitudinal capsule generatrices
        const RingSegs = 12;
        for (let S = 0; S < RingSegs; ++S)
        {
            const A0 = (S / RingSegs) * Math.PI * 2.0;
            const A1 = ((S + 1) / RingSegs) * Math.PI * 2.0;
            const U0x = Right[0] * Math.cos(A0) + Fwd[0] * Math.sin(A0);
            const U0y = Right[1] * Math.cos(A0) + Fwd[1] * Math.sin(A0);
            const U0z = Right[2] * Math.cos(A0) + Fwd[2] * Math.sin(A0);
            const U1x = Right[0] * Math.cos(A1) + Fwd[0] * Math.sin(A1);
            const U1y = Right[1] * Math.cos(A1) + Fwd[1] * Math.sin(A1);
            const U1z = Right[2] * Math.cos(A1) + Fwd[2] * Math.sin(A1);

            PushLine(
                P0[0] + Lift0 * U0x, P0[1] + Lift0 * U0y, P0[2] + Lift0 * U0z,
                P0[0] + Lift0 * U1x, P0[1] + Lift0 * U1y, P0[2] + Lift0 * U1z,
                3.0, CapCol
            );
            PushLine(
                P1[0] + Lift1 * U0x, P1[1] + Lift1 * U0y, P1[2] + Lift1 * U0z,
                P1[0] + Lift1 * U1x, P1[1] + Lift1 * U1y, P1[2] + Lift1 * U1z,
                3.0, CapCol
            );

            if (S % 3 === 0)
            {
                PushLine(
                    P0[0] + Lift0 * U0x, P0[1] + Lift0 * U0y, P0[2] + Lift0 * U0z,
                    P1[0] + Lift1 * U0x, P1[1] + Lift1 * U0y, P1[2] + Lift1 * U0z,
                    3.0, CapCol
                );
            }
        }
    }

    return {
        VerticesF32: new Float32Array(Verts),
        VertexCount
    };
}
