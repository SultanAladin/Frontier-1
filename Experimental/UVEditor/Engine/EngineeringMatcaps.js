/*==========================================================================================================================================
                                                        ENGINEERINGMATCAPS.JS
==========================================================================================================================================*/
// Procedural CAD / Automotive / Composite / Textile MatCap Synthesizer (16-Slot 4x4 Studio Library):
//   0..7  : Precision Engineering & Class-A CAD Finishes (CNC Alu, Grade-5 Ti, CAD Clay, Chrome, Gunmetal, Steel Blue, Brass, Zebra)
//   8     : Vulcanized Rubber (Oren-Nayar matte elastomer + micro-grain + soft grazing elastomer Fresnel)
//   9     : Normal Check (View-space RGB Normal diagnostic sphere for bevel / continuity inspection)
//   10..11: Metallic Car Flakes (3D Voronoi multi-angle sparkling metallic micro-flakes + flip-flop basecoat + gloss clearcoat)
//   12    : Carbon Fibre (Procedurally baked 2x2 twill woven carbon tows + Kajiya-Kay anisotropic glint + wet epoxy clearcoat)
//   13..15: Technical & Luxury Fabrics (Woven Oxford Textile, Brushed Alcantara Velvet Suede, Tactical Ripstop Nylon)

export const MATCAP_STUDIO_CATALOG = [
    {
        slot: 0, id: 'cnc-alu', label: 'CNC Aluminum', short: 'CNC Alu', category: 'metal',
        kind: 'brushed-metal',
        skyTop: [214, 222, 232], skyHorizon: [148, 158, 170], groundDark: [38, 43, 50], groundBounce: [92, 100, 112],
        stripColor: [252, 254, 255], rimColor: [216, 228, 242],
        stripSharpness: 18.0, anisoX: 0.42, anisoY: 0.16, brushAmp: 0.055, fresnelGain: 0.38, horizonSharp: 11.0
    },
    {
        slot: 1, id: 'ti-grade5', label: 'Grade-5 Titanium', short: 'Titanium', category: 'metal',
        kind: 'brushed-metal',
        skyTop: [192, 188, 182], skyHorizon: [122, 119, 116], groundDark: [28, 28, 31], groundBounce: [76, 75, 78],
        stripColor: [248, 244, 236], rimColor: [188, 198, 212],
        stripSharpness: 24.0, anisoX: 0.36, anisoY: 0.13, brushAmp: 0.045, fresnelGain: 0.44, horizonSharp: 14.0
    },
    {
        slot: 2, id: 'cad-clay', label: 'CAD Clay', short: 'CAD Clay', category: 'diagnostic',
        kind: 'cad-clay',
        skyTop: [204, 200, 194], skyHorizon: [146, 143, 138], groundDark: [68, 66, 65], groundBounce: [112, 108, 104],
        stripColor: [236, 233, 228], rimColor: [164, 174, 186],
        stripSharpness: 5.5, anisoX: 0.55, anisoY: 0.45, brushAmp: 0.0, fresnelGain: 0.14, horizonSharp: 3.5
    },
    {
        slot: 3, id: 'studio-chrome', label: 'Class-A Chrome', short: 'Chrome', category: 'metal',
        kind: 'chrome',
        skyTop: [228, 236, 248], skyHorizon: [176, 192, 212], groundDark: [14, 17, 22], groundBounce: [68, 76, 88],
        stripColor: [255, 255, 255], rimColor: [226, 238, 255],
        stripSharpness: 52.0, anisoX: 0.28, anisoY: 0.075, brushAmp: 0.0, fresnelGain: 0.62, horizonSharp: 34.0
    },
    {
        slot: 4, id: 'gunmetal-ano', label: 'Gunmetal Ano', short: 'Gunmetal', category: 'metal',
        kind: 'anodized',
        skyTop: [86, 96, 110], skyHorizon: [48, 54, 64], groundDark: [15, 18, 23], groundBounce: [36, 42, 50],
        stripColor: [208, 224, 242], rimColor: [124, 152, 184],
        stripSharpness: 28.0, anisoX: 0.34, anisoY: 0.12, brushAmp: 0.025, fresnelGain: 0.46, horizonSharp: 16.0
    },
    {
        slot: 5, id: 'layout-blue', label: 'Steel Blue', short: 'Steel Blue', category: 'metal',
        kind: 'anodized',
        skyTop: [82, 126, 176], skyHorizon: [42, 74, 114], groundDark: [14, 24, 38], groundBounce: [34, 56, 86],
        stripColor: [226, 240, 255], rimColor: [132, 188, 244],
        stripSharpness: 30.0, anisoX: 0.32, anisoY: 0.11, brushAmp: 0.02, fresnelGain: 0.48, horizonSharp: 15.0
    },
    {
        slot: 6, id: 'cnc-brass', label: 'Turned Brass', short: 'Brass', category: 'metal',
        kind: 'brushed-metal',
        skyTop: [212, 184, 126], skyHorizon: [142, 116, 68], groundDark: [36, 27, 15], groundBounce: [92, 72, 42],
        stripColor: [255, 244, 214], rimColor: [236, 206, 146],
        stripSharpness: 26.0, anisoX: 0.38, anisoY: 0.12, brushAmp: 0.05, fresnelGain: 0.42, horizonSharp: 16.0
    },
    {
        slot: 7, id: 'cad-zebra', label: 'Zebra CAD', short: 'Zebra', category: 'diagnostic',
        kind: 'zebra',
        skyTop: [236, 240, 246], skyHorizon: [188, 196, 206], groundDark: [22, 26, 32], groundBounce: [52, 58, 68],
        stripColor: [255, 255, 255], rimColor: [204, 220, 240],
        stripSharpness: 32.0, anisoX: 0.30, anisoY: 0.10, brushAmp: 0.0, fresnelGain: 0.30, horizonSharp: 18.0
    },
    {
        slot: 8, id: 'vulcan-rubber', label: 'Vulcan Rubber', short: 'Rubber', category: 'polymer',
        kind: 'rubber',
        baseDark: [18, 20, 24], baseLit: [62, 67, 76], specTint: [168, 178, 194], rimColor: [112, 124, 142]
    },
    {
        slot: 9, id: 'normal-rgb', label: 'Normal Map', short: 'Normal', category: 'diagnostic',
        kind: 'normal'
    },
    {
        slot: 10, id: 'car-flake-blue', label: 'Car Flake Blue', short: 'Flake Blue', category: 'automotive',
        kind: 'car-flake',
        faceColor: [30, 118, 236], flopColor: [8, 22, 64], shadowColor: [5, 11, 30],
        flakeBright: [186, 232, 255], flakeWarm: [120, 185, 255], rimColor: [180, 218, 255], seed: 17.3
    },
    {
        slot: 11, id: 'car-flake-ruby', label: 'Car Flake Ruby', short: 'Flake Ruby', category: 'automotive',
        kind: 'car-flake',
        faceColor: [218, 32, 56], flopColor: [68, 8, 24], shadowColor: [24, 4, 10],
        flakeBright: [255, 214, 176], flakeWarm: [255, 118, 136], rimColor: [255, 196, 208], seed: 53.7
    },
    {
        slot: 12, id: 'carbon-fiber', label: 'Carbon Fibre', short: 'Carbon', category: 'composite',
        kind: 'carbon-fiber',
        towDark: [15, 17, 21], towSheen: [168, 178, 194], rimColor: [198, 214, 236]
    },
    {
        slot: 13, id: 'woven-fabric', label: 'Woven Fabric', short: 'Fabric', category: 'fabric',
        kind: 'fabric-woven',
        warpColor: [58, 74, 102], weftColor: [88, 96, 112], sheenColor: [192, 206, 228]
    },
    {
        slot: 14, id: 'velvet-suede', label: 'Velvet Suede', short: 'Velvet', category: 'fabric',
        kind: 'fabric-velvet',
        coreColor: [42, 34, 52], midColor: [84, 68, 102], sheenColor: [212, 188, 228]
    },
    {
        slot: 15, id: 'ripstop-fabric', label: 'Ripstop Nylon', short: 'Ripstop', category: 'fabric',
        kind: 'fabric-ripstop',
        baseColor: [56, 68, 60], ribColor: [76, 90, 80], sheenColor: [178, 196, 182]
    }
];

