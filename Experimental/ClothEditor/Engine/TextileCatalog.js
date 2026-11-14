/*============================================================================================================================================
                                                            TEXTILECATALOG.JS
============================================================================================================================================*/
// 🧩 Measured physical textile specifications (GSM, thickness, Warp/Weft crimp slack, cubic lock wall, bias shear,
//    flexural bending rigidity, and Coulomb friction) for real-fabric tailoring and 1:1 pattern verification.

export const TEXTILE_CATALOG = {
    cottonPoplin: {
        key: 'cottonPoplin',
        name: 'Crisp Cotton Poplin',
        code: 'WOV-135',
        category: 'Plain Weave Woven',
        gsm: 135,                  // [g/m^2]
        thicknessMm: 0.34,         // [mm]
        warpModulus: 1850,         // [N/m]
        weftModulus: 1550,         // [N/m]
        shearModulus: 340,         // [N/m]
        warpCrimpPct: 2.0,         // [%] decrimp slack before taut thread lock
        weftCrimpPct: 2.8,         // [%]
        wallModulus: 14500,        // [N/m] cubic strain-limiting wall above crimp
        compressionRatio: 0.05,    // [-] soft buckling under compression (prevents membrane locking)
        bendRigidity: 0.42,        // [mN*m] crisp shirt/dress fold radius
        bendHysteresis2HB: 0.18,   // [mN] KES-FB bending moment hysteresis (inter-yarn crossover slip)
        shearHysteresis2HG: 0.55,  // [N/m] KES-FB shear hysteresis at 0.5-5 deg
        lockingAngleDeg: 15.5,     // [deg] Peng & Cao (2005) weave trellis jamming angle
        frictionStatic: 0.44,      // [-]
        frictionKinetic: 0.34,     // [-]
        damping: 0.014,            // [-]
        albedo: [0.91, 0.89, 0.85],
        roughness: 0.62,
        sheen: 0.18,
        weaveScale: 68.0,
    },
    mulberrySilk: {
        key: 'mulberrySilk',
        name: 'Mulberry Silk Charmeuse',
        code: 'SLK-085',
        category: 'Satin Float Woven',
        gsm: 85,
        thicknessMm: 0.22,
        warpModulus: 1400,
        weftModulus: 1200,
        shearModulus: 95,          // Low bias shear -> liquid bias drape & fluted folds
        warpCrimpPct: 2.4,
        weftCrimpPct: 3.2,
        wallModulus: 11000,
        compressionRatio: 0.03,
        bendRigidity: 0.06,        // Fine micro-pleats and fluid movement
        bendHysteresis2HB: 0.02,   // Very low KES-FB hysteresis -> liquid recovery
        shearHysteresis2HG: 0.09,
        lockingAngleDeg: 21.0,     // Satin float weave permits deeper bias trellis before lock
        frictionStatic: 0.22,
        frictionKinetic: 0.16,
        damping: 0.009,
        albedo: [0.76, 0.19, 0.26],
        roughness: 0.28,
        sheen: 0.72,
        weaveScale: 96.0,
    },
    rawDenim: {
        key: 'rawDenim',
        name: 'Raw Selvedge Denim 12oz',
        code: 'DNM-410',
        category: '3x1 Right-Hand Twill',
        gsm: 410,
        thicknessMm: 1.10,
        warpModulus: 3600,
        weftModulus: 2900,
        shearModulus: 880,
        warpCrimpPct: 1.2,         // Strict 1.2% warp lock -> zero rubbery sag!
        weftCrimpPct: 1.8,
        wallModulus: 26000,
        compressionRatio: 0.08,
        bendRigidity: 2.35,        // Broad architectural folds
        bendHysteresis2HB: 0.95,   // High KES-FB twill crossover memory -> sculptural denim creases
        shearHysteresis2HG: 2.10,
        lockingAngleDeg: 13.5,
        frictionStatic: 0.58,
        frictionKinetic: 0.46,
        damping: 0.022,
        albedo: [0.15, 0.24, 0.40],
        roughness: 0.76,
        sheen: 0.12,
        weaveScale: 42.0,
    },
    merinoWool: {
        key: 'merinoWool',
        name: 'Merino Suiting Wool',
        code: 'WOL-260',
        category: '2x2 Twill Worsted',
        gsm: 260,
        thicknessMm: 0.68,
        warpModulus: 2100,
        weftModulus: 1850,
        shearModulus: 420,
        warpCrimpPct: 2.8,
        weftCrimpPct: 3.6,
        wallModulus: 15500,
        compressionRatio: 0.06,
        bendRigidity: 0.95,
        bendHysteresis2HB: 0.36,
        shearHysteresis2HG: 0.72,
        lockingAngleDeg: 18.5,
        frictionStatic: 0.52,
        frictionKinetic: 0.40,
        damping: 0.018,
        albedo: [0.22, 0.25, 0.29],
        roughness: 0.70,
        sheen: 0.22,
        weaveScale: 52.0,
    },
    cottonJersey: {
        key: 'cottonJersey',
        name: 'Stretch Cotton Jersey Knit',
        code: 'KNT-190',
        category: 'Single-Knit Weft Loop',
        gsm: 190,
        thicknessMm: 0.58,
        warpModulus: 520,          // Wales stretch moderately
        weftModulus: 340,          // Courses stretch generously around curves
        shearModulus: 180,
        warpCrimpPct: 16.0,        // Knit loop decrimp before yarn lock
        weftCrimpPct: 28.0,
        wallModulus: 6800,
        compressionRatio: 0.04,
        bendRigidity: 0.14,
        bendHysteresis2HB: 0.06,
        shearHysteresis2HG: 0.42,
        lockingAngleDeg: 28.0,
        frictionStatic: 0.48,
        frictionKinetic: 0.38,
        damping: 0.017,
        albedo: [0.18, 0.46, 0.56],
        roughness: 0.68,
        sheen: 0.15,
        weaveScale: 60.0,
    },
    duchesseSatin: {
        key: 'duchesseSatin',
        name: 'Couture Duchesse Satin',
        code: 'SAT-225',
        category: 'High-Density Heavy Satin',
        gsm: 225,
        thicknessMm: 0.52,
        warpModulus: 2600,
        weftModulus: 2200,
        shearModulus: 480,
        warpCrimpPct: 1.6,
        weftCrimpPct: 2.2,
        wallModulus: 19500,
        compressionRatio: 0.06,
        bendRigidity: 1.35,        // Sculptural couture gown volume
        bendHysteresis2HB: 0.48,
        shearHysteresis2HG: 0.38,
        lockingAngleDeg: 16.0,
        frictionStatic: 0.28,
        frictionKinetic: 0.20,
        damping: 0.015,
        albedo: [0.84, 0.75, 0.62],
        roughness: 0.31,
        sheen: 0.68,
        weaveScale: 84.0,
    },
};

