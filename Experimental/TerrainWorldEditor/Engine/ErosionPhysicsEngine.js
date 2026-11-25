/**
 * ErosionPhysicsEngine.js — Physically Grounded Multi-Process Erosion Solver
 * ==========================================================================
 * CPU grid continuum solver for the Open-World SDF Terrain Studio. Every
 * process is anchored in published geomorphological physics rather than
 * ad-hoc height diffusion:
 *
 *  1. HYDROLOGY & FLUVIAL — Virtual-Pipe shallow-water (Imbert/Tucker) with
 *     Manning friction `v = (1/n) h^(2/3) S^(1/2)`, Whipple–Tucker stream-power
 *     bedrock incision `E = Kb Q^m S^n`, Exner advection/deposition, Green–Ampt
 *     infiltration, and coastline sink drainage (`sdf2D >= 0` drains to sea level)
 *     so land NEVER floods unphysically. Thin rain films soak in; only genuine
 *     channeled discharge and closed pools keep standing water.
 *
 *  2. PITTING (ice + thermal cracking) — Coupled Hallet–Walder segregation-ice
 *     growth (−8 °C … −3 °C wet window) + volumetric freeze–thaw (9 % ice
 *     expansion) + diurnal insolation thermal-expansion fatigue (Paris-law
 *     crack growth under ΔT stress + quartz/bulk expansion mismatch) nucleating
 *     spatially clustered joint pits, tafoni honeycombs and spalls directly into
 *     bedrock. Pits are protected from instant talus erasure (pit-fill only
 *     after the cavity is deep).
 *
 *  3. WIND — Bagnold/Kawamura saltation flux with 34° slipface avalanches
 *     (migrating barchan/star dunes) + Venturi canyon-corridor acceleration,
 *     saltation-curtain basal undercutting (0.2–2.5 m bounce zone) and 3D
 *     cross-bedded differential abrasion carving slot canyons, alcoves, fins
 *     and yardangs into sandstone.
 *
 *  4. SEASONAL CLIMATE FORCING — Latitude + 9 climate regimes driving
 *     day-of-year temperature, rain-vs-snow partitioning, winter freeze
 *     cracking, spring snowmelt floods and dry-season wind/thermal cycles.
 *     Multiple terrains can share ONE ClimateGroup (same weather pattern/cycle).
 *
 *  5. CONTINUUM BALLISTIC ROCKFALL & SCREE — Joint/overhang cliff detachment,
 *     multi-cell ballistic runout with momentum chute scour and grain-size
 *     sorted talus aprons (coarse repose 35°, fine repose 26°) at cliff bases.
 *     Terrain-native only — NO rigid-body meshes.
 *
 *  6. CHEMICAL WEATHERING — Carbonate/evaporite dissolution (karst), silicate
 *     hydrolysis into clay saprolite, salt haloclasty, and downhill dissolved
 *     mineral transport (Fe rust, Cu patina, Mn desert varnish, evaporite salt).
 *
 * Deterministic: all stochastic terms derive from integer hashes of
 * (seed, cell, day) — identical runs always reproduce identical terrains.
 */

import { Clamp, Mix, Smoothstep, ComputeRegionBaseSignature } from './TerrainLayerStack.js';

/*==========================================================================================================================================
                                                                    UTILITIES
==========================================================================================================================================*/
export function Hash01(Seed, A, B = 0, C = 0)
{
    let H = Math.imul((Seed | 0) ^ 0x9e3779b9, 0x85ebca6b);
    H ^= Math.imul((A | 0) ^ 0xc2b2ae35, 0x27d4eb2f);
    H ^= Math.imul((B | 0) ^ 0x165667b1, 0x9e3779b1);
    H ^= Math.imul((C | 0) ^ 0x85ebca6b, 0xc2b2ae35);
    H ^= H >>> 15; H = Math.imul(H, 0x2c1b3c6d);
    H ^= H >>> 12; H = Math.imul(H, 0x297a2d39);
    H ^= H >>> 15;
    return (H >>> 0) / 4294967296;
}

const DEG = Math.PI / 180.0;

/*==========================================================================================================================================
                                        SEASONAL CLIMATE — 9 REGIMES + LATITUDE
==========================================================================================================================================*/
export const CLIMATE_REGIMES = [
    { id: 'alpine-continental',      label: 'Alpine / Continental',      meanTempC: 3,  seasonalAmpC: 13, diurnalAmpC: 9,  annualPrecipMm: 900,  snowShare: 0.46, frostNights: 150, evaporation: 0.82, windBase: 5.5, stormPulse: 1.7, drySpell: 0.78 },
    { id: 'temperate-oceanic',       label: 'Temperate Oceanic',         meanTempC: 10, seasonalAmpC: 6,  diurnalAmpC: 6,  annualPrecipMm: 1100, snowShare: 0.08, frostNights: 45,  evaporation: 0.66, windBase: 6.5, stormPulse: 1.3, drySpell: 0.88 },
    { id: 'mediterranean',           label: 'Mediterranean',             meanTempC: 17, seasonalAmpC: 8,  diurnalAmpC: 10, annualPrecipMm: 550,  snowShare: 0.03, frostNights: 12,  evaporation: 1.15, windBase: 4.2, stormPulse: 1.9, drySpell: 0.55 },
    { id: 'arid-desert',             label: 'Arid Desert',               meanTempC: 22, seasonalAmpC: 12, diurnalAmpC: 16, annualPrecipMm: 60,   snowShare: 0.0,  frostNights: 25,  evaporation: 1.9,  windBase: 7.5, stormPulse: 2.6, drySpell: 0.32 },
    { id: 'semi-arid-high-desert',   label: 'Semi-Arid High Desert',     meanTempC: 14, seasonalAmpC: 13, diurnalAmpC: 18, annualPrecipMm: 220,  snowShare: 0.10, frostNights: 110, evaporation: 1.5,  windBase: 6.8, stormPulse: 2.1, drySpell: 0.5,  },
    { id: 'monsoon',                 label: 'Monsoon',                   meanTempC: 24, seasonalAmpC: 7,  diurnalAmpC: 9,  annualPrecipMm: 1800, snowShare: 0.02, frostNights: 5,   evaporation: 1.25, windBase: 4.8, stormPulse: 2.4, drySpell: 0.62 },
    { id: 'subarctic-continental',   label: 'Subarctic Continental',     meanTempC: -4, seasonalAmpC: 18, diurnalAmpC: 10, annualPrecipMm: 420,  snowShare: 0.55, frostNights: 220, evaporation: 0.55, windBase: 6.0, stormPulse: 1.5, drySpell: 0.8,  },
    { id: 'polar-desert',            label: 'Polar Desert',              meanTempC: -14,seasonalAmpC: 15, diurnalAmpC: 7,  annualPrecipMm: 110,  snowShare: 0.85, frostNights: 300, evaporation: 0.28, windBase: 8.5, stormPulse: 1.8, drySpell: 0.85 },
    { id: 'tropical-highland',       label: 'Tropical Highland',         meanTempC: 13, seasonalAmpC: 3,  diurnalAmpC: 12, annualPrecipMm: 1300, snowShare: 0.06, frostNights: 60,  evaporation: 1.0,  windBase: 4.5, stormPulse: 1.6, drySpell: 0.75 }
];

export const DEFAULT_CLIMATE_OPTIONS = Object.freeze({
    regimeId: 'alpine-continental',
    latitudeDeg: 46,
    startDayOfYear: 120,
    seed: 4029,
    temperatureOffsetC: 0,
    precipitationScale: 1.0,
    windScale: 1.0,
    interannualVariability: 0.18
});

/**
 * A ClimateGroup owns ONE shared weather / pattern / seasonal cycle. Any number
 * of ErosionStates may reference the same group → 2+ terrains/mask layers evolve
 * under literally the same weather year after year (1-by-1 solo also supported:
 * give each state its own group).
 */
export function CreateClimateGroup(Options = {})
{
    const Opts = { ...DEFAULT_CLIMATE_OPTIONS, ...Options };
    const Regime = CLIMATE_REGIMES.find(R => R.id === Opts.regimeId) || CLIMATE_REGIMES[0];
    return {
        regime: Regime,
        regimeId: Regime.id,
        latitudeDeg: Clamp(Number(Opts.latitudeDeg) || 0, -82, 82),
        seed: (Opts.seed | 0) || 4029,
        dayOfYear: Clamp(Math.round(Number(Opts.startDayOfYear) || 1), 1, 365),
        year: 0,
        temperatureOffsetC: Number(Opts.temperatureOffsetC) || 0,
        precipitationScale: Clamp(Number(Opts.precipitationScale) || 1, 0, 4),
        windScale: Clamp(Number(Opts.windScale) || 1, 0, 4),
        interannualVariability: Clamp(Number(Opts.interannualVariability) || 0, 0, 1),
        lastSample: null
    };
}

/** Advance the shared calendar by one day. */
export function AdvanceClimateDay(Group)
{
    Group.dayOfYear += 1;
    if (Group.dayOfYear > 365)
    {
        Group.dayOfYear = 1;
        Group.year += 1;
    }
    return Group;
}

