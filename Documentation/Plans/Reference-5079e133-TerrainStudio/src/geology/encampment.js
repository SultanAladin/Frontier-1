/**
 * Ancestral Puebloan Arch Encampment (`arch-encampment`).
 *
 * Optional rock + village feature set that dramatically improves terrain readability from the
 * default camera: a natural boulder-built sandstone arch gate frames the campsite, while
 * terraced masonry room blocks, a round kiva, a stepped watchtower, drying racks, a campfire ring
 * and a leaning ladder give clear human scale to the landscape.
 *
 * All elements are volumetric SDF primitives (convex half-space polyhedra) so they dual-contour
 * into crisp, watertight, game-ready geometry — no textures, no billboards.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
  rotateAroundAxis,
  vnorm,
  clamp,
} from '../core/vec3.js';
import {
  createBoxPoly,
  clipPolyByHalfSpace,
  measurePoly,
  transformPoly,
  compilePolyBlock,
} from './polyhedron.js';
import {
  compileCurvedArchRib,
  addBoulderMassifPier,
  buildBoulderArchSpanFromStartToFinish,
} from './arch-curves.js';

const UP = [0, 1, 0];

/**
 * Builds a single crisp masonry unit: a slightly-bevelled box with planar course joints.
 */
function masonryUnit(center, half, yawRad, frame, rng, opts = {}) {
  const {
    groundY = 0,
    bevel = 0.03,
    seedSalt = rng.int(1, 999999),
    clipBase = true,
  } = opts;

  const [cx, cy, cz] = center;
  const [hx, hy, hz] = half;

  let poly = createBoxPoly(
    -hx, -hy, -hz,
    hx, hy, hz,
    'masonry'
  );

  // Chipped, weathered corners: two oblique chamfers mimic hand-laid stone courses
  const cornerN = vnorm([1, 0.35, 0.55]);
  poly = clipPolyByHalfSpace(poly, cornerN, hx * 0.86 + hy * 0.30 + hz * 0.20, 'chip') || poly;
  const cornerN2 = vnorm([-0.42, 0.30, -1.0]);
  poly = clipPolyByHalfSpace(poly, cornerN2, hx * 0.30 + hy * 0.26 + hz * 0.72, 'chip') || poly;

  if (clipBase && cy - hy < groundY + 0.2) {
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
  }

  poly = transformPoly(poly, [cx, cy, cz], UP, yawRad, null);

  return compilePolyBlock(poly, frame, {
    edgeBevel: bevel,
    smoothUnionR: bevel * 0.5,
    detailScale: 1.0,
    isEolian: false,
    seedSalt,
  });
}

/**
 * Round kiva: a ring of curved masonry piers under a flat roof slab with a ladder entry.
 */
function buildKiva(blocks, center, radius, height, frame, rng, opts) {
  const { groundY = 0, bevel = 0.03, spanScale = 1 } = opts;
  const [kx, kz] = [center[0], center[2]];
  const ringCount = 14;

  for (let i = 0; i < ringCount; i++) {
    const ang = (i / ringCount) * Math.PI * 2;
    const px = kx + Math.cos(ang) * radius;
    const pz = kz + Math.sin(ang) * radius;
    const comp = masonryUnit(
      [px, groundY + height * 0.5, pz],
      [radius * 0.30, height * 0.52, radius * 0.30],
      ang + Math.PI * 0.5,
      frame,
      rng,
      { groundY, bevel }
    );
    if (comp) blocks.push(comp);
  }

  // Flat roof slabs spanning the kiva
  const roof = masonryUnit(
    [kx, groundY + height * 1.02, kz],
    [radius * 1.06, height * 0.11, radius * 1.06],
    rng.signed() * 0.10,
    frame,
    rng,
    { groundY, bevel, clipBase: false }
  );
  if (roof) blocks.push(roof);

  // Entry hatch ring + leaning entry ladder
  const hatch = masonryUnit(
    [kx + radius * 0.18, groundY + height * 1.16, kz - radius * 0.10],
    [radius * 0.20, height * 0.07, radius * 0.20],
    rng.signed() * 0.3,
    frame,
    rng,
    { groundY, bevel, clipBase: false }
  );
  if (hatch) blocks.push(hatch);

  for (let r = 0; r < 3; r++) {
    const ladder = masonryUnit(
      [kx + radius * 0.92, groundY + height * 0.72 - r * height * 0.16, kz - radius * 0.55],
      [radius * 0.045, height * 0.42, radius * 0.045],
      Math.PI * 0.22,
      frame,
      rng,
      { groundY, bevel: bevel * 0.6, clipBase: false }
    );
    if (ladder) blocks.push(ladder);
  }

  return blocks;
}

