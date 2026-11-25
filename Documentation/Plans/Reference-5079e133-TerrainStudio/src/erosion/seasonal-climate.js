/**
 * Location-based seasonal climate for the live terrain simulation.
 *
 * This module is intentionally separate from the Snow effect stack. Snow,
 * rain, wind and chemical layers still own *how much* annual forcing they
 * apply; the seasonal climate decides *when* that forcing arrives during a
 * simulated year, whether precipitation is rain or snow, when stored water
 * freezes in pores, and when snow/ice melt. Latitude supplies the hemisphere
 * and seasonal phase, while the climate regime supplies the annual weather
 * pattern (for example a high desert with rare storm pulses and hard night
 * frosts, versus an alpine climate with winter snow and spring melt).
 */

const clamp = (value, min, max, fallback) => {
  const n = Number(value ?? fallback);
  return Number.isFinite(n) ? Math.max(min, Math.min(max, n)) : fallback;
};
const clamp01 = (value, fallback = 0) => clamp(value, 0, 1, fallback);
const lerp = (a, b, t) => a + (b - a) * t;
const smoothstep = (a, b, x) => {
  const t = clamp((x - a) / Math.max(1e-9, b - a), 0, 1, 0);
  return t * t * (3 - 2 * t);
};
const fract = (x) => x - Math.floor(x);

function hash01(seed, a, b = 0) {
  let h = Math.imul((Number(seed) || 4029) ^ 0x9e3779b9, 0x85ebca6b);
  h ^= Math.imul((a | 0) ^ 0xc2b2ae35, 0x27d4eb2f);
  h ^= Math.imul((b | 0) ^ 0x165667b1, 0x9e3779b1);
  h ^= h >>> 15;
  h = Math.imul(h, 0x2c1b3c6d);
  h ^= h >>> 12;
  h = Math.imul(h, 0x297a2d39);
  h ^= h >>> 15;
  return (h >>> 0) / 4294967296;
}

