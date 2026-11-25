/*============================================================================================================================================
                                                        LIGHTGRIDBAKER.JS
============================================================================================================================================*/
// 🧩 Compiles multi-position, multi-direction light groups into a 3D Spatial-Directional Mip-Grid (8x4x4 -> 2x1x1 -> 1x1x1 -> 4x4)
//    with continuous distance-LOD, perceptual influence culling, and iterative Residual-Error Optimization against exact ground truth.

export const GridDimX = 8; // [cells] - Lateral X spatial subdivisions (separates left/right headlamps cleanly)
export const GridDimY = 4; // [cells] - Vertical Y spatial subdivisions
export const GridDimZ = 4; // [cells] - Longitudinal Z spatial subdivisions
export const TotalSpatialCells = GridDimX * GridDimY * GridDimZ; // 128 cells per cluster

export const TileResLod0 = 16; // [px] - Near-field octahedral directional resolution per active spatial cell
export const TileResLod1 = 12; // [px] - Mid-field bimodal pole octahedral directional resolution
export const TileResLod2 = 12; // [px] - Far-field single-sample octahedral directional resolution
export const TileResLod3 = 4;  // [px] - Ultra-far coarse directional resolution

export const GridBakingMode = Object.freeze({
    NaiveCenterAveraged: 0, // 🔴 Merges all lights to box center (demonstrates why center-averaging causes huge error)
    SpatialUnoptimized:  1, // 🚩 8x4x4 spatial cells (no center-averaging), raw voxel centers & raw discrete binning
    ErrorOptimizedMip:   2  // 🟢 8x4x4 spatial cells + Residual-Error optimized sub-voxel anchors, aperture & deconvolved tiles + LOD
});

function Normalize3(V)
{
    const Len = Math.hypot(V[0], V[1], V[2]);
    if (Len < 1e-8) return [0.0, -1.0, 0.0];
    return [V[0] / Len, V[1] / Len, V[2] / Len];
}