/**
 * Stacked, tapering masonry watchtower with slot windows (achieved by splitting the box courses).
 */
function buildWatchtower(blocks, center, width, height, frame, rng, opts) {
  const { groundY = 0, bevel = 0.03 } = opts;
  const [tx, tz] = [center[0], center[2]];
  const courses = 7;

  for (let c = 0; c < courses; c++) {
    const f = c / (courses - 1);
    const taper = 1.0 - 0.26 * f;
    const courseH = height / courses;
    const y = groundY + courseH * (c + 0.5);
    const hw = width * 0.5 * taper;

    if (c >= 4 && c <= 5) {
      // Window course: split into two flanking piers with a dark void between them
      for (const s of [-1, 1]) {
        const pier = masonryUnit(
          [tx + s * hw * 0.62, y, tz],
          [hw * 0.36, courseH * 0.52, hw * (0.94 - 0.02 * c)],
          rng.signed() * 0.03,
          frame,
          rng,
          { groundY, bevel }
        );
        if (pier) blocks.push(pier);
      }
      const lintel = masonryUnit(
        [tx, y + courseH * 0.42, tz],
        [hw * 1.0, courseH * 0.12, hw * 0.98],
        rng.signed() * 0.02,
        frame,
        rng,
        { groundY, bevel }
      );
      if (lintel) blocks.push(lintel);
    } else {
      const block = masonryUnit(
        [tx + rng.signed() * hw * 0.06, y, tz + rng.signed() * hw * 0.05],
        [hw, courseH * 0.53, hw * (0.95 + rng.signed() * 0.03)],
        rng.signed() * 0.05,
        frame,
        rng,
        { groundY, bevel }
      );
      if (block) blocks.push(block);
    }
  }
  return blocks;
}