export const CLIMATE_REGIMES = Object.freeze([
  Object.freeze({
    code: 0,
    id: 'alpine-continental',
    label: 'Alpine / continental mountains',
    description: 'Cold snowy winters, spring snowmelt, convective summer rain and strong freeze-thaw at altitude.',
    referenceLatitudeDeg: 46,
    annualPrecipMm: 900,
    annualSnowShare: 0.46,
    frostNightsPerYear: 150,
    meanTempC: 3,
    seasonalAmpC: 13,
    diurnalAmpC: 9,
    precipCold: 1.28,
    precipWarm: 0.86,
    snowPropensity: 0.96,
    frostDew: 0.55,
    evaporation: 0.82,
    windCold: 1.05,
    windWarm: 0.9,
    stormBlockProbability: 0.16,
    stormPulse: 1.7,
    drySpellFactor: 0.78,
  }),
  Object.freeze({
    code: 1,
    id: 'temperate-oceanic',
    label: 'Temperate oceanic / coastal',
    description: 'Mild, wet winters and cool summers; rain occurs in every season and persistent lowland snow is uncommon.',
    referenceLatitudeDeg: 50,
    annualPrecipMm: 1100,
    annualSnowShare: 0.08,
    frostNightsPerYear: 45,
    meanTempC: 10,
    seasonalAmpC: 6,
    diurnalAmpC: 6,
    precipCold: 1.28,
    precipWarm: 0.82,
    snowPropensity: 0.38,
    frostDew: 0.5,
    evaporation: 0.66,
    windCold: 1.18,
    windWarm: 0.92,
    stormBlockProbability: 0.2,
    stormPulse: 1.55,
    drySpellFactor: 0.82,
  }),
  Object.freeze({
    code: 2,
    id: 'mediterranean',
    label: 'Mediterranean winter-rain',
    description: 'Wet winters and long dry summers; low snow except on cold high ground, with strong summer evaporation.',
    referenceLatitudeDeg: 38,
    annualPrecipMm: 550,
    annualSnowShare: 0.03,
    frostNightsPerYear: 28,
    meanTempC: 16,
    seasonalAmpC: 8,
    diurnalAmpC: 10,
    precipCold: 1.9,
    precipWarm: 0.08,
    snowPropensity: 0.14,
    frostDew: 0.34,
    evaporation: 1.28,
    windCold: 1.0,
    windWarm: 0.92,
    stormBlockProbability: 0.13,
    stormPulse: 2.2,
    drySpellFactor: 0.28,
  }),
  Object.freeze({
    code: 3,
    id: 'arid-desert',
    label: 'Hot arid desert',
    description: 'Only tens of millimetres of precipitation in a normal year, almost no snow, rare short storms and intense evaporation.',
    referenceLatitudeDeg: 24,
    annualPrecipMm: 35,
    annualSnowShare: 0.005,
    frostNightsPerYear: 8,
    meanTempC: 24,
    seasonalAmpC: 9,
    diurnalAmpC: 19,
    precipCold: 1.35,
    precipWarm: 0.55,
    snowPropensity: 0.015,
    frostDew: 0.72,
    evaporation: 2.25,
    windCold: 0.95,
    windWarm: 1.12,
    stormBlockProbability: 0.035,
    stormPulse: 7.5,
    drySpellFactor: 0.012,
  }),
  Object.freeze({
    code: 4,
    id: 'semi-arid-high-desert',
    label: 'Semi-arid high desert / steppe',
    description: 'Sparse convective rain, cold clear nights, episodic snow and frequent pore-water freezing without a deep persistent snowpack.',
    referenceLatitudeDeg: 39,
    annualPrecipMm: 240,
    annualSnowShare: 0.16,
    frostNightsPerYear: 135,
    meanTempC: 11,
    seasonalAmpC: 14,
    diurnalAmpC: 17,
    precipCold: 0.78,
    precipWarm: 1.28,
    snowPropensity: 0.28,
    frostDew: 0.82,
    evaporation: 1.5,
    windCold: 0.98,
    windWarm: 1.1,
    stormBlockProbability: 0.09,
    stormPulse: 3.8,
    drySpellFactor: 0.16,
  }),
  Object.freeze({
    code: 5,
    id: 'monsoon',
    label: 'Monsoon wet-summer',
    description: 'A short, intense warm wet season followed by a long dry season; snow is mainly confined to high cold terrain.',
    referenceLatitudeDeg: 25,
    annualPrecipMm: 1500,
    annualSnowShare: 0.02,
    frostNightsPerYear: 6,
    meanTempC: 24,
    seasonalAmpC: 5,
    diurnalAmpC: 8,
    precipCold: 0.08,
    precipWarm: 2.35,
    snowPropensity: 0.05,
    frostDew: 0.16,
    evaporation: 0.95,
    windCold: 0.82,
    windWarm: 1.12,
    stormBlockProbability: 0.24,
    stormPulse: 2.1,
    drySpellFactor: 0.38,
  }),
  Object.freeze({
    code: 6,
    id: 'subarctic-continental',
    label: 'Subarctic continental',
    description: 'Long severe winters, stored snow, abrupt spring melt and a short thaw season with strong frost action.',
    referenceLatitudeDeg: 63,
    annualPrecipMm: 450,
    annualSnowShare: 0.52,
    frostNightsPerYear: 235,
    meanTempC: -4,
    seasonalAmpC: 18,
    diurnalAmpC: 8,
    precipCold: 0.78,
    precipWarm: 1.25,
    snowPropensity: 1.0,
    frostDew: 0.62,
    evaporation: 0.46,
    windCold: 1.08,
    windWarm: 0.88,
    stormBlockProbability: 0.13,
    stormPulse: 1.6,
    drySpellFactor: 0.74,
  }),
  Object.freeze({
    code: 7,
    id: 'polar-desert',
    label: 'Polar desert',
    description: 'Very cold and dry; precipitation is mostly snow, melt is brief, and liquid-water erosion is strongly seasonal.',
    referenceLatitudeDeg: 74,
    annualPrecipMm: 120,
    annualSnowShare: 0.9,
    frostNightsPerYear: 330,
    meanTempC: -14,
    seasonalAmpC: 16,
    diurnalAmpC: 6,
    precipCold: 0.82,
    precipWarm: 1.02,
    snowPropensity: 1.0,
    frostDew: 0.5,
    evaporation: 0.28,
    windCold: 1.15,
    windWarm: 0.9,
    stormBlockProbability: 0.08,
    stormPulse: 1.8,
    drySpellFactor: 0.55,
  }),
  Object.freeze({
    code: 8,
    id: 'tropical-highland',
    label: 'Tropical highland wet/dry',
    description: 'Small annual temperature swing but large day-night swings; distinct wet and dry seasons and frost mainly at altitude.',
    referenceLatitudeDeg: 12,
    annualPrecipMm: 950,
    annualSnowShare: 0.04,
    frostNightsPerYear: 70,
    meanTempC: 15,
    seasonalAmpC: 2.5,
    diurnalAmpC: 12,
    precipCold: 0.52,
    precipWarm: 1.5,
    snowPropensity: 0.16,
    frostDew: 0.58,
    evaporation: 0.86,
    windCold: 0.9,
    windWarm: 1.0,
    stormBlockProbability: 0.18,
    stormPulse: 2.0,
    drySpellFactor: 0.42,
  }),
]);

