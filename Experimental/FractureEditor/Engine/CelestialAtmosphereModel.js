/*==========================================================================================================================================
                                                     CELESTIALATMOSPHEREMODEL.JS
==========================================================================================================================================*/
// 🧩 Astronomical solar ephemeris, Bruneton/Hillaire spectral atmosphere LUT integration, and PCG volumetric cloud density fields.

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEFAULT PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

export function CreateDefaultSunParameters()
{
    return {
        AngularDiameterDeg: 0.533,
        TintRgb: [1.0, 0.96, 0.90],
        SkyGainMultiplier: 1.00,
        DirectBeamMultiplier: 1.15,
        LocalHours: 15.65,
        AnimateCycle: false,
        CycleSpeed: 1.0,
        LatitudeDeg: -26.19,
        LongitudeDeg: 28.32,
        Month: 10,
        Day: 31
    };
}

export function CreateDefaultAtmosphereParameters()
{
    return {
        GroundRadiusKm: 6360.0,
        AtmosphereHeightKm: 100.0,
        RayleighMultiplier: 1.0,
        RayleighScaleHeightKm: 8.0,
        MieMultiplier: 1.0,
        MieAbsorptionMultiplier: 1.1,
        MieScaleHeightKm: 1.2,
        MiePhaseG: 0.80,
        OzoneMultiplier: 1.0,
        OzonePeakKm: 25.0,
        OzoneWidthKm: 15.0,
        GroundAlbedo: 0.14,
        TwilightGlow: 0.62,
        HorizonWhiteLine: 0.24,
        CivilTwilightEnabled: true,
        SkyBrightness: 1.05,
        SkyTintRgb: [0.96, 0.99, 1.0],
        GroundTintRgb: [0.16, 0.17, 0.15],
        BakedFetchEnabled: true,
        PreviewMode: "altitude", // altitude | panorama | transmittance | sphere
        BakedSunDirection: null,
        BakedRevision: 1
    };
}