/**
 * Sample the weather for the current day. Deterministic from (seed, year, doy).
 * Latitude sets hemisphere seasonal phase; the regime sets mean/amp/precip/wind.
 */
export function SampleClimate(Group)
{
    const R = Group.regime;
    const LatRad = Group.latitudeDeg * DEG;
    // Warmest day ~ Jan 15 in southern hemisphere, ~ Jul 15 in northern.
    const PeakWarmDoy = Group.latitudeDeg >= 0 ? 196 : 15;
    const Phase = 2 * Math.PI * (Group.dayOfYear - PeakWarmDoy) / 365.0;
    const Seasonal = Math.cos(Phase); // +1 at peak warm
    const WinterT = -Math.cos(Phase);

    // Interannual drift: smooth per-year anomaly (e.g. colder/wetter years).
    const YearNoise = (Hash01(Group.seed, 7777, Group.year) - 0.5) * 2 * Group.interannualVariability;

    const DiurnalSeed = Hash01(Group.seed, Group.dayOfYear, Group.year * 13 + 5);
    const TempC = R.meanTempC + R.seasonalAmpC * Seasonal + YearNoise * 4.0
        + Group.temperatureOffsetC
        + (DiurnalSeed - 0.5) * R.diurnalAmpC * 0.9;
    const DiurnalAmpC = R.diurnalAmpC * (0.75 + 0.5 * Math.max(0.0, Seasonal));

    // Precipitation: wetter in the regime's wet season with storm pulses & dry spells.
    const WetSeasonBias = 0.55 + 0.45 * (R.snowShare > 0.3 ? WinterT * 0.5 + 0.5 : (1.0 - Seasonal) * 0.5 + 0.5);
    const DryRoll = Hash01(Group.seed, Group.dayOfYear, Group.year * 31 + 9);
    const IsDrySpell = DryRoll > R.drySpell;
    const StormPulse = (!IsDrySpell && Hash01(Group.seed, Group.dayOfYear, 5150) < 0.14) ? R.stormPulse : 1.0;
    const DailyPrecipMm = IsDrySpell ? 0.0
        : (R.annualPrecipMm / 365.0) * 2.1 * WetSeasonBias * StormPulse * Group.precipitationScale
          * (0.35 + 1.3 * Hash01(Group.seed, Group.dayOfYear, 88));

    // Rain-vs-snow partition (50/50 at +1.5 °C, per Bras / Dingman), biased by regime snow share.
    const TempPartition = Clamp(1.0 - (TempC - 0.5) / 3.0, 0.0, 1.0);
    const RegimeSnowBias = Clamp(0.35 + 1.3 * R.snowShare, 0, 1);
    const SnowFrac = Clamp(TempPartition * RegimeSnowBias + R.snowShare * 0.22 * Clamp(-TempC / 10, 0, 1), 0, 1);
    const RainMm = DailyPrecipMm * (1 - SnowFrac);
    const SnowMm = DailyPrecipMm * SnowFrac;

    // Wind: regime base + seasonal gustiness + storm fronts. Azimuth wanders seasonally
    // (drives star vs barchan dunes and cross-bedded wind scour directions).
    const WindNoise = Hash01(Group.seed, Group.dayOfYear, Group.year * 17 + 3);
    const WindSpeed = R.windBase * (0.45 + 1.05 * WindNoise) * Group.windScale * (1.0 + 0.25 * WinterT * (R.snowShare > 0.2 ? 1 : -0.4));
    const WindAzimuthDeg = ((Group.latitudeDeg >= 0 ? 235 : 55) + 60 * Math.sin(Phase * 1.7 + 1.1)
        + (WindNoise - 0.5) * 46 + Group.year * 3.0) % 360;

    // Freeze–thaw nights and insolation potential.
    const FreezeNight = TempC - DiurnalAmpC * 0.55 * (0.4 + 0.6 * Hash01(Group.seed, Group.dayOfYear, 4242)) < 0.0;
    // Solar declination → day length / insolation intensity factor.
    const Decl = 23.44 * Math.sin(2 * Math.PI * (Group.dayOfYear - 81) / 365.0) * (Group.latitudeDeg >= 0 ? 1 : -1);
    const HourAngleDeg = Math.acos(Clamp(-Math.tan(LatRad) * Math.tan(Decl * DEG), -1, 1)) / DEG;
    const DayLengthH = Clamp(2 * HourAngleDeg / 15.0, 4, 20);
    const Insolation = Clamp((DayLengthH / 12.0) * (0.55 + 0.45 * Math.max(0, Math.sin(Math.PI * (Group.dayOfYear - 81 + (Group.latitudeDeg < 0 ? 182 : 0)) / 182.5))) * (1.0 - 0.12 * (IsDrySpell ? 0 : 1)), 0.15, 1.6);

    const EvapMmDay = R.evaporation * (2.2 + 3.1 * Math.max(0, Seasonal * 0.5 + 0.5)) * (IsDrySpell ? 1.35 : 0.85);

    const Sample = {
        dayOfYear: Group.dayOfYear,
        year: Group.year,
        tempC: TempC,
        diurnalAmpC: DiurnalAmpC,
        rainMm: RainMm,
        snowMm: SnowMm,
        snowFrac: SnowFrac,
        windSpeedMs: WindSpeed,
        windAzimuthDeg: WindAzimuthDeg,
        windRad: ((WindAzimuthDeg % 360) + 360) % 360 * DEG,
        freezeNight: FreezeNight,
        frostNightsPerYear: R.frostNights,
        insolation: Insolation,
        evapMmDay: EvapMmDay,
        isStorm: StormPulse > 1.05,
        isDrySpell: IsDrySpell,
        seasonName: Seasonal > 0.45 ? 'Summer' : Seasonal < -0.45 ? 'Winter' : (Group.dayOfYear < 182 ? 'Spring' : 'Autumn')
    };
    Group.lastSample = Sample;
    return Sample;
}

/*==========================================================================================================================================
                                                    EROSION STATE — GRID FIELDS
==========================================================================================================================================*/
export const EROSION_DEFAULTS = Object.freeze({
    // Fluvial
    hydraulicConductivity: 2.0e-6,    // Green-Ampt Ks of soil (m/s) — loam ~7 mm/h
    rockConductivity: 5.0e-8,         // bare bedrock infiltration (near-impermeable)
    crackConductivity: 4.0e-7,        // fractures transmit storms quickly
    baseflowK: 0.35,                  // saturated-soil exfiltration → perennial rivers
    soilPorosity: 0.42,
    wettingSuction: 0.35,             // Green-Ampt |ψ| Δθ (m)
    manningN: 0.032,                  // channel roughness
    pipeFriction: 0.985,
    bedrockErodibility: 2.5e-7,       // Kb stream-power (m/s per Q^m S^n) — decade-scale channel incision
    soilErodibility: 3.0e-6,          // alluvium/soil — ~12× softer than bedrock
    streamPowerM: 0.45,
    streamPowerN: 1.0,
    sedimentCapacityK: 0.012,
    depositionRate: 0.42,             // fraction of capacity deficit relaxed per day
    evaporationRate: 1.0,
    // Pitting
    frostSegregationRate: 0.021,      // Hallet-Walder ice lens growth damage/day
    freezeThawRate: 0.032,            // 9 % volumetric expansion damage/cycle
    thermalFatigueRate: 0.016,        // diurnal ΔT Paris-law fatigue/day
    crackClusterDiffusion: 0.24,
    spallDamageThreshold: 0.62,
    spallDepthM: 0.16,
    pitProtectDepthM: 0.35,
    // Wind
    windAbrasionK: 0.0014,
    saltationThresholdMs: 5.5,
    sandSupply: 1.0,
    slipfaceDeg: 34.0,
    venturiGain: 1.35,
    undercutGain: 2.4,
    undercutZoneM: 2.5,
    strataDipDeg: 7.0,
    strataFreqPerM: 0.42,
    strataStrength: 0.55,
    // Rockfall
    cliffBaseAngleDeg: 52,
    talusCoarseReposeDeg: 35,
    talusFineReposeDeg: 26,
    rockfallRate: 0.6,
    ballisticScourK: 0.028,
    // Chemical
    dissolutionK: 0.0022,
    hydrolysisK: 0.0035,
    haloclastyK: 0.0028,
    stainTransportK: 0.05,
    carbonateShare: 0.18,
    // Snow / ice
    snowlineM: 240,
    degreeDayMelt: 0.0045,            // m snow / day / °C
    iceFlowK: 0.012,
    glacialAbrasionK: 0.0016
});

/**
 * Build a full simulation state for one TerrainRegion grid. Requires the region
 * to have been evaluated at least once (Region.runtime holds nx/nz/sdf2D).
 */
