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
// Solver catalog (Options A-F). Written for the from-scratch ClothSolver.js -- each summary states
// what that mode actually computes, including where it departs from its paper.
// --------------------------------------------------------------------------------------------------------------------------------------------
export const SOLVER_CATALOG = {
    avbd: {
        id: 'avbd', option: 'Option A', shortCode: 'A · AVBD', badge: 'OPT A · AVBD',
        title: 'Option A · AVBD — Augmented Vertex Block Descent',
        role: 'Authoring fit + stiff game hero garments',
        citation: 'Giles, Diaz & Yuksel, SIGGRAPH 2025 (extends VBD, Chen et al. 2024)',
        summary: 'Colour-ordered per-vertex 3x3 Newton (VBD) on an orthotropic membrane. Seams and body contact are hard constraints in the AVBD sense: dual variables + a penalty that grows while their error stays large.',
    },
    xpbd: {
        id: 'xpbd', option: 'Option B', shortCode: 'B · XPBD', badge: 'OPT B · XPBD',
        title: 'Option B · XPBD — Extended Position-Based Dynamics',
        role: 'Game runtime',
        citation: 'Macklin, Müller & Chentanez, MIG 2016',
        summary: 'Substepped constraint projection with physical compliance (alpha-tilde = 1/(k h^2)) and per-substep Lagrange multipliers for warp, weft, shear, bending and seams. The cheapest, most predictable mode.',
    },
    scr: {
        id: 'scr', option: 'Option C', shortCode: 'C · SCR', badge: 'OPT C · SCR',
        title: 'Option C · Stable Cosserat Rods (graph form)',
        role: 'Authoring — folds, knits, piping-like behaviour',
        citation: 'Hsu, Wang, Wu & Yuksel, SIGGRAPH 2025 (+ Alg. 1/2)',
        summary: 'Mesh edges are Cosserat rods: split position/orientation iterations, diagonal stretch Hessian (Eq. 29), and the closed-form quaternion update q = (v*b*e3 + lambda*b)/(lambda^2 - |v|^2) with lambda from |v|+|b| refined by the Banach fixed point stored as gamma (Eq. 22/25-27). Graph form of Eq. 17 for b. Hinge bending and a mass-weighted seam stitch pass are added, as membrane cloth still needs them (force balance alone leaves multi-mm seam gaps).',
    },
    gn_knot: {
        id: 'gn_knot', option: 'Option D', shortCode: 'D · GN-KNOT', badge: 'OPT D · GN-KNOT',
        title: 'Option D · Gauss-Newton knots & layers (Guo et al.)',
        role: 'Authoring — layered cloth, knots & ties',
        citation: 'Guo et al., SIGGRAPH 2025 (primary simulator Sec. 6.1; templates Sec. 4-5 in KnotTemplates.js)',
        summary: 'Quasistatic Gauss-Newton: one Newton solve per substep with contact inside the system (H_con = G_con * G_con^T, signed barycentric gradients), then an intersection-checked step that backtracks by 0.9^k until penetration stops growing. Can drape over tessellated knot-pipe colliders.',
    },
    jgs2: {
        id: 'jgs2', option: 'Option E', shortCode: 'E · JGS2', badge: 'OPT E · JGS2',
        title: 'Option E · JGS2 — second-order Jacobi/Gauss-Seidel',
        role: 'Stiff cloth, game-leaning research mode',
        citation: 'Lan et al., ACM TOG 44(4) 2025',
        summary: 'Local solves made second-order-aware: the Schur form (H_ii - H_iC H_CC^-1 H_Ci) dx = rhs - H_iC H_CC^-1 rhs_C (their Eq. 13) with coupling blocks H_ij assembled from the shared faces. H_CC is kept block-diagonal and there is no cubature precompute, so this is JGS2 with those two approximations stated. A seam stitch pass supplements the force solve.',
    },
    kes_aa: {
        id: 'kes_aa', option: 'Option F', shortCode: 'F · KES-AA', badge: 'OPT F · KES-AA',
        title: 'Option F · Kawabata shell + Anderson(2)',
        role: 'Authoring — textile hand-feel',
        citation: 'Kawabata KES-FB data model; Anderson acceleration, Walker & Ni 2011',
        summary: 'Non-orthogonal warp/weft shell: coupled stresses S_u = C11 e_u + C12 e_v (C12 > 0), compressive buckling cap, trellis shear lock, and 2HB/2HG hysteresis as rate damping. Anderson(m=2) is applied inside one substep, where the fixed-point operator is actually constant, and is rejected unless the total energy falls.',
    },
};