export const MATCAP_COUNT = MATCAP_STUDIO_CATALOG.length;

function SmoothStep01(Edge0, Edge1, X)
{
    const T = Math.max(0, Math.min(1, (X - Edge0) / (Edge1 - Edge0)));
    return T * T * (3 - 2 * T);
}

function ClampByte(V)
{
    return V <= 0 ? 0 : (V >= 255 ? 255 : V);
}

// Fast deterministic 3D integer hash -> [0, 1) for procedural micro-flakes & elastomer grain
function Hash31(Ix, Iy, Iz, Seed = 0)
{
    let N = Math.imul(Ix, 374761393) + Math.imul(Iy, 668265263) + Math.imul(Iz, 2147483647) + Math.imul(Seed | 0, 1597334677);
    N = (N ^ (N >>> 13)) | 0;
    N = Math.imul(N, 1274126177);
    return ((N ^ (N >>> 16)) >>> 0) / 4294967296;
}

function ValueNoise2D(U, V, Seed = 0)
{
    const Ix = Math.floor(U), Iy = Math.floor(V);
    const Fx = U - Ix, Fy = V - Iy;
    const Sx = Fx * Fx * (3 - 2 * Fx);
    const Sy = Fy * Fy * (3 - 2 * Fy);
    const N00 = Hash31(Ix, Iy, 11, Seed);
    const N10 = Hash31(Ix + 1, Iy, 11, Seed);
    const N01 = Hash31(Ix, Iy + 1, 11, Seed);
    const N11 = Hash31(Ix + 1, Iy + 1, 11, Seed);
    const Nx0 = N00 + (N10 - N00) * Sx;
    const Nx1 = N01 + (N11 - N01) * Sx;
    return Nx0 + (Nx1 - Nx0) * Sy;
}