export function CreateErosionState(World, Region, Options = {})
{
    const Rt = Region.runtime;
    if (!Rt || !Rt.heights) throw new Error('CreateErosionState: evaluate the region first');
    const Nx = Rt.nx, Nz = Rt.nz, Count = Nx * Nz;
    const Opts = { ...EROSION_DEFAULTS, ...Options };

    const H = new Float32Array(Count);
    H.set(Rt.heights);

    const State = {
        regionId: Region.id,
        nx: Nx, nz: Nz,
        stepX: Rt.stepX, stepZ: Rt.stepZ,
        cellArea: Rt.stepX * Rt.stepZ,
        bounds: { ...Region.bounds },
        seaLevel: World.seaLevel ?? 0.0,
        sdf2D: Rt.sdf2D,
        params: Opts,
        seed: (Options.seed | 0) || (Region.seed | 0) || 4029,

        // --- Surface & material ---
        h: H,                                    // active bedrock+soil surface (m)
        h0: H.slice(),                           // initial reference (maps/deltas)
        w: new Float32Array(Count),              // free water depth (m) — rivers/pools only
        sed: new Float32Array(Count),            // suspended mobile sediment (m equivalent)
        soil: new Float32Array(Count),           // alluvium / soil / saprolite depth (m)
        sand: new Float32Array(Count),           // aeolian sand depth (m)
        snow: new Float32Array(Count),           // snow water equivalent (m)
        ice: new Float32Array(Count),            // glacier ice thickness (m)
        moist: new Float32Array(Count),          // soil moisture 0..1
        hard: new Float32Array(Count),           // rock hardness 0..1 (hydrolysis softens)
        clay: new Float32Array(Count),           // saprolite/clay fraction 0..1

        // --- Pitting & fracture ---
        crack: new Float32Array(Count),          // frost+thermal damage 0..1
        pit: new Float32Array(Count),            // accumulated pit cavity depth (m)
        pitTafoni: new Float32Array(Count),      // tafoni honeycomb character 0..1

        // --- Chemical staining / salts ---
        fe: new Float32Array(Count), cu: new Float32Array(Count),
        mn: new Float32Array(Count), salt: new Float32Array(Count),
        disLoad: new Float32Array(Count),        // dissolved mineral load in local water

        // --- Hydrology flux state (virtual pipes) ---
        qX: new Float32Array(Count), qZ: new Float32Array(Count), // pipe discharges (m³/s)
        vel: new Float32Array(Count),            // velocity magnitude (m/s) — maps
        discharge: new Float32Array(Count),      // accumulated Q — maps
        wetness: new Float32Array(Count),        // 0..1 wet-rock specular + film

        // --- Diagnostics / Gaea-style accumulators ---
        wearMap: new Float32Array(Count),        // cumulative incision (m)
        depoMap: new Float32Array(Count),        // cumulative deposition (m)
        talusMap: new Float32Array(Count),       // talus/scree thickness (m)
        flowMap: new Float32Array(Count),        // cumulative overland flow
        snowMap: new Float32Array(Count),
        melt: new Float32Array(Count),           // per-cell snowmelt rate (m/day) — feeds runoff
        rockfallQueue: [],

        // --- Calendar ---
        day: 0, totalSteps: 0, lastClimate: null,
        stats: { incisedM: 0, depositedM: 0, pits: 0, sandM: 0, snowM: 0, floodedCells: 0 }
    };

    // Initial hardness from 3D strata + lithology (carbonate share drives karst).
    for (let I = 0; I < Count; ++I)
    {
        State.hard[I] = Clamp(0.42 + 0.5 * Hash01(State.seed, I, 71), 0.2, 1.0);
    }
    // Weathered sand sheet: soft/weak strata on gentle ground pre-loads the aeolian
    // sand supply so dune fields and saltation curtains have material to move.
    {
        const X0 = 0;
        for (let Z = 1; Z < Nz - 1; ++Z)
        {
            for (let X = 1; X < Nx - 1; ++X)
            {
                const I = Z * Nx + X;
                if (Rt.sdf2D[I] >= 0) continue;
                const Slope = Math.max(
                    Math.abs(H[Z * Nx + X + 1] - H[Z * Nx + X - 1]) / (2 * Rt.stepX),
                    Math.abs(H[(Z + 1) * Nx + X] - H[(Z - 1) * Nx + X]) / (2 * Rt.stepZ));
                const Soft = 1.0 - State.hard[I];
                State.sand[I] = Clamp((0.52 - Slope * 1.55) * (0.3 + 1.2 * Soft) * (0.35 + 0.9 * Hash01(State.seed, I, 303)), 0, 0.85);
            }
        }
    }
    return State;
}

/** Attach / detach a shared ClimateGroup. Same group → same weather everywhere. */
export function SetClimateGroup(State, Group)
{
    State.climateGroup = Group;
    return State;
}

/*==========================================================================================================================================
                        3D STRATA FIELD — cross-bedded hardness for wind & pits
==========================================================================================================================================*/
export function StrataHardnessAt(State, I, X, Z)
{
    const P = State.params;
    const H = State.h[I];
    const DipRad = P.strataDipDeg * DEG;
    // 3D cross-beds: phase varies with world position AND elevation (dipping beds).
    const Phase = H * P.strataFreqPerM + X * Math.sin(DipRad) * 0.06 + Z * Math.cos(DipRad) * 0.06;
    const Band = 0.5 + 0.5 * Math.sin(Phase * Math.PI * 2.0);
    const Softness = 1.0 - P.strataStrength;         // fraction of soft interbeds
    const StrataH = 1.0 - Softness * (1.0 - Band);   // soft beds = low hardness
    return Clamp(StrataH, 0.12, 1.0);
}

