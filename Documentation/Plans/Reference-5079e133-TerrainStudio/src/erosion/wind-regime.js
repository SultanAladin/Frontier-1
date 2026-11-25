/**
 * Time-varying prevailing-wind regimes shared by Terrain Studio and the
 * Rock & Cliff Studio SDF bake.
 *
 * Real erosive wind does not sweep left/right like a windscreen wiper, but
 * it also does not teleport from north to south between two time steps. A
 * synoptic weather pattern persists in one compass sector for a spell;
 * when a front or a new pressure pattern arrives, the surface wind veers
 * or backs through the intervening bearings over a finite transition.
 * This module therefore schedules a continuous direction trajectory:
 * quota-balanced weather spells joined by smooth frontal transitions whose
 * duration grows with the angular change. It is neither a sine wave nor a
 * sequence of instantaneous sector jumps.
 *
 * The quota of spell units assigned to each sector is balanced inside every
 * weather-pattern cycle. That matters for finite simulations and offline
 * bakes: a purely random sequence could spend most of a short run on one
 * side by chance, whereas the quota schedule realizes the intended wind
 * rose closely while still changing sector in a non-monotonic order.
 *
 * Modes are stored numerically in Terrain layer params (the inspector's
 * generic select contract) and as readable strings in Rock params/CLI:
 *   0 / "fixed"    one prevailing direction (legacy behaviour)
 *   1 / "variable" prevailing-weighted wind rose: persistent weather spells
 *                  over the full compass, smoothly veering/backing between
 *                  sectors, with the prevailing sector most frequent and
 *                  left/right sector pairs balanced
 *   2 / "sectors"  weather fronts across ±range: each sector anchors a spell
 *                  and frontal transitions blend through intermediate
 *                  bearings instead of jumping
 *   3 / "rotate"   full wind rose: all eight sectors receive equal spell
 *                  quotas, joined by the same smooth frontal transitions
 *
 * Legacy "oscillate" / "swing" values migrate to "variable". The old
 * sinusoidal wiper is intentionally no longer exposed.
 */

export const WIND_SHIFT_MODES = Object.freeze([
  Object.freeze({ value: 0, id: 'fixed', label: 'Fixed prevailing wind' }),
  Object.freeze({ value: 1, id: 'variable', label: 'Variable wind rose (prevailing + smooth sector shifts)' }),
  Object.freeze({ value: 2, id: 'sectors', label: 'Weather fronts (smooth veer/back between sectors)' }),
  Object.freeze({ value: 3, id: 'rotate', label: 'Full wind rose (all sectors, smooth shifts)' }),
]);

const MODE_ALIASES = new Map([
  ['0', 'fixed'],
  ['fixed', 'fixed'],
  ['steady', 'fixed'],
  ['prevailing', 'fixed'],
  ['1', 'variable'],
  ['variable', 'variable'],
  ['variable-wind-rose', 'variable'],
  ['prevailing-variable', 'variable'],
  ['weather', 'variable'],
  ['oscillate', 'variable'],
  ['oscillating', 'variable'],
  ['swing', 'variable'],
  ['seasonal', 'variable'],
  ['seasonal-swing', 'variable'],
  ['2', 'sectors'],
  ['sector', 'sectors'],
  ['sectors', 'sectors'],
  ['fronts', 'sectors'],
  ['weather-fronts', 'sectors'],
  ['3', 'rotate'],
  ['rotate', 'rotate'],
  ['rotating', 'rotate'],
  ['wind-rose', 'rotate'],
  ['full-wind-rose', 'rotate'],
  ['rose', 'rotate'],
]);

const ROSE_OFFSETS_DEG = Object.freeze([-180, -135, -90, -45, 0, 45, 90, 135]);