export const TEXTILE_COLOR_SWATCHES = [
    { name: 'Terracotta Atelier', hex: '#c4513d', rgb: [0.77, 0.32, 0.24] },
    { name: 'Couture Crimson',    hex: '#a23b5c', rgb: [0.64, 0.23, 0.36] },
    { name: 'Selvedge Indigo',    hex: '#2f5d8c', rgb: [0.18, 0.36, 0.55] },
    { name: 'Aegean Petrol',      hex: '#1f6f78', rgb: [0.12, 0.44, 0.47] },
    { name: 'Emerald Velvet',     hex: '#3b7a62', rgb: [0.23, 0.48, 0.38] },
    { name: 'Champagne Silk',     hex: '#d6bf9e', rgb: [0.84, 0.75, 0.62] },
    { name: 'Atelier Ecru',       hex: '#e8e3d9', rgb: [0.91, 0.89, 0.85] },
    { name: 'Obsidian Noir',      hex: '#262830', rgb: [0.15, 0.16, 0.19] },
];

for (const tex of Object.values(TEXTILE_CATALOG))
{
    tex.shortLabel = `${tex.gsm} GSM ${tex.name}`;
}

export function cloneTextileMaterial(presetKey = 'cottonPoplin')
{
    const base = TEXTILE_CATALOG[presetKey] || TEXTILE_CATALOG.cottonPoplin;
    return {
        ...base,
        albedo: [...base.albedo],
    };
}