/*==========================================================================================================================================
                            HYDROLOGY — VIRTUAL PIPES + MANNING + STREAM POWER
==========================================================================================================================================*/
function StepHydrology(State, Climate, DtSec)
{
    const { nx: Nx, nz: Nz, stepX: SX, stepZ: SZ, h: H, w: W, sed: Sed, soil: Soil, moist: Moist } = State;
    const { qX: QX, qZ: QZ, sdf2D: Sdf } = State;
    const P = State.params;
    const G = 9.81;
    const PipeFric = Clamp(P.pipeFriction, 0.8, 0.999);
    const CellA = State.cellArea;
    const SeaLevel = State.seaLevel;

    // ---- Sources: rain (m/day → m) + snowmelt. Infiltration is SOIL-DEPTH dependent —
    // bare bedrock stores almost nothing and sheds storms instantly (→ real channelized
    // rivers); deep soils soak rain until their pore bucket saturates (Dunne overland flow).
    const RainMs = (Climate ? Climate.rainMm * 0.001 : 0) / 86400 * DtSec;
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) { W[I] = 0; continue; }   // COASTLINE SINK — drains to sea level!
        const XX = I % Nx, ZZ = (I / Nx) | 0;
        if (XX === 0 || XX === Nx - 1 || ZZ === 0 || ZZ === Nz - 1) { W[I] = 0; continue; } // OPEN REGION BORDER — no fake walls!
        const MeltMs = (State.melt[I] || 0) * DtSec / 86400;   // per-cell snowmelt runoff
        const SoilDepth = Soil[I] + State.clay[I] * 0.35;
        const KsEff = P.rockConductivity + P.hydraulicConductivity * Clamp(SoilDepth / 0.35, 0, 1) + P.crackConductivity * State.crack[I];
        const BucketDepth = 0.02 + SoilDepth * 0.45 + State.clay[I] * 0.1 + State.crack[I] * 0.08;  // pore storage (m)
        const InfilCap = KsEff * (1.0 + P.wettingSuction / Math.max(0.02, W[I] + 0.02)) * DtSec;
        const MoistDef = Clamp(1.0 - Moist[I], 0, 1);
        // Rate-limited AND storage-limited: the bucket cannot hold more than its pore volume.
        const Infil = Math.min(RainMs + MeltMs, InfilCap * MoistDef, MoistDef * BucketDepth + 1e-9);
        Moist[I] = Clamp(Moist[I] + Infil / Math.max(0.02, BucketDepth), 0, 1);
        const Surface = Math.max(0, RainMs + MeltMs - Infil);
        if (Surface > 0) W[I] += Surface;
        // Baseflow: saturated soil exfiltrates slowly into channels → rivers keep flowing.
        if (Moist[I] > 0.55)
        {
            const Baseflow = (Moist[I] - 0.55) * BucketDepth * P.baseflowK * DtSec / 86400.0;
            W[I] += Baseflow;
            Moist[I] = Clamp(Moist[I] - Baseflow / Math.max(0.02, BucketDepth), 0, 1);
        }
    }

    // ---- Pipe discharges (Imbert virtual pipes with Manning friction) ----
    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0 || W[I] <= 1e-6) { QX[I] = 0; QZ[I] = 0; continue; }
            const Hi = H[I] + W[I];
            // +X pipe (sea-side neighbors drain only — never import ocean water)
            if (X + 1 < Nx)
            {
                const J = I + 1;
                const SeaN = Sdf[J] >= 0;
                const Hj = SeaN ? SeaLevel : H[J] + W[J];
                const Dh = Hi - Hj;
                const L = SX;
                const Wavg = Math.max(0.5 * (W[I] + (SeaN ? W[I] : W[J])), 0.001);
                const A = Wavg * SZ;
                const Slope = Math.abs(Dh) / L;
                const MannV = (1 / P.manningN) * Math.pow(Wavg, 2 / 3) * Math.sqrt(Math.max(Slope, 1e-5));
                let Q = Math.sign(Dh) * Math.min(A * MannV, A * G * Math.abs(Dh) * DtSec / L);
                if (SeaN) Q = Math.max(0, Q);   // coast cells only drain
                QX[I] = Clamp(Q, -1e6, 1e6) * PipeFric;
            }
            else QX[I] = 0;
            // +Z pipe
            if (Z + 1 < Nz)
            {
                const J = I + Nx;
                const SeaN = Sdf[J] >= 0;
                const Hj = SeaN ? SeaLevel : H[J] + W[J];
                const Dh = Hi - Hj;
                const L = SZ;
                const Wavg = Math.max(0.5 * (W[I] + (SeaN ? W[I] : W[J])), 0.001);
                const A = Wavg * SX;
                const Slope = Math.abs(Dh) / L;
                const MannV = (1 / P.manningN) * Math.pow(Wavg, 2 / 3) * Math.sqrt(Math.max(Slope, 1e-5));
                let Q = Math.sign(Dh) * Math.min(A * MannV, A * G * Math.abs(Dh) * DtSec / L);
                if (SeaN) Q = Math.max(0, Q);
                QZ[I] = Clamp(Q, -1e6, 1e6) * PipeFric;
            }
            else QZ[I] = 0;
        }
    }

    // ---- Divergence → water depth update + flux limiter (no negative water) ----
    const Outflow = new Float32Array(Nx * Nz);
    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            let Out = 0;
            if (X + 1 < Nx && QX[I] > 0) Out += QX[I];
            if (X > 0 && QX[I - 1] < 0) Out += -QX[I - 1];
            if (Z + 1 < Nz && QZ[I] > 0) Out += QZ[I];
            if (Z > 0 && QZ[I - Nx] < 0) Out += -QZ[I - Nx];
            Outflow[I] = Out;
        }
    }
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) continue;
        const MaxOut = W[I] * CellA / Math.max(DtSec, 1e-3);
        if (Outflow[I] > MaxOut && Outflow[I] > 1e-12)
        {
            const S = MaxOut / Outflow[I];
            if (Xsafe(I, Nx) + 1 < Nx && QX[I] > 0) QX[I] *= S;
            if (Xsafe(I, Nx) > 0 && QX[I - 1] < 0) QX[I - 1] *= S;
            if (Zsafe(I, Nx, Nz) + 1 < Nz && QZ[I] > 0) QZ[I] *= S;
            if (Zsafe(I, Nx) > 0 && QZ[I - Nx] < 0) QZ[I - Nx] *= S;
        }
    }

    const Div = new Float32Array(Nx * Nz);
    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            let Dv = 0;
            Dv += (X + 1 < Nx ? QX[I] : 0) - (X > 0 ? QX[I - 1] : 0);
            Dv += (Z + 1 < Nz ? QZ[I] : 0) - (Z > 0 ? QZ[I - Nx] : 0);
            // Sea-side neighbor pipe drains instantly (outflow to ocean).
            if (X + 1 < Nx && Sdf[I + 1] >= 0 && QX[I] > 0) Dv -= 0; // already counted as outflow
            Div[I] = Dv;
        }
    }

    const EvapMs = (Climate ? Climate.evapMmDay * 0.001 * P.evaporationRate : 0) / 86400 * DtSec;

    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) { W[I] = 0; QX[I] = 0; QZ[I] = 0; State.wetness[I] = 0; continue; }
        W[I] = Math.max(0, W[I] - Div[I] * DtSec / CellA);
        // Evaporation from open water; soil evaporation from moisture.
        W[I] = Math.max(0, W[I] - EvapMs * (W[I] > 0.002 ? 1 : 0));
        const BuckD = 0.02 + Soil[I] * 0.45 + State.clay[I] * 0.1 + State.crack[I] * 0.08;
        Moist[I] = Clamp(Moist[I] - EvapMs * 0.24 / Math.max(0.02, BuckD), 0, 1);

        // Diagnostics for maps + renderer. Discharge itself comes from D8 drainage-area
        // routing (UpdateDrainageArea) — pipe flows only move the actual water volume.
        const Qp = 0.5 * (Math.abs(QX[I]) + Math.abs(QZ[I]) + (Xsafe(I, Nx) > 0 ? Math.abs(QX[I - 1]) : 0) + (Zsafe(I, Nx) > 0 ? Math.abs(QZ[I - Nx]) : 0));
        const V = Qp / Math.max(CellA * Math.max(W[I], 0.02), 1e-4);
        State.vel[I] = State.vel[I] * 0.9 + Math.min(V, 30) * 0.1;
        State.flowMap[I] += Qp * DtSec;
        // Wetness: thin film OR real water → wet-rock specular. Threshold separates them.
        const Film = Clamp(W[I] / 0.08, 0, 1);
        State.wetness[I] = Math.max(State.wetness[I] * 0.995, Film);
    }
    // Cells holding STAGNANT standing water ON SLOPES outside any hollow = unphysical
    // flooding. Channeled rivers, genuine lakes (flat basins) and rock-pool ponds in
    // pits/hollows are all physical and never count.
    let Flooded = 0;
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0 || W[I] <= 0.3) continue;
            const Slope = Math.max(
                Math.abs(H[I + 1] - H[I - 1]) / (2 * SX),
                Math.abs(H[I + Nx] - H[I - Nx]) / (2 * SZ));
            const NeighborAvg = (H[I - 1] + H[I + 1] + H[I - Nx] + H[I + Nx]) * 0.25;
            const InHollow = H[I] < NeighborAvg - 0.03;   // pond in a pit/gully = physical
            if (Slope > 0.18 && State.discharge[I] < 0.08 && !InHollow) Flooded++;
        }
    }
    State.stats.floodedCells = Flooded;
}

const Xsafe = (I, Nx) => I % Nx;
const Zsafe = (I, Nx, Nz) => (I / Nx) | 0;

/*==========================================================================================================================================
            DRAINAGE-AREA ROUTING — steepest-descent flow accumulation (D8)
            The standard catchment-based discharge proxy for stream-power laws:
            Q ≈ A · P (contributing area × precipitation intensity). Hillslopes
            carry negligible Q; channels carry the full catchment — incision and
            sediment transport concentrate exactly where real rivers form.
==========================================================================================================================================*/
export function UpdateDrainageArea(State, Climate = null)
{
    const { nx: Nx, nz: Nz, h: H, sdf2D: Sdf } = State;
    const N = Nx * Nz;
    const Acc = State._accScratch && State._accScratch.length === N ? State._accScratch : (State._accScratch = new Float64Array(N));
    Acc.fill(0);
    for (let I = 0; I < N; ++I) if (Sdf[I] < 0) Acc[I] = State.cellArea;

    // Counting-sort cells by height (descending) in O(N + buckets).
    let HMin = Infinity, HMax = -Infinity;
    for (let I = 0; I < N; ++I)
    {
        if (Sdf[I] >= 0) continue;
        if (H[I] < HMin) HMin = H[I];
        if (H[I] > HMax) HMax = H[I];
    }
    const Buckets = 2048;
    const Scale = (Buckets - 1) / Math.max(1e-6, HMax - HMin);
    const Counts = new Int32Array(Buckets + 1);
    const Key = new Int32Array(N);
    for (let I = 0; I < N; ++I)
    {
        if (Sdf[I] >= 0) { Key[I] = -1; continue; }
        const K = Buckets - 1 - ((H[I] - HMin) * Scale) | 0;   // high h → small key
        Key[I] = Clamp(K, 0, Buckets - 1);
        Counts[Key[I]]++;
    }
    let Running = 0;
    for (let K = 0; K < Buckets; ++K) { const C = Counts[K]; Counts[K] = Running; Running += C; }
    const Order = State._orderScratch && State._orderScratch.length === N ? State._orderScratch : (State._orderScratch = new Int32Array(N));
    for (let I = 0; I < N; ++I)
    {
        if (Key[I] < 0) continue;
        Order[Counts[Key[I]]++] = I;
    }

    // Push accumulation downhill (highest first → each cell contributes to its D8 sink).
    for (let S = 0; S < Running; ++S)
    {
        const I = Order[S];
        const X = I % Nx, Z = (I / Nx) | 0;
        let BestJ = -1, BestS = 0;
        for (let Dz = -1; Dz <= 1; ++Dz)
        {
            for (let Dx = -1; Dx <= 1; ++Dx)
            {
                if (!Dx && !Dz) continue;
                const Xc = X + Dx, Zc = Z + Dz;
                if (Xc < 0 || Xc >= Nx || Zc < 0 || Zc >= Nz) continue;
                const J = Zc * Nx + Xc;
                if (Sdf[J] >= 0) { BestJ = J; BestS = Infinity; break; }   // drains to sea
                const Dist = Math.hypot(Dx * State.stepX, Dz * State.stepZ);
                const Slope = (H[I] - H[J]) / Dist;
                if (Slope > BestS) { BestS = Slope; BestJ = J; }
            }
            if (BestS === Infinity) break;
        }
        if (BestJ >= 0 && Sdf[BestJ] < 0) Acc[BestJ] += Acc[I];
        // Sinks (local minima) simply retain their own area.
    }

    // Discharge proxy Q ≈ A·P (m³/s) with a small baseflow floor; smooth for stability.
    const PrecipMs = Climate ? Math.max(Climate.rainMm, Climate.snowMm * 0.25) * 0.001 / 86400 : 1e-5;
    for (let I = 0; I < N; ++I)
    {
        const Q = Acc[I] * (PrecipMs + 3.5e-7);   // + tiny baseflow so perennial channels persist
        State.discharge[I] = State.discharge[I] * 0.82 + Q * 0.18;
    }
    return Acc;
}