// --------------------------------------------------------------------------------------------------------------------------------------------
// Zhang et al. (SIGGRAPH 2025) "PIE": optimal cloth mesh resolution from material stiffness.
// Their Eq. (4):   c1 * r^6 + c2 * r^4 = B / E,   c1 = 32.08 m^-3, c2 = -1.876e-4 m^-1,
// with r in [2.5, 50] mm. B is bending stiffness [N m], E a Young's modulus [N/m^2].
// r grows monotonically with B/E in the calibrated band, so bisection finds it.
// The paper then builds non-uniform sizing maps around garment features; this Atelier keeps a
// per-fabric (and per-axis) spacing, which is the global part of their method only.
// --------------------------------------------------------------------------------------------------------------------------------------------
function radiusFromStiffnessRatio(B, E)
{
    const y = B / Math.max(1.0, E);
    let lo = 0.0025, hi = 0.050;
    for (let i = 0; i < 40; ++i)
    {
        const mid = 0.5 * (lo + hi);
        const value = 32.08 * Math.pow(mid, 6) - 1.876e-4 * Math.pow(mid, 4);
        if (value < y) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

export function estimateClothResolutionPIE(textile)
{
    const tex = textile || TEXTILE_CATALOG.cottonPoplin;
    const thicknessM = Math.max(1e-4, (tex.thicknessMm || 0.35) * 1e-3);
    const bendingB = Math.max(1e-9, (tex.bendRigidity || 0.42) * 1e-3); // catalog stores mN*m
    // Sheet modulus [N/m] over thickness is a Young's modulus [N/m^2]. Woven yarns are not taut
    // until crimp is pulled out, so discount by crimp; the factor is an Atelier calibration.
    const youngWarp = ((tex.warpModulus || 1800) / (1 + 0.18 * (tex.warpCrimpPct ?? 2))) / thicknessM;
    const youngWeft = ((tex.weftModulus || 1500) / (1 + 0.18 * (tex.weftCrimpPct ?? 2.5))) / thicknessM;
    const youngIso = Math.sqrt(youngWarp * youngWeft);

    const rWarpMm = radiusFromStiffnessRatio(bendingB, youngWarp) * 1000;
    const rWeftMm = radiusFromStiffnessRatio(bendingB, youngWeft) * 1000;
    const rPieBaseMm = radiusFromStiffnessRatio(bendingB, youngIso) * 1000;

    // r_opt ~ c * lambda (Zhang Eq. after Fig. 4). c is left as one Atelier constant.
    const wavelengthMm = rPieBaseMm / 0.27;
    // Vandeparre et al. wrinklon transition L_w ~ lambda^(3/2) * (delta / h^2)^(1/4).
    const lambdaM = wavelengthMm * 1e-3;
    const wrinklonTransitionMm = Math.pow(lambdaM, 1.5)
        * Math.pow(Math.max(0.01, tex.compressionRatio || 0.05) / (thicknessM * thicknessM), 0.25) * 1000;

    const roundHalf = (v) => Math.round(v * 2) / 2;
    return {
        rPieBaseMm, rWarpMm, rWeftMm,
        anisotropyRatio: rWarpMm / Math.max(0.1, rWeftMm),
        wavelengthMm, wrinklonTransitionMm,
        seamSourceMm: Math.max(2.5, rPieBaseMm * 0.72),
        // Spacing choices for this app (not from the paper): authoring is finer than r*, games coarser.
        authoringSpacingMm: roundHalf(Math.min(18.5, Math.max(9.5, rPieBaseMm * 1.35))),
        gameSpacingMm: roundHalf(Math.min(24, Math.max(13.5, rPieBaseMm * 2.05))),
    };
}
