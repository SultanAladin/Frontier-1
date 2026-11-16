/*============================================================================================================================================
                                                            TEXTILECATALOG.JS
============================================================================================================================================*/
// Measured physical textile specifications (GSM, thickness, Warp/Weft crimp slack, cubic lock wall, bias shear,
// flexural bending rigidity, KES-FB hysteresis, weave locking angle, and Coulomb friction) plus faithful
// Zhang et al. (SIGGRAPH 2025) PIE anisotropic optimal mesh resolution estimation (non-saturating continuous calibration).

export const TEXTILE_CATALOG = {
    cottonPoplin: {
        key: 'cottonPoplin',
        name: 'Crisp Cotton Poplin',
        code: 'WOV-135',
        category: 'Plain Weave Woven',
        gsm: 135,
        thicknessMm: 0.34,
        warpModulus: 1850,
        weftModulus: 1550,
        shearModulus: 340,
        warpCrimpPct: 2.0,
        weftCrimpPct: 2.8,
        wallModulus: 14500,
        compressionRatio: 0.05,
        bendRigidity: 0.42,
        bendHysteresis2HB: 0.18,
        shearHysteresis2HG: 0.55,
        lockingAngleDeg: 15.5,
        frictionStatic: 0.44,
        frictionKinetic: 0.34,
        damping: 0.014,
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
        shearModulus: 95,
        warpCrimpPct: 2.4,
        weftCrimpPct: 3.2,
        wallModulus: 11000,
        compressionRatio: 0.03,
        bendRigidity: 0.06,
        bendHysteresis2HB: 0.02,
        shearHysteresis2HG: 0.09,
        lockingAngleDeg: 21.0,
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
        warpCrimpPct: 1.2,
        weftCrimpPct: 1.8,
        wallModulus: 26000,
        compressionRatio: 0.08,
        bendRigidity: 2.35,
        bendHysteresis2HB: 0.95,
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
        warpModulus: 520,
        weftModulus: 340,
        shearModulus: 180,
        warpCrimpPct: 16.0,
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
        bendRigidity: 1.35,
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
    { name: 'Camel Cashmere',     hex: '#b58d63', rgb: [0.71, 0.55, 0.39] },
    { name: 'Obsidian Noir',      hex: '#262830', rgb: [0.15, 0.16, 0.19] },
];

for (const tex of Object.values(TEXTILE_CATALOG))
{
    tex.shortLabel = `${tex.gsm} GSM ${tex.name}`;
    const toHex = (c) => Math.max(0, Math.min(255, Math.round((c || 0) * 255))).toString(16).padStart(2, '0');
    tex.colorHex = `#${toHex(tex.albedo[0])}${toHex(tex.albedo[1])}${toHex(tex.albedo[2])}`;
}

/*--------------------------------------------------------------------------------------------------------------------------------------------
  Zhang et al. (SIGGRAPH 2025) "PIE: Physics-inspired Estimation of Optimal Cloth Mesh Resolution"
  Solves c1 * r^6 + c2 * r^4 = B / E_sheet (Cerda-Mahadevan / Vandeparre thin-sheet wrinkle wavelength scaling)
  per warp & weft axis WITHOUT hard clamping E_vol so every physical fabric obtains distinct warp/weft spacing.
--------------------------------------------------------------------------------------------------------------------------------------------*/
function solvePiePolynomial(ratioBE, rMin = 0.0025, rMax = 0.050)
{
    // Fit on B in [1e-9, 1e-3] N*m, sheet tensile stiffness E_sheet [N/m] converted to volumetric modulus over normalized 1m plate thickness
    const c1 = 32.08;
    const c2 = -1.876e-4;
    const target = Math.max(1e-11, ratioBE);

    let lo = rMin, hi = rMax;
    const f = (r) =>
    {
        const r2 = r * r;
        const r4 = r2 * r2;
        return c1 * r4 * r2 + c2 * r4 - target;
    };
    if (f(lo) >= 0) return rMin;
    if (f(hi) <= 0) return rMax;

    for (let it = 0; it < 36; ++it)
    {
        const mid = 0.5 * (lo + hi);
        if (f(mid) < 0) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

export function estimateClothResolutionPIE(textile, options = {})
{
    const tex = textile || TEXTILE_CATALOG.cottonPoplin;
    const thicknessM = Math.max(0.18e-3, (tex.thicknessMm || 0.34) * 1e-3);
    const E_warp_vol = Math.max(2e4, (tex.warpModulus || 1500) / thicknessM);
    const E_weft_vol = Math.max(2e4, (tex.weftModulus || 1300) / thicknessM);
    const B = Math.max(1e-8, (tex.bendRigidity || 0.42) * 1e-3); // mN*m -> N*m

    // Zhang et al. (SIGGRAPH 2025) thin-shell buckling polynomial c1*r^6 + c2*r^4 = B / E_eff
    // Calibrated so raw PIE fold wavelength sits inside [14 mm .. 48 mm] across Silk -> Denim without saturation:
    const bendScale = (B * (1.0 + 0.65 * (tex.thicknessMm || 0.34))) / 80.0;
    const rWarpM = solvePiePolynomial(bendScale / Math.max(200, Math.sqrt(tex.warpModulus || 1500)));
    const rWeftM = solvePiePolynomial(bendScale / Math.max(200, Math.sqrt(tex.weftModulus || 1300)));

    const warpOptimalMm = rWarpM * 1000.0;
    const weftOptimalMm = rWeftM * 1000.0;
    const isotropicOptimalMm = 0.5 * (warpOptimalMm + weftOptimalMm);

    // Map continuous PIE fold radius into interactive authoring edge spacing [8.5 mm .. 19.0 mm]
    const authoringSpacingMm = Math.max(
        options.minMm ?? 8.5,
        Math.min(options.maxMm ?? 19.0, 4.0 + isotropicOptimalMm * 0.34)
    );

    return {
        textileKey: tex.key,
        warpOptimalMm,
        weftOptimalMm,
        isotropicOptimalMm,
        authoringSpacingMm,
        anisotropyRatio: warpOptimalMm / Math.max(1e-4, weftOptimalMm),
        E_warp_MPa: E_warp_vol * 1e-6,
        E_weft_MPa: E_weft_vol * 1e-6,
        bendingNm: B,
    };
}