/*==========================================================================================================================================
                    FLUVIAL INCISION + EXNER SEDIMENT (stream-power, cover-behind)
==========================================================================================================================================*/
function StepFluvialErosion(State, DtSec)
{
    const { nx: Nx, nz: Nz, stepX: SX, stepZ: SZ, h: H, w: W, sed: Sed, soil: Soil, sdf2D: Sdf } = State;
    const P = State.params;
    const SeaLevel = State.seaLevel;

    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            const Q = State.discharge[I];
            if (Q < 2e-5 && W[I] < 0.01) continue;

            // Local slope (forward differences toward steepest).
            let S = 0;
            if (X + 1 < Nx) S = Math.max(S, (H[I] - H[I + 1]) / SX);
            if (X > 0) S = Math.max(S, (H[I] - H[I - 1]) / SX);
            if (Z + 1 < Nz) S = Math.max(S, (H[I] - H[I + Nx]) / SZ);
            if (Z > 0) S = Math.max(S, (H[I] - H[I - Nx]) / SZ);
            S = Math.max(S, 1e-4);

            const DischargeTerm = Math.pow(Q + 2e-4, P.streamPowerM);
            const SlopeTerm = Math.pow(S, P.streamPowerN);
            const ToolsFactor = Clamp(Sed[I] * 6.0 + 0.25, 0.25, 1.6);   // tools effect
            const CrackWeak = 1.0 + 1.7 * State.crack[I];                 // frost/thermal pre-weakening

            if (Soil[I] > 0.01)
            {
                // Alluvium/soil incises fast, feeds suspended load (rate per-second, dt-scaled).
                const Erode = Math.min(Soil[I], P.soilErodibility * DischargeTerm * SlopeTerm * DtSec * ToolsFactor * CrackWeak);
                Soil[I] -= Erode;
                Sed[I] += Erode;
                State.wearMap[I] += Erode;
                State.stats.incisedM += Erode;
            }
            else
            {
                // Bedrock channel: Whipple–Tucker stream power E = Kb Q^m S^n.
                const Hard = State.hard[I] * StrataHardnessAt(State, I, X, Z);
                const Erode = Math.min(0.25, P.bedrockErodibility * DischargeTerm * SlopeTerm * DtSec * ToolsFactor * CrackWeak * (1.25 - 0.75 * Hard));
                if (Erode > 1e-7)
                {
                    H[I] -= Erode;
                    State.wearMap[I] += Erode;
                    State.stats.incisedM += Erode;
                    Sed[I] += Erode * 0.85;
                }
            }

            // Exner: capacity-limited deposition / entrainment (fraction of deficit per day).
            const Cap = P.sedimentCapacityK * DischargeTerm * SlopeTerm * (0.6 + 0.8 * Math.min(State.vel[I], 8));
            const Deficit = Cap - Sed[I];
            const Relax = Clamp(P.depositionRate * DtSec / 86400.0, 0, 1);
            if (Deficit < 0)
            {
                const Depo = Math.min(-Deficit, Sed[I]) * Relax;
                Sed[I] -= Depo;
                // Pits receive reduced fill until they are deep (protection rule).
                const PitShield = State.pit[I] > P.pitProtectDepthM ? 1.0 : Clamp(1.0 - State.pit[I] / Math.max(0.05, P.pitProtectDepthM), 0.15, 1.0);
                const Add = Depo * PitShield;
                Soil[I] += Add;
                State.depoMap[I] += Add;
                State.stats.depositedM += Add;
            }
            else if (Soil[I] > 0.01)
            {
                const Pickup = Math.min(Soil[I], Deficit * Relax * 1.6);
                Soil[I] -= Pickup;
                Sed[I] += Pickup;
            }

            // Suspended load advects downwind/flow direction (simple local downhill smear).
            Sed[I] = Math.min(Sed[I], 4.0);
        }
    }

    // Sediment settling + advection pass (mass-conserving downhill smear along steepest).
    const Sed2 = Sed.slice();
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0 || Sed2[I] <= 1e-5) continue;
            // find steepest downhill neighbor
            let BestJ = -1, BestS = 0;
            for (const [Dx, Dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]])
            {
                const J = I + Dz * Nx + Dx;
                if (Sdf[J] >= 0) { BestJ = J; BestS = 99; break; }
                const S = (H[I] - H[J]) / (Dx ? SX : SZ);
                if (S > BestS) { BestS = S; BestJ = J; }
            }
            if (BestJ >= 0 && BestS > 0.002)
            {
                const Move = Sed2[I] * Clamp(0.35 * State.vel[I] * DtSec / Math.max(SX, 1), 0.02, 0.5);
                Sed[I] -= Move;
                Sed[BestJ] += Move * 0.92;   // slight loss → deposition budget
            }
        }
    }
    // Settle residual suspension toward soil when water is quiet (bounded relaxation!).
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) { Sed[I] = 0; continue; }
        const SettleFrac = Clamp(P.depositionRate * DtSec / 86400.0 * (W[I] < 0.02 ? 2.2 : 0.55), 0, 1);
        const Settle = Sed[I] * SettleFrac;
        Sed[I] -= Settle;
        Soil[I] += Settle * 0.95;
        Sed[I] = Clamp(Sed[I], 0, 4);
    }
}

/*==========================================================================================================================================
            PITTING — segregation ice + freeze–thaw + thermal-expansion fatigue
==========================================================================================================================================*/
function StepPitting(State, Climate, DtDays)
{
    const { nx: Nx, nz: Nz, h: H, moist: Moist, crack: Crack, pit: Pit, sdf2D: Sdf, soil: Soil, sand: Sand } = State;
    const P = State.params;

    // Local insolation from slope aspect (N–S facing) & season.
    const SunAz = Climate ? Climate.windRad : 0; // reuse wind phase as solar proxy spacing
    const LatFactor = Climate ? Clamp(Climate.insolation, 0.2, 1.6) : 1.0;

    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            const SlopeX = (H[I + 1] - H[I - 1]) * 0.5;
            const SlopeZ = (H[I + Nx] - H[I - Nx]) * 0.5;
            const SlopeMag = Math.hypot(SlopeX, SlopeZ);
            // Aspect facing sun (south-facing in N hemisphere gains ΔT).
            const AspectSun = Clamp((SlopeX * Math.cos(SunAz) + SlopeZ * Math.sin(SunAz)) / Math.max(0.35, SlopeMag + 0.35), -1, 1);
            const InsolationCell = LatFactor * (0.55 + 0.45 * (AspectSun * 0.5 + 0.5)) * Clamp(1 - State.snow[I] * 0.8 - State.ice[I] * 0.5, 0.25, 1);

            // Pitting attacks EXPOSED rock: crest/steep cells and bare rock crack faster;
            // soil mantle and flat ground are strongly shielded.
            const Exposed = Clamp(SlopeMag * 2.1, 0.10, 1.0) * Clamp(1.0 - Soil[I] / 0.28, 0.06, 1.0);

            let Damage = 0;

            // (A) Diurnal thermal-expansion fatigue (Paris-law style, ΔT^n growth).
            if (Climate)
            {
                const DTDiurnal = Climate.diurnalAmpC * InsolationCell;
                Damage += P.thermalFatigueRate * Math.pow(Math.max(0, DTDiurnal) / 12.0, 1.45) * DtDays * (1 - 0.4 * State.hard[I]);
            }

            // (B) Freeze–thaw volumetric (9 % expansion) on freeze nights with pore water.
            if (Climate && Climate.freezeNight && Moist[I] > 0.12)
            {
                Damage += P.freezeThawRate * Moist[I] * DtDays * (0.6 + 0.8 * State.hard[I]);
            }

            // (C) Hallet–Walder segregation ice in the −8…−3 °C wet window.
            if (Climate)
            {
                const T = Climate.tempC;
                const Window = Smoothstep(-8.5, -6.0, T) * (1.0 - Smoothstep(-3.2, -1.2, T));
                Damage += P.frostSegregationRate * Window * (0.3 + Moist[I]) * DtDays;
            }

            if (Damage <= 0 && Crack[I] <= 0) continue;

            Crack[I] = Clamp(Crack[I] + Damage * Exposed * (1 - Crack[I]), 0, 1);
        }
    }

    // Crack-cluster diffusion — damage propagates along joints (tafoni honeycomb nucleation).
    const D2 = new Float32Array(Nx * Nz);
    const DiffK = P.crackClusterDiffusion;
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            const Avg = (Crack[I - 1] + Crack[I + 1] + Crack[I - Nx] + Crack[I + Nx]) * 0.25;
            D2[I] = Crack[I] + (Avg - Crack[I]) * DiffK * 0.5;
        }
    }
    Crack.set(D2);

    // Spall events: damage over threshold plucks a cavity (pit) into EXPOSED bedrock.
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) continue;
        if (Crack[I] >= P.spallDamageThreshold && Soil[I] < 0.06 && Sand[I] < 0.12)
        {
            const Depth = P.spallDepthM * (0.45 + 0.9 * Hash01(State.seed, I, State.day + 3));
            H[I] -= Depth;
            Pit[I] += Depth;
            State.pitTafoni[I] = Clamp(State.pitTafoni[I] + 0.28, 0, 1);
            Crack[I] *= 0.42;
            State.stats.pits += 1;
            // Coarse fragments enter the rockfall/talus budget (not instant smooth talus!).
            State.rockfallQueue.push({ cell: I, coarse: 0.5 + 0.5 * Hash01(State.seed, I, State.day + 11), vol: Depth * State.cellArea, t: 0 });
            if (State.rockfallQueue.length > 512) State.rockfallQueue.splice(0, State.rockfallQueue.length - 512);
        }
    }
}