export const DEFAULT_SEASONAL_CLIMATE_PARAMS = Object.freeze({
  seasonalClimateMode: 1, // 0 = off, 1 = location-based seasons
  climateRegime: 0,
  climateLatitudeDeg: 46,
  climateStartDayOfYear: 15,
  climateStepsPerYear: 240,
  climateYearsToSimulate: 20, // 0 = continuous
  climateInterannualVariability: 0.28,
  climatePrecipitationScale: 1.0,
  climateTemperatureOffsetC: 0,
});

export function climateRegimeFor(value = 0) {
  if (typeof value === 'string') {
    return CLIMATE_REGIMES.find((regime) => regime.id === value)
      || CLIMATE_REGIMES[clamp(value, 0, CLIMATE_REGIMES.length - 1, 0) | 0]
      || CLIMATE_REGIMES[0];
  }
  const code = Math.round(clamp(value, 0, CLIMATE_REGIMES.length - 1, 0));
  return CLIMATE_REGIMES[code] || CLIMATE_REGIMES[0];
}

export function normalizeSeasonalClimateParams(params = {}) {
  const p = params && typeof params === 'object' ? params : {};
  const regime = climateRegimeFor(p.climateRegime ?? DEFAULT_SEASONAL_CLIMATE_PARAMS.climateRegime);
  return {
    seasonalClimateMode: Math.round(clamp(
      p.seasonalClimateMode,
      0,
      1,
      p.seasonalClimateEnabled === false ? 0 : DEFAULT_SEASONAL_CLIMATE_PARAMS.seasonalClimateMode,
    )),
    climateRegime: regime.code,
    climateLatitudeDeg: clamp(p.climateLatitudeDeg, -82, 82, DEFAULT_SEASONAL_CLIMATE_PARAMS.climateLatitudeDeg),
    climateStartDayOfYear: Math.round(clamp(p.climateStartDayOfYear, 1, 365, DEFAULT_SEASONAL_CLIMATE_PARAMS.climateStartDayOfYear)),
    climateStepsPerYear: Math.round(clamp(p.climateStepsPerYear, 24, 1460, DEFAULT_SEASONAL_CLIMATE_PARAMS.climateStepsPerYear)),
    climateYearsToSimulate: clamp(p.climateYearsToSimulate, 0, 250, DEFAULT_SEASONAL_CLIMATE_PARAMS.climateYearsToSimulate),
    climateInterannualVariability: clamp01(p.climateInterannualVariability, DEFAULT_SEASONAL_CLIMATE_PARAMS.climateInterannualVariability),
    climatePrecipitationScale: clamp(p.climatePrecipitationScale, 0, 3, DEFAULT_SEASONAL_CLIMATE_PARAMS.climatePrecipitationScale),
    climateTemperatureOffsetC: clamp(p.climateTemperatureOffsetC, -15, 15, DEFAULT_SEASONAL_CLIMATE_PARAMS.climateTemperatureOffsetC),
  };
}

/** Geographic starting points for the built-in terrain catalogue. */
export function climateDefaultsForLandscape(landscapeId = '') {
  const base = { ...DEFAULT_SEASONAL_CLIMATE_PARAMS };
  const set = (regimeId, latitudeDeg, overrides = {}) => ({
    ...base,
    climateRegime: climateRegimeFor(regimeId).code,
    climateLatitudeDeg: latitudeDeg,
    ...overrides,
  });
  switch (landscapeId) {
    case 'icelandic-highlands':
    case 'icelandic-green-highlands':
    case 'studlagil-basalt':
    case 'fjadrargljufur-palagonite':
      return set('subarctic-continental', 65);
    case 'himalayan-karakoram':
      return set('alpine-continental', 28, { climateTemperatureOffsetC: -3 });
    case 'alpine-divide':
    case 'volcanic-caldera':
    case 'glacial-lake-watershed':
    case 'spline-road-tunnel-test':
    case 'nivation-cirque-strata':
      return set('alpine-continental', 46);
    case 'coastal-massif':
      return set('temperate-oceanic', 48);
    case 'layered-plateau':
    case 'fault-escarpment':
    case 'badlands-domes':
    case 'grand-canyon':
    case 'rugged-desert-crags':
    case 'spline-canyon-river-test':
    case 'utah-wind-rain-canyon':
      return set('semi-arid-high-desert', 39);
    case 'namib-erg-dunes':
      return set('arid-desert', -23);
    case 'sahara-tassili-erg':
    case 'desert-oasis-erg':
    case 'yardang-wind-corridor':
      return set('arid-desert', 24);
    case 'frost-pitted-rainbow-range':
      // Rainbow Andes analogue: southern-hemisphere tropical highland,
      // where the wet/dry rhythm and nightly altitude frost matter more
      // than a large annual temperature swing.
      return set('tropical-highland', -13, { climateTemperatureOffsetC: -5 });
    case 'lunar-mare-craters':
      return set('polar-desert', 0, { seasonalClimateMode: 0 });
    default:
      return base;
  }
}