export function CreateDefaultCloudsParameters()
{
    return {
        ActiveScope: "global", // global | local
        Global: {
            Enabled: true,
            FollowWind: true,
            Coverage: 0.56,
            BaseHeightM: 900.0,
            ThicknessM: 1150.0,
            CeilingHeightM: 3800.0,
            DensityScale: 1.05,
            ExtinctionPerM: 0.012,
            FeatureScale: 1.0,
            PhaseG: 0.36,
            Morphology: "cumulus", // cumulus | stratocumulus | cirrus | cumulonimbus | stratus | altocumulus
            AnvilAmount: 0.42,
            WindXMps: 6.5,
            WindYMps: 2.2,
            ShadowStrength: 0.68,
            ShadowClockEnabled: true,
            Seed: 0xC10D5
        },
        Local: {
            Enabled: true,
            FollowWind: true,
            Coverage: 0.68,
            DensityScale: 1.25,
            FeatureScale: 1.35,
            PhaseG: 0.40,
            CentreX: 1.65,
            CentreY: 2.05,
            CentreZ: -1.10,
            HalfExtentX: 1.45,
            HalfExtentY: 0.58,
            HalfExtentZ: 1.25,
            WindXMps: 4.0,
            WindYMps: 1.5,
            Seed: 0x77A41
        }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SOLAR EPHEMERIS SOLVER
//------------------------------------------------------------------------------------------------------------------------

const DaysBeforeMonth = [0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];

export function EvaluateSolarPositionAtHour(SunParams, HourValue)
{
    const Month = Math.max(1, Math.min(12, Math.round(SunParams.Month)));
    const Day = Math.max(1, Math.min(31, Math.round(SunParams.Day)));
    const DayOfYear = DaysBeforeMonth[Month] + Day;
    const Gamma = (2.0 * Math.PI / 365.0) * (DayOfYear - 1 + (HourValue - 12.0) / 24.0);

    // Spencer (1971) Equation of Time [minutes] and Solar Declination δ [radians]
    const EoTMinutes = 229.18 * (
        0.000075 +
        0.001868 * Math.cos(Gamma) -
        0.032077 * Math.sin(Gamma) -
        0.014615 * Math.cos(2 * Gamma) -
        0.040849 * Math.sin(2 * Gamma)
    );
    const DeclinationRad =
        0.006918 -
        0.399912 * Math.cos(Gamma) +
        0.070257 * Math.sin(Gamma) -
        0.006758 * Math.cos(2 * Gamma) +
        0.000907 * Math.sin(2 * Gamma) -
        0.002697 * Math.cos(3 * Gamma) +
        0.001480 * Math.sin(3 * Gamma);

    const LatRad = (SunParams.LatitudeDeg * Math.PI) / 180.0;
    const TrueSolarHours = HourValue + EoTMinutes / 60.0;
    const HourAngleRad = ((TrueSolarHours - 12.0) * 15.0 * Math.PI) / 180.0;

    const SinEl = Math.max(-1.0, Math.min(1.0,
        Math.sin(LatRad) * Math.sin(DeclinationRad) +
        Math.cos(LatRad) * Math.cos(DeclinationRad) * Math.cos(HourAngleRad)
    ));
    const ElevationRad = Math.asin(SinEl);
    const ElevationDeg = (ElevationRad * 180.0) / Math.PI;

    const CosEl = Math.max(1e-5, Math.cos(ElevationRad));
    const SinAz = (-Math.cos(DeclinationRad) * Math.sin(HourAngleRad)) / CosEl;
    const CosAz = (Math.sin(DeclinationRad) - Math.sin(LatRad) * SinEl) / (Math.max(1e-5, Math.cos(LatRad)) * CosEl);
    let AzimuthRad = Math.atan2(SinAz, CosAz);
    if (AzimuthRad < 0) AzimuthRad += 2.0 * Math.PI;
    const AzimuthDeg = (AzimuthRad * 180.0) / Math.PI;

    // Kasten & Young (1989) Optical Air Mass
    const AirMass = ElevationDeg > -0.5
        ? 1.0 / (Math.max(0.015, Math.sin(Math.max(0, ElevationRad)) + 0.50572 * Math.pow(Math.max(0.2, ElevationDeg + 6.07995), -1.6364)))
        : 38.0;

    // World space: +Y up, +Z South, +X East
    const Direction = [
        Math.cos(ElevationRad) * Math.sin(AzimuthRad),
        Math.sin(ElevationRad),
        -Math.cos(ElevationRad) * Math.cos(AzimuthRad)
    ];

    return {
        ElevationDeg,
        AzimuthDeg,
        DeclinationDeg: (DeclinationRad * 180.0) / Math.PI,
        EquationOfTimeMin: EoTMinutes,
        AirMass: Math.min(38.0, AirMass),
        Direction,
        DayOfYear
    };
}

export function EvaluateSolarEphemeris(SunParams)
{
    const Current = EvaluateSolarPositionAtHour(SunParams, SunParams.LocalHours);
    const DiurnalCurve = [];
    let SunriseHour = null, SunsetHour = null, PeakElevation = -90, SolarNoonHour = 12.0;

    for (let i = 0; i <= 96; ++i)
    {
        const h = (i / 96) * 24.0;
        const s = EvaluateSolarPositionAtHour(SunParams, h);
        DiurnalCurve.push({ Hour: h, ElevationDeg: s.ElevationDeg, AzimuthDeg: s.AzimuthDeg });
        if (s.ElevationDeg > PeakElevation)
        {
            PeakElevation = s.ElevationDeg;
            SolarNoonHour = h;
        }
        if (i > 0)
        {
            const prev = DiurnalCurve[i - 1];
            if (prev.ElevationDeg <= 0 && s.ElevationDeg > 0 && SunriseHour === null)
            {
                SunriseHour = prev.Hour + (0 - prev.ElevationDeg) / (s.ElevationDeg - prev.ElevationDeg) * 0.25;
            }
            if (prev.ElevationDeg > 0 && s.ElevationDeg <= 0)
            {
                SunsetHour = prev.Hour + (prev.ElevationDeg - 0) / (prev.ElevationDeg - s.ElevationDeg) * 0.25;
            }
        }
    }

    let PhaseLabel = "Daylight";
    if (Current.ElevationDeg < -18) PhaseLabel = "Astronomical Night";
    else if (Current.ElevationDeg < -12) PhaseLabel = "Astronomical Twilight";
    else if (Current.ElevationDeg < -6) PhaseLabel = "Nautical Twilight";
    else if (Current.ElevationDeg < 0) PhaseLabel = "Civil Twilight";
    else if (Current.ElevationDeg < 8) PhaseLabel = "Golden Low Sun";

    return {
        ...Current,
        DiurnalCurve,
        SunriseHour,
        SunsetHour,
        SolarNoonHour,
        PeakElevation,
        PhaseLabel
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           BRUNETON / HILLAIRE SKY ATMOSPHERE
//------------------------------------------------------------------------------------------------------------------------

const BaseRayleigh = [5.802e-3, 13.558e-3, 33.10e-3]; // [1/km]
const BaseMieScat = 3.996e-3;                         // [1/km]
const BaseMieAbs = 4.40e-3;                           // [1/km]
const BaseOzoneAbs = [0.650e-3, 1.881e-3, 0.085e-3];  // [1/km]

export function RayleighDensityAtKm(Atmos, AltKm)
{
    return AltKm < 0 ? 1.0 : Math.exp(-AltKm / Math.max(0.5, Atmos.RayleighScaleHeightKm));
}

export function MieDensityAtKm(Atmos, AltKm)
{
    return AltKm < 0 ? 1.0 : Math.exp(-AltKm / Math.max(0.15, Atmos.MieScaleHeightKm));
}

export function OzoneDensityAtKm(Atmos, AltKm)
{
    return Math.max(0.0, 1.0 - Math.abs(AltKm - Atmos.OzonePeakKm) / Math.max(1.0, Atmos.OzoneWidthKm));
}

export function EvaluateTransmittanceToTop(Atmos, AltitudeKm, CosZenith, Steps = 16)
{
    const Rg = Atmos.GroundRadiusKm;
    const Rt = Rg + Atmos.AtmosphereHeightKm;
    const r = Rg + Math.max(0.001, AltitudeKm);

    const b = 2.0 * r * CosZenith;
    const c = r * r - Rt * Rt;
    const disc = Math.max(0, b * b - 4.0 * c);
    const PathLenKm = Math.max(0.01, (-b + Math.sqrt(disc)) * 0.5);

    // Check ground intersection when looking downward
    if (CosZenith < 0)
    {
        const cg = r * r - Rg * Rg;
        const dg = b * b - 4.0 * cg;
        if (dg >= 0 && (-b - Math.sqrt(dg)) * 0.5 > 0)
        {
            return [0.0, 0.0, 0.0];
        }
    }

    const ds = PathLenKm / Steps;
    let tauR = 0, tauM = 0, tauO = 0;
    for (let i = 0; i < Steps; ++i)
    {
        const t = (i + 0.5) * ds;
        const ri = Math.sqrt(r * r + t * t + 2.0 * r * t * CosZenith);
        const h = Math.max(0, ri - Rg);
        tauR += RayleighDensityAtKm(Atmos, h) * ds;
        tauM += MieDensityAtKm(Atmos, h) * ds;
        tauO += OzoneDensityAtKm(Atmos, h) * ds;
    }

    const Ext = [0, 1, 2].map((ch) =>
        Atmos.RayleighMultiplier * BaseRayleigh[ch] * tauR +
        (Atmos.MieMultiplier * BaseMieScat + Atmos.MieAbsorptionMultiplier * BaseMieAbs) * tauM +
        Atmos.OzoneMultiplier * BaseOzoneAbs[ch] * tauO
    );
    return [Math.exp(-Ext[0]), Math.exp(-Ext[1]), Math.exp(-Ext[2])];
}

export function EvaluateSkyRayRadiance(Atmos, ViewDir, SunDir, SunTint = [1, 1, 1], SkyGain = 1.0)
{
    const Rg = Atmos.GroundRadiusKm;
    const Rt = Rg + Atmos.AtmosphereHeightKm;
    const r = Rg + 0.05;
    const mu = ViewDir[1];

    if (mu < -0.02)
    {
        // Below horizon ground bounce preview
        const SunEl = Math.max(0, SunDir[1]);
        const gScale = Atmos.GroundAlbedo * (0.15 + 0.85 * SunEl) * Atmos.SkyBrightness;
        return [
            Atmos.GroundTintRgb[0] * gScale,
            Atmos.GroundTintRgb[1] * gScale,
            Atmos.GroundTintRgb[2] * gScale
        ];
    }

    const clampedMu = Math.max(-0.02, mu);
    const b = 2.0 * r * clampedMu;
    const c = r * r - Rt * Rt;
    const maxT = Math.min(280.0, (-b + Math.sqrt(Math.max(0, b * b - 4.0 * c))) * 0.5);

    const CosTheta = ViewDir[0] * SunDir[0] + ViewDir[1] * SunDir[1] + ViewDir[2] * SunDir[2];
    const PhaseR = (3.0 / (16.0 * Math.PI)) * (1.0 + CosTheta * CosTheta);
    const g = Math.max(-0.9, Math.min(0.96, Atmos.MiePhaseG));
    const g2 = g * g;
    const PhaseM = (3.0 / (8.0 * Math.PI)) * ((1.0 - g2) * (1.0 + CosTheta * CosTheta)) / ((2.0 + g2) * Math.pow(1.0 + g2 - 2.0 * g * CosTheta, 1.5));

    const Steps = 12;
    const ds = maxT / Steps;
    let trans = [1.0, 1.0, 1.0];
    let accum = [0.0, 0.0, 0.0];

    for (let i = 0; i < Steps; ++i)
    {
        const t = (i + 0.5) * ds;
        const ri = Math.sqrt(r * r + t * t + 2.0 * r * t * clampedMu);
        const h = Math.max(0, ri - Rg);
        const dR = RayleighDensityAtKm(Atmos, h);
        const dM = MieDensityAtKm(Atmos, h);
        const dO = OzoneDensityAtKm(Atmos, h);

        const SunTrans = EvaluateTransmittanceToTop(Atmos, h, SunDir[1], 6);

        for (let ch = 0; ch < 3; ++ch)
        {
            const betaR = Atmos.RayleighMultiplier * BaseRayleigh[ch] * dR;
            const betaM = Atmos.MieMultiplier * BaseMieScat * dM;
            const betaExt = betaR + (Atmos.MieMultiplier * BaseMieScat + Atmos.MieAbsorptionMultiplier * BaseMieAbs) * dM + Atmos.OzoneMultiplier * BaseOzoneAbs[ch] * dO;
            const stepT = Math.exp(-betaExt * ds);
            const scatter = (betaR * PhaseR + betaM * PhaseM);
            // Hillaire multiple-scattering isotropic floor
            const multiScatter = (betaR + betaM * 0.35) * 0.055 * (0.25 + 0.75 * Math.max(0, SunDir[1] + 0.18));
            const inScatter = (scatter * SunTrans[ch] + multiScatter) * ds * 18.5 * SkyGain * SunTint[ch];
            accum[ch] += trans[ch] * inScatter;
            trans[ch] *= stepT;
        }
    }

    // Analytic civil twilight & horizon band
    const HorizonBand = Math.exp(-Math.abs(mu) * 18.0);
    const TwilightFactor = Math.max(0, 1.0 - Math.abs(SunDir[1] - 0.02) * 3.2) * Atmos.TwilightGlow;
    accum[0] += HorizonBand * (TwilightFactor * 0.42 + Atmos.HorizonWhiteLine * 0.14);
    accum[1] += HorizonBand * (TwilightFactor * 0.19 + Atmos.HorizonWhiteLine * 0.15);
    accum[2] += HorizonBand * (TwilightFactor * 0.07 + Atmos.HorizonWhiteLine * 0.17);

    return [
        accum[0] * Atmos.SkyBrightness * Atmos.SkyTintRgb[0],
        accum[1] * Atmos.SkyBrightness * Atmos.SkyTintRgb[1],
        accum[2] * Atmos.SkyBrightness * Atmos.SkyTintRgb[2]
    ];
}

export function ToneMapReinhardSrgb(LinearRgb)
{
    return LinearRgb.map((v) =>
    {
        const mapped = Math.max(0, v) / (1.0 + Math.max(0, v));
        const srgb = Math.pow(mapped, 1.0 / 2.2);
        return Math.max(0, Math.min(255, Math.round(srgb * 255)));
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                      DETERMINISTIC PCG 4-OCTAVE CLOUD DENSITY
//------------------------------------------------------------------------------------------------------------------------

function HashLattice3D(ix, iy, iz, seed)
{
    let h = (Math.imul(ix, 374761393) + Math.imul(iy, 668265263) + Math.imul(iz, 2147483647) + Math.imul(seed, 1597334677)) >>> 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
    return ((h ^ (h >>> 16)) >>> 0) / 4294967295.0;
}

function ValueNoise3D(x, y, z, seed)
{
    const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
    const fx = x - ix, fy = y - iy, fz = z - iz;
    const ux = fx * fx * (3.0 - 2.0 * fx);
    const uy = fy * fy * (3.0 - 2.0 * fy);
    const uz = fz * fz * (3.0 - 2.0 * fz);

    const c000 = HashLattice3D(ix, iy, iz, seed);
    const c100 = HashLattice3D(ix + 1, iy, iz, seed);
    const c010 = HashLattice3D(ix, iy + 1, iz, seed);
    const c110 = HashLattice3D(ix + 1, iy + 1, iz, seed);
    const c001 = HashLattice3D(ix, iy, iz + 1, seed);
    const c101 = HashLattice3D(ix + 1, iy, iz + 1, seed);
    const c011 = HashLattice3D(ix, iy + 1, iz + 1, seed);
    const c111 = HashLattice3D(ix + 1, iy + 1, iz + 1, seed);

    const x00 = c000 + (c100 - c000) * ux;
    const x10 = c010 + (c110 - c010) * ux;
    const x01 = c001 + (c101 - c001) * ux;
    const x11 = c011 + (c111 - c011) * ux;
    const y0 = x00 + (x10 - x00) * uy;
    const y1 = x01 + (x11 - x01) * uy;
    return y0 + (y1 - y0) * uz;
}

export function FbmCloudNoise3D(x, y, z, seed)
{
    let sum = 0.0, amp = 0.52, freq = 1.0;
    for (let oct = 0; oct < 4; ++oct)
    {
        sum += amp * ValueNoise3D(x * freq, y * freq, z * freq, seed + oct * 97);
        freq *= 2.05;
        amp *= 0.5;
    }
    return sum;
}

function VerticalMorphologyProfile(hFrac, Morphology, AnvilAmount)
{
    const bottomRound = Math.min(1.0, hFrac / 0.16);
    if (Morphology === "cirrus")
    {
        return Math.exp(-Math.pow((hFrac - 0.72) / 0.22, 2)) * 0.65;
    }
    if (Morphology === "stratus")
    {
        return bottomRound * Math.min(1.0, (1.0 - hFrac) / 0.25) * 0.9;
    }
    if (Morphology === "cumulonimbus")
    {
        const tower = bottomRound * Math.min(1.0, (1.0 - hFrac) / 0.08);
        const anvil = hFrac > 0.72 ? AnvilAmount * Math.exp(-Math.pow((hFrac - 0.86) / 0.12, 2)) * 0.75 : 0;
        return Math.min(1.3, tower + anvil);
    }
    if (Morphology === "stratocumulus")
    {
        return bottomRound * Math.min(1.0, (1.0 - hFrac) / 0.35);
    }
    if (Morphology === "altocumulus")
    {
        return Math.sin(Math.PI * Math.max(0, Math.min(1, hFrac))) * 0.85;
    }
    // Cumulus default
    return bottomRound * Math.pow(Math.max(0, 1.0 - hFrac * 0.82), 0.65);
}

// 📝 Evaluates monotonic shaped global cloud density at world coordinates (X, Z horizontal [m], AltitudeM vertical [m]).
export function SampleGlobalCloudDensity(GlobalSpec, WorldXM, WorldZM, AltitudeM)
{
    if (!GlobalSpec.Enabled) return 0.0;
    const EffectiveTop = Math.min(GlobalSpec.CeilingHeightM, GlobalSpec.BaseHeightM + GlobalSpec.ThicknessM);
    if (AltitudeM <= GlobalSpec.BaseHeightM || AltitudeM >= EffectiveTop) return 0.0;

    const hFrac = (AltitudeM - GlobalSpec.BaseHeightM) / Math.max(1.0, EffectiveTop - GlobalSpec.BaseHeightM);
    const Profile = VerticalMorphologyProfile(hFrac, GlobalSpec.Morphology, GlobalSpec.AnvilAmount);

    const Scale = 0.00095 / Math.max(0.25, GlobalSpec.FeatureScale);
    const AnvilSpread = (GlobalSpec.Morphology === "cumulonimbus" && hFrac > 0.72) ? (1.0 - GlobalSpec.AnvilAmount * 0.28) : 1.0;
    const n = FbmCloudNoise3D(WorldXM * Scale * AnvilSpread, AltitudeM * Scale * 1.4, WorldZM * Scale * AnvilSpread, GlobalSpec.Seed);

    const Threshold = 1.0 - Math.max(0.0, Math.min(1.0, GlobalSpec.Coverage));
    const Shaped = Math.max(0.0, (n - Threshold * 0.82) / Math.max(0.14, 1.0 - Threshold * 0.72));
    return Shaped * Profile * GlobalSpec.DensityScale;
}

export function SampleLocalCloudDensity(LocalSpec, NormX, NormY, NormZ)
{
    if (!LocalSpec.Enabled) return 0.0;
    const rSq = NormX * NormX + NormY * NormY + NormZ * NormZ;
    if (rSq >= 1.0) return 0.0;
    const EllipsoidMask = Math.pow(1.0 - rSq, 0.85);
    const Scale = 2.4 / Math.max(0.25, LocalSpec.FeatureScale);
    const n = FbmCloudNoise3D(
        (NormX + LocalSpec.CentreX * 0.3) * Scale,
        (NormY + LocalSpec.CentreY * 0.3) * Scale,
        (NormZ + LocalSpec.CentreZ * 0.3) * Scale,
        LocalSpec.Seed
    );
    const Threshold = 1.0 - Math.max(0.0, Math.min(1.0, LocalSpec.Coverage));
    const Shaped = Math.max(0.0, (n - Threshold * 0.80) / Math.max(0.15, 1.0 - Threshold * 0.70));
    return Shaped * EllipsoidMask * LocalSpec.DensityScale;
}