export function buildArchEncampment(params, tectonics, rng, addTalusFn = null) {
  const S = clamp(Number(params.scale) || 19.0, 8, 40);
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const curves = [];

  const strikeRad = (params.strikeAngle ?? -8) * DEG2RAD;
  const dipRad = clamp(params.dipAngle ?? 8, 2, 22) * DEG2RAD;
  const beddingFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.72;
  const bevel = S * (0.0016 + 0.0026 * (params.edgeWear ?? 0.30));
  const smoothR = S * 0.0030;

  // ---------------------------------------------------------------------------
  // 1. Boulder-built sandstone arch GATE (the hero silhouette framing the village)
  // ---------------------------------------------------------------------------
  addBoulderMassifPier(
    blocks,
    -0.40 * S, -0.06 * S,
    0.30 * S, 0.48 * S, 0.30 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 5 }
  );
  addBoulderMassifPier(
    blocks,
    0.36 * S, 0.02 * S,
    0.26 * S, 0.38 * S, 0.27 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 4 }
  );

  const spanH = (0.50 + 0.05 * overhang) * S;
  const ctrlY = spanH / 0.75;
  const p0 = [-0.40 * S, groundY + 0.01 * S, -0.06 * S];
  const p1 = [-0.35 * S, groundY + ctrlY * 1.10,  0.05 * S];
  const p2 = [ 0.26 * S, groundY + ctrlY * 0.88, -0.04 * S];
  const p3 = [ 0.36 * S, groundY + 0.01 * S,  0.02 * S];
  const archRib = compileCurvedArchRib(
    p0,
    p1,
    p2,
    p3,
    beddingFrame,
    rng,
    {
      S,
      rPierIn: S * 0.052,
      rPierOut: S * 0.050,
      rCrownIn: S * 0.026,
      rCrownOut: S * 0.022,
      waistT: 0.52,
      waistPinch: 0.80,
      edgeBevel: S * 0.0022,
      smoothUnionR: smoothR,
      isEolian: true,
      crookedAmp: 1.05,
    }
  );
  curves.push(archRib);
  buildBoulderArchSpanFromStartToFinish(blocks, archRib, beddingFrame, rng, {
    S,
    groundY,
    rockStyle: 'sandstone',
    bevel,
    smoothR,
    spanScale: 0.96,
    leftMassScale: 1.20,
    rightMassScale: 1.02,
    portalCenter: [0.0, groundY + 0.20 * S, 0.0],
    minPortalClear: S * 0.175,
  });

  // ---------------------------------------------------------------------------
  // 2. Terraced masonry room blocks tucked into the arch's left alcove
  // ---------------------------------------------------------------------------
  const rooms = Math.max(3, Math.min(12, Number(params.encampmentRooms) || 7));
  const baseX = -0.22 * S;
  const baseZ = 0.26 * S;
  for (let r = 0; r < rooms; r++) {
    const terrace = Math.floor(r / 3);
    const col = r % 3;
    const roomW = S * (0.045 + 0.010 * rng.float());
    const roomH = S * (0.032 + 0.008 * rng.float());
    const roomD = S * (0.040 + 0.012 * rng.float());
    const rx = baseX + col * S * 0.062 + rng.signed() * S * 0.006;
    const rz = baseZ + terrace * S * 0.030 + rng.signed() * S * 0.008;
    const ry = groundY + roomH * (terrace * 2.0 + 1.0);

    const room = masonryUnit(
      [rx, ry, rz],
      [roomW, roomH, roomD],
      rng.signed() * 0.09,
      beddingFrame,
      rng,
      { groundY, bevel }
    );
    if (room) blocks.push(room);

    // Doorway void: a smaller offset block leaves a shadowed opening normals-compatible with SDF union
    if (r % 2 === 0) {
      const jamb = masonryUnit(
        [rx + roomW * 0.92, ry - roomH * 0.05, rz + roomD * 0.2],
        [roomW * 0.20, roomH * 0.46, roomD * 0.92],
        rng.signed() * 0.05,
        beddingFrame,
        rng,
        { groundY, bevel }
      );
      if (jamb) blocks.push(jamb);
    }
  }

  // ---------------------------------------------------------------------------
  // 3. Round kiva on the plaza + stepped watchtower on the right buttress toe
  // ---------------------------------------------------------------------------
  buildKiva(blocks, [0.09 * S, groundY, 0.20 * S], S * 0.055, S * 0.052, beddingFrame, rng, {
    groundY,
    bevel,
  });

  buildWatchtower(blocks, [0.30 * S, groundY, 0.16 * S], S * 0.070, S * 0.24, beddingFrame, rng, {
    groundY,
    bevel,
  });

  // ---------------------------------------------------------------------------
  // 4. Campfire ring, plaza flagstones & seating boulders — instant human scale
  // ---------------------------------------------------------------------------
  const fireX = 0.02 * S;
  const fireZ = 0.09 * S;
  for (let i = 0; i < 9; i++) {
    const ang = (i / 9) * Math.PI * 2 + rng.signed() * 0.12;
    const rr = S * 0.020;
    const stone = masonryUnit(
      [fireX + Math.cos(ang) * rr, groundY + S * 0.006, fireZ + Math.sin(ang) * rr],
      [S * 0.0075, S * 0.0065, S * 0.0075],
      ang,
      beddingFrame,
      rng,
      { groundY, bevel: bevel * 0.7 }
    );
    if (stone) blocks.push(stone);
  }

  // Charred logs inside the fire ring
  for (let i = 0; i < 3; i++) {
    const ang = (i / 3) * Math.PI * 2 + 0.4;
    const log = masonryUnit(
      [fireX + Math.cos(ang) * S * 0.008, groundY + S * 0.005, fireZ + Math.sin(ang) * S * 0.008],
      [S * 0.0135, S * 0.0034, S * 0.0034],
      ang,
      beddingFrame,
      rng,
      { groundY, bevel: bevel * 0.55 }
    );
    if (log) blocks.push(log);
  }

  // Flagstone plaza slabs
  for (let i = 0; i < 7; i++) {
    const ang = (i / 7) * Math.PI * 2;
    const slab = masonryUnit(
      [fireX + Math.cos(ang) * S * 0.045, groundY + S * 0.0016, fireZ + Math.sin(ang) * S * 0.045],
      [S * 0.021, S * 0.0022, S * 0.016],
      ang + rng.signed() * 0.3,
      beddingFrame,
      rng,
      { groundY, bevel: bevel * 0.4 }
    );
    if (slab) blocks.push(slab);
  }

  if (addTalusFn) {
    addTalusFn(blocks, beddingFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.46) * 1.02,
      scale: S * 0.92,
      spanX: S * 0.94,
      groundY,
      edgeBevel: bevel,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, curves, groundY };
}