const MONTH_NAMES = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const MONTH_STARTS = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];

function dateParts(dayOfYearFloat) {
  const doy = Math.floor(((dayOfYearFloat - 1) % 365.25 + 365.25) % 365.25) + 1;
  let month = 0;
  for (let i = MONTH_STARTS.length - 1; i >= 0; i--) {
    if (doy > MONTH_STARTS[i]) { month = i; break; }
  }
  return {
    dayOfYear: doy,
    monthIndex: month,
    monthName: MONTH_NAMES[month],
    dayOfMonth: doy - MONTH_STARTS[month],
  };
}

function localSeasonName(monthIndex, latitudeDeg, precipitationMultiplier) {
  if (Math.abs(latitudeDeg) < 12) return precipitationMultiplier >= 0.85 ? 'Wet season' : 'Dry season';
  const north = ['Winter', 'Winter', 'Spring', 'Spring', 'Spring', 'Summer', 'Summer', 'Summer', 'Autumn', 'Autumn', 'Autumn', 'Winter'];
  const south = ['Summer', 'Summer', 'Autumn', 'Autumn', 'Autumn', 'Winter', 'Winter', 'Winter', 'Spring', 'Spring', 'Spring', 'Summer'];
  return (latitudeDeg < 0 ? south : north)[monthIndex] || 'Season';
}

/**
 * Evaluate the climate at an elapsed live-simulation time in years.
 *
 * `elapsedYears` is measured from the configured start day. At the default
 * 240 steps/year and two substeps per 60 fps frame, one climate year takes
 * about two real seconds and a 20-year run about forty seconds; changing the
 * steps/year or frame substeps changes that wall-clock pacing without
 * changing the calendar arithmetic.
 */