// --------------------------------------------------------------------------------------------------------------------------------------------
// Candidate Solver Architecture Catalog (Options A–F for Side-by-Side Game Runtime + Tailoring Authoring Evaluation)
// --------------------------------------------------------------------------------------------------------------------------------------------
export const SOLVER_CATALOG = {
    avbd: {
        id: 'avbd',
        option: 'Option A',
        shortCode: 'A · AVBD',
        badge: 'OPT A · AVBD',
        title: 'Option A · AVBD (Giles, Chen & Yuksel 2025)',
        role: 'Game + Authoring Baseline',
        citation: 'Giles, Chen & Yuksel (SIGGRAPH 2025) — Augmented Vertex Block Descent',
        summary: 'Per-vertex 3x3 SPD Hessian Newton descent with dual Lagrange multiplier stiffness augmentation across color batches.',
    },
    xpbd: {
        id: 'xpbd',
        option: 'Option B',
        shortCode: 'B · XPBD',
        badge: 'OPT B · XPBD',
        title: 'Option B · XPBD (Macklin et al. 2016)',
        role: 'Lightweight Game Runtime',
        citation: 'Macklin, Muller & Chentanez (MIG 2016) — Extended Position-Based Dynamics',
        summary: 'Color-batched scalar compliance projection (alpha = 1 / (k h^2)) for orthotropic warp/weft/shear, bending, and seams.',
    },
    scr: {
        id: 'scr',
        option: 'Option C',
        shortCode: 'C · Cosserat (Hsu 25)',
        badge: 'OPT C · SCR',
        title: 'Option C · Stable Cosserat Rods (Hsu, Wang, Wu & Yuksel 2025)',
        role: 'Game + Knit/Piping/Yarn Cloth',
        citation: 'Hsu, Wang, Wu & Yuksel (Utah Graphics Lab, SIGGRAPH 2025) + Hsu (2026) Viscous Stable Cosserat Rods',
        summary: 'Split isotropic diagonal VBD position update + closed-form Banach fixed-point unit quaternion orientation solve (|q_i| = 1) with Kelvin-Voigt yarn viscosity.',
    },
    gn_knot: {
        id: 'gn_knot',
        option: 'Option D',
        shortCode: 'D · FPB-GN (Guo 25)',
        badge: 'OPT D · FPB-GN',
        title: 'Option D · Unified Contact Gauss-Newton (Guo, Wang et al. 2025)',
        role: 'Authoring + Layered Folds/Knots',
        citation: 'Guo, Wang, Liu, Li, Wang, Yang & Huamin Wang (2025, guo-2025-fpb) — Fast Physics-Based Modeling',
        summary: 'Unified Gauss-Newton (H = J^T J) assembling VF/EE contact Hessians directly inside 3x3 block solves with 1.2^K lifespan-weighted contact history and 0.9^k intersection-safe step filtering.',
    },
    jgs2: {
        id: 'jgs2',
        option: 'Option E',
        shortCode: 'E · JGS2 (Lan 25)',
        badge: 'OPT E · JGS2',
        title: 'Option E · Second-Order Schur-Coupled GS (Lan, Wang & Yang 2025)',
        role: '60 FPS GPU Game + Stiff Authoring',
        citation: 'Lan, Lu, Yuan, Xu, Su, Huamin Wang, Jiang & Yin Yang (ACM TOG 44(4), 2025) — JGS2',
        summary: 'Eliminates local VBD/Jacobi overshoot via 1-ring Schur-complement perturbation subspace (H_ii - H_iC H_CC^-1 H_Ci) and neighbor residual lookahead.',
    },
    kes_aa: {
        id: 'kes_aa',
        option: 'Option F',
        shortCode: 'F · KES-AA (Kawabata)',
        badge: 'OPT F · KES-AA',
        title: 'Option F · Kawabata Co-Rotational Shell + Anderson(2) (KES-FB + PIE)',
        role: 'Precision CAD Authoring + Drape',
        citation: 'Kawabata (Text. Res. J.); Peng & Cao (Composites A 2005); Walker & Ni (SIAM J. Numer. Anal. 2011); Cerda & Mahadevan (PRL 2003)',
        summary: 'Non-orthogonal warp/weft fiber-frame co-rotational shell with measured KES-FB 2HG/2HB yarn-crossover hysteresis and energy-guarded Anderson(m=2) acceleration.',
    },
};