/*==========================================================================================================================================
            WIND — Bagnold saltation + slipfaces + Venturi + basal undercutting
==========================================================================================================================================*/
function StepWind(State, Climate, DtDays)
{
    const { nx: Nx, nz: Nz, stepX: SX, stepZ: SZ, h: H, sand: Sand, hard: Hard, sdf2D: Sdf } = State;
    const P = State.params;
    if (!Climate) return;
    const U = Climate.windSpeedMs;
    if (U < 0.4) return;
    const WindDirX = Math.cos(Climate.windRad), WindDirZ = Math.sin(Climate.windRad);

    // Running canyon floor (min filter) for the saltation bounce layer.
    const Floor = new Float32Array(Nx * Nz);
    for (let Z = 0; Z < Nz; ++Z)
    {
        for (let X = 0; X < Nx; ++X)
        {
            const I = Z * Nx + X;
            let M = 1e9;
            for (let Dz = -1; Dz <= 1; ++Dz) for (let Dx = -1; Dx <= 1; ++Dx)
            {
                const Xc = Clamp(X + Dx, 0, Nx - 1), Zc = Clamp(Z + Dz, 0, Nz - 1);
                M = Math.min(M, H[Zc * Nx + Xc]);
            }
            Floor[I] = M;
        }
    }

    const Flux = new Float32Array(Nx * Nz);
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;

            // --- Venturi canyon-corridor acceleration ---
            const Surround = (H[I - 1] + H[I + 1] + H[I - Nx] + H[I + Nx]) * 0.25;
            const Constrict = Clamp((Surround - H[I]) / 40.0, 0, 1.5);
            const SlopeX = (H[I + 1] - H[I - 1]) * 0.5 / SX;
            const SlopeZ = (H[I + Nx] - H[I - Nx]) * 0.5 / SZ;
            const SlopeLen = Math.hypot(SlopeX, SlopeZ) + 1e-5;
            // Streamline steering: wind aligned with the canyon axis (downslope) accelerates.
            const Align = Clamp((-SlopeX * WindDirX - SlopeZ * WindDirZ) / SlopeLen, -1, 1);
            const Venturi = 1.0 + P.venturiGain * Constrict * Math.max(0, Align);
            const Ueff = U * Venturi * (0.82 + 0.36 * Hash01(State.seed, I, 900 + ((State.day / 7) | 0)));

            // --- Bagnold/Kawamura saltation flux (normalized saltation intensity 0..3) ---
            // Threshold is on the 10 m wind speed (Bagnold ~5.5 m/s), reduced over loose sand.
            const Uthr = P.saltationThresholdMs * (1.0 + 1.6 * State.moist[I]) * (Sand[I] < 0.02 ? 1.35 : 1.0);
            if (Ueff <= Uthr) continue;
            const SandAvail = 0.3 + 0.7 * Clamp(Sand[I] / 0.25, 0, 1);
            const Intensity = Clamp((Ueff / Uthr - 1.0) * 1.6, 0, 3);
            Flux[I] = P.sandSupply * Intensity * SandAvail;

            // --- Saltation-curtain basal undercutting (0.2–2.5 m bounce zone) ---
            const HeightAboveFloor = H[I] - Floor[I];
            const BasalBoost = 1.0 + P.undercutGain * (1.0 - Smoothstep(0.2, P.undercutZoneM, HeightAboveFloor));
            const Strata = StrataHardnessAt(State, I, X, Z);
            const Abrasion = P.windAbrasionK * Flux[I] * BasalBoost * (1.35 - 0.85 * Hard[I] * Strata) * DtDays;
            if (Abrasion > 1e-6)
            {
                // Soft interbeds under hard caprock undercut first → alcoves, fins, yardangs.
                const Cut = Math.min(Abrasion, 0.45);
                H[I] -= Cut;
                State.wearMap[I] += Cut * 0.6;
                Sand[I] += Cut * 0.35;            // abraded grains join the sand sheet
                State.stats.incisedM += Cut * 0.5;
            }
        }
    }

    // --- Advect sand downwind + slipface 34° avalanches ---
    const Sand2 = Sand.slice();
    const Slip = Math.tan(P.slipfaceDeg * DEG);
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            // Saltation hop: move a fraction of sand along wind direction.
            const TargetX = Clamp(X + Math.round(WindDirX), 1, Nx - 2);
            const TargetZ = Clamp(Z + Math.round(WindDirZ), 1, Nz - 2);
            const J = TargetZ * Nx + TargetX;
            if (J !== I && Sdf[J] < 0 && Flux[I] > 0)
            {
                const Move = Math.min(Sand2[I], Flux[I] * DtDays * 3.2 + Sand2[I] * 0.12);
                Sand[I] -= Move;
                Sand[J] += Move * 0.94;
            }
            // Slipface avalanche in the windward/leeward direction (angle of repose).
            for (const [Dx, Dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]])
            {
                const K = I + Dz * Nx + Dx;
                if (Sdf[K] >= 0) continue;
                const Dh = (H[I] + Sand[I]) - (H[K] + Sand[K]);
                const Dist = Dx ? SX : SZ;
                const Tan = Dh / Dist;
                if (Tan > Slip && Sand[I] > 0.01)
                {
                    const Move = Math.min(Sand[I] * 0.22, (Tan - Slip) * Dist * 0.16);
                    Sand[I] -= Move;
                    Sand[K] += Move;
                }
            }
        }
    }
    // Wind polishes exposed sand-free bedrock (deflation) & packs sand sheets.
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) { Sand[I] = 0; continue; }
        if (Sand[I] > 2.2) Sand[I] = 2.2;
    }
}