export function seasonalClimateAt(paramsInput = {}, elapsedYearsInput = 0, seedInput = 4029) {
  const params = normalizeSeasonalClimateParams(paramsInput);
  const regime = climateRegimeFor(params.climateRegime);
  const enabled = params.seasonalClimateMode > 0;
  const elapsedYears = Math.max(0, Number(elapsedYearsInput) || 0);
  const absoluteDay = (params.climateStartDayOfYear - 1) + elapsedYears * 365.25;
  const date = dateParts(absoluteDay + 1);
  const yearIndex = Math.floor(elapsedYears);
  const yearFraction = fract(elapsedYears);
  const seed = Number(seedInput ?? paramsInput?.seed) || 4029;

  if (!enabled) {
    return {
      enabled: false,
      params,
      regime,
      elapsedYears,
      year: yearIndex + 1,
      yearIndex,
      yearFraction,
      ...date,
      hemisphere: params.climateLatitudeDeg < -0.5 ? 'south' : params.climateLatitudeDeg > 0.5 ? 'north' : 'equatorial',
      seasonName: 'Seasonal climate off',
      warmth: 0,
      astronomicalWarmth: 0,
      temperatureC: regime.meanTempC + params.climateTemperatureOffsetC,
      nightMinC: regime.meanTempC + params.climateTemperatureOffsetC,
      dayMaxC: regime.meanTempC + params.climateTemperatureOffsetC,
      temperatureAnomalyC: 0,
      temperatureAmplitudeScale: 1,
      precipitationMultiplier: 1,
      rainMultiplier: 1,
      snowMultiplier: 1,
      snowFraction: regime.annualSnowShare,
      meltMultiplier: 1,
      frostMultiplier: 1,
      moistureMultiplier: 1,
      evaporationMultiplier: 1,
      windMultiplier: 1,
      lowElevationRetention: 0.34,
      stormPulse: false,
      referenceAnnualPrecipMm: regime.annualPrecipMm * params.climatePrecipitationScale,
      referenceAnnualSnowMmSWE: regime.annualPrecipMm * regime.annualSnowShare * params.climatePrecipitationScale,
      currentYearPrecipMm: regime.annualPrecipMm * params.climatePrecipitationScale,
      currentYearSnowMmSWE: regime.annualPrecipMm * regime.annualSnowShare * params.climatePrecipitationScale,
      frostNightsPerYear: regime.frostNightsPerYear,
      solarDeclinationDeg: 0,
      noonSunElevationDeg: 90 - Math.abs(params.climateLatitudeDeg),
      phaseFromColdest: yearFraction,
    };
  }

  const latitude = params.climateLatitudeDeg;
  const absLat = Math.abs(latitude);
  const southern = latitude < -0.5;
  const hemisphere = southern ? 'south' : latitude > 0.5 ? 'north' : 'equatorial';
  const phaseDay = absoluteDay + (southern ? 182.625 : 0);
  // Coldest reference lag: mid-January in the north, mid-July in the south.
  const astronomicalWarmth = -Math.cos((2 * Math.PI * (phaseDay - 14)) / 365.25);
  const latitudeSeasonality = 0.16 + 0.84 * smoothstep(2, 48, absLat);
  const warmth = clamp(astronomicalWarmth * latitudeSeasonality, -1, 1, 0);
  const warm01 = (astronomicalWarmth + 1) * 0.5;

  // Deterministic wet/dry and warm/cold years: a 20-year run is a sequence
  // of related but non-identical years rather than one loop repeated 20×.
  const variability = params.climateInterannualVariability;
  const wetAnomaly = clamp(1 + (hash01(seed, yearIndex + 101, regime.code * 17 + 3) - 0.5) * 2 * variability, 0.25, 2.4, 1);
  const temperatureAnomalyC = (hash01(seed, yearIndex + 733, regime.code * 29 + 11) - 0.5) * 2 * variability * 3.2;

  const latitudeTempCorrection = (regime.referenceLatitudeDeg - absLat) * 0.42;
  const seasonalAmp = regime.seasonalAmpC * (0.22 + 0.78 * smoothstep(2, 52, absLat));
  const temperatureC = regime.meanTempC
    + latitudeTempCorrection
    + params.climateTemperatureOffsetC
    + temperatureAnomalyC
    + seasonalAmp * astronomicalWarmth;
  const diurnalHalf = regime.diurnalAmpC * 0.5;
  const nightMinC = temperatureC - diurnalHalf * 1.08;
  const dayMaxC = temperatureC + diurnalHalf * 0.92;

  const precipPatternRaw = lerp(regime.precipCold, regime.precipWarm, warm01);
  const precipPattern = precipPatternRaw / Math.max(0.05, (regime.precipCold + regime.precipWarm) * 0.5);
  // Three-day weather blocks make storms persist for a few simulation steps
  // instead of flickering on/off each substep. Arid regimes spend most
  // blocks almost dry, then receive a short geomorphically useful pulse.
  const weatherBlock = Math.floor(absoluteDay / 3);
  const stormChance = clamp(regime.stormBlockProbability * (0.35 + precipPattern), 0, 0.85, 0);
  const stormPulse = hash01(seed, weatherBlock + 50021, regime.code * 101 + 19) < stormChance;
  const stormMultiplier = stormPulse ? regime.stormPulse : regime.drySpellFactor;
  const precipitationMultiplier = clamp(
    precipPattern * stormMultiplier * wetAnomaly * params.climatePrecipitationScale,
    0,
    8,
    1,
  );

  const snowSuitability = regime.snowPropensity * (1 - smoothstep(-4, 5, temperatureC));
  const snowFraction = clamp01(snowSuitability, 0);
  // A small high-terrain floor remains for snowy regimes: valley reference
  // temperature can be above freezing while summits still receive snow.
  const snowMultiplier = clamp(
    precipitationMultiplier * (0.10 * regime.snowPropensity + 1.38 * snowFraction),
    0,
    2.6,
    1,
  );
  // Keep truly wintry precipitation falling as snow: a small rain-on-snow
  // component can still appear near the melting point, but deep-winter
  // precipitation must not turn into a visible rain storm.
  const rainMultiplier = clamp(precipitationMultiplier * Math.pow(Math.max(0, 1 - snowFraction), 1.35) * 1.15, 0, 6, 1);

  const warmIndex = smoothstep(-2, 14, temperatureC);
  const meltMultiplier = clamp(
    0.04 + 2.25 * Math.pow(warmIndex, 1.3) + 0.28 * rainMultiplier * warmIndex,
    0.02,
    3.6,
    1,
  );
  const coldIndex = 1 - smoothstep(-7, 5, temperatureC);
  const lowElevationRetention = clamp(0.09 + 0.83 * coldIndex, 0.05, 0.95, 0.34);

  const freezeIndex = 1 - smoothstep(-7, 1.5, nightMinC);
  const thawIndex = smoothstep(-3, 7, dayMaxC);
  const moistureIndex = clamp01(
    0.10 + 0.42 * clamp(precipitationMultiplier, 0, 1.6, 0) + 0.46 * regime.frostDew * freezeIndex,
    0,
  );
  const frostMultiplier = clamp(
    (0.08 + 1.35 * freezeIndex) * (0.38 + 0.62 * thawIndex) * (0.30 + 0.70 * moistureIndex) * 1.55,
    0,
    2.2,
    1,
  );
  const moistureMultiplier = clamp(0.18 + 0.72 * moistureIndex + 0.24 * clamp(precipitationMultiplier, 0, 2, 0), 0.12, 1.8, 1);
  const evaporationMultiplier = clamp(
    regime.evaporation * (0.42 + 0.95 * smoothstep(-5, 30, temperatureC)) * (1.08 - 0.24 * clamp(precipitationMultiplier, 0, 1.5, 0)),
    0.04,
    3.2,
    1,
  );
  const windPattern = lerp(regime.windCold, regime.windWarm, warm01);
  const windMultiplier = clamp(windPattern * (stormPulse ? 1.14 : 0.96), 0.15, 1.8, 1);

  const solarDeclinationDeg = -23.44 * Math.cos((2 * Math.PI * (date.dayOfYear + 10)) / 365.25);
  const noonSunElevationDeg = clamp(90 - Math.abs(latitude - solarDeclinationDeg), -10, 90, 45);
  const phaseFromColdest = fract((phaseDay - 14) / 365.25);

  return {
    enabled: true,
    params,
    regime,
    elapsedYears,
    year: yearIndex + 1,
    yearIndex,
    yearFraction,
    ...date,
    hemisphere,
    seasonName: localSeasonName(date.monthIndex, latitude, precipPattern * wetAnomaly),
    warmth,
    astronomicalWarmth,
    temperatureC,
    nightMinC,
    dayMaxC,
    temperatureAnomalyC,
    temperatureAmplitudeScale: clamp(seasonalAmp / Math.max(1, regime.seasonalAmpC), 0.18, 1.25, 1),
    precipitationMultiplier,
    rainMultiplier,
    snowMultiplier,
    snowFraction,
    meltMultiplier,
    frostMultiplier,
    moistureMultiplier,
    evaporationMultiplier,
    windMultiplier,
    lowElevationRetention,
    stormPulse,
    wetAnomaly,
    referenceAnnualPrecipMm: regime.annualPrecipMm * params.climatePrecipitationScale,
    referenceAnnualSnowMmSWE: regime.annualPrecipMm * regime.annualSnowShare * params.climatePrecipitationScale,
    currentYearPrecipMm: regime.annualPrecipMm * params.climatePrecipitationScale * wetAnomaly,
    currentYearSnowMmSWE: regime.annualPrecipMm * regime.annualSnowShare * params.climatePrecipitationScale * wetAnomaly,
    frostNightsPerYear: regime.frostNightsPerYear,
    solarDeclinationDeg,
    noonSunElevationDeg,
    phaseFromColdest,
  };
}

export function formatLatitude(latitudeDeg) {
  const lat = clamp(latitudeDeg, -82, 82, 0);
  if (Math.abs(lat) < 0.05) return '0° equator';
  return `${Math.abs(lat).toFixed(Math.abs(lat % 1) < 0.05 ? 0 : 1)}°${lat < 0 ? 'S' : 'N'}`;
}

export function seasonalClimateSummary(paramsInput = {}, elapsedYears = 0, seed = 4029) {
  const state = seasonalClimateAt(paramsInput, elapsedYears, seed);
  if (!state.enabled) return 'Seasonal climate off';
  return `${state.regime.label} · ${formatLatitude(state.params.climateLatitudeDeg)} · Year ${state.year} ${state.monthName} ${state.dayOfMonth} · ${state.seasonName}`;
}