// --------------------------------------------------------------------------------------------------------------------------------------------
// Zhang, Wang, Liu, Pei, Xu & Huamin Wang (2025, zhang-2025-pie): Physics-Inspired Estimation of Optimal Cloth Mesh Resolution
// Solves c1 * r^6 + c2 * r^4 = B / E_vol (Eq. 4, calibrated on Cerda & Mahadevan 2003 & Vandeparre et al. 2011 wrinklon scaling)
// --------------------------------------------------------------------------------------------------------------------------------------------
function solvePiePolynomialRadiusMeters(yRatioM3)
{
    const c1 = 32.08;          // [m^-3] Zhang-2025-PIE Section 4.1.1
    const c2 = -1.876e-4;      // [m^-1]
    const yClamped = Math.max(1e-12, Math.min(2e-6, yRatioM3));
    let lo = 0.0025;           // 2.5 mm minimum physical mesh bound in Zhang-2025-PIE
    let hi = 0.0500;           // 50.0 mm maximum coarse bound
    for (let it = 0; it < 22; ++it)
    {
        const mid = 0.5 * (lo + hi);
        const r2 = mid * mid;
        const r4 = r2 * r2;
        const val = c1 * r4 * r2 + c2 * r4;
        if (val < yClamped) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

export function estimateClothResolutionPIE(textile)
{
    const tex = textile || TEXTILE_CATALOG.cottonPoplin;
    const hMeters = Math.max(1.2e-4, (tex.thicknessMm || 0.35) * 1e-3);
    const B = Math.max(1e-8, (tex.bendRigidity || 0.42) * 1e-3); // [N*m]
    // Effective decrimp-to-taut volumetric Young's modulus E_vol [N/m^2] along Warp and Weft
    const decrimpFactorWarp = 1.0 / (1.0 + 0.18 * (tex.warpCrimpPct ?? 2.0));
    const decrimpFactorWeft = 1.0 / (1.0 + 0.18 * (tex.weftCrimpPct ?? 2.8));
    const EwarpVol = Math.max(2e3, Math.min(2e6, ((tex.warpModulus || 1800) * decrimpFactorWarp) / hMeters));
    const EweftVol = Math.max(2e3, Math.min(2e6, ((tex.weftModulus || 1500) * decrimpFactorWeft) / hMeters));
    const EisoVol = 0.5 * (EwarpVol + EweftVol);

    const rWarpMm = solvePiePolynomialRadiusMeters(B / EwarpVol) * 1000.0;
    const rWeftMm = solvePiePolynomialRadiusMeters(B / EweftVol) * 1000.0;
    const rPieBaseMm = solvePiePolynomialRadiusMeters(B / EisoVol) * 1000.0;

    // Cerda & Mahadevan (2003, PRL 90, 074302) characteristic wrinkle wavelength lambda ~ r_opt / c (c ~ 0.27 from Pythagorean quarter-wave Fig. 4)
    const wavelengthMm = rPieBaseMm / 0.27;
    // Vandeparre et al. (2011, PRL) wrinklon transition length L_w ~ lambda^(3/2) * (delta / h^2)^(1/4)
    const lambdaM = wavelengthMm * 1e-3;
    const deltaComp = Math.max(0.015, tex.compressionRatio || 0.05);
    const wrinklonTransitionMm = Math.pow(lambdaM, 1.5) * Math.pow(deltaComp / (hMeters * hMeters), 0.25) * 1000.0;

    // Interactive Atelier Authoring spacing (clamped to smooth interactive Delaunay grid [9.5, 19.0] mm) & 60 FPS Game Runtime spacing
    const authoringSpacingMm = Math.round(Math.max(9.5, Math.min(18.5, rPieBaseMm * 1.35)) * 2.0) * 0.5;
    const gameSpacingMm = Math.round(Math.max(13.5, Math.min(24.0, rPieBaseMm * 2.05)) * 2.0) * 0.5;
    const seamSourceMm = Math.max(2.5, rPieBaseMm * 0.72);

    return {
        rPieBaseMm,
        rWarpMm,
        rWeftMm,
        anisotropyRatio: rWarpMm / Math.max(0.1, rWeftMm),
        wavelengthMm,
        wrinklonTransitionMm,
        seamSourceMm,
        authoringSpacingMm,
        gameSpacingMm,
    };
}