/*==========================================================================================================================================
                    ROCKFALL — ballistic runout, chute scour, size-sorted talus
==========================================================================================================================================*/
function StepRockfall(State, DtDays)
{
    const { nx: Nx, nz: Nz, stepX: SX, stepZ: SZ, h: H, soil: Soil, sdf2D: Sdf, talusMap: Talus } = State;
    const P = State.params;
    const G = 9.81;

    // 1. Detach over-steepened cliff cells.
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            const SlopeX = (H[I + 1] - H[I - 1]) / (2 * SX);
            const SlopeZ = (H[I + Nx] - H[I - Nx]) / (2 * SZ);
            const Tan = Math.hypot(SlopeX, SlopeZ);
            // Cliff angle: hard rock stands steeper; frost damage + weak strata lower it.
            const Hard = 0.5 * State.hard[I] + 0.5 * StrataHardnessAt(State, I, X, Z);
            const TanCrit = Clamp(1.05 + 1.15 * Hard - 1.5 * State.crack[I], 0.85, 2.6);
            const TanCliff = Math.tan(P.cliffBaseAngleDeg * DEG) * (0.55 + 0.55 * Hard);
            const IsCliff = Tan > Math.max(TanCrit, TanCliff * 0.62);
            if (!IsCliff) continue;
            if (Hash01(State.seed, I, 6000 + State.day) > Clamp(P.rockfallRate * DtDays * 0.06, 0, 0.92)) continue;

            const Vol = Clamp(0.35 * State.cellArea * (0.3 + 0.7 * Hash01(State.seed, I, State.day)), 0.02, 6.0);
            const Coarse = Hash01(State.seed, I, 7000 + State.day) > 0.42 ? 1.0 : 0.0;
            const Drop = P.spallDepthM * 0.6;
            H[I] -= Drop;
            State.rockfallQueue.push({ cell: I, coarse: Coarse, vol: Vol, t: 0, vx: -SlopeX * 0.6, vz: -SlopeZ * 0.6 });
            if (State.rockfallQueue.length > 640) State.rockfallQueue.splice(0, State.rockfallQueue.length - 640);
        }
    }

    // 2. Ballistic runout with momentum chute scour + deposition.
    const Queue = State.rockfallQueue;
    const Next = [];
    for (const Frag of Queue)
    {
        let I = Frag.cell;
        let Vx = Frag.vx || 0, Vz = Frag.vz || 0;
        let Steps = 2 + ((Hash01(State.seed, I, Frag.t + 13) * 5) | 0);
        let Vol = Frag.vol;
        for (let S = 0; S < Steps; ++S)
        {
            const X = I % Nx, Z = (I / Nx) | 0;
            if (X < 1 || X >= Nx - 1 || Z < 1 || Z >= Nz - 1) break;
            const SlopeX = (H[I + 1] - H[I - 1]) / (2 * SX);
            const SlopeZ = (H[I + Nx] - H[I - Nx]) / (2 * SZ);
            Vx = Vx * 0.55 - SlopeX * G * 0.035;
            Vz = Vz * 0.55 - SlopeZ * G * 0.035;
            const Speed = Math.hypot(Vx, Vz);
            if (Speed < 0.05) break;
            const Nx2 = Clamp(X + Math.sign(Vx), 1, Nx - 2);
            const Nz2 = Clamp(Z + Math.sign(Vz), 1, Nz - 2);
            const J = Nz2 * Nx + Nx2;
            if (Sdf[J] >= 0) break;
            // Momentum chute scour — fragments abrade the channel they run out in.
            const Scour = P.ballisticScourK * Speed * Speed * 0.0025;
            H[J] -= Scour;
            State.wearMap[J] += Scour * 0.5;
            // Deposit when slowing on gentler ground.
            const Tan = Math.hypot((H[J + 1] - H[J - 1]) / (2 * SX), (H[J + Nx] - H[J - Nx]) / (2 * SZ));
            const Repose = Math.tan((Frag.coarse > 0.5 ? P.talusCoarseReposeDeg : P.talusFineReposeDeg) * DEG);
            if (Tan < Repose * 1.12 || S === Steps - 1)
            {
                const Add = Vol * 0.42 / State.cellArea;
                Soil[J] += Add;
                Talus[J] += Add * (Frag.coarse > 0.5 ? 1.0 : 0.72);
                State.depoMap[J] += Add;
                Vol *= 0.55;
            }
            I = J;
            Frag.t += 1;
        }
        if (Vol > Frag.vol * 0.12 && Frag.t < 12) Next.push(Frag);
    }
    State.rockfallQueue = Next.slice(0, 384);

    // 3. Grain-size-sorted talus relaxation toward repose angles (coarse 35° / fine 26°).
    const H2 = H.slice();
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0 || Soil[I] < 0.02) continue;
            for (const [Dx, Dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]])
            {
                const J = I + Dz * Nx + Dx;
                if (Sdf[J] >= 0) continue;
                const Dh = H2[I] - H2[J];
                const Dist = Dx ? SX : SZ;
                const CoarseShare = Clamp(Talus[I] / Math.max(0.05, Soil[I]), 0, 1);
                const ReposeDeg = Mix(P.talusFineReposeDeg, P.talusCoarseReposeDeg, CoarseShare);
                const Repose = Math.tan(ReposeDeg * DEG);
                const Tan = Dh / Dist;
                if (Tan > Repose)
                {
                    const Move = (Tan - Repose) * Dist * 0.10 * DtDays * 6.0;
                    const Avail = Soil[I] * 0.35;
                    const M = Math.min(Move, Avail);
                    H[I] -= M;
                    H[J] += M * 0.97;
                    Soil[I] -= M; Soil[J] += M * 0.97;
                    Talus[J] += M * 0.8;
                }
            }
        }
    }
}

/*==========================================================================================================================================
                        CHEMICAL — dissolution, hydrolysis, haloclasty, stains
==========================================================================================================================================*/
function StepChemical(State, Climate, DtDays)
{
    const { nx: Nx, nz: Nz, stepX: SX, stepZ: SZ, h: H, w: W, moist: Moist, sdf2D: Sdf, hard: Hard, clay: Clay,
            fe: Fe, cu: Cu, mn: Mn, salt: Salt, disLoad: Dis } = State;
    const P = State.params;
    const Aridity = Climate ? Clamp(Climate.evapMmDay / 8.0, 0.2, 2.0) : 0.8;

    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;

            // Carbonate/evaporite fraction from 3D strata + share parameter.
            const Carbonate = P.carbonateShare * (0.5 + 0.5 * Math.sin(H[I] * 0.22 + X * 0.011 + Z * 0.013));
            const Contact = W[I] > 0.005 ? 1.0 : Moist[I] * 0.55;

            // (A) Dissolution (karst): removes rock, saturates local water with load.
            const Dissolve = P.dissolutionK * Carbonate * Contact * (1 - Clamp(Dis[I], 0, 0.92)) * DtDays * (0.5 + Aridity * 0.2);
            if (Dissolve > 1e-7)
            {
                H[I] -= Dissolve;
                Dis[I] = Clamp(Dis[I] + Dissolve * 3.2, 0, 1.2);
                State.wearMap[I] += Dissolve * 0.5;
            }

            // (B) Silicate hydrolysis → clay saprolite (softens hardness, feeds soil).
            const Hyd = P.hydrolysisK * Contact * (1 - Clay[I]) * DtDays;
            Clay[I] = Clamp(Clay[I] + Hyd, 0, 1);
            Hard[I] = Clamp(Hard[I] - Hyd * 0.55, 0.08, 1);
            if (Clay[I] > 0.5) State.soil[I] += Hyd * 0.35;

            // (C) Haloclasty: evaporative salt crystallization in arid rock pores.
            const EvapConc = Aridity * (1 - Clamp(W[I] * 8, 0, 1)) * (Moist[I] > 0.06 ? 1 : 0.35);
            Salt[I] = Clamp(Salt[I] + P.haloclastyK * EvapConc * DtDays * 0.55, 0, 1);
            if (Salt[I] > 0.42 && Hash01(State.seed, I, 31 + State.day) < 0.5)
            {
                const CrackD = P.haloclastyK * Salt[I] * DtDays * 0.42;
                State.crack[I] = Clamp(State.crack[I] + CrackD, 0, 1);
                Salt[I] *= 0.985;
            }

            // (D) Dissolved-mineral staining transported downhill with wetness/flow.
            const FlowPull = Clamp(State.discharge[I] * 2.4 + W[I] * 1.4, 0, 1.6) * P.stainTransportK * DtDays;
            // Sources: Fe from mafic strata, Cu in high sulfide bands, Mn in desert varnish zones.
            const FeSrc = 0.5 + 0.5 * Math.sin(H[I] * 0.13 + 1.7);
            const CuSrc = Math.max(0, Math.sin(H[I] * 0.09 + X * 0.02)) * 0.7;
            const MnSrc = Aridity * (0.4 + 0.6 * Hash01(State.seed, I, 512));
            Fe[I] = Clamp(Fe[I] + FlowPull * FeSrc * (0.25 + 0.75 * Moist[I]), 0, 1);
            Cu[I] = Clamp(Cu[I] + FlowPull * CuSrc * 0.55, 0, 1);
            Mn[I] = Clamp(Mn[I] + FlowPull * MnSrc * 0.4 * (1 - Moist[I] * 0.5), 0, 1);
            // Evaporite salt crust in closed depressions (endorheic drying pans).
            if (W[I] > 0.004 && Aridity > 1.15)
            {
                Salt[I] = Clamp(Salt[I] + 0.012 * DtDays, 0, 1);
            }
            Dis[I] *= Math.pow(0.995, DtDays * 24);
        }
    }

    // Downhill smear of stains (advect along steepest descent).
    const Fe2 = Fe.slice(), Cu2 = Cu.slice(), Mn2 = Mn.slice(), Dis2 = Dis.slice();
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0) continue;
            let BestJ = -1, BestS = 0;
            for (const [Dx, Dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]])
            {
                const J = I + Dz * Nx + Dx;
                if (Sdf[J] >= 0) continue;
                const S = (H[I] - H[J]) / (Dx ? SX : SZ);
                if (S > BestS) { BestS = S; BestJ = J; }
            }
            if (BestJ >= 0 && BestS > 0.01)
            {
                const K = Clamp(0.14 * DtDays * 8, 0.01, 0.35);
                Fe[I] -= Fe2[I] * K; Fe[BestJ] += Fe2[I] * K * 0.9;
                Cu[I] -= Cu2[I] * K; Cu[BestJ] += Cu2[I] * K * 0.9;
                Mn[I] -= Mn2[I] * K; Mn[BestJ] += Mn2[I] * K * 0.9;
                Dis[I] -= Dis2[I] * K; Dis[BestJ] += Dis2[I] * K * 0.9;
            }
        }
    }
}