function finite(value, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function clamp(value, lo, hi) {
  return Math.max(lo, Math.min(hi, value));
}

export function normalizeWindShiftMode(value, fallback = 'fixed') {
  if (typeof value === 'number' && Number.isFinite(value)) {
    const byNumber = WIND_SHIFT_MODES.find((mode) => mode.value === Math.round(value));
    if (byNumber) return byNumber.id;
  }
  const key = String(value ?? '').trim().toLowerCase().replace(/_/g, '-');
  if (MODE_ALIASES.has(key)) return MODE_ALIASES.get(key);
  const safeFallback = MODE_ALIASES.get(String(fallback ?? '').trim().toLowerCase().replace(/_/g, '-'));
  return safeFallback || 'fixed';
}

export function windShiftModeValue(value, fallback = 'fixed') {
  const id = normalizeWindShiftMode(value, fallback);
  return WIND_SHIFT_MODES.find((mode) => mode.id === id)?.value ?? 0;
}

export function windShiftModeLabel(value, fallback = 'fixed') {
  const id = normalizeWindShiftMode(value, fallback);
  return WIND_SHIFT_MODES.find((mode) => mode.id === id)?.label ?? 'Fixed prevailing wind';
}

/** Wrap an azimuth to the same -180..180 range used by the studio sliders. */
export function normalizeWindAzimuthDeg(value) {
  let deg = finite(value, 0) % 360;
  if (deg > 180) deg -= 360;
  if (deg < -180) deg += 360;
  return deg === 0 ? 0 : deg;
}

/** Shortest signed turn from one bearing to another, in -180..180 degrees. */
export function windAngleDeltaDeg(fromDeg, toDeg) {
  return normalizeWindAzimuthDeg(finite(toDeg, 0) - finite(fromDeg, 0));
}

function circularDistanceDeg(a, b) {
  return Math.abs(windAngleDeltaDeg(a, b));
}

function smootherstep01(value) {
  const t = clamp(finite(value, 0), 0, 1);
  return t * t * t * (t * (t * 6 - 15) + 10);
}

function transitionStrength(params = {}) {
  return clamp(finite(
    params.windShiftTransition ?? params.windTransition ?? params.shiftTransition,
    0.55
  ), 0, 1);
}

function transitionUnitsForDelta(deltaDeg, params = {}, totalScheduleUnits = 32) {
  const steps45 = Math.max(1, Math.ceil(Math.abs(deltaDeg) / 45));
  const scale = 0.45 + 1.65 * transitionStrength(params);
  // A quota unit is not the same physical duration in every mode: the
  // variable rose has 32 units/cycle, while a fronts schedule may have
  // only five spell anchors/cycle. Express a frontal turn first as a
  // fraction of the whole weather-pattern cycle (roughly 3% per 45° at
  // the default strength), then convert back to this schedule's units.
  const cycleFraction = (0.012 + 0.020 * steps45) * scale;
  return cycleFraction * Math.max(1, finite(totalScheduleUnits, 32));
}

function prevailingAzimuthDeg(params = {}) {
  return finite(
    params.windAzimuth ?? params.windAzimuthDeg ?? params.azimuthDeg ?? params.azimuth,
    35
  );
}

function shiftRangeDeg(params = {}) {
  return clamp(
    finite(params.windShiftRange ?? params.windShiftRangeDeg ?? params.shiftRangeDeg, 55),
    0,
    180
  );
}

function sectorCount(params = {}) {
  return clamp(Math.round(finite(params.windShiftSectors ?? params.shiftSectors, 5)), 2, 9);
}

function regimeSeed(params = {}) {
  return Math.round(finite(
    params.windSeed ?? params.windShiftSeed ?? params.seed ?? params.seedSalt,
    0
  )) | 0;
}

function episodesPerCycle(params = {}, fallback = 24) {
  return clamp(Math.round(finite(
    params.windShiftEpisodes ?? params.windEpisodesPerCycle ?? params.episodesPerCycle,
    fallback
  )), 4, 64);
}

function hash32(a = 0, b = 0, c = 0, d = 0) {
  let h = 2166136261 >>> 0;
  for (const value of [a, b, c, d]) {
    h ^= (Number(value) | 0) >>> 0;
    h = Math.imul(h, 16777619);
    h ^= h >>> 13;
    h = Math.imul(h, 0x5bd1e995);
  }
  return h >>> 0;
}

function unitHash(a = 0, b = 0, c = 0, d = 0) {
  return hash32(a, b, c, d) / 4294967296;
}

/**
 * Prevailing-weighted wind-rose probabilities for the eight 45° sectors.
 * `windShiftRange` is the spread control: small values concentrate time
 * near the prevailing sector; 180° approaches a broad, nearly uniform rose
 * while retaining a modest prevailing peak. Left/right pairs always have
 * identical weights, so the intended distribution cannot favour one side.
 */
export function windRoseWeightsDeg(params = {}) {
  const range = shiftRangeDeg(params);
  if (range <= 0) {
    return ROSE_OFFSETS_DEG.map((offsetDeg) => ({ offsetDeg, weight: offsetDeg === 0 ? 1 : 0 }));
  }
  const spread = range / 180;
  // A narrow spread stays tightly prevailing/adjacent; the usual 55–60°
  // settings yield roughly one-third prevailing, one-fifth in each adjacent
  // sector, and a few percent in side/rear/opposite sectors; 180° is broad.
  const kappa = 0.14 + 4.70 * Math.exp(-range / 50);
  const raw = ROSE_OFFSETS_DEG.map((offsetDeg) => {
    const rad = offsetDeg * Math.PI / 180;
    return Math.exp(kappa * (Math.cos(rad) - 1));
  });
  const rawSum = raw.reduce((sum, value) => sum + value, 0) || 1;
  const uniformMix = 0.012 + 0.28 * Math.pow(spread, 1.35);
  return ROSE_OFFSETS_DEG.map((offsetDeg, index) => ({
    offsetDeg,
    weight: (1 - uniformMix) * (raw[index] / rawSum) + uniformMix / ROSE_OFFSETS_DEG.length,
  }));
}

function allocateVariableCounts(weights, episodes, seed, cycleIndex) {
  // Indices in ROSE_OFFSETS_DEG. Pair groups are allocated two episodes at
  // a time (one to each side), preserving exact left/right balance.
  const groups = [
    { indices: [4] },    // prevailing
    { indices: [3, 5] }, // ±45°
    { indices: [2, 6] }, // ±90°
    { indices: [1, 7] }, // ±135°
    { indices: [0] },    // opposite
  ];
  const counts = new Array(weights.length).fill(0);
  const allocated = groups.map((group) => {
    const target = group.indices.reduce((sum, index) => sum + weights[index].weight, 0) * episodes;
    const unit = group.indices.length;
    const total = unit === 2 ? Math.floor(target / 2) * 2 : Math.floor(target);
    for (const index of group.indices) counts[index] = total / unit;
    return total;
  });
  let remaining = episodes - allocated.reduce((sum, value) => sum + value, 0);

  while (remaining > 0) {
    let bestGroup = -1;
    let bestScore = -Infinity;
    for (let groupIndex = 0; groupIndex < groups.length; groupIndex++) {
      const group = groups[groupIndex];
      const unit = group.indices.length;
      if (unit > remaining) continue;
      const target = group.indices.reduce((sum, index) => sum + weights[index].weight, 0) * episodes;
      const deficitPerUnit = (target - allocated[groupIndex]) / unit;
      const score = deficitPerUnit + unitHash(seed, cycleIndex, groupIndex, 0x71a3) * 1e-6;
      if (score > bestScore) {
        bestScore = score;
        bestGroup = groupIndex;
      }
    }
    if (bestGroup < 0) break;
    const group = groups[bestGroup];
    for (const index of group.indices) counts[index]++;
    allocated[bestGroup] += group.indices.length;
    remaining -= group.indices.length;
  }
  return counts;
}

function allocateEqualCounts(count, episodes) {
  const base = Math.floor(episodes / count);
  const counts = new Array(count).fill(base);
  for (let i = 0; i < episodes - base * count; i++) counts[i]++;
  return counts;
}

function sequenceFromCounts(offsets, counts, seed, cycleIndex, jitterDeg, localityScale = 1) {
  const selected = new Array(counts.length).fill(0);
  const total = counts.reduce((sum, value) => sum + value, 0);
  const sequence = [];
  const locality = clamp(finite(localityScale, 1), 0, 1.5);
  let previousOffset = null;
  let previousIndex = -1;
  let previousJitter = 0;
  let runIndex = -1;

  for (let position = 0; position < total; position++) {
    const candidates = offsets
      .map((offset, index) => ({ offset, index }))
      .filter(({ index }) => selected[index] < counts[index]);

    // Fair-deficit ordering keeps every prefix close to the intended wind
    // rose. On top of that, a persistence/locality term models a real
    // pressure pattern: staying in the same sector is most likely, moving
    // to a neighbouring sector is next most likely, and a direct jump to a
    // distant sector is penalized. The timeline below still blends any
    // remaining large anchor change over time, so the realized direction
    // never teleports. A small deterministic hash only breaks ties and
    // changes side order by cycle.
    candidates.sort((a, b) => {
      const score = (candidate) => {
        const ideal = counts[candidate.index] * (position + 1) / total;
        let localityScore = 0;
        if (previousOffset !== null) {
          const distance = circularDistanceDeg(candidate.offset, previousOffset);
          if (distance <= 0.01) localityScore = 1.55;
          else if (distance <= 45.01) localityScore = 0.55;
          else if (distance <= 90.01) localityScore = 0.0;
          else localityScore = -1.15 * (distance - 90) / 90;
          localityScore *= locality;
        }
        return ideal - selected[candidate.index]
          + localityScore
          + unitHash(seed, cycleIndex, position, candidate.offset) * 0.08;
      };
      return score(b) - score(a);
    });
    const chosen = candidates[0];
    selected[chosen.index]++;

    if (chosen.index !== previousIndex) {
      runIndex++;
      const globalRun = cycleIndex * Math.max(total, 1) + runIndex;
      previousJitter = jitterDeg > 0
        ? (unitHash(seed ^ 0x5f3759df, globalRun, chosen.offset, 0x27d4) * 2 - 1) * jitterDeg
        : 0;
    }
    sequence.push(chosen.offset + previousJitter);
    previousOffset = chosen.offset;
    previousIndex = chosen.index;
  }
  return sequence;
}

/**
 * Direction offsets (degrees relative to prevailing) for one complete
 * weather-pattern cycle. The returned sequence is the quota-unit anchor
 * schedule from which `windDirectionTimelineDeg` builds weather spells
 * and smooth frontal transitions. Consecutive equal entries are one longer
 * persistent spell, not separate instantaneous episodes.
 */
export function windDirectionScheduleDeg(params = {}, cycleIndex = 0) {
  const mode = normalizeWindShiftMode(params.windShiftMode ?? params.windDirectionMode ?? params.shiftMode);
  const range = shiftRangeDeg(params);
  const seed = regimeSeed(params);
  const cycle = Math.round(finite(cycleIndex, 0));

  if (mode === 'fixed' || (mode !== 'rotate' && range <= 0)) return [0];

  if (mode === 'variable') {
    const episodes = episodesPerCycle(params, 32);
    const weights = windRoseWeightsDeg(params);
    const counts = allocateVariableCounts(weights, episodes, seed, cycle);
    const jitter = clamp(3 + range * 0.08, 3, 12);
    return sequenceFromCounts(ROSE_OFFSETS_DEG, counts, seed, cycle, jitter);
  }

  if (mode === 'sectors') {
    const count = sectorCount(params);
    const offsets = Array.from({ length: count }, (_, index) =>
      count === 1 ? 0 : -range + (2 * range * index) / (count - 1));
    // Front anchors arrive in a seeded, non-monotonic order. Sorting by a
    // stable per-cycle hash gives every sector exactly one spell anchor per
    // cycle; the timeline blends between those anchors through the
    // intervening bearings rather than jumping or scanning monotonically.
    return offsets
      .map((offset, index) => ({ offset, key: unitHash(seed, cycle, index, 0x51f7) }))
      .sort((a, b) => a.key - b.key)
      .map(({ offset }) => offset);
  }

  // Full wind rose: equal quotas in all eight sectors. A weaker locality
  // term keeps this mode from degenerating into a regular clock-like march
  // around the compass; the timeline transitions provide the smoothness.
  const episodes = episodesPerCycle(params, 16);
  const counts = allocateEqualCounts(ROSE_OFFSETS_DEG.length, episodes);
  return sequenceFromCounts(ROSE_OFFSETS_DEG, counts, seed, cycle, 5, 0.18);
}

/**
 * Continuous direction timeline for one weather-pattern cycle.
 *
 * Consecutive quota units in the same sector are compressed into one
 * persistent `dwell` segment. Between spells, a `transition` segment turns
 * along the shortest circular arc; its relative duration increases with
 * the angular change (about one quota unit per 45° at the default frontal
 * transition strength, plus a small base duration). The final transition
 * aims at the next cycle's first spell, so the trajectory is continuous
 * across cycle boundaries as well. Segment start/end values are fractions
 * of this cycle, after normalizing dwell + transition durations.
 */
export function windDirectionTimelineDeg(params = {}, cycleIndex = 0) {
  const mode = normalizeWindShiftMode(params.windShiftMode ?? params.windDirectionMode ?? params.shiftMode);
  const range = shiftRangeDeg(params);
  const cycle = Math.round(finite(cycleIndex, 0));
  if (mode === 'fixed' || (mode !== 'rotate' && range <= 0)) {
    return [Object.freeze({
      kind: 'dwell', start: 0, end: 1, durationUnits: 1,
      fromOffsetDeg: 0, toOffsetDeg: 0, deltaDeg: 0,
    })];
  }

  const schedule = windDirectionScheduleDeg(params, cycle);
  const runs = [];
  for (const offsetDeg of schedule) {
    const last = runs[runs.length - 1];
    if (last && Math.abs(windAngleDeltaDeg(last.offsetDeg, offsetDeg)) <= 1e-9) {
      last.units++;
    } else {
      runs.push({ offsetDeg, units: 1 });
    }
  }

  const seed = regimeSeed(params);
  const transitionAfterRun = runs.map((run, runIndex) => {
    const nextOffsetDeg = runIndex + 1 < runs.length
      ? runs[runIndex + 1].offsetDeg
      : (windDirectionScheduleDeg(params, cycle + 1)[0] || 0);
    let deltaDeg = windAngleDeltaDeg(run.offsetDeg, nextOffsetDeg);
    if (Math.abs(Math.abs(deltaDeg) - 180) <= 1e-9) {
      // An exactly-opposite turn has two equally short arcs. Alternate the
      // veer/back side deterministically instead of always rotating the
      // transition through the same side of the compass.
      deltaDeg = unitHash(seed, cycle, runIndex, 0x6d2b) < 0.5 ? -180 : 180;
    }
    if (Math.abs(deltaDeg) <= 1e-9) return null;
    return {
      kind: 'transition',
      durationUnits: transitionUnitsForDelta(deltaDeg, params, schedule.length),
      fromOffsetDeg: run.offsetDeg,
      // Keep the unwrapped end here: a turn through +/-180 must pass
      // through the intervening bearings, then normalize on output.
      toOffsetDeg: run.offsetDeg + deltaDeg,
      deltaDeg,
    };
  });

  const dwellUnits = runs.map((run) => run.units);

  const rawSegments = [];
  runs.forEach((run, runIndex) => {
    rawSegments.push({
      kind: 'dwell',
      durationUnits: dwellUnits[runIndex],
      fromOffsetDeg: run.offsetDeg,
      toOffsetDeg: run.offsetDeg,
      deltaDeg: 0,
    });
    if (transitionAfterRun[runIndex]) rawSegments.push(transitionAfterRun[runIndex]);
  });

  const totalUnits = rawSegments.reduce((sum, segment) => sum + segment.durationUnits, 0) || 1;
  let cursor = 0;
  return rawSegments.map((segment, index) => {
    const start = cursor / totalUnits;
    cursor += segment.durationUnits;
    const end = index === rawSegments.length - 1 ? 1 : cursor / totalUnits;
    return Object.freeze({ ...segment, start, end });
  });
}

/**
 * Direction offset (degrees) added to the prevailing azimuth after
 * `cycles` complete weather-pattern cycles. Fractional cycles sample the
 * continuous spell/transition trajectory, using a quintic smootherstep so
 * the turn rate eases in and out at both ends of a frontal transition.
 */
export function windShiftOffsetDeg(params = {}, cycles = 0) {
  const mode = normalizeWindShiftMode(params.windShiftMode ?? params.windDirectionMode ?? params.shiftMode);
  const range = shiftRangeDeg(params);
  if (mode === 'fixed' || (mode !== 'rotate' && range <= 0)) return 0;

  const c = finite(cycles, 0);
  const cycleIndex = Math.floor(c);
  const fraction = clamp(c - cycleIndex, 0, 1);
  const timeline = windDirectionTimelineDeg(params, cycleIndex);
  const segment = timeline.find((entry) => fraction < entry.end) || timeline[timeline.length - 1];
  if (!segment || segment.kind === 'dwell') return segment?.fromOffsetDeg || 0;
  const local = (fraction - segment.start) / Math.max(1e-9, segment.end - segment.start);
  return segment.fromOffsetDeg + segment.deltaDeg * smootherstep01(local);
}

/** Effective azimuth (degrees, direction blown TOWARD) after `cycles`. */
export function effectiveWindAzimuthDeg(params = {}, cycles = 0) {
  return normalizeWindAzimuthDeg(prevailingAzimuthDeg(params) + windShiftOffsetDeg(params, cycles));
}

/**
 * Actual trajectory azimuths across a bake/simulation span, bounded for
 * HUD and CLI reporting. Samples are taken at weather-spell centres and at
 * frontal-transition midpoints, so the report shows both the sectors where
 * the wind dwells and the intermediate bearings it really erodes through
 * while veering/backing between them.
 */
export function sampleWindDirectionSequenceDeg(params = {}, totalCycles = 1, maxSamples = 17) {
  const mode = normalizeWindShiftMode(params.windShiftMode ?? params.windDirectionMode ?? params.shiftMode);
  const prevailing = prevailingAzimuthDeg(params);
  if (mode === 'fixed') return Array.from({ length: 5 }, () => normalizeWindAzimuthDeg(prevailing));

  const cycles = Math.max(0.001, finite(totalCycles, 1));
  const cap = clamp(Math.round(finite(maxSamples, 17)), 2, 32);
  const all = [];
  const wholeCycles = Math.ceil(cycles);
  for (let cycleIndex = 0; cycleIndex < wholeCycles; cycleIndex++) {
    const timeline = windDirectionTimelineDeg(params, cycleIndex);
    for (const segment of timeline) {
      const centre = cycleIndex + (segment.start + segment.end) * 0.5;
      if (centre <= cycles + 1e-9) {
        all.push(effectiveWindAzimuthDeg(params, centre));
      }
    }
  }
  if (!all.length) return [normalizeWindAzimuthDeg(prevailing)];
  if (all.length <= cap) return all;
  return Array.from({ length: cap }, (_, index) =>
    all[Math.round((index * (all.length - 1)) / (cap - 1))]);
}

function windShiftPeriodSteps(params = {}) {
  // A full weather pattern contains multiple spells and frontal turns.
  // Enforce a minimum period in step-sampled simulations so even a legacy
  // or user-entered very short value cannot compress a large veer/back
  // into one or two steps and recreate an apparent compass teleport.
  return clamp(
    finite(params.windShiftPeriod ?? params.windShiftPeriodSteps ?? params.shiftPeriodSteps, 720),
    240,
    36000
  );
}

/** Weather-pattern cycles elapsed at a Terrain simulation step. */
export function windCyclesAtStep(params = {}, step = 0) {
  return Math.max(0, finite(step, 0)) / windShiftPeriodSteps(params);
}

export function effectiveWindAzimuthAtStep(params = {}, step = 0) {
  return effectiveWindAzimuthDeg(params, windCyclesAtStep(params, step));
}

/** Compact, UI-friendly description of the configured regime. */
export function describeWindRegime(params = {}) {
  const mode = normalizeWindShiftMode(params.windShiftMode ?? params.windDirectionMode ?? params.shiftMode);
  const range = Math.round(shiftRangeDeg(params));
  const period = Math.round(windShiftPeriodSteps(params));
  if (mode === 'fixed') return 'fixed prevailing direction';
  if (mode === 'variable') return `variable wind rose · prevailing-weighted weather spells · smooth frontal transitions · spread ${range}° · ${period} steps/weather pattern`;
  if (mode === 'sectors') return `${sectorCount(params)} weather-front sectors across ±${range}° · smooth veer/back transitions · ${period} steps/weather pattern`;
  return `full wind rose · all-sector weather spells · smooth frontal transitions · ${period} steps/weather pattern`;
}