function FastPow4(X) { const X2 = X * X; return X2 * X2; }
function FastPow8(X) { const X2 = X * X, X4 = X2 * X2; return X4 * X4; }
function FastPow16(X) { const X2 = X * X, X4 = X2 * X2, X8 = X4 * X4; return X8 * X8; }

// Evaluate clearcoat dual studio softbox reflection for automotive paint & epoxy composites (zero Math.pow!)
function EvaluateClearcoatSoftbox(Rx, Ry, Nz)
{
    const Au1 = Math.abs((Rx * 0.93 + (Ry - 0.48) * 0.36) / 0.27);
    const Av1 = Math.abs((-Rx * 0.36 + (Ry - 0.48) * 0.93) / 0.082);
    const Strip1 = Math.exp(-0.5 * (Au1 * Au1 * Au1 + Av1 * Av1 * Math.sqrt(Av1)));

    const Au2 = Math.abs(((Rx - 0.46) * 0.86 - (Ry - 0.16) * 0.51) / 0.20);
    const Av2 = Math.abs(((Rx - 0.46) * 0.51 + (Ry - 0.16) * 0.86) / 0.085);
    const Strip2 = Math.exp(-0.5 * (Au2 * Au2 * Au2 + Av2 * Av2 * Math.sqrt(Av2))) * 0.56;

    const OmNz = 1.0 - Nz;
    const ClearFresnel = 0.04 + 0.96 * FastPow4(OmNz) * OmNz;
    return { strip: Strip1 * 0.95 + Strip2, fresnel: ClearFresnel };
}