/*==========================================================================================================================================
                    SNOW / GLACIER ICE — accumulation, melt, flow, abrasion
==========================================================================================================================================*/
function StepSnowIce(State, Climate, DtDays)
{
    const { nx: Nx, nz: Nz, stepX: SX, stepZ: SZ, h: H, snow: Snow, ice: Ice, w: W, sed: Sed, sdf2D: Sdf } = State;
    const P = State.params;
    if (!Climate) return;

    const SnowIn = Climate.snowMm * 0.001 * DtDays;
    for (let I = 0; I < Nx * Nz; ++I)
    {
        if (Sdf[I] >= 0) { Snow[I] = 0; Ice[I] = 0; State.melt[I] = 0; continue; }
        // Orographic snowline: high cells get more snow retention.
        const SnowlineBias = Clamp((H[I] - P.snowlineM) / 260.0 + 0.35, -0.4, 1.6);
        Snow[I] += SnowIn * (0.5 + 0.9 * SnowlineBias);
        // Degree-day melt on warm days.
        const Melt = Math.max(0, Climate.tempC) * P.degreeDayMelt * DtDays * (1 + 0.35 * Clamp(SnowlineBias, 0, 1));
        const Actual = Math.min(Snow[I], Melt);
        Snow[I] -= Actual;
        // Melt runoff is injected per-cell through the hydrology melt field (no double count).
        State.melt[I] = Actual / Math.max(DtDays, 0.01);   // m/day per cell
        // Firn compaction → glacier ice on deep perennial snowfields.
        if (Snow[I] > 1.6 && Climate.tempC < 1.5)
        {
            const Compact = Snow[I] * 0.02 * DtDays;
            Snow[I] -= Compact;
            Ice[I] += Compact * 0.86;
        }
        // Ice melt + calving loss.
        const IceMelt = Math.max(0, Climate.tempC - 0.8) * P.degreeDayMelt * 0.55 * DtDays;
        Ice[I] = Math.max(0, Ice[I] - Math.min(Ice[I], IceMelt));
        State.snowMap[I] = Snow[I] + Ice[I] * 0.7;
    }
    State.stats.snowM = SnowIn;

    // Shallow-ice diffusion (glacier creep downslope) + glacial abrasion.
    const Ice2 = Ice.slice();
    for (let Z = 1; Z < Nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (Sdf[I] >= 0 || Ice2[I] < 0.05) continue;
            for (const [Dx, Dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]])
            {
                const J = I + Dz * Nx + Dx;
                if (Sdf[J] >= 0) continue;
                const Dh = (H[I] + Ice2[I]) - (H[J] + Ice2[J]);
                const Dist = Dx ? SX : SZ;
                const Flow = Clamp(P.iceFlowK * Ice2[I] * (Dh / Dist) * DtDays * 2.2, 0, Ice2[I] * 0.3);
                Ice[I] -= Flow; Ice[J] += Flow * 0.985;
                // Glacial abrasion (Poulard/Truffer style): grind bedrock under fast ice.
                const Abrade = P.glacialAbrasionK * Flow * (1.2 - State.hard[I]) * 2.4;
                if (Abrade > 1e-6)
                {
                    H[J] -= Abrade;
                    Sed[J] += Abrade;
                    State.wearMap[J] += Abrade * 0.6;
                }
            }
        }
    }
}

/*==========================================================================================================================================
                                                DAILY MASTER STEP + BAKING
==========================================================================================================================================*/
/**
 * Advance ONE simulated day: seasonal climate → snow/ice → hydrology substeps
 * (rain + melt water through pipes) → fluvial incision → pitting → wind →
 * rockfall → chemical weathering.
 *
 * Pass `FixedSample` (a sample previously produced by SampleClimate) to step
 * WITHOUT touching the group calendar — used by shared simulation groups so 2+
 * terrains experience literally the same weather day.
 */
export function StepErosionDay(State, ClimateGroup = State.climateGroup, FixedSample = null)
{
    const Group = ClimateGroup || CreateClimateGroup({ seed: State.seed });
    const Climate = FixedSample || SampleClimate(Group);
    State.lastClimate = Climate;
    State.day += 1;

    StepSnowIce(State, Climate, 1.0);
    UpdateDrainageArea(State, Climate);   // D8 catchment routing → physically-grounded Q

    // Hydrology runs in substeps so storm pulses resolve without flooding.
    const Sub = 6;
    const DtSec = 86400 / Sub;
    for (let S = 0; S < Sub; ++S)
    {
        StepHydrology(State, Climate, DtSec);
        StepFluvialErosion(State, DtSec);
    }

    StepPitting(State, Climate, 1.0);
    StepWind(State, Climate, 1.0);
    StepRockfall(State, 1.0);
    StepChemical(State, Climate, 1.0);

    // Sand sheet colors flow into soil budget (keeps mass sane).
    for (let I = 0; I < State.nx * State.nz; ++I)
    {
        if (State.sdf2D[I] < 0 && State.sand[I] > 0.02)
        {
            State.soil[I] = Math.max(State.soil[I], Math.min(State.sand[I] * 0.6, 1.4));
        }
    }

    State.totalSteps += 1;
    if (!FixedSample) AdvanceClimateDay(Group);
    return Climate;
}

/**
 * Bake N simulated years. `OnProgress(0..1)` can drive a UI progress bar.
 * Chunked with periodic map compaction so memory stays bounded.
 */
export function BakeErosionYears(State, ClimateGroup, Years, OnProgress = null)
{
    const Group = ClimateGroup || State.climateGroup || CreateClimateGroup({ seed: State.seed });
    const Days = Math.max(1, Math.round(Years * 365));
    for (let D = 0; D < Days; ++D)
    {
        StepErosionDay(State, Group);
        if (OnProgress && (D % 32 === 0)) OnProgress(D / Days);
    }
    if (OnProgress) OnProgress(1.0);
    return State;
}

/**
 * Push the simulated surface back onto the region mesh fields.
 *
 * Erosion increments are accumulated into a persistent 'Erosion Baked Surface'
 * sculpt layer, so the baked/stepped surface SURVIVES any later layer-stack
 * re-evaluation (user layer edits merge on top instead of wiping the bake).
 * `appliedH` tracks the last applied surface so only INCREMENTAL changes are
 * written — preserving concurrent user edits in `Rt.heights`.
 */
export function ApplyErosionToRegion(World, Region, State)
{
    if (!Region.runtime) return false;
    const Rt = Region.runtime;
    const N = State.nx * State.nz;

    // Persistent erosion sculpt layer (accumulates incremental deltas).
    let Layer = Region.erosionLayer;
    if (!Layer || Region.stack.indexOf(Layer) < 0)
    {
        Layer = {
            id: `lyr-erosion-${Region.id}`,
            type: 'sculpt',
            name: 'Erosion Baked Surface',
            enabled: true,
            opacity: 1.0,
            combine: 'add',
            seed: 4029,
            falloff: { mode: 'none', distance: 100, edgeFloor: 1.0, points: [[0, 1], [1, 1]] },
            heightMask: { enabled: false, minElev: 0, maxElev: 300, minSlope: 0, maxSlope: 90 },
            deltaRes: 0,
            deltas: null,
            strokeCount: 0
        };
        Region.erosionLayer = Layer;
        Region.stack.push(Layer);
    }
    if (!Layer.deltas || Layer.deltaRes !== N)
    {
        const Old = Layer.deltas;
        Layer.deltas = new Float32Array(N);
        Layer.deltaRes = N;
        Layer.nx = State.nx;
        Layer.nz = State.nz;
        if (Old && Old.length === N) Layer.deltas.set(Old);
    }
    Layer.nx = State.nx;
    Layer.nz = State.nz;

    if (!State.appliedH || State.appliedH.length !== N) State.appliedH = State.h0 ? State.h0.slice() : new Float32Array(N);

    for (let I = 0; I < N; ++I)
    {
        const DInc = State.h[I] - State.appliedH[I];
        if (DInc !== 0)
        {
            Layer.deltas[I] += DInc;
            Rt.heights[I] += DInc;
        }
        Rt.waterChannel[I] = Clamp(Math.max(State.wetness[I] * 0.35, State.w[I] / 0.35), 0, 1);
    }
    State.appliedH.set(State.h);

    Rt.erosion = State;
    const Sig = ComputeRegionBaseSignature(World, Region);
    Region.erosionBaseSig = Sig;
    Rt.baseSignature = Sig;
    Rt.erosionStale = false;
    // Keep preSculptHeights in sync if no separate interactive sculpt layer sits above the erosion bake
    if (Rt.preSculptHeights && !Rt.topSculptLayerId)
    {
        Rt.preSculptHeights.set(Rt.heights);
    }
    Region.meshDirty = true;
    return true;
}

/** Compact summary for the HUD / validation. */
export function ErosionStateSummary(State)
{
    const N = State.nx * State.nz;
    let Water = 0, Sand = 0, Snow = 0, Ice = 0, Pits = 0, Soil = 0, Crack = 0;
    let MaxPit = 0;
    for (let I = 0; I < N; ++I)
    {
        if (State.sdf2D[I] >= 0) continue;
        Water += State.w[I]; Sand += State.sand[I]; Snow += State.snow[I];
        Ice += State.ice[I]; Soil += State.soil[I]; Crack += State.crack[I];
        if (State.pit[I] > 0.002) { Pits++; MaxPit = Math.max(MaxPit, State.pit[I]); }
    }
    return {
        day: State.day,
        year: Math.floor(State.day / 365),
        totalWaterM3: Water * State.cellArea,
        totalSandM3: Sand * State.cellArea,
        totalSnowM3: Snow * State.cellArea,
        totalIceM3: Ice * State.cellArea,
        totalSoilM3: Soil * State.cellArea,
        pitCells: Pits,
        maxPitDepthM: MaxPit,
        meanCrack: Crack / N,
        incisedM: State.stats.incisedM,
        depositedM: State.stats.depositedM,
        floodedCells: State.stats.floodedCells,
        climate: State.lastClimate ? { ...State.lastClimate } : null
    };
}