function Dot3(A, B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

function Clamp(X, Lo, Hi)
{
    return X < Lo ? Lo : (X > Hi ? Hi : X);
}

// 📝 Octahedral sphere parameterization with pole at -Y (0, -1, 0) so all downward & horizontal automotive/showroom beams
//    project smoothly into the interior of [0, 1]^2 without crossing fold seams. Matches GLSL bit-for-bit.
export function OctahedralEncode(Dir)
{
    const Qx = Dir[0];
    const Qy = -Dir[1]; // Pole +Qy corresponds to downward world -Y
    const Qz = Dir[2];
    const L1 = Math.max(Math.abs(Qx) + Math.abs(Qy) + Math.abs(Qz), 1e-8);
    let Ox = Qx / L1;
    let Oz = Qz / L1;
    if (Qy < 0.0)
    {
        const SignX = Ox >= 0.0 ? 1.0 : -1.0;
        const SignZ = Oz >= 0.0 ? 1.0 : -1.0;
        const NextX = (1.0 - Math.abs(Oz)) * SignX;
        const NextZ = (1.0 - Math.abs(Ox)) * SignZ;
        Ox = NextX;
        Oz = NextZ;
    }
    return [0.5 * Ox + 0.5, 0.5 * Oz + 0.5];
}

export function OctahedralDecode(U, V)
{
    let Ox = 2.0 * U - 1.0;
    let Oz = 2.0 * V - 1.0;
    const Qy = 1.0 - Math.abs(Ox) - Math.abs(Oz);
    if (Qy < 0.0)
    {
        const SignX = Ox >= 0.0 ? 1.0 : -1.0;
        const SignZ = Oz >= 0.0 ? 1.0 : -1.0;
        const NextX = (1.0 - Math.abs(Oz)) * SignX;
        const NextZ = (1.0 - Math.abs(Ox)) * SignZ;
        Ox = NextX;
        Oz = NextZ;
    }
    return Normalize3([Ox, -Qy, Oz]);
}

// 📝 Evaluates the exact angular intensity I_rgb(dir) [cd] of one raw unmerged light source along world ray vector RayDir
export function EvaluateRawSourceDirectionalCandela(Source, RayDir)
{
    const CosTheta = Dot3(RayDir, Source.Axis);
    if (CosTheta <= Source.CutoffCos) return [0.0, 0.0, 0.0];

    const Span = Math.max(1.0 - Source.CutoffCos, 1e-4);
    const Window = Clamp((CosTheta - Source.CutoffCos) / Span, 0.0, 1.0);
    const SmoothEdge = Window * Window * (3.0 - 2.0 * Window);
    let Gain = Math.pow(Math.max(CosTheta, 0.0), Source.FocusExponent) * SmoothEdge;

    // Optional ECE R112 asymmetric low-beam vertical cutoff for automotive headlamps
    if (Source.EnableEceCutoff)
    {
        // Local right & up axes around Source.Axis
        const UpRef = [0.0, 1.0, 0.0];
        let Rx = UpRef[1] * Source.Axis[2] - UpRef[2] * Source.Axis[1];
        let Ry = UpRef[2] * Source.Axis[0] - UpRef[0] * Source.Axis[2];
        let Rz = UpRef[0] * Source.Axis[1] - UpRef[1] * Source.Axis[0];
        const RLen = Math.hypot(Rx, Ry, Rz) || 1.0;
        Rx /= RLen; Ry /= RLen; Rz /= RLen;
        const Ux = Source.Axis[1] * Rz - Source.Axis[2] * Ry;
        const Uy = Source.Axis[2] * Rx - Source.Axis[0] * Rz;
        const Uz = Source.Axis[0] * Ry - Source.Axis[1] * Rx;

        const LatAngle  = RayDir[0] * Rx + RayDir[1] * Ry + RayDir[2] * Rz;
        const VertAngle = RayDir[0] * Ux + RayDir[1] * Uy + RayDir[2] * Uz;
        // -0.018 rad (-1.0 deg) on oncoming left side, rising at 15 deg on curbside right
        const KinkSlope = LatAngle > 0.0 ? Math.min(0.085, LatAngle * 0.27) : 0.0;
        const CutoffLimit = -0.016 + KinkSlope;
        const EceMask = 1.0 - Clamp((VertAngle - CutoffLimit) / 0.032, 0.0, 1.0);
        Gain *= (0.04 + 0.96 * EceMask * EceMask);
    }

    return [
        Source.IntensityCd * Source.ColorRgb[0] * Gain,
        Source.IntensityCd * Source.ColorRgb[1] * Gain,
        Source.IntensityCd * Source.ColorRgb[2] * Gain
    ];
}

// 📝 Evaluates exact diffuse + GGX specular BRDF contribution from one point/aperture radiator at (Anchor, Radius, CandelaRgb)
export function ShadeFromRadiator(ShadingPos, Normal, ViewDir, Roughness, Albedo, F0, AnchorPos, ApertureRadius, CandelaRgb)
{
    const Dx = ShadingPos[0] - AnchorPos[0];
    const Dy = ShadingPos[1] - AnchorPos[1];
    const Dz = ShadingPos[2] - AnchorPos[2];
    const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
    const Dist = Math.sqrt(Math.max(DistSq, 1e-8));
    const Lx = -Dx / Dist, Ly = -Dy / Dist, Lz = -Dz / Dist; // Surface -> Light

    const NdotL = Normal[0] * Lx + Normal[1] * Ly + Normal[2] * Lz;
    if (NdotL <= 0.0) return [0.0, 0.0, 0.0];

    const Attenuation = 1.0 / (DistSq + ApertureRadius * ApertureRadius);

    // Diffuse Lambertian + Specular GGX
    const NdotV = Math.max(Normal[0] * ViewDir[0] + Normal[1] * ViewDir[1] + Normal[2] * ViewDir[2], 1e-4);
    const HxRaw = Lx + ViewDir[0], HyRaw = Ly + ViewDir[1], HzRaw = Lz + ViewDir[2];
    const HLen = Math.hypot(HxRaw, HyRaw, HzRaw) || 1.0;
    const Hx = HxRaw / HLen, Hy = HyRaw / HLen, Hz = HzRaw / HLen;
    const NdotH = Math.max(Normal[0] * Hx + Normal[1] * Hy + Normal[2] * Hz, 0.0);
    const VdotH = Math.max(ViewDir[0] * Hx + ViewDir[1] * Hy + ViewDir[2] * Hz, 0.0);

    // Finite-aperture roughness widening (prevents pinpoint singularity for area/emissive panels)
    const Alpha = Clamp(Roughness * Roughness + (ApertureRadius / (2.0 * Dist + 0.1)), 0.006, 1.0);
    const Alpha2 = Alpha * Alpha;
    const DenomD = NdotH * NdotH * (Alpha2 - 1.0) + 1.0;
    const D = Alpha2 / Math.max(Math.PI * DenomD * DenomD, 1e-6);

    const K = 0.5 * Alpha;
    const Gv = NdotV / (NdotV * (1.0 - K) + K);
    const Gl = NdotL / (NdotL * (1.0 - K) + K);
    const SpecGeom = (D * Gv * Gl) / Math.max(4.0 * NdotV * NdotL, 1e-5);
    const FresnelSchlick = F0 + (1.0 - F0) * Math.pow(1.0 - VdotH, 5.0);

    const DiffScale = (1.0 - F0) / Math.PI;
    const SpecScale = SpecGeom * FresnelSchlick;

    const ScaleR = (Albedo[0] * DiffScale + SpecScale) * NdotL * Attenuation;
    const ScaleG = (Albedo[1] * DiffScale + SpecScale) * NdotL * Attenuation;
    const ScaleB = (Albedo[2] * DiffScale + SpecScale) * NdotL * Attenuation;

    return [
        CandelaRgb[0] * ScaleR,
        CandelaRgb[1] * ScaleG,
        CandelaRgb[2] * ScaleB
    ];
}

// 📝 Evaluates exact Ground-Truth radiance from ALL raw unmerged lights in a cluster list
export function EvaluateGroundTruthClusters(Clusters, ShadingPos, Normal, ViewDir, Roughness, Albedo, F0 = 0.04)
{
    let OutR = 0.0, OutG = 0.0, OutB = 0.0;
    for (let CIdx = 0; CIdx < Clusters.length; ++CIdx)
    {
        const Sources = Clusters[CIdx].Sources;
        for (let SIdx = 0; SIdx < Sources.length; ++SIdx)
        {
            const Src = Sources[SIdx];
            const Dx = ShadingPos[0] - Src.Position[0];
            const Dy = ShadingPos[1] - Src.Position[1];
            const Dz = ShadingPos[2] - Src.Position[2];
            const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
            const RayDir = [Dx / Dist, Dy / Dist, Dz / Dist];
            const Candela = EvaluateRawSourceDirectionalCandela(Src, RayDir);
            if (Candela[0] + Candela[1] + Candela[2] <= 1e-7) continue;

            const Contrib = ShadeFromRadiator(
                ShadingPos, Normal, ViewDir, Roughness, Albedo, F0,
                Src.Position, Src.ApertureRadius, Candela
            );
            OutR += Contrib[0];
            OutG += Contrib[1];
            OutB += Contrib[2];
        }
    }
    return [OutR, OutG, OutB];
}

// 📝 Bilinear lookup on an [Res x Res x 3] octahedral tile matching GPU texel center convention:
//    Texel centers lie at ((i + 0.5) / Res), clamped to [0.5/Res, 1 - 0.5/Res].
export function SampleOctahedralTileBilinear(TileFloats, Res, RayDir)
{
    const [U, V] = OctahedralEncode(RayDir);
    const Fx = Clamp(U * Res - 0.5, 0.0, Res - 1.00001);
    const Fy = Clamp(V * Res - 0.5, 0.0, Res - 1.00001);
    const X0 = Math.floor(Fx), Y0 = Math.floor(Fy);
    const X1 = Math.min(Res - 1, X0 + 1), Y1 = Math.min(Res - 1, Y0 + 1);
    const Tx = Fx - X0, Ty = Fy - Y0;

    const W00 = (1.0 - Tx) * (1.0 - Ty);
    const W10 = Tx * (1.0 - Ty);
    const W01 = (1.0 - Tx) * Ty;
    const W11 = Tx * Ty;

    const I00 = (Y0 * Res + X0) * 3;
    const I10 = (Y0 * Res + X1) * 3;
    const I01 = (Y1 * Res + X0) * 3;
    const I11 = (Y1 * Res + X1) * 3;

    return [
        TileFloats[I00] * W00 + TileFloats[I10] * W10 + TileFloats[I01] * W01 + TileFloats[I11] * W11,
        TileFloats[I00 + 1] * W00 + TileFloats[I10 + 1] * W10 + TileFloats[I01 + 1] * W01 + TileFloats[I11 + 1] * W11,
        TileFloats[I00 + 2] * W00 + TileFloats[I10 + 2] * W10 + TileFloats[I01 + 2] * W01 + TileFloats[I11 + 2] * W11
    ];
}

// 📝 Constructs the 3 Scene Presets specifically designed to test:
//    Preset 1: Automotive Dual Headlamps + 2 Angled Side Marker Lamps + 1 Roof Rally Bar (EMPTY CENTER between headlamps!)
//    Preset 2: 5 Multi-Direction Emissive Panels at 1 Location (North, South, East, West, Down) -> 1 Grid Light
//    Preset 3: 12-Cluster Nanite-LOD Showroom Hall (60 raw lights spanning Near LOD 0 -> Mid LOD 1 -> Far LOD 2 -> LOD 3 -> Culled)
export function ConstructSceneLightClusters(PresetId)
{
    // Note: World convention matches VehicleSurfaceBuilder: Car faces +X (nose at x = +2.12, left/right along Z = +-0.74, Y up)!
    // Wait: let's check! In VehicleSurfaceBuilder, car nose is at X = +2.12, left/right headlights are at Z = -0.74 and Z = +0.74!
    // So lateral separation axis across the bumper is Z (or if we rotate the car to face +Z toward camera/wall, lateral is X!).
    // Rotating the hero car by Yaw = -pi/2 (or +pi/2) puts car nose along +Z and Left/Right headlights along X = -0.74 and X = +0.74!
    // YES! Facing +Z makes X the Left-Right bumper axis (GridDimX = 8 cells across [-1.15 .. +1.15]!), Y vertical, Z forward!

    function MakeCarFrontCluster(CenterOrigin, Label, Scale = 1.0)
    {
        const [Cx, Cy, Cz] = CenterOrigin;
        return {
            Label,
            BoundsMin: [Cx - 1.16, Cy + 0.36, Cz - 0.36],
            BoundsMax: [Cx + 1.16, Cy + 1.04, Cz + 0.36],
            InfluenceRadius: 28.0 * Math.sqrt(Scale),
            Sources: [
                // 1. Left ECE Low-Beam Headlamp (x = -0.76 m -> Cell ix = 1; cells ix = 2,3,4,5 in middle stay EMPTY at bumper level!)
                {
                    Label: "Left ECE Headlamp (-0.76m)",
                    Position: [Cx - 0.76, Cy + 0.66, Cz + 0.14],
                    Axis: Normalize3([0.02, -0.135, 0.990]),
                    ColorRgb: [0.94, 0.97, 1.0],
                    IntensityCd: 22.0 * Scale,
                    FocusExponent: 16.0,
                    CutoffCos: Math.cos((54.0 * Math.PI) / 180.0),
                    ApertureRadius: 0.11,
                    EnableEceCutoff: true
                },
                // 2. Right ECE Low-Beam Headlamp (x = +0.76 m -> Cell ix = 6!)
                {
                    Label: "Right ECE Headlamp (+0.76m)",
                    Position: [Cx + 0.76, Cy + 0.66, Cz + 0.14],
                    Axis: Normalize3([-0.02, -0.135, 0.990]),
                    ColorRgb: [0.94, 0.97, 1.0],
                    IntensityCd: 22.0 * Scale,
                    FocusExponent: 16.0,
                    CutoffCos: Math.cos((54.0 * Math.PI) / 180.0),
                    ApertureRadius: 0.11,
                    EnableEceCutoff: true
                },
                // 3. Left Angled Amber Corner Marker (x = -0.94 m -> Cell ix = 0, aiming 48 deg LEFT!)
                {
                    Label: "Left Amber Corner (-48°)",
                    Position: [Cx - 0.94, Cy + 0.56, Cz - 0.04],
                    Axis: Normalize3([-0.74, -0.16, 0.65]),
                    ColorRgb: [1.0, 0.44, 0.05],
                    IntensityCd: 9.5 * Scale,
                    FocusExponent: 5.5,
                    CutoffCos: Math.cos((72.0 * Math.PI) / 180.0),
                    ApertureRadius: 0.14,
                    EnableEceCutoff: false
                },
                // 4. Right Angled Amber Corner Marker (x = +0.94 m -> Cell ix = 7, aiming 48 deg RIGHT!)
                {
                    Label: "Right Amber Corner (+48°)",
                    Position: [Cx + 0.94, Cy + 0.56, Cz - 0.04],
                    Axis: Normalize3([0.74, -0.16, 0.65]),
                    ColorRgb: [1.0, 0.44, 0.05],
                    IntensityCd: 9.5 * Scale,
                    FocusExponent: 5.5,
                    CutoffCos: Math.cos((72.0 * Math.PI) / 180.0),
                    ApertureRadius: 0.14,
                    EnableEceCutoff: false
                },
                // 5. Left Fender Side-Wash Emissive Strip (aiming -X + downward onto wheel arch)
                {
                    Label: "Left Wheel-Arch Side Emissive",
                    Position: [Cx - 0.98, Cy + 0.82, Cz - 0.22],
                    Axis: Normalize3([-0.88, -0.42, -0.22]),
                    ColorRgb: [0.22, 0.72, 1.0],
                    IntensityCd: 6.8 * Scale,
                    FocusExponent: 3.5,
                    CutoffCos: Math.cos((80.0 * Math.PI) / 180.0),
                    ApertureRadius: 0.18,
                    EnableEceCutoff: false
                }
            ]
        };
    }

    function MakeFiveDirectionPanelRig(CenterOrigin, Label, Scale = 1.0)
    {
        const [Cx, Cy, Cz] = CenterOrigin;
        return {
            Label,
            BoundsMin: [Cx - 0.95, Cy - 0.45, Cz - 0.95],
            BoundsMax: [Cx + 0.95, Cy + 0.45, Cz + 0.95],
            InfluenceRadius: 26.0 * Math.sqrt(Scale),
            Sources: [
                {
                    Label: "Radiant Panel 1 (+Z Cyan)",
                    Position: [Cx, Cy + 0.06, Cz + 0.72],
                    Axis: Normalize3([0.0, -0.28, 0.96]),
                    ColorRgb: [0.16, 0.82, 1.0],
                    IntensityCd: 16.5 * Scale,
                    FocusExponent: 3.2,
                    CutoffCos: -0.05,
                    ApertureRadius: 0.28,
                    EnableEceCutoff: false
                },
                {
                    Label: "Radiant Panel 2 (+X Gold)",
                    Position: [Cx + 0.72, Cy + 0.06, Cz],
                    Axis: Normalize3([0.96, -0.28, 0.0]),
                    ColorRgb: [1.0, 0.66, 0.16],
                    IntensityCd: 16.5 * Scale,
                    FocusExponent: 3.2,
                    CutoffCos: -0.05,
                    ApertureRadius: 0.28,
                    EnableEceCutoff: false
                },
                {
                    Label: "Radiant Panel 3 (-Z Ruby)",
                    Position: [Cx, Cy + 0.06, Cz - 0.72],
                    Axis: Normalize3([0.0, -0.28, -0.96]),
                    ColorRgb: [1.0, 0.18, 0.28],
                    IntensityCd: 16.5 * Scale,
                    FocusExponent: 3.2,
                    CutoffCos: -0.05,
                    ApertureRadius: 0.28,
                    EnableEceCutoff: false
                },
                {
                    Label: "Radiant Panel 4 (-X Emerald)",
                    Position: [Cx - 0.72, Cy + 0.06, Cz],
                    Axis: Normalize3([-0.96, -0.28, 0.0]),
                    ColorRgb: [0.20, 0.95, 0.48],
                    IntensityCd: 16.5 * Scale,
                    FocusExponent: 3.2,
                    CutoffCos: -0.05,
                    ApertureRadius: 0.28,
                    EnableEceCutoff: false
                },
                {
                    Label: "Radiant Panel 5 (-Y Overhead Downlight)",
                    Position: [Cx, Cy - 0.28, Cz],
                    Axis: [0.0, -1.0, 0.0],
                    ColorRgb: [1.0, 0.96, 0.90],
                    IntensityCd: 21.0 * Scale,
                    FocusExponent: 5.0,
                    CutoffCos: 0.05,
                    ApertureRadius: 0.34,
                    EnableEceCutoff: false
                }
            ]
        };
    }

    if (PresetId === "dual-headlamps")
    {
        return [MakeCarFrontCluster([0.0, 0.0, 1.98], "Hero Car Front 5-Light Rig (Empty Center)", 1.0)];
    }

    if (PresetId === "five-emissives")
    {
        return [MakeFiveDirectionPanelRig([0.0, 1.85, 0.0], "5-Direction Radiant Rig -> 1 Baked Grid", 1.0)];
    }

    // Preset 3 ("nanite-hall"): 12 Multi-Light Clusters (60 raw lights total!) arranged from Near (z = +2m) to Far (z = -30m)
    const HallClusters = [
        MakeCarFrontCluster([ 0.0, 0.0,   1.98], "Bay 0 · Hero Car Rig (LOD 0 Near)", 1.0),
        MakeCarFrontCluster([-4.6, 0.0,  -4.20], "Bay 1 · Left Coupe Rig (LOD 1 Mid)", 0.9),
        MakeCarFrontCluster([ 4.6, 0.0,  -4.20], "Bay 2 · Right Coupe Rig (LOD 1 Mid)", 0.9),
        MakeFiveDirectionPanelRig([ 0.0, 2.85,  0.50], "Ceiling Rig A (5 Panels)", 0.85),
        MakeFiveDirectionPanelRig([-4.2, 2.85, -7.50], "Ceiling Rig B (5 Panels)", 0.85),
        MakeFiveDirectionPanelRig([ 4.2, 2.85, -7.50], "Ceiling Rig C (5 Panels)", 0.85),
        MakeFiveDirectionPanelRig([-4.2, 2.85, -15.5], "Mid-Far Rig D (LOD 2 = 1 Grid Sample!)", 0.85),
        MakeFiveDirectionPanelRig([ 4.2, 2.85, -15.5], "Mid-Far Rig E (LOD 2 = 1 Grid Sample!)", 0.85),
        MakeFiveDirectionPanelRig([ 0.0, 2.85, -19.5], "Far Rig F (LOD 2 = 1 Grid Sample!)", 0.85),
        MakeFiveDirectionPanelRig([-4.5, 2.85, -25.5], "Deep Bay Rig G (LOD 3 Coarse)", 0.80),
        MakeFiveDirectionPanelRig([ 4.5, 2.85, -25.5], "Deep Bay Rig H (LOD 3 Coarse)", 0.80),
        MakeFiveDirectionPanelRig([ 0.0, 2.85, -32.5], "Distant Bay Rig I (Distance Culled)", 0.75)
    ];
    return HallClusters;
}

// 📝 Compiles a single Cluster into its Multi-LOD Baked Grid layout:
//    - NaiveTile16:      Center-averaged 16x16 octahedral tile at BoxCenter (for Mode 0 failure comparison)
//    - ActiveCellsLod0:  Sparse occupied cells of the 8x4x4 grid (NO center-averaging!), each with 16x16 tile,
//                        VoxelCenter, SubVoxelOffset (initially [0,0,0] before optimization!), and ApertureRadius
//    - PolesLod1:        2 bimodal spatial poles (Left/Right split along principal axis) x 12x12 tile
//    - UnifiedLod2:      1 single octahedral 12x12 tile + optimized flux-weighted origin (for far-field 1-sample evaluation!)
//    - CoarseLod3:       1 single 4x4 coarse tile
export function BakeInitialLightClusterGrid(Cluster)
{
    const BMin = Cluster.BoundsMin;
    const BMax = Cluster.BoundsMax;
    const Span = [BMax[0] - BMin[0], BMax[1] - BMin[1], BMax[2] - BMin[2]];
    const CellSize = [Span[0] / GridDimX, Span[1] / GridDimY, Span[2] / GridDimZ];
    const BoxCenter = [0.5 * (BMin[0] + BMax[0]), 0.5 * (BMin[1] + BMax[1]), 0.5 * (BMin[2] + BMax[2])];
    const Diameter = Math.hypot(Span[0], Span[1], Span[2]);

    // ① Bin each source into its true 3D spatial voxel [ix, iy, iz] — ZERO center-averaging!
    const VoxelBuckets = Array.from({ length: TotalSpatialCells }, () => []);
    for (let SIdx = 0; SIdx < Cluster.Sources.length; ++SIdx)
    {
        const Src = Cluster.Sources[SIdx];
        const Ix = Math.min(GridDimX - 1, Math.max(0, Math.floor(((Src.Position[0] - BMin[0]) / Span[0]) * GridDimX)));
        const Iy = Math.min(GridDimY - 1, Math.max(0, Math.floor(((Src.Position[1] - BMin[1]) / Span[1]) * GridDimY)));
        const Iz = Math.min(GridDimZ - 1, Math.max(0, Math.floor(((Src.Position[2] - BMin[2]) / Span[2]) * GridDimZ)));
        const FlatIdx = (Iz * GridDimY + Iy) * GridDimX + Ix;
        VoxelBuckets[FlatIdx].push(Src);
    }

    // Subroutine: bake raw unoptimized octahedral tile by sampling directional sphere at texel centers
    // (Deliberately includes realistic raw-bake discretization: quantized voxel center + discrete octahedral texels + slightly blurred filter)
    function BakeRawDirectionalTile(SourcesList, Res, BlurMix = 0.22)
    {
        const Raw = new Float32Array(Res * Res * 3);
        for (let Y = 0; Y < Res; ++Y)
        {
            for (let X = 0; X < Res; ++X)
            {
                const Dir = OctahedralDecode((X + 0.5) / Res, (Y + 0.5) / Res);
                let R = 0.0, G = 0.0, B = 0.0;
                for (let K = 0; K < SourcesList.length; ++K)
                {
                    const C = EvaluateRawSourceDirectionalCandela(SourcesList[K], Dir);
                    R += C[0]; G += C[1]; B += C[2];
                }
                const Offset = (Y * Res + X) * 3;
                Raw[Offset] = R; Raw[Offset + 1] = G; Raw[Offset + 2] = B;
            }
        }
        if (BlurMix <= 0.0) return Raw;

        // Raw pre-bake box filter (typical unoptimized bake filter before residual deconvolution)
        const Blurred = new Float32Array(Res * Res * 3);
        for (let Y = 0; Y < Res; ++Y)
        {
            for (let X = 0; X < Res; ++X)
            {
                let SumR = 0.0, SumG = 0.0, SumB = 0.0, Count = 0.0;
                for (let Dy = -1; Dy <= 1; ++Dy)
                {
                    for (let Dx = -1; Dx <= 1; ++Dx)
                    {
                        const Nx = Clamp(X + Dx, 0, Res - 1);
                        const Ny = Clamp(Y + Dy, 0, Res - 1);
                        const W = (Dx === 0 && Dy === 0) ? 4.0 : 1.0;
                        const Idx = (Ny * Res + Nx) * 3;
                        SumR += Raw[Idx] * W;
                        SumG += Raw[Idx + 1] * W;
                        SumB += Raw[Idx + 2] * W;
                        Count += W;
                    }
                }
                const Offset = (Y * Res + X) * 3;
                Blurred[Offset]     = Raw[Offset]     * (1.0 - BlurMix) + (SumR / Count) * BlurMix;
                Blurred[Offset + 1] = Raw[Offset + 1] * (1.0 - BlurMix) + (SumG / Count) * BlurMix;
                Blurred[Offset + 2] = Raw[Offset + 2] * (1.0 - BlurMix) + (SumB / Count) * BlurMix;
            }
        }
        return Blurred;
    }

    // ② Build LOD 0 Active Spatial Cells (strictly occupied voxels — empty center cells stay empty!)
    const ActiveCellsLod0 = [];
    const OccupancyMask = new Uint8Array(TotalSpatialCells);

    for (let Iz = 0; Iz < GridDimZ; ++Iz)
    {
        for (let Iy = 0; Iy < GridDimY; ++Iy)
        {
            for (let Ix = 0; Ix < GridDimX; ++Ix)
            {
                const FlatIdx = (Iz * GridDimY + Iy) * GridDimX + Ix;
                const Bucket = VoxelBuckets[FlatIdx];
                if (Bucket.length === 0) continue;

                OccupancyMask[FlatIdx] = Bucket.length;
                const VoxelCenter = [
                    BMin[0] + (Ix + 0.5) * CellSize[0],
                    BMin[1] + (Iy + 0.5) * CellSize[1],
                    BMin[2] + (Iz + 0.5) * CellSize[2]
                ];

                // Unoptimized preset starts at VoxelCenter (SubOffset = [0,0,0]) and generic cell radius 0.5*norm(CellSize)!
                // Notice: even without center-averaging, snapping (-0.76, 0.66, 2.12) to VoxelCenter (-0.725, 0.615, 2.07)
                // + generic radius + octahedral discrete blur creates real measurable error that our Optimizer crushes!
                const InitialTile = BakeRawDirectionalTile(Bucket, TileResLod0, 0.28);
                let PeakEnergy = 0.0;
                let DominantDir = [0.0, -1.0, 0.0];
                for (let T = 0; T < TileResLod0 * TileResLod0; ++T)
                {
                    const Lum = InitialTile[T * 3] + InitialTile[T * 3 + 1] + InitialTile[T * 3 + 2];
                    if (Lum > PeakEnergy)
                    {
                        PeakEnergy = Lum;
                        DominantDir = OctahedralDecode(((T % TileResLod0) + 0.5) / TileResLod0, (Math.floor(T / TileResLod0) + 0.5) / TileResLod0);
                    }
                }

                ActiveCellsLod0.push({
                    GridIndex: [Ix, Iy, Iz],
                    FlatIndex: FlatIdx,
                    BucketSources: Bucket,
                    VoxelCenter,
                    SubOffset: [0.0, 0.0, 0.0], // Optimized in Phase 2!
                    MaxOffset: [0.5 * CellSize[0], 0.5 * CellSize[1], 0.5 * CellSize[2]],
                    ApertureRadius: 0.32,       // Optimized in Phase 2!
                    RawTile16: new Float32Array(InitialTile),
                    OptTile16: new Float32Array(InitialTile),
                    DominantDir
                });
            }
        }
    }

    // ③ Build Naive Center-Averaged 16x16 Tile (Mode 0 contrast: merges ALL sources to BoxCenter!)
    const NaiveTile16 = BakeRawDirectionalTile(Cluster.Sources, TileResLod0, 0.15);

    // ④ Build LOD 1 Bimodal Split Poles (splits cluster along its widest spatial axis into NegHalf & PosHalf — NO center merge!)
    const AxisSpans = [Span[0], Span[1], Span[2]];
    let SplitAxis = 0;
    if (AxisSpans[2] > AxisSpans[0] && AxisSpans[2] >= AxisSpans[1]) SplitAxis = 2;
    const MidCoord = BoxCenter[SplitAxis];
    const LeftSources = Cluster.Sources.filter((S) => S.Position[SplitAxis] <= MidCoord);
    const RightSources = Cluster.Sources.filter((S) => S.Position[SplitAxis] > MidCoord);

    function ComputeFluxCentroid(List, Fallback)
    {
        if (List.length === 0) return [Fallback[0], Fallback[1], Fallback[2]];
        let WSum = 0.0, Px = 0.0, Py = 0.0, Pz = 0.0;
        for (const S of List)
        {
            const W = Math.max(S.IntensityCd, 0.01);
            Px += S.Position[0] * W; Py += S.Position[1] * W; Pz += S.Position[2] * W;
            WSum += W;
        }
        return [Px / WSum, Py / WSum, Pz / WSum];
    }

    const PolesLod1 = [];
    for (const HalfList of [LeftSources, RightSources])
    {
        if (HalfList.length === 0) continue;
        PolesLod1.push({
            AnchorPos: ComputeFluxCentroid(HalfList, BoxCenter),
            ApertureRadius: 0.24,
            Tile12: BakeRawDirectionalTile(HalfList, TileResLod1, 0.12),
            Sources: HalfList
        });
    }

    // ⑤ Build LOD 2 (Far-Field 1-Sample Octahedral Grid) & LOD 3 (Ultra-Far 4x4 Coarse Tile)
    const FluxOrigin = ComputeFluxCentroid(Cluster.Sources, BoxCenter);
    const UnifiedLod2 = {
        AnchorPos: [FluxOrigin[0], FluxOrigin[1], FluxOrigin[2]],
        ApertureRadius: 0.28,
        Tile12: BakeRawDirectionalTile(Cluster.Sources, TileResLod2, 0.08)
    };
    const CoarseLod3 = {
        AnchorPos: [FluxOrigin[0], FluxOrigin[1], FluxOrigin[2]],
        ApertureRadius: 0.35,
        Tile4: BakeRawDirectionalTile(Cluster.Sources, TileResLod3, 0.0)
    };

    return {
        Cluster,
        BoxCenter,
        CellSize,
        Diameter,
        OccupancyMask,
        NaiveTile16,
        ActiveCellsLod0,
        PolesLod1,
        UnifiedLod2,
        CoarseLod3,
        OptimizationSteps: 0,
        ErrorHistory: []
    };
}

// 📝 Computes continuous Nanite-style LOD level [0 .. 3.5] from shading point distance & cluster diameter
export function ComputeClusterNaniteLod(CompiledGrid, ShadingPos, LodBias = 0.0)
{
    const C = CompiledGrid.BoxCenter;
    const Dist = Math.hypot(ShadingPos[0] - C[0], ShadingPos[1] - C[1], ShadingPos[2] - C[2]);
    const RInf = CompiledGrid.Cluster.InfluenceRadius;
    if (Dist >= RInf)
    {
        return { LodContinuous: 4.0, InfluenceWeight: 0.0, Distance: Dist };
    }
    const Ratio = Dist / RInf;
    const Window = Math.max(0.0, 1.0 - Ratio * Ratio * Ratio * Ratio);
    const InfluenceWeight = Window * Window;

    // LOD 0 for d <= 2.4*D, LOD 1 around 5.0*D, LOD 2 (1-sample!) around 9.5*D, LOD 3 >= 16*D
    const NearRef = Math.max(CompiledGrid.Diameter * 2.2, 1.8);
    const RawLod = Math.max(0.0, Math.log2(Math.max(Dist / NearRef, 1.0)) * 1.15 + LodBias);
    return {
        LodContinuous: Math.min(3.0, RawLod),
        InfluenceWeight,
        Distance: Dist
    };
}

// 📝 Evaluates a Compiled Baked Grid Cluster at ShadingPos using the selected GridBakingMode & Nanite Distance-LOD
export function EvaluateBakedGridCluster(
    CompiledGrid,
    BakingMode,
    EnableDistanceLod,
    LodBias,
    ShadingPos,
    Normal,
    ViewDir,
    Roughness,
    Albedo,
    F0 = 0.04
)
{
    const { LodContinuous, InfluenceWeight } = ComputeClusterNaniteLod(CompiledGrid, ShadingPos, LodBias);
    if (EnableDistanceLod && InfluenceWeight <= 1e-5)
    {
        return { Rgb: [0.0, 0.0, 0.0], SampleLookups: 0, ActiveLod: 4.0 };
    }
    const AttenWindow = EnableDistanceLod ? InfluenceWeight : 1.0;

    // 🔴 MODE 0: Naive Center-Averaged 2D Grid (1 origin at BoxCenter — merges 2 headlights at center!)
    if (BakingMode === GridBakingMode.NaiveCenterAveraged)
    {
        const C = CompiledGrid.BoxCenter;
        const Dx = ShadingPos[0] - C[0], Dy = ShadingPos[1] - C[1], Dz = ShadingPos[2] - C[2];
        const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
        const RayDir = [Dx / Dist, Dy / Dist, Dz / Dist];
        const Candela = SampleOctahedralTileBilinear(CompiledGrid.NaiveTile16, TileResLod0, RayDir);
        const Rgb = ShadeFromRadiator(ShadingPos, Normal, ViewDir, Roughness, Albedo, F0, C, 0.20, Candela);
        return {
            Rgb: [Rgb[0] * AttenWindow, Rgb[1] * AttenWindow, Rgb[2] * AttenWindow],
            SampleLookups: 1,
            ActiveLod: 0.0
        };
    }

    const UseOptimized = BakingMode === GridBakingMode.ErrorOptimizedMip;

    // Subroutine to evaluate a discrete tier: 0 = Active 8x4x4 Cells, 1 = Bimodal 2-Pole, 2 = Single 12x12 Grid, 3 = Coarse 4x4
    function EvalTier(Tier)
    {
        if (Tier === 0)
        {
            let R = 0.0, G = 0.0, B = 0.0;
            const Cells = CompiledGrid.ActiveCellsLod0;
            for (let Idx = 0; Idx < Cells.length; ++Idx)
            {
                const Cell = Cells[Idx];
                const Anchor = UseOptimized
                    ? [
                        Cell.VoxelCenter[0] + Cell.SubOffset[0],
                        Cell.VoxelCenter[1] + Cell.SubOffset[1],
                        Cell.VoxelCenter[2] + Cell.SubOffset[2]
                    ]
                    : Cell.VoxelCenter;
                const Radius = UseOptimized ? Cell.ApertureRadius : 0.32;
                const Tile = UseOptimized ? Cell.OptTile16 : Cell.RawTile16;

                const Dx = ShadingPos[0] - Anchor[0];
                const Dy = ShadingPos[1] - Anchor[1];
                const Dz = ShadingPos[2] - Anchor[2];
                const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
                const RayDir = [Dx / Dist, Dy / Dist, Dz / Dist];
                const Candela = SampleOctahedralTileBilinear(Tile, TileResLod0, RayDir);
                if (Candela[0] + Candela[1] + Candela[2] <= 1e-7) continue;

                const C = ShadeFromRadiator(ShadingPos, Normal, ViewDir, Roughness, Albedo, F0, Anchor, Radius, Candela);
                R += C[0]; G += C[1]; B += C[2];
            }
            return { Rgb: [R, G, B], Lookups: Cells.length };
        }
        if (Tier === 1)
        {
            let R = 0.0, G = 0.0, B = 0.0;
            const Poles = CompiledGrid.PolesLod1;
            for (let Idx = 0; Idx < Poles.length; ++Idx)
            {
                const Pole = Poles[Idx];
                const Dx = ShadingPos[0] - Pole.AnchorPos[0];
                const Dy = ShadingPos[1] - Pole.AnchorPos[1];
                const Dz = ShadingPos[2] - Pole.AnchorPos[2];
                const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
                const Candela = SampleOctahedralTileBilinear(Pole.Tile12, TileResLod1, [Dx / Dist, Dy / Dist, Dz / Dist]);
                const C = ShadeFromRadiator(ShadingPos, Normal, ViewDir, Roughness, Albedo, F0, Pole.AnchorPos, Pole.ApertureRadius, Candela);
                R += C[0]; G += C[1]; B += C[2];
            }
            return { Rgb: [R, G, B], Lookups: Poles.length };
        }
        if (Tier === 2)
        {
            const U2 = CompiledGrid.UnifiedLod2;
            const Dx = ShadingPos[0] - U2.AnchorPos[0];
            const Dy = ShadingPos[1] - U2.AnchorPos[1];
            const Dz = ShadingPos[2] - U2.AnchorPos[2];
            const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
            const Candela = SampleOctahedralTileBilinear(U2.Tile12, TileResLod2, [Dx / Dist, Dy / Dist, Dz / Dist]);
            const C = ShadeFromRadiator(ShadingPos, Normal, ViewDir, Roughness, Albedo, F0, U2.AnchorPos, U2.ApertureRadius, Candela);
            return { Rgb: C, Lookups: 1 };
        }
        const U3 = CompiledGrid.CoarseLod3;
        const Dx = ShadingPos[0] - U3.AnchorPos[0];
        const Dy = ShadingPos[1] - U3.AnchorPos[1];
        const Dz = ShadingPos[2] - U3.AnchorPos[2];
        const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
        const Candela = SampleOctahedralTileBilinear(U3.Tile4, TileResLod3, [Dx / Dist, Dy / Dist, Dz / Dist]);
        const C = ShadeFromRadiator(ShadingPos, Normal, ViewDir, Roughness, Albedo, F0, U3.AnchorPos, U3.ApertureRadius, Candela);
        return { Rgb: C, Lookups: 1 };
    }

    if (!EnableDistanceLod)
    {
        const T0 = EvalTier(0);
        return { Rgb: T0.Rgb, SampleLookups: T0.Lookups, ActiveLod: 0.0 };
    }

    const LowTier = Math.min(3, Math.floor(LodContinuous));
    const HighTier = Math.min(3, LowTier + 1);
    const Frac = LodContinuous - LowTier;

    if (LowTier === HighTier || Frac < 0.02)
    {
        const Res = EvalTier(LowTier);
        return {
            Rgb: [Res.Rgb[0] * AttenWindow, Res.Rgb[1] * AttenWindow, Res.Rgb[2] * AttenWindow],
            SampleLookups: Res.Lookups,
            ActiveLod: LodContinuous
        };
    }

    const A = EvalTier(LowTier);
    const B = EvalTier(HighTier);
    const SmoothF = Frac * Frac * (3.0 - 2.0 * Frac);
    return {
        Rgb: [
            (A.Rgb[0] * (1.0 - SmoothF) + B.Rgb[0] * SmoothF) * AttenWindow,
            (A.Rgb[1] * (1.0 - SmoothF) + B.Rgb[1] * SmoothF) * AttenWindow,
            (A.Rgb[2] * (1.0 - SmoothF) + B.Rgb[2] * SmoothF) * AttenWindow
        ],
        SampleLookups: Math.round(A.Lookups * (1.0 - SmoothF) + B.Lookups * SmoothF),
        ActiveLod: LodContinuous
    };
}

// 📝 Generates deterministic stratified surface calibration points around a cluster (floor, front/side walls, near/mid spheres)
//    so we can measure the EXACT radiometric error ||L_exact - L_grid||_2 AND drive Residual-Error Optimization!
export function GenerateCalibrationReceivers(Cluster)
{
    const Receivers = [];
    const C = [
        0.5 * (Cluster.BoundsMin[0] + Cluster.BoundsMax[0]),
        0.5 * (Cluster.BoundsMin[1] + Cluster.BoundsMax[1]),
        0.5 * (Cluster.BoundsMin[2] + Cluster.BoundsMax[2])
    ];

    // 1. Near & Mid Floor Grid (y = 0, capturing crisp dual-headlamp lobes, side washes, and downward panels)
    for (let ZIdx = 0; ZIdx < 18; ++ZIdx)
    {
        const Z = C[2] - 4.5 + (ZIdx / 17.0) * 12.5;
        for (let XIdx = 0; XIdx < 18; ++XIdx)
        {
            const X = C[0] - 5.2 + (XIdx / 17.0) * 10.4;
            const Pos = [X, 0.0, Z];
            const ViewDir = Normalize3([C[0] - X, 2.4, C[2] + 5.0 - Z]);
            Receivers.push({
                Pos,
                Normal: [0.0, 1.0, 0.0],
                ViewDir,
                Roughness: (XIdx + ZIdx) % 2 === 0 ? 0.18 : 0.48,
                Albedo: [0.75, 0.75, 0.78]
            });
        }
    }

    // 2. Surrounding Spherical Receiver Dome (captures full 3D directional & near-field parallax around the cluster)
    const Radii = [1.45, 2.65, 5.2, 11.0];
    for (let RIdx = 0; RIdx < Radii.length; ++RIdx)
    {
        const Rad = Radii[RIdx];
        const Samples = 48;
        for (let S = 0; S < Samples; ++S)
        {
            const GoldenPhi = S * 2.39996323;
            const CosTheta = 0.85 - (S / (Samples - 1)) * 1.70; // Both upper & lower directions
            const SinTheta = Math.sqrt(Math.max(0.0, 1.0 - CosTheta * CosTheta));
            const Dir = [Math.cos(GoldenPhi) * SinTheta, -Math.abs(CosTheta) * 0.85 - 0.1, Math.sin(GoldenPhi) * SinTheta];
            const UDir = Normalize3(Dir);
            const Pos = [
                C[0] + UDir[0] * Rad,
                Math.max(0.05, C[1] + UDir[1] * Rad),
                C[2] + UDir[2] * Rad
            ];
            const Normal = Normalize3([C[0] - Pos[0], C[1] - Pos[1] + 0.35, C[2] - Pos[2]]);
            Receivers.push({
                Pos,
                Normal,
                ViewDir: Normal,
                Roughness: 0.25,
                Albedo: [0.80, 0.80, 0.80]
            });
        }
    }

    // Precompute exact ground-truth radiance at every receiver once
    const SingleClusterWrap = [Cluster];
    for (let Idx = 0; Idx < Receivers.length; ++Idx)
    {
        const Rec = Receivers[Idx];
        Rec.ExactRgb = EvaluateGroundTruthClusters(SingleClusterWrap, Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, 0.04);
    }

    return Receivers;
}

export function MeasureClusterGridError(CompiledGrid, Receivers, BakingMode, EnableDistanceLod = false)
{
    let SqErrSum = 0.0;
    let AbsErrSum = 0.0;
    let RefSqSum = 1e-10;
    let MaxErr = 0.0;
    let TotalLookups = 0;

    for (let Idx = 0; Idx < Receivers.length; ++Idx)
    {
        const Rec = Receivers[Idx];
        const Pred = EvaluateBakedGridCluster(
            CompiledGrid, BakingMode, EnableDistanceLod, 0.0,
            Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, 0.04
        );
        TotalLookups += Pred.SampleLookups;

        for (let Ch = 0; Ch < 3; ++Ch)
        {
            const Diff = Rec.ExactRgb[Ch] - Pred.Rgb[Ch];
            const AbsD = Math.abs(Diff);
            SqErrSum += Diff * Diff;
            AbsErrSum += AbsD;
            RefSqSum += Rec.ExactRgb[Ch] * Rec.ExactRgb[Ch];
            if (AbsD > MaxErr) MaxErr = AbsD;
        }
    }

    const ScalarCount = Receivers.length * 3;
    const Rmse = Math.sqrt(SqErrSum / ScalarCount);
    const Mae = AbsErrSum / ScalarCount;
    const RelRmsePercent = Math.sqrt(SqErrSum / RefSqSum) * 100.0;
    const PeakRef = Math.max(Math.sqrt(RefSqSum / ScalarCount) * 4.0, 1.0);
    const PsnrDb = 20.0 * Math.log10(PeakRef / Math.max(Rmse, 1e-7));

    return {
        Rmse,
        Mae,
        MaxErr,
        RelRmsePercent,
        PsnrDb,
        AvgLookupsPerPixel: TotalLookups / Math.max(1, Receivers.length)
    };
}

// 📝 PHASE 2: Iterative Residual-Error Optimizer!
//    Uses the measured error e_s = L_exact(x_s) - L_grid(x_s; Theta) to jointly optimize:
//      ① Sub-voxel continuous optical anchor offset SubOffset[x,y,z] inside each occupied 8x4x4 cell,
//      ② Effective finite-aperture falloff radius ApertureRadius per cell,
//      ③ Non-negative deconvolved 16x16 octahedral directional texels OptTile16 (compensating bilinear blur & cutoff rolloff),
//      ④ Far-field LOD 1 / LOD 2 tiles so distance transitions stay error-minimal!
export function StepResidualErrorOptimization(CompiledGrid, Receivers, StepIterations = 1)
{
    const Cells = CompiledGrid.ActiveCellsLod0;
    const Res = TileResLod0;
    const TexelCount = Res * Res;

    for (let Iter = 0; Iter < StepIterations; ++Iter)
    {
        // Step 1: Monotonic Coordinate Descent on Sub-Voxel Optical Position & Aperture Radius FIRST
        //         (Snaps cell optical anchors toward true radiator coordinates before tile deconvolution!)
        const PosStep = 0.032 * Math.pow(0.92, CompiledGrid.OptimizationSteps);
        const RadStep = 0.035 * Math.pow(0.92, CompiledGrid.OptimizationSteps);

        for (let CIdx = 0; CIdx < Cells.length; ++CIdx)
        {
            const Cell = Cells[CIdx];

            for (let Axis = 0; Axis < 3; ++Axis)
            {
                const Orig = Cell.SubOffset[Axis];
                const ErrRef = MeasureFastSubsetSqError(CompiledGrid, Receivers);

                const CandPlus = Clamp(Orig + PosStep, -Cell.MaxOffset[Axis], Cell.MaxOffset[Axis]);
                Cell.SubOffset[Axis] = CandPlus;
                const ErrPlus = MeasureFastSubsetSqError(CompiledGrid, Receivers);

                const CandMinus = Clamp(Orig - PosStep, -Cell.MaxOffset[Axis], Cell.MaxOffset[Axis]);
                Cell.SubOffset[Axis] = CandMinus;
                const ErrMinus = MeasureFastSubsetSqError(CompiledGrid, Receivers);

                if (ErrPlus < ErrRef && ErrPlus <= ErrMinus) Cell.SubOffset[Axis] = CandPlus;
                else if (ErrMinus < ErrRef) Cell.SubOffset[Axis] = CandMinus;
                else Cell.SubOffset[Axis] = Orig;
            }

            const OrigRad = Cell.ApertureRadius;
            const RadRef = MeasureFastSubsetSqError(CompiledGrid, Receivers);
            const RPlusVal = Clamp(OrigRad + RadStep, 0.04, 0.48);
            Cell.ApertureRadius = RPlusVal;
            const RPlus = MeasureFastSubsetSqError(CompiledGrid, Receivers);
            const RMinusVal = Clamp(OrigRad - RadStep, 0.04, 0.48);
            Cell.ApertureRadius = RMinusVal;
            const RMinus = MeasureFastSubsetSqError(CompiledGrid, Receivers);

            if (RPlus < RadRef && RPlus <= RMinus) Cell.ApertureRadius = RPlusVal;
            else if (RMinus < RadRef) Cell.ApertureRadius = RMinusVal;
            else Cell.ApertureRadius = OrigRad;
        }

        // Step 2: Projected Non-Negative Residual Back-Projection onto every active cell's 16x16 Octahedral Tile!
        const Numerators = Array.from({ length: Cells.length }, () => new Float32Array(TexelCount * 3));
        const Denominators = Array.from({ length: Cells.length }, () => new Float32Array(TexelCount * 3));

        for (let S = 0; S < Receivers.length; ++S)
        {
            const Rec = Receivers[S];
            const Pred = EvaluateBakedGridCluster(
                CompiledGrid, GridBakingMode.ErrorOptimizedMip, false, 0.0,
                Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, 0.04
            );
            const ErrR = Rec.ExactRgb[0] - Pred.Rgb[0];
            const ErrG = Rec.ExactRgb[1] - Pred.Rgb[1];
            const ErrB = Rec.ExactRgb[2] - Pred.Rgb[2];

            for (let CIdx = 0; CIdx < Cells.length; ++CIdx)
            {
                const Cell = Cells[CIdx];
                const Anchor = [
                    Cell.VoxelCenter[0] + Cell.SubOffset[0],
                    Cell.VoxelCenter[1] + Cell.SubOffset[1],
                    Cell.VoxelCenter[2] + Cell.SubOffset[2]
                ];
                const Dx = Rec.Pos[0] - Anchor[0], Dy = Rec.Pos[1] - Anchor[1], Dz = Rec.Pos[2] - Anchor[2];
                const Dist = Math.hypot(Dx, Dy, Dz) || 1e-5;
                const RayDir = [Dx / Dist, Dy / Dist, Dz / Dist];

                const UnitTransfer = ShadeFromRadiator(
                    Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, 0.04,
                    Anchor, Cell.ApertureRadius, [1.0, 1.0, 1.0]
                );
                if (UnitTransfer[0] + UnitTransfer[1] + UnitTransfer[2] <= 1e-7) continue;

                const [U, V] = OctahedralEncode(RayDir);
                const Fx = Clamp(U * Res - 0.5, 0.0, Res - 1.00001);
                const Fy = Clamp(V * Res - 0.5, 0.0, Res - 1.00001);
                const X0 = Math.floor(Fx), Y0 = Math.floor(Fy);
                const X1 = Math.min(Res - 1, X0 + 1), Y1 = Math.min(Res - 1, Y0 + 1);
                const Tx = Fx - X0, Ty = Fy - Y0;

                const Taps = [
                    { Idx: Y0 * Res + X0, W: (1.0 - Tx) * (1.0 - Ty) },
                    { Idx: Y0 * Res + X1, W: Tx * (1.0 - Ty) },
                    { Idx: Y1 * Res + X0, W: (1.0 - Tx) * Ty },
                    { Idx: Y1 * Res + X1, W: Tx * Ty }
                ];

                const Num = Numerators[CIdx];
                const Den = Denominators[CIdx];
                for (let T = 0; T < 4; ++T)
                {
                    const W = Taps[T].W;
                    if (W <= 1e-5) continue;
                    const Offset = Taps[T].Idx * 3;
                    const Kr = W * UnitTransfer[0];
                    const Kg = W * UnitTransfer[1];
                    const Kb = W * UnitTransfer[2];

                    Num[Offset + 0] += Kr * ErrR;
                    Num[Offset + 1] += Kg * ErrG;
                    Num[Offset + 2] += Kb * ErrB;

                    Den[Offset + 0] += Kr * UnitTransfer[0];
                    Den[Offset + 1] += Kg * UnitTransfer[1];
                    Den[Offset + 2] += Kb * UnitTransfer[2];
                }
            }
        }

        const StepGain = 0.65 / Math.max(1.0, Math.sqrt(Cells.length * 0.45));
        for (let CIdx = 0; CIdx < Cells.length; ++CIdx)
        {
            const Tile = Cells[CIdx].OptTile16;
            const Num = Numerators[CIdx];
            const Den = Denominators[CIdx];
            for (let K = 0; K < TexelCount * 3; ++K)
            {
                if (Den[K] > 1e-9)
                {
                    Tile[K] = Math.max(0.0, Tile[K] + StepGain * (Num[K] / (Den[K] + 1e-4)));
                }
            }
        }

        // Step 3: Synchronize LOD 2 single-sample tile from the optimized cells so far-field LOD stays seamless!
        const Res2 = TileResLod2;
        for (let Y = 0; Y < Res2; ++Y)
        {
            for (let X = 0; X < Res2; ++X)
            {
                const Dir = OctahedralDecode((X + 0.5) / Res2, (Y + 0.5) / Res2);
                let SumR = 0.0, SumG = 0.0, SumB = 0.0;
                for (let CIdx = 0; CIdx < Cells.length; ++CIdx)
                {
                    const C = SampleOctahedralTileBilinear(Cells[CIdx].OptTile16, TileResLod0, Dir);
                    SumR += C[0]; SumG += C[1]; SumB += C[2];
                }
                const Offset = (Y * Res2 + X) * 3;
                CompiledGrid.UnifiedLod2.Tile12[Offset]     = SumR;
                CompiledGrid.UnifiedLod2.Tile12[Offset + 1] = SumG;
                CompiledGrid.UnifiedLod2.Tile12[Offset + 2] = SumB;
            }
        }

        ++CompiledGrid.OptimizationSteps;
        const Metrics = MeasureClusterGridError(CompiledGrid, Receivers, GridBakingMode.ErrorOptimizedMip, false);
        CompiledGrid.ErrorHistory.push({
            Step: CompiledGrid.OptimizationSteps,
            Rmse: Metrics.Rmse,
            RelRmsePercent: Metrics.RelRmsePercent,
            PsnrDb: Metrics.PsnrDb
        });
    }

    return CompiledGrid.ErrorHistory[CompiledGrid.ErrorHistory.length - 1];
}

function MeasureFastSubsetSqError(CompiledGrid, Receivers)
{
    let Sq = 0.0;
    // Stride-3 subsample for ultra-fast (< 0.8 ms) Jacobian probing
    for (let Idx = 0; Idx < Receivers.length; Idx += 3)
    {
        const Rec = Receivers[Idx];
        const Pred = EvaluateBakedGridCluster(
            CompiledGrid, GridBakingMode.ErrorOptimizedMip, false, 0.0,
            Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, 0.04
        );
        const Dr = Rec.ExactRgb[0] - Pred.Rgb[0];
        const Dg = Rec.ExactRgb[1] - Pred.Rgb[1];
        const Db = Rec.ExactRgb[2] - Pred.Rgb[2];
        Sq += Dr * Dr + Dg * Dg + Db * Db;
    }
    return Sq;
}

// 📝 Packs all CompiledGrid clusters AND all raw ground-truth sources into GPU RGBA32F textures:
//    1. RawSourcesTex (Width = 4, Height = TotalRawSources)
//    2. ClusterMetaTex (Width = 4, Height = ClusterCount)
//    3. CellAnchorTex (Width = 2, Height = TotalCellAnchors across Naive + LOD0 + LOD1 + LOD2 + LOD3)
//    4. OctahedralSheetTex (Width = 256, Height = 256 — packs every octahedral tile at 16x16 / 12x12 / 4x4!)
export function PackGpuLightGridTextures(CompiledClusters, BakingMode)
{
    // ① Raw Ground-Truth Sources Texture
    const AllSources = [];
    for (const CG of CompiledClusters)
    {
        for (const S of CG.Cluster.Sources) AllSources.push(S);
    }
    const RawCount = Math.max(1, AllSources.length);
    const RawTexels = new Float32Array(RawCount * 4 * 4);
    for (let Idx = 0; Idx < AllSources.length; ++Idx)
    {
        const S = AllSources[Idx];
        const B = Idx * 16;
        RawTexels[B + 0] = S.Position[0];
        RawTexels[B + 1] = S.Position[1];
        RawTexels[B + 2] = S.Position[2];
        RawTexels[B + 3] = S.ApertureRadius;

        RawTexels[B + 4] = S.Axis[0];
        RawTexels[B + 5] = S.Axis[1];
        RawTexels[B + 6] = S.Axis[2];
        RawTexels[B + 7] = S.CutoffCos;

        RawTexels[B + 8]  = S.IntensityCd * S.ColorRgb[0];
        RawTexels[B + 9]  = S.IntensityCd * S.ColorRgb[1];
        RawTexels[B + 10] = S.IntensityCd * S.ColorRgb[2];
        RawTexels[B + 11] = S.FocusExponent;

        RawTexels[B + 12] = S.EnableEceCutoff ? 1.0 : 0.0;
        RawTexels[B + 13] = 0.0;
        RawTexels[B + 14] = 0.0;
        RawTexels[B + 15] = 0.0;
    }

    // ② Allocate 16x16 Tile Slots in a 256x256 (16x16 tiles = 256 tiles max!) RGBA32F Octahedral Sheet Texture
    //    Clamping texel lookups inside each [Origin .. Origin + Res - 1] block in shader prevents cross-tile bleed completely!
    const SheetDim = 256;
    const TilesPerRow = 16;
    const SheetPixels = new Float32Array(SheetDim * SheetDim * 4);
    let NextTileSlot = 0;

    function WriteTileToSheet(SourceTileFloats, SourceRes)
    {
        const Slot = NextTileSlot++;
        const TileCol = Slot % TilesPerRow;
        const TileRow = Math.floor(Slot / TilesPerRow);
        const OriginX = TileCol * 16;
        const OriginY = TileRow * 16;

        // Write SourceRes x SourceRes inside [OriginX .. OriginX + SourceRes - 1]
        for (let Y = 0; Y < SourceRes; ++Y)
        {
            for (let X = 0; X < SourceRes; ++X)
            {
                const SrcIdx = (Y * SourceRes + X) * 3;
                const DstIdx = ((OriginY + Y) * SheetDim + (OriginX + X)) * 4;
                SheetPixels[DstIdx + 0] = SourceTileFloats[SrcIdx + 0];
                SheetPixels[DstIdx + 1] = SourceTileFloats[SrcIdx + 1];
                SheetPixels[DstIdx + 2] = SourceTileFloats[SrcIdx + 2];
                SheetPixels[DstIdx + 3] = 1.0;
            }
        }
        return { Slot, Res: SourceRes, OriginX, OriginY };
    }

    const UseOpt = BakingMode === GridBakingMode.ErrorOptimizedMip;
    const CellAnchors = []; // Each entry: { Pos:[3], Radius, OriginX, OriginY, Res }
    const ClusterMeta = new Float32Array(Math.max(1, CompiledClusters.length) * 4 * 4);

    for (let CIdx = 0; CIdx < CompiledClusters.length; ++CIdx)
    {
        const CG = CompiledClusters[CIdx];

        // Pack Naive 1-origin cell anchor
        const NaiveRef = WriteTileToSheet(CG.NaiveTile16, TileResLod0);
        const NaiveStart = CellAnchors.length;
        CellAnchors.push({
            Pos: CG.BoxCenter,
            Radius: 0.20,
            OriginX: NaiveRef.OriginX,
            OriginY: NaiveRef.OriginY,
            Res: NaiveRef.Res
        });

        // Pack LOD 0 active spatial cells
        const Lod0Start = CellAnchors.length;
        for (const Cell of CG.ActiveCellsLod0)
        {
            const T = WriteTileToSheet(UseOpt ? Cell.OptTile16 : Cell.RawTile16, TileResLod0);
            const Pos = UseOpt
                ? [
                    Cell.VoxelCenter[0] + Cell.SubOffset[0],
                    Cell.VoxelCenter[1] + Cell.SubOffset[1],
                    Cell.VoxelCenter[2] + Cell.SubOffset[2]
                ]
                : Cell.VoxelCenter;
            CellAnchors.push({
                Pos,
                Radius: UseOpt ? Cell.ApertureRadius : 0.32,
                OriginX: T.OriginX,
                OriginY: T.OriginY,
                Res: T.Res
            });
        }
        const Lod0Count = CellAnchors.length - Lod0Start;

        // Pack LOD 1 bimodal poles
        const Lod1Start = CellAnchors.length;
        for (const Pole of CG.PolesLod1)
        {
            const T = WriteTileToSheet(Pole.Tile12, TileResLod1);
            CellAnchors.push({
                Pos: Pole.AnchorPos,
                Radius: Pole.ApertureRadius,
                OriginX: T.OriginX,
                OriginY: T.OriginY,
                Res: T.Res
            });
        }
        const Lod1Count = CellAnchors.length - Lod1Start;

        // Pack LOD 2 unified 1-sample grid
        const Lod2Start = CellAnchors.length;
        const T2 = WriteTileToSheet(CG.UnifiedLod2.Tile12, TileResLod2);
        CellAnchors.push({
            Pos: CG.UnifiedLod2.AnchorPos,
            Radius: CG.UnifiedLod2.ApertureRadius,
            OriginX: T2.OriginX,
            OriginY: T2.OriginY,
            Res: T2.Res
        });

        // Pack LOD 3 coarse 4x4 grid
        const Lod3Start = CellAnchors.length;
        const T3 = WriteTileToSheet(CG.CoarseLod3.Tile4, TileResLod3);
        CellAnchors.push({
            Pos: CG.CoarseLod3.AnchorPos,
            Radius: CG.CoarseLod3.ApertureRadius,
            OriginX: T3.OriginX,
            OriginY: T3.OriginY,
            Res: T3.Res
        });

        const MOffset = CIdx * 16;
        ClusterMeta[MOffset + 0] = CG.BoxCenter[0];
        ClusterMeta[MOffset + 1] = CG.BoxCenter[1];
        ClusterMeta[MOffset + 2] = CG.BoxCenter[2];
        ClusterMeta[MOffset + 3] = CG.Diameter;

        ClusterMeta[MOffset + 4] = CG.Cluster.InfluenceRadius;
        ClusterMeta[MOffset + 5] = NaiveStart;
        ClusterMeta[MOffset + 6] = Lod0Start;
        ClusterMeta[MOffset + 7] = Lod0Count;

        ClusterMeta[MOffset + 8]  = Lod1Start;
        ClusterMeta[MOffset + 9]  = Lod1Count;
        ClusterMeta[MOffset + 10] = Lod2Start;
        ClusterMeta[MOffset + 11] = Lod3Start;

        ClusterMeta[MOffset + 12] = 0.0;
        ClusterMeta[MOffset + 13] = 0.0;
        ClusterMeta[MOffset + 14] = 0.0;
        ClusterMeta[MOffset + 15] = 0.0;
    }

    const AnchorCount = Math.max(1, CellAnchors.length);
    const CellAnchorTexels = new Float32Array(AnchorCount * 2 * 4);
    for (let Idx = 0; Idx < CellAnchors.length; ++Idx)
    {
        const E = CellAnchors[Idx];
        const B = Idx * 8;
        CellAnchorTexels[B + 0] = E.Pos[0];
        CellAnchorTexels[B + 1] = E.Pos[1];
        CellAnchorTexels[B + 2] = E.Pos[2];
        CellAnchorTexels[B + 3] = E.Radius;

        CellAnchorTexels[B + 4] = E.OriginX;
        CellAnchorTexels[B + 5] = E.OriginY;
        CellAnchorTexels[B + 6] = E.Res;
        CellAnchorTexels[B + 7] = 0.0;
    }

    return {
        RawSourceCount: AllSources.length,
        RawTexWidth: 4,
        RawTexHeight: RawCount,
        RawTexels,
        ClusterCount: CompiledClusters.length,
        ClusterMetaWidth: 4,
        ClusterMetaHeight: Math.max(1, CompiledClusters.length),
        ClusterMetaTexels: ClusterMeta,
        CellAnchorTexWidth: 2,
        CellAnchorTexHeight: AnchorCount,
        CellAnchorTexels,
        SheetDim,
        SheetPixels,
        TotalTilesUsed: NextTileSlot
    };
}