export function SynthesizeMatcapPixels(Spec, CellSize, TargetBuffer = null)
{
    const Data = TargetBuffer || new Uint8ClampedArray(CellSize * CellSize * 4);
    const Kx = -0.36, Ky = 0.64, Kz = 0.677;
    const Fx = 0.58, Fy = -0.28, Fz = 0.762;
    const Hx = -0.205, Hy = 0.365, Hz = 0.908; // Half-vector between Key (-0.36,0.64,0.677) and View (0,0,1)

    for (let y = 0; y < CellSize; y++)
    {
        const Ny = 1 - (2 * (y + 0.5)) / CellSize;
        for (let x = 0; x < CellSize; x++)
        {
            const Nx = (2 * (x + 0.5)) / CellSize - 1;
            const R2 = Nx * Nx + Ny * Ny;
            const ClampR2 = Math.min(1.0, R2);
            const Nz = Math.sqrt(Math.max(0.0, 1.0 - ClampR2));
            const Rn = R2 > 1.0 ? 1.0 / Math.sqrt(R2) : 1.0;
            const Sx = Nx * Rn, Sy = Ny * Rn;
            const idx = (y * CellSize + x) * 4;

            // Exact view reflection vector R = reflect((0,0,-1), N) = (2*Nx*Nz, 2*Ny*Nz, 2*Nz^2 - 1)
            const Rx = 2.0 * Sx * Nz;
            const Ry = 2.0 * Sy * Nz;

            // 1. View-Space RGB Normal Diagnostic MatCap
            if (Spec.kind === 'normal')
            {
                const EdgeBevel = 0.88 + 0.12 * SmoothStep01(0.0, 0.08, Nz);
                Data[idx]     = ClampByte((Sx * 0.5 + 0.5) * 255.0 * EdgeBevel);
                Data[idx + 1] = ClampByte((Sy * 0.5 + 0.5) * 255.0 * EdgeBevel);
                Data[idx + 2] = ClampByte((Nz * 0.5 + 0.5) * 255.0 * EdgeBevel);
                Data[idx + 3] = 255;
                continue;
            }

            // 2. Vulcanized Tire / Matte Elastomer Rubber MatCap
            if (Spec.kind === 'rubber')
            {
                const NdotK = Math.max(0.0, Sx * Kx + Sy * Ky + Nz * Kz);
                const Wrap = Math.max(0.0, Math.min(1.0, (Sx * Kx + Sy * Ky + Nz * Kz + 0.52) / 1.52));
                // Oren-Nayar style flattened diffuse response for high-roughness vulcanized polymer
                const OrenDiff = Math.pow(Wrap, 0.78) * (0.86 + 0.14 * (1.0 - Nz * 0.5));
                const Fill = Math.max(0.0, Sx * Fx + Sy * Fy + Nz * Fz) * 0.24;

                // Subtle multi-octave vulcanized elastomer micro-grain
                const U = Sx / (1.0 + 0.30 * Nz) * 44.0;
                const V = Sy / (1.0 + 0.30 * Nz) * 44.0;
                const Grain = (ValueNoise2D(U, V, 91) * 0.65 + ValueNoise2D(U * 2.1, V * 2.1, 193) * 0.35 - 0.5) * 0.085;

                // Broad satin-matte elastomer specular lobe + grazing rim
                const NdotH = Math.max(0.0, Sx * Hx + Sy * Hy + Nz * Hz);
                const BroadSpec = Math.pow(NdotH, 6.5) * (0.16 + Grain * 0.35) * (0.35 + 0.65 * NdotK);
                const Rim = Math.pow(1.0 - Nz, 2.7) * 0.20 * SmoothStep01(0.0, 0.12, Nz);
                const Silhouette = 0.80 + 0.20 * SmoothStep01(0.0, 0.18, Nz);

                const DiffMix = Math.max(0.0, Math.min(1.0, OrenDiff * 0.82 + Fill + Grain));
                const R = (Spec.baseDark[0] + (Spec.baseLit[0] - Spec.baseDark[0]) * DiffMix + Spec.specTint[0] * BroadSpec + Spec.rimColor[0] * Rim) * Silhouette;
                const G = (Spec.baseDark[1] + (Spec.baseLit[1] - Spec.baseDark[1]) * DiffMix + Spec.specTint[1] * BroadSpec + Spec.rimColor[1] * Rim) * Silhouette;
                const B = (Spec.baseDark[2] + (Spec.baseLit[2] - Spec.baseDark[2]) * DiffMix + Spec.specTint[2] * BroadSpec + Spec.rimColor[2] * Rim) * Silhouette;

                Data[idx]     = ClampByte(R);
                Data[idx + 1] = ClampByte(G);
                Data[idx + 2] = ClampByte(B);
                Data[idx + 3] = 255;
                continue;
            }

            // 3. Procedurally Baked Metallic Car Paint (3D Voronoi Sparkling Micro-Flakes + Clearcoat)
            if (Spec.kind === 'car-flake')
            {
                const Seed = Math.round((Spec.seed || 17.0) * 10);
                // Angle-dependent pearlescent basecoat flop (bright face-on -> deep rich flop at grazing angle)
                const FlopT = Math.pow(Nz, 1.35);
                const KeyLit = Math.max(0.0, Math.min(1.0, (Sx * Kx + Sy * Ky + Nz * Kz + 0.28) / 1.28));
                const BaseR = Spec.shadowColor[0] + ((Spec.flopColor[0] + (Spec.faceColor[0] - Spec.flopColor[0]) * FlopT) - Spec.shadowColor[0]) * (0.28 + 0.72 * KeyLit);
                const BaseG = Spec.shadowColor[1] + ((Spec.flopColor[1] + (Spec.faceColor[1] - Spec.flopColor[1]) * FlopT) - Spec.shadowColor[1]) * (0.28 + 0.72 * KeyLit);
                const BaseB = Spec.shadowColor[2] + ((Spec.flopColor[2] + (Spec.faceColor[2] - Spec.flopColor[2]) * FlopT) - Spec.shadowColor[2]) * (0.28 + 0.72 * KeyLit);

                // Two-layer cellular micro-flake field in low-distortion spherical chart space
                const Scale = 46.0;
                const Pu = Sx / (1.0 + 0.26 * Nz) * Scale;
                const Pv = Sy / (1.0 + 0.26 * Nz) * Scale;
                const CellX = Math.floor(Pu), CellY = Math.floor(Pv);
                let BestD2 = 99.0, BestIdX = CellX, BestIdY = CellY;
                for (let dy = -1; dy <= 1; dy++)
                {
                    for (let dx = -1; dx <= 1; dx++)
                    {
                        const Cx = CellX + dx, Cy = CellY + dy;
                        const Jx = Cx + 0.16 + 0.68 * Hash31(Cx, Cy, 1, Seed);
                        const Jy = Cy + 0.16 + 0.68 * Hash31(Cx, Cy, 2, Seed);
                        const D2 = (Pu - Jx) * (Pu - Jx) + (Pv - Jy) * (Pv - Jy);
                        if (D2 < BestD2) { BestD2 = D2; BestIdX = Cx; BestIdY = Cy; }
                    }
                }

                // Individual aluminum/mica micro-flake orientation normal & sparkle glint
                const TiltX = (Hash31(BestIdX, BestIdY, 3, Seed) - 0.5) * 0.68;
                const TiltY = (Hash31(BestIdX, BestIdY, 4, Seed) - 0.5) * 0.68;
                const FlakeTone = Hash31(BestIdX, BestIdY, 5, Seed);
                let Fnx = Sx + TiltX, Fny = Sy + TiltY, Fnz = Nz + 0.15;
                const FnLen = Math.hypot(Fnx, Fny, Fnz) || 1.0;
                Fnx /= FnLen; Fny /= FnLen; Fnz /= FnLen;

                const FlakeNdotH = Math.max(0.0, Fnx * Hx + Fny * Hy + Fnz * Hz);
                const FlakeCellMask = SmoothStep01(0.42, 0.08, BestD2);
                const fh8 = FastPow8(FlakeNdotH);
                const GlintSharp = fh8 * fh8 * fh8 * 1.45 + fh8 * 0.36;
                const Sparkle = GlintSharp * FlakeCellMask * (0.25 + 0.75 * KeyLit) * SmoothStep01(0.0, 0.22, Nz);

                const FlkColR = Spec.flakeWarm[0] + (Spec.flakeBright[0] - Spec.flakeWarm[0]) * FlakeTone;
                const FlkColG = Spec.flakeWarm[1] + (Spec.flakeBright[1] - Spec.flakeWarm[1]) * FlakeTone;
                const FlkColB = Spec.flakeWarm[2] + (Spec.flakeBright[2] - Spec.flakeWarm[2]) * FlakeTone;

                // Automotive Class-A Clearcoat layer over basecoat + flakes
                const Coat = EvaluateClearcoatSoftbox(Rx, Ry, Nz);
                const SkyCoat = SmoothStep01(-0.15, 0.85, Ry) * Coat.fresnel * 0.62;
                const Rim = FastPow4(1.0 - Nz) * 0.42 * SmoothStep01(0.0, 0.10, Nz);
                const EdgeDark = 0.82 + 0.18 * SmoothStep01(0.0, 0.12, Nz);

                const R = (BaseR + FlkColR * Sparkle + 255.0 * Coat.strip * 0.92 + Spec.rimColor[0] * (SkyCoat + Rim)) * EdgeDark;
                const G = (BaseG + FlkColG * Sparkle + 255.0 * Coat.strip * 0.92 + Spec.rimColor[1] * (SkyCoat + Rim)) * EdgeDark;
                const B = (BaseB + FlkColB * Sparkle + 255.0 * Coat.strip * 0.92 + Spec.rimColor[2] * (SkyCoat + Rim)) * EdgeDark;

                Data[idx]     = ClampByte(R);
                Data[idx + 1] = ClampByte(G);
                Data[idx + 2] = ClampByte(B);
                Data[idx + 3] = 255;
                continue;
            }

            // 4. Procedurally Baked 2x2 Twill Carbon Fibre + Wet Epoxy Clearcoat
            if (Spec.kind === 'carbon-fiber')
            {
                const TowFreq = 13.5;
                const U = Sx / (1.0 + 0.28 * Nz) * TowFreq + 8.0;
                const V = Sy / (1.0 + 0.28 * Nz) * TowFreq + 8.0;
                const Ix = Math.floor(U), Iy = Math.floor(V);
                const Fu = U - Ix, Fv = V - Iy;

                // 2x2 Twill weave: diagonal 2-over / 2-under stepping pattern
                const TwillStep = (((Ix + Iy) % 4) + 4) % 4;
                const IsWarpTop = TwillStep < 2; // Warp runs vertically (tangent along Y), Weft runs horizontally (tangent along X)
                const Across = IsWarpTop ? Fu : Fv;
                const Along = IsWarpTop ? Fv : Fu;

                // Cylindrical tow bundle profile + inter-tow gap + crimp dip at weave transitions
                const TowGap = SmoothStep01(0.0, 0.12, Across) * SmoothStep01(1.0, 0.88, Across);
                const CrimpPhase = IsWarpTop ? (((Iy + Ix) % 4 + 4) % 4) * 0.5 + Along * 0.5 : (((Ix + Iy - 2) % 4 + 4) % 4) * 0.5 + Along * 0.5;
                const CrimpLift = Math.sin(CrimpPhase * Math.PI);
                const Filaments = 0.90 + 0.10 * Math.sin(Across * Math.PI * 10.0);

                // Perturb tow surface normal across bundle curvature + crimp slope
                const SlopeAcross = Math.cos(Across * Math.PI) * 0.26;
                const SlopeAlong = Math.cos(CrimpPhase * Math.PI) * 0.14;
                let Tnx = Sx + (IsWarpTop ? SlopeAcross : SlopeAlong);
                let Tny = Sy + (IsWarpTop ? SlopeAlong : SlopeAcross);
                let Tnz = Nz;
                const TnLen = Math.hypot(Tnx, Tny, Tnz) || 1.0;
                Tnx /= TnLen; Tny /= TnLen; Tnz /= TnLen;

                // Orthogonalize fibre tangent T to sphere normal for Kajiya-Kay anisotropic sheen
                let Tx = IsWarpTop ? 0.0 : 1.0;
                let Ty = IsWarpTop ? 1.0 : 0.0;
                let Tz = 0.0;
                const TdotN = Tx * Tnx + Ty * Tny;
                Tx -= TdotN * Tnx; Ty -= TdotN * Tny; Tz -= TdotN * Tnz;
                const TLen = Math.hypot(Tx, Ty, Tz) || 1.0;
                Tx /= TLen; Ty /= TLen; Tz /= TLen;

                // Anisotropic carbon filament reflection against Key & Fill half-vectors
                const TdotH1 = Tx * Hx + Ty * Hy + Tz * Hz;
                const Sin2_1 = Math.max(0.0, 1.0 - TdotH1 * TdotH1);
                const TowNdotK = Math.max(0.0, Tnx * Kx + Tny * Ky + Tnz * Kz);
                const s4_1 = Sin2_1 * Sin2_1;
                const Aniso1 = s4_1 * s4_1 * Sin2_1 * (0.25 + 0.75 * TowNdotK);

                // Secondary studio fill anisotropic catch so crossing tows shimmer across opposite quadrants
                const H2x = 0.34, H2y = -0.16, H2z = 0.926;
                const TdotH2 = Tx * H2x + Ty * H2y + Tz * H2z;
                const Sin2_2 = Math.max(0.0, 1.0 - TdotH2 * TdotH2);
                const Aniso2 = FastPow8(Sin2_2) * 0.42;

                const TowShade = (Aniso1 * 0.92 + Aniso2) * TowGap * (0.72 + 0.28 * CrimpLift) * Filaments;

                // Wet gloss epoxy resin clearcoat over woven carbon
                const Coat = EvaluateClearcoatSoftbox(Rx, Ry, Nz);
                const Rim = FastPow4(1.0 - Nz) * 0.36 * SmoothStep01(0.0, 0.12, Nz);
                const EdgeDark = 0.82 + 0.18 * SmoothStep01(0.0, 0.13, Nz);
                const GapAO = 0.35 + 0.65 * TowGap;

                const R = (Spec.towDark[0] * GapAO + Spec.towSheen[0] * TowShade + 255.0 * Coat.strip * 0.88 + Spec.rimColor[0] * Rim) * EdgeDark;
                const G = (Spec.towDark[1] * GapAO + Spec.towSheen[1] * TowShade + 255.0 * Coat.strip * 0.88 + Spec.rimColor[1] * Rim) * EdgeDark;
                const B = (Spec.towDark[2] * GapAO + Spec.towSheen[2] * TowShade + 255.0 * Coat.strip * 0.88 + Spec.rimColor[2] * Rim) * EdgeDark;

                Data[idx]     = ClampByte(R);
                Data[idx + 1] = ClampByte(G);
                Data[idx + 2] = ClampByte(B);
                Data[idx + 3] = 255;
                continue;
            }

            // 5. Procedurally Baked Fabrics (Woven Textile, Brushed Alcantara Velvet, Tactical Ripstop Nylon)
            if (Spec.kind === 'fabric-woven' || Spec.kind === 'fabric-velvet' || Spec.kind === 'fabric-ripstop')
            {
                const KeyWrap = Math.max(0.0, Math.min(1.0, (Sx * Kx + Sy * Ky + Nz * Kz + 0.46) / 1.46));
                const FillWrap = Math.max(0.0, Sx * Fx + Sy * Fy + Nz * Fz) * 0.22;
                const Silhouette = 0.78 + 0.22 * SmoothStep01(0.0, 0.16, Nz);

                if (Spec.kind === 'fabric-velvet')
                {
                    // Luxury Alcantara / Microfiber Suede: combed fibrous nap + strong grazing backscatter halo
                    const U = Sx / (1.0 + 0.28 * Nz) * 56.0;
                    const V = Sy / (1.0 + 0.28 * Nz) * 56.0;
                    const Nap = (ValueNoise2D(U * 1.4, V * 0.65, 311) * 0.65 + ValueNoise2D(U * 2.8, V * 2.8, 577) * 0.35 - 0.5) * 0.12;
                    const Diff = Math.max(0.0, Math.min(1.0, Math.pow(KeyWrap, 1.15) * 0.78 + FillWrap + Nap));
                    // Charlie / Ashikhmin velvet grazing sheen
                    const GrazingVelvet = Math.pow(1.0 - Nz, 1.85) * (0.48 + 0.52 * KeyWrap) * SmoothStep01(0.0, 0.10, Nz);

                    const R = (Spec.coreColor[0] + (Spec.midColor[0] - Spec.coreColor[0]) * Diff + Spec.sheenColor[0] * GrazingVelvet * 0.72) * Silhouette;
                    const G = (Spec.coreColor[1] + (Spec.midColor[1] - Spec.coreColor[1]) * Diff + Spec.sheenColor[1] * GrazingVelvet * 0.72) * Silhouette;
                    const B = (Spec.coreColor[2] + (Spec.midColor[2] - Spec.coreColor[2]) * Diff + Spec.sheenColor[2] * GrazingVelvet * 0.72) * Silhouette;

                    Data[idx]     = ClampByte(R);
                    Data[idx + 1] = ClampByte(G);
                    Data[idx + 2] = ClampByte(B);
                    Data[idx + 3] = 255;
                    continue;
                }

                // Interlaced Woven Oxford / Ripstop Textile Yarn Grid
                const Freq = Spec.kind === 'fabric-ripstop' ? 26.0 : 21.0;
                const U = Sx / (1.0 + 0.28 * Nz) * Freq + 12.0;
                const V = Sy / (1.0 + 0.28 * Nz) * Freq + 12.0;
                const Ix = Math.floor(U), Iy = Math.floor(V);
                const Fu = U - Ix, Fv = V - Iy;
                const IsWarp = ((Ix + Iy) & 1) === 0;
                const Across = IsWarp ? Fu : Fv;
                const Along = IsWarp ? Fv : Fu;

                const ThreadArch = Math.sin(Across * Math.PI);
                const PlyTwist = 0.92 + 0.08 * Math.sin((Across * 2.5 + Along * 1.5) * Math.PI * 2.0);
                const GapAO = 0.52 + 0.48 * Math.pow(ThreadArch, 0.55);

                // Perturb thread micro-normal for tactile woven relief under studio key light
                const ThreadSlope = Math.cos(Across * Math.PI) * 0.24;
                const LocalKey = Math.max(0.0, (Sx + (IsWarp ? ThreadSlope : 0)) * Kx + (Sy + (IsWarp ? 0 : ThreadSlope)) * Ky + Nz * Kz);
                const ClothLight = (0.26 + 0.74 * (KeyWrap * 0.55 + LocalKey * 0.45) + FillWrap) * GapAO * PlyTwist;

                let BaseCol = IsWarp ? Spec.warpColor : Spec.weftColor;
                let RibBoost = 0.0;
                if (Spec.kind === 'fabric-ripstop')
                {
                    const IsRib = (Math.abs(Ix) % 5 === 0) || (Math.abs(Iy) % 5 === 0);
                    BaseCol = IsRib ? Spec.ribColor : Spec.baseColor;
                    RibBoost = IsRib ? 0.10 * LocalKey : 0.0;
                }

                // Soft microfiber cloth grazing fuzz + subtle nylon sheen for ripstop
                const FuzzPow = Spec.kind === 'fabric-ripstop' ? 2.6 : 2.15;
                const ClothSheen = Math.pow(1.0 - Nz, FuzzPow) * (0.36 + 0.64 * KeyWrap) * 0.52;
                const NylonLobe = Spec.kind === 'fabric-ripstop'
                    ? Math.pow(Math.max(0.0, Sx * Hx + Sy * Hy + Nz * Hz), 9.0) * 0.16 * GapAO
                    : 0.0;

                const R = (BaseCol[0] * ClothLight + Spec.sheenColor[0] * (ClothSheen + NylonLobe + RibBoost)) * Silhouette;
                const G = (BaseCol[1] * ClothLight + Spec.sheenColor[1] * (ClothSheen + NylonLobe + RibBoost)) * Silhouette;
                const B = (BaseCol[2] * ClothLight + Spec.sheenColor[2] * (ClothSheen + NylonLobe + RibBoost)) * Silhouette;

                Data[idx]     = ClampByte(R);
                Data[idx + 1] = ClampByte(G);
                Data[idx + 2] = ClampByte(B);
                Data[idx + 3] = 255;
                continue;
            }

            if (Spec.kind === 'zebra')
            {
                const Coord = (Rx * 0.76 + Ry * 0.65) * 6.5 + Math.atan2(Sy, Sx) * 0.18;
                const Wave = Math.sin(Coord * Math.PI);
                const Band = SmoothStep01(-0.14, 0.14, Wave);
                const Form = 0.72 + 0.28 * Math.max(0, Sx * Kx + Sy * Ky + Nz * Kz);
                const Rim = Math.pow(1.0 - Nz, 3.4) * 0.28;
                const DarkR = 24, DarkG = 28, DarkB = 34;
                const BrightR = 242, BrightG = 246, BrightB = 252;
                const EdgeContour = SmoothStep01(0.0, 0.14, Nz) * 0.18 + 0.82;
                Data[idx]     = Math.min(255, ((DarkR + (BrightR - DarkR) * Band) * Form + Spec.rimColor[0] * Rim) * EdgeContour);
                Data[idx + 1] = Math.min(255, ((DarkG + (BrightG - DarkG) * Band) * Form + Spec.rimColor[1] * Rim) * EdgeContour);
                Data[idx + 2] = Math.min(255, ((DarkB + (BrightB - DarkB) * Band) * Form + Spec.rimColor[2] * Rim) * EdgeContour);
                Data[idx + 3] = 255;
                continue;
            }

            if (Spec.kind === 'cad-clay')
            {
                const KeyWrap = Math.max(0, Math.min(1, (Sx * Kx + Sy * Ky + Nz * Kz + 0.48) / 1.48));
                const SkyHemi = Sy * 0.5 + 0.5;
                const Bounce = Math.max(0, Sx * Fx + Sy * Fy + Nz * Fz) * 0.22;
                const SoftSpec = Math.pow(Math.max(0, Sx * -0.22 + Sy * 0.44 + Nz * 0.87), 14.0) * 0.16;
                const Silhouette = 0.74 + 0.26 * SmoothStep01(0.0, 0.28, Nz);
                const Blend = KeyWrap * 0.72 + SkyHemi * 0.28;
                Data[idx]     = Math.min(255, (Spec.groundDark[0] + (Spec.skyTop[0] - Spec.groundDark[0]) * Blend + Spec.groundBounce[0] * Bounce + 255 * SoftSpec) * Silhouette);
                Data[idx + 1] = Math.min(255, (Spec.groundDark[1] + (Spec.skyTop[1] - Spec.groundDark[1]) * Blend + Spec.groundBounce[1] * Bounce + 252 * SoftSpec) * Silhouette);
                Data[idx + 2] = Math.min(255, (Spec.groundDark[2] + (Spec.skyTop[2] - Spec.groundDark[2]) * Blend + Spec.groundBounce[2] * Bounce + 248 * SoftSpec) * Silhouette);
                Data[idx + 3] = 255;
                continue;
            }

            const HorizonT = SmoothStep01(-1.0 / Spec.horizonSharp - 0.05, 1.0 / Spec.horizonSharp - 0.05, Ry);
            const SkyGrad = SmoothStep01(-0.05, 0.95, Ry);
            const FloorBounce = SmoothStep01(-0.18, -0.88, Ry);

            const EnvR = HorizonT * (Spec.skyHorizon[0] + (Spec.skyTop[0] - Spec.skyHorizon[0]) * SkyGrad)
                       + (1.0 - HorizonT) * (Spec.groundDark[0] + (Spec.groundBounce[0] - Spec.groundDark[0]) * FloorBounce);
            const EnvG = HorizonT * (Spec.skyHorizon[1] + (Spec.skyTop[1] - Spec.skyHorizon[1]) * SkyGrad)
                       + (1.0 - HorizonT) * (Spec.groundDark[1] + (Spec.groundBounce[1] - Spec.groundDark[1]) * FloorBounce);
            const EnvB = HorizonT * (Spec.skyHorizon[2] + (Spec.skyTop[2] - Spec.skyHorizon[2]) * SkyGrad)
                       + (1.0 - HorizonT) * (Spec.groundDark[2] + (Spec.groundBounce[2] - Spec.groundDark[2]) * FloorBounce);

            const Au1 = Math.abs((Rx * 0.93 + (Ry - 0.50) * 0.36) / Spec.anisoX);
            const Av1 = Math.abs((-Rx * 0.36 + (Ry - 0.50) * 0.93) / Spec.anisoY);
            const Strip1 = Math.exp(-0.5 * (Au1 * Au1 * Math.sqrt(Au1) + Av1 * Av1 * Math.sqrt(Av1)));

            const Au2 = Math.abs(((Rx - 0.46) * 0.86 - (Ry - 0.18) * 0.51) / (Spec.anisoX * 0.72));
            const Av2 = Math.abs(((Rx - 0.46) * 0.51 + (Ry - 0.18) * 0.86) / (Spec.anisoY * 0.95));
            const Strip2 = Math.exp(-0.5 * (Au2 * Au2 * Math.sqrt(Au2) + Av2 * Av2 * Math.sqrt(Av2))) * 0.58;

            let BrushMod = 1.0;
            if (Spec.brushAmp > 0.0)
            {
                const Radius = Math.sqrt(ClampR2);
                const Angle = Math.atan2(Sy, Sx);
                const FineRings = Math.sin(Radius * 68.0 + Math.sin(Angle * 4.0) * 1.4) * 0.5
                                + Math.sin(Angle * 36.0 + Radius * 12.0) * 0.5;
                BrushMod = 1.0 + FineRings * Spec.brushAmp * (0.35 + 0.65 * Radius);
            }

            const Fresnel = FastPow4(1.0 - Nz) * Spec.fresnelGain;
            const CadRimDark = 0.80 + 0.20 * SmoothStep01(0.0, 0.13, Nz);
            const StripSum = (Strip1 * 0.88 + Strip2) * (Spec.kind === 'chrome' ? 1.18 : 0.78);

            const OutR = (EnvR * BrushMod + Spec.stripColor[0] * StripSum + Spec.rimColor[0] * Fresnel) * CadRimDark;
            const OutG = (EnvG * BrushMod + Spec.stripColor[1] * StripSum + Spec.rimColor[1] * Fresnel) * CadRimDark;
            const OutB = (EnvB * BrushMod + Spec.stripColor[2] * StripSum + Spec.rimColor[2] * Fresnel) * CadRimDark;

            Data[idx]     = Math.max(0, Math.min(255, OutR));
            Data[idx + 1] = Math.max(0, Math.min(255, OutG));
            Data[idx + 2] = Math.max(0, Math.min(255, OutB));
            Data[idx + 3] = 255;
        }
    }
    return Data;
}

export function RenderMatcapSphereToImageData(Ctx, Spec, CellSize, OffsetX = 0, OffsetY = 0)
{
    const Img = Ctx.createImageData(CellSize, CellSize);
    SynthesizeMatcapPixels(Spec, CellSize, Img.data);
    Ctx.putImageData(Img, OffsetX, OffsetY);
}
