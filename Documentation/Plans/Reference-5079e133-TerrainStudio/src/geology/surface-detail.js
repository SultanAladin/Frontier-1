/**
 * Lithology-Specific 3D Geological Surface Sculptor:
 *
 * 1. Utah Eolian Sandstone Regime (`isEolian` / `isDesertEolian`):
 *    - Multi-scale truncated eolian cross-bedding foreset packages, wind-etched bounding partings,
 *      and crisp conchoidal/planar sandstone spall facets
 * 2. Stratified Sedimentary Regime (`isStratifiedSedimentary`):
 *    - Multi-order rhythmic horizontal/dipping stone laminations, recessed shale partings,
 *      and natural strike-oblique cleavage scarplets modulated by bed hardness
 * 3. Limestone Karst Regime (`isKarst`):
 *    - Domain-warped aperiodic vertical rainwater dissolution flutes (rillenkarren) + irregular
 *      solution pockets & vertical karst cleavage (zero Cartesian waffle grids!)
 * 4. Crystalline / Jointed / Foliated Regime (Granite, Basalt, Schist, Dolomite, Alpine Crag, Erratics):
 *    - Multi-tier parallel-cleavage stepped rock scarplets across full rock faces & arêtes
 */

import { DEG2RAD, buildStrikeDipFrame, clamp, smin, smax, smoothstep } from '../core/vec3.js';
import { evalPlanarCleavage3D, scalarField3D } from '../core/spatial-noise.js';
import { evalCompiledBlock, lastSecondPlaneDist } from './polyhedron.js';
import { evalCurvedArchRib } from './arch-curves.js';
import { evalCrackedMonolithColor } from './cracks-demo.js';
import { FORMATION_PRESETS } from './presets.js';

const _cleav1 = { facet: 0, wallDist: 0, wnx: 0, wny: 0, wnz: 0, cellId: 0 };
const _cleav2 = { facet: 0, wallDist: 0, wnx: 0, wny: 0, wnz: 0, cellId: 0 };
const _cleav3 = { facet: 0, wallDist: 0, wnx: 0, wny: 0, wnz: 0, cellId: 0 };

/**
 * Smooth C2 periodic asymmetric foreset / bedding step profile in [-0.5, 0.5].
 */
function smoothForesetLedge(phase) {
  const f = phase - Math.floor(phase);
  const rise = f / 0.82;
  const drop = smoothstep(0.82, 1.0, f);
  return (rise * (1.0 - drop) + (f - 1.0) * drop) - 0.41;
}

/**
 * Crisp tabular sedimentary stratum profile in [-0.5, 0.5]:
 * Flat vertical face across [0.14 .. 0.86], sharp recessed parting notch near 0 / 1.
 */
function tabularBedProfile(phase) {
  const f = phase - Math.floor(phase);
  const up = smoothstep(0.0, 0.16, f);
  const down = 1.0 - smoothstep(0.84, 1.0, f);
  return (up * down) - 0.72;
}

export function createGeologicalSdfEvaluator(macro, params, rng) {
  const { blocks, cuts, groundY } = macro;
  const curves = macro.curves || [];
  const crackNet = macro.crackNetwork || null;
  const S = params.scale;
  const archetype = params.archetype || 'cliff-wall';
  const preset = FORMATION_PRESETS[archetype] || FORMATION_PRESETS['cliff-wall'];
  const aniso = preset.fabricAnisotropy || [0.85, 2.5, 1.1];
  const isDesert = !!preset.isDesertEolian;
  const isSedimentary = !!preset.isStratifiedSedimentary;
  const isKarst = !!preset.isKarst;
  const isSpireOrHoodoo = archetype === 'desert-spires' || archetype === 'hoodoo-columns';
  const isArch = archetype === 'desert-arch' || archetype === 'double-arch' || archetype === 'sea-arch';
  const slotCanyon = archetype === 'slot-canyon' ? (macro.slotCanyon || null) : null;
  const slotSamples = slotCanyon?.samples?.length > 1 ? slotCanyon.samples : null;
  const _slotPath = { z: 0, x: 0, floorY: 0, halfGap: 1, crestY: 1 };
  const slotPathAt = slotSamples
    ? (wz) => {
        const first = slotSamples[0];
        const last = slotSamples[slotSamples.length - 1];
        if (wz <= first.z) return first;
        if (wz >= last.z) return last;
        const f = ((wz - first.z) / Math.max(1e-9, last.z - first.z)) * (slotSamples.length - 1);
        const i = Math.min(slotSamples.length - 2, Math.max(0, Math.floor(f)));
        const t = clamp(f - i, 0, 1);
        const a = slotSamples[i];
        const b = slotSamples[i + 1];
        _slotPath.z = wz;
        _slotPath.x = a.x + (b.x - a.x) * t;
        _slotPath.floorY = a.floorY + (b.floorY - a.floorY) * t;
        _slotPath.halfGap = a.halfGap + (b.halfGap - a.halfGap) * t;
        _slotPath.crestY = a.crestY + (b.crestY - a.crestY) * t;
        return _slotPath;
      }
    : null;

  const facetAmp = params.facetAmplitude ?? 0.46;
  const cleavSharp = params.cleavageSharpness ?? 0.78;
  const aperture = params.jointAperture ?? 0.45;
  const edgeWear = params.edgeWear ?? 0.38;

  const macroCleavFreq = 1 / (S * 0.24);
  const mesoFreq = 1 / (S * 0.125);
  const microFreq = 1 / (S * 0.052);

  const mesoAmp = S * 0.0135 * facetAmp;
  const microAmp = S * 0.0042 * facetAmp;
  const jointNotchDepth = S * 0.010 * (0.30 + 0.65 * aperture);
  const talusBlendR = S * (isDesert ? 0.028 : 0.022);
  const cutBevelR = S * (isDesert ? (0.002 + 0.004 * edgeWear) : (0.003 + 0.006 * edgeWear));
  const edgeZoneWidth = S * 0.09;

  const sharpness1 = 8.0 + 14.0 * cleavSharp;
  const sharpness2 = 9.0 + 12.0 * cleavSharp;
  const tiltAmp = 0.042 * facetAmp;

  const restSalt = rng.int(100, 999999);
  const warpSalt = rng.int(100, 999999);
  const warpFreq = 1 / (S * 0.34);

  // Pre-generate 8 eolian dune cross-bedding foreset packages
  const duneSets = [];
  for (let i = 0; i < 8; i++) {
    const az = rng.float() * Math.PI * 2;
    const dip = rng.range(0.18, 0.38);
    duneSets.push({
      nx: Math.cos(az) * Math.sin(dip),
      ny: Math.cos(dip),
      nz: Math.sin(az) * Math.sin(dip),
      freq: 1 / (S * rng.range(0.046, 0.078)),
      strength: rng.range(0.50, 0.90),
    });
  }
  const boundingSetFreq = 1 / (S * 0.088);

  let maxBlockSmooth = talusBlendR;
  for (let i = 0; i < blocks.length; i++) {
    if (blocks[i].smoothUnionR > maxBlockSmooth) maxBlockSmooth = blocks[i].smoothUnionR;
  }
  for (let i = 0; i < curves.length; i++) {
    if (curves[i].smoothUnionR > maxBlockSmooth) maxBlockSmooth = curves[i].smoothUnionR;
  }

  const baseDetailMargin = mesoAmp * 2.1 + microAmp * 1.8 + jointNotchDepth * 1.5 + maxBlockSmooth * 0.70 + cutBevelR;
  const detailMargin = baseDetailMargin;

  function buildBrickContext(cx, cy, cz, cullRadius) {
    const localBlocks = [];
    const localCurves = [];
    const margin = cullRadius + maxBlockSmooth;

    for (let i = 0; i < blocks.length; i++) {
      const b = blocks[i];
      const dx = cx - b.cx, dy = cy - b.cy, dz = cz - b.cz;
      const maxR = b.radius + margin;
      if (dx * dx + dy * dy + dz * dz <= maxR * maxR) {
        localBlocks.push(b);
      }
    }

    for (let i = 0; i < curves.length; i++) {
      const cb = curves[i];
      const dx = cx - cb.cx, dy = cy - cb.cy, dz = cz - cb.cz;
      const maxR = cb.radius + margin;
      if (dx * dx + dy * dy + dz * dz <= maxR * maxR) {
        localCurves.push(cb);
      }
    }

    if (localBlocks.length === 0 && localCurves.length === 0) return null;

    const localCuts = [];
    for (let i = 0; i < cuts.length; i++) {
      const c = cuts[i];
      const dx = cx - c.cx, dy = cy - c.cy, dz = cz - c.cz;
      const maxR = c.radius + margin;
      if (dx * dx + dy * dy + dz * dz <= maxR * maxR) {
        localCuts.push(c);
      }
    }

    return { blocks: localBlocks, curves: localCurves, cuts: localCuts };
  }

  const fullCtx = { blocks, curves, cuts };

  function evalMacroWithWinner(wx, wy, wz, ctx, outState) {
    const bList = ctx.blocks;
    let dBedrock = 1e9;
    let dSecondBedrock = 1e9;
    let dBoulder = 1e9;
    let dSecondBoulder = 1e9;
    let dTalus = 1e9;

    let bestBedrockBlock = null;
    let bestBedrockSecondPlane = -1e9;
    let bestRawBedrockDist = 1e9;
    let bestBoulderBlock = null;
    let bestBoulderSecondPlane = -1e9;
    let bestTalusBlock = null;

    const pruneMargin = maxBlockSmooth + 0.3;

    for (let i = 0; i < bList.length; i++) {
      const b = bList[i];
      const curBest = dBedrock < dBoulder ? (dBedrock < dTalus ? dBedrock : dTalus) : (dBoulder < dTalus ? dBoulder : dTalus);
      const thresh = curBest + pruneMargin;
      if (
        wx - b.maxX > thresh || b.minX - wx > thresh ||
        wy - b.maxY > thresh || b.minY - wy > thresh ||
        wz - b.maxZ > thresh || b.minZ - wz > thresh
      ) {
        continue;
      }

      const d = evalCompiledBlock(b, wx, wy, wz);
      const secPlane = lastSecondPlaneDist;

      if (b.isTalus) {
        if (d < dTalus) bestTalusBlock = b;
        dTalus = smin(dTalus, d, talusBlendR);
      } else if (b.isBoulder) {
        if (d < dBoulder) {
          dSecondBoulder = dBoulder;
          dBoulder = d;
          bestBoulderBlock = b;
          bestBoulderSecondPlane = secPlane;
        } else if (d < dSecondBoulder) {
          dSecondBoulder = d;
        }
      } else {
        if (d < bestRawBedrockDist) {
          bestRawBedrockDist = d;
          bestBedrockBlock = b;
          bestBedrockSecondPlane = secPlane;
        }
        if (b.smoothUnionR > 1e-4) {
          if (d < dBedrock) dSecondBedrock = dBedrock;
          else if (d < dSecondBedrock) dSecondBedrock = d;
          dBedrock = smin(dBedrock, d, b.smoothUnionR);
        } else {
          if (d < dBedrock) {
            dSecondBedrock = dBedrock;
            dBedrock = d;
          } else if (d < dSecondBedrock) {
            dSecondBedrock = d;
          }
        }
      }
    }

    const cvList = ctx.curves || [];
    for (let i = 0; i < cvList.length; i++) {
      const cb = cvList[i];
      const curBest = dBedrock < dBoulder ? (dBedrock < dTalus ? dBedrock : dTalus) : (dBoulder < dTalus ? dBoulder : dTalus);
      const thresh = curBest + pruneMargin;
      if (
        wx - cb.maxX > thresh || cb.minX - wx > thresh ||
        wy - cb.maxY > thresh || cb.minY - wy > thresh ||
        wz - cb.maxZ > thresh || cb.minZ - wz > thresh
      ) {
        continue;
      }
      const d = evalCurvedArchRib(cb, wx, wy, wz);
      if (d < bestRawBedrockDist) {
        bestRawBedrockDist = d;
        bestBedrockBlock = cb;
        bestBedrockSecondPlane = -0.06;
      }
      if (cb.smoothUnionR > 1e-4) {
        dBedrock = smin(dBedrock, d, cb.smoothUnionR);
      } else if (d < dBedrock) {
        dSecondBedrock = dBedrock;
        dBedrock = d;
      }
    }

    const cList = ctx.cuts;
    for (let i = 0; i < cList.length; i++) {
      const c = cList[i];
      const cBevel = c.edgeBevel > 0 ? c.edgeBevel : cutBevelR;
      if (
        wx - c.maxX > cBevel + 0.12 || c.minX - wx > cBevel + 0.12 ||
        wy - c.maxY > cBevel + 0.12 || c.minY - wy > cBevel + 0.12 ||
        wz - c.maxZ > cBevel + 0.12 || c.minZ - wz > cBevel + 0.12
      ) {
        continue;
      }
      const dCut = evalCompiledBlock(c, wx, wy, wz);
      dBedrock = smax(dBedrock, -dCut, cBevel);
    }

    let dMacro = dTalus < 1e8 ? smin(dBedrock, dTalus, talusBlendR) : dBedrock;
    let bestBlock = dTalus < dBedrock ? bestTalusBlock : bestBedrockBlock;
    let bestSecPlane = bestBedrockSecondPlane;

    if (crackNet && crackNet.evalMacroCracks && dBedrock < S * 0.06) {
      const crackedBed = crackNet.evalMacroCracks(wx, wy, wz, dBedrock);
      dMacro = dTalus < 1e8 ? smin(crackedBed, dTalus, talusBlendR) : crackedBed;
      dBedrock = crackedBed;
    }

    if (dBoulder < dMacro) {
      dMacro = dBoulder;
      bestBlock = bestBoulderBlock;
      bestSecPlane = bestBoulderSecondPlane;
    }

    if (groundY !== null && groundY !== undefined) {
      const dGround = groundY - wy;
      if (dGround > dMacro) dMacro = dGround;
    }

    if (outState) {
      outState.dMacro = dMacro;
      outState.bestBlock = bestBlock;
      outState.secondPlane = bestSecPlane;
      outState.dBedrock = dBedrock;
      outState.dSecondBedrock = dSecondBedrock;
      outState.dBoulder = dBoulder;
      outState.dSecondBoulder = dSecondBoulder;
    }
    return dMacro;
  }

  function evalMacro(wx, wy, wz, ctx) {
    return evalMacroWithWinner(wx, wy, wz, ctx || fullCtx, null);
  }

  const _state = {
    dMacro: 0,
    bestBlock: null,
    secondPlane: -1e9,
    dBedrock: 0,
    dSecondBedrock: 0,
    dBoulder: 0,
    dSecondBoulder: 0,
  };

  function evalFine(wx, wy, wz, ctx) {
    const dMacro = evalMacroWithWinner(wx, wy, wz, ctx || fullCtx, _state);

    if (Math.abs(dMacro) > detailMargin * 1.35) return dMacro;
    if (groundY !== null && wy <= groundY + 1e-4) return Math.max(dMacro, groundY - wy);

    const blk = _state.bestBlock;
    if (!blk) return dMacro;

    const warp1 = scalarField3D(wx * warpFreq, wy * warpFreq, wz * warpFreq, warpSalt) * S * 0.016;
    const warp2 = scalarField3D(wx * warpFreq * 2.1, wy * warpFreq * 2.1, wz * warpFreq * 2.1, warpSalt + 73) * S * 0.007;
    const warp = warp1 + warp2;
    const px = wx + blk.fy0 * warp;
    const py = wy + blk.fy1 * warp;
    const pz = wz + blk.fy2 * warp;

    let disp = 0;

    if (blk.isTalus) {
      const rill1 = scalarField3D(px * microFreq * 0.45, py * microFreq * 0.25, pz * microFreq * 0.45, warpSalt + 31);
      const rill2 = scalarField3D(px * microFreq * 1.1, py * microFreq * 0.5, pz * microFreq * 1.1, warpSalt + 67);
      evalPlanarCleavage3D(
        px * mesoFreq * 1.2, py * mesoFreq * 0.9, pz * mesoFreq * 1.2,
        sharpness1 * 0.85, tiltAmp, 0.75, (blk.seedSalt & 511) + 7, _cleav1
      );
      disp += (rill1 * 0.5 + rill2 * 0.3 + _cleav1.facet * 0.5) * mesoAmp * 0.52;
    } else if (isSedimentary || blk.isSedimentary) {
      // STRATIFIED SEDIMENTARY REGIME:
      // Domain-warped natural horizontal/dipping stone laminations + irregular stepped rock fracture facets!
      const hardness = blk.hardness ?? 0.78;
      const dScale = blk.detailScale;
      const bedWarp = scalarField3D(wx * warpFreq * 1.4, wy * warpFreq * 0.7, wz * warpFreq * 1.4, warpSalt + 19) * S * 0.011;
      const bedCoord = (wx * blk.fy0 + wy * blk.fy1 + wz * blk.fy2) + bedWarp;

      const majorBed = -tabularBedProfile(bedCoord / (S * 0.056));
      const minorLam = -tabularBedProfile((bedCoord + S * 0.014) / (S * 0.023));
      const lateralMod = clamp(0.55 + 0.55 * scalarField3D(wx * mesoFreq * 0.42, wy * mesoFreq * 0.55, wz * mesoFreq * 0.42, restSalt), 0.30, 1.15);

      const lamStrength = hardness < 0.45 ? 1.35 : 0.78;
      disp += (majorBed * 0.65 + minorLam * 0.35) * mesoAmp * 0.92 * lamStrength * lateralMod * dScale;

      const ux = (px * blk.fx0 + py * blk.fx1 + pz * blk.fx2) * 0.42;
      const uy = bedCoord * 1.55;
      const uz = (px * blk.fz0 + py * blk.fz1 + pz * blk.fz2) * 0.58;
      evalPlanarCleavage3D(
        ux * mesoFreq * 0.95, uy * mesoFreq * 0.85, uz * mesoFreq * 0.95,
        sharpness1 * 1.05, tiltAmp * 0.70, 0.82, (blk.seedSalt & 511) + 13, _cleav1
      );
      evalPlanarCleavage3D(
        ux * microFreq * 0.85 + 7.1, uy * microFreq * 0.9 + 3.4, uz * microFreq * 0.85 + 5.2,
        sharpness2, tiltAmp * 0.55, 0.80, (blk.seedSalt & 511) + 57, _cleav2
      );
      disp += (_cleav1.facet * 0.68 + _cleav2.facet * 0.26) * mesoAmp * dScale;
    } else if (slotPathAt && !blk.isBoulder) {
      // SLOT-CANYON REGIME:
      // Do not run the generic eolian cross-bed ledge stack at full strength
      // here: on the old fins it produced regular horizontal louvers. Slot
      // walls are dominated by flood-aligned flutes/scallops and helical
      // polish, with cross-bedding only as a subdued differential relief.
      const dScale = blk.detailScale;
      const path = slotPathAt(wz);
      const side = wx >= path.x ? 1 : -1;
      const q = (wx - path.x) / Math.max(0.05, path.halfGap);
      const absQ = Math.abs(q);
      const height = Math.max(1e-6, slotCanyon.height || S * 0.82);
      const yf = clamp((wy - (groundY ?? 0)) / height, 0, 1.25);
      const innerMask = clamp(1.15 - Math.max(0, absQ - 0.72) / 0.85, 0, 1);
      const floorMask = clamp(1 - Math.abs(wy - path.floorY) / (S * 0.075), 0, 1) *
        clamp(1.25 - absQ / 1.45, 0, 1);

      // Longitudinal cuspate flutes: slow variation downstream, finer
      // variation with height/side. A second field adds scallop packets.
      const fluteA = scalarField3D(
        wz / (S * 0.175),
        wy / (S * 0.052),
        side * 2.7 + (slotCanyon.phase || 0),
        warpSalt + 701
      );
      const fluteB = scalarField3D(
        wz / (S * 0.068) + 4.2,
        wy / (S * 0.150) + 1.7,
        side * 1.9,
        warpSalt + 733
      );
      const cusp = 0.58 - Math.pow(Math.abs(fluteA), 0.70);
      const helical = Math.sin((wz / Math.max(1e-6, slotCanyon.length || S)) * Math.PI * 5.0 + yf * Math.PI * 2.15 + side * 0.85 + (slotCanyon.phase || 0));
      const innerDisp = (cusp * 0.62 + fluteB * 0.20 + helical * 0.09) * mesoAmp * 1.55 * dScale;

      // Subtle cross-bed relief: a single side-consistent, domain-warped
      // foreset field. Switching dune sets at regular bed heights (as the
      // generic desert regime does) stamped a train of horizontal grooves.
      const slotDune = duneSets[side > 0 ? 1 : 5];
      const bedPhaseWarp = scalarField3D(wx / (S * 0.24), wy / (S * 0.18), wz / (S * 0.24), warpSalt + 797) * 0.38;
      const crossBed = smoothForesetLedge(
        (px * slotDune.nx + py * slotDune.ny + pz * slotDune.nz) * slotDune.freq + bedPhaseWarp
      ) * mesoAmp * 0.085 * dScale;

      // Outer fins: broad world-space slickrock undulation. This must NOT
      // use the winning block's seed: the fin is assembled from hundreds of
      // tier/segment blocks, and per-block cleavage resets at every seam,
      // making the flank look like a waffle of unrelated tiles.
      const outerFieldA = scalarField3D(wx / (S * 0.230), wy / (S * 0.135), wz / (S * 0.230), warpSalt + 811);
      const outerFieldB = scalarField3D(wx / (S * 0.072) + 3.7, wy / (S * 0.165) + 1.9, wz / (S * 0.072) + 6.2, warpSalt + 827);
      const outerDisp = (outerFieldA * 0.38 + outerFieldB * 0.13) * mesoAmp * dScale;

      disp += outerDisp * (1 - innerMask) + innerDisp * innerMask + crossBed * (0.35 + 0.65 * (1 - innerMask));

      // Bed scallops / incipient potholes. Positive displacement carves the
      // floor; the flood process subsequently deepens only where its flow,
      // tools and eddies actually concentrate.
      if (floorMask > 0.001) {
        const bedField = scalarField3D(wx / (S * 0.082), 0.35, wz / (S * 0.082), warpSalt + 761);
        const pothole = Math.max(0, bedField - 0.18) * 0.85;
        const bedScallop = scalarField3D(wx / (S * 0.150), 1.1, wz / (S * 0.045), warpSalt + 787) * 0.18;
        disp += (pothole + bedScallop) * mesoAmp * 1.35 * floorMask * dScale;
      }
    } else if (isDesert || blk.isEolian) {
      // UTAH SANDSTONE REGIME:
      // Multi-set cross-bedding ledges + stepped fracture plates + micro-rock scarplets!
      const dScale = blk.detailScale;
      const bedCoord = (px * blk.fy0 + py * blk.fy1 + pz * blk.fy2) + warp * 0.85;
      const setSpace = bedCoord * boundingSetFreq;
      const setIdx = Math.floor(setSpace);
      const setFrac = setSpace - setIdx;

      const ds0 = duneSets[((setIdx % 8) + 8) % 8];
      const ds1 = duneSets[(((setIdx + 1) % 8) + 8) % 8];
      const blendSet = smoothstep(0.80, 1.0, setFrac);

      const f0 = smoothForesetLedge((px * ds0.nx + py * ds0.ny + pz * ds0.nz) * ds0.freq) * ds0.strength;
      const f1 = smoothForesetLedge((px * ds1.nx + py * ds1.ny + pz * ds1.nz) * ds1.freq) * ds1.strength;
      const crossBedRelief = f0 + (f1 - f0) * blendSet;

      const nd = (setFrac - 0.90) * 11.0;
      const boundNotch = Math.exp(-nd * nd) * 0.88;
      const bedStep = (setIdx & 1) ? 0.28 * smoothstep(0.08, 0.28, setFrac) * (1 - smoothstep(0.75, 0.92, setFrac)) : -0.18;

      const etchMask = clamp(scalarField3D(wx * mesoFreq * 0.42, wy * mesoFreq * 0.42, wz * mesoFreq * 0.42, restSalt) * 1.15 + 0.55, 0.26, 1.08);
      disp += (crossBedRelief * 0.48 + boundNotch * 0.72 + bedStep * 0.56) * mesoAmp * 1.15 * dScale * etchMask;

      const ux = (px * blk.fx0 + py * blk.fx1 + pz * blk.fx2) * 0.48;
      const uy = bedCoord * 1.35;
      const uz = (px * blk.fz0 + py * blk.fz1 + pz * blk.fz2) * 0.54;

      evalPlanarCleavage3D(
        ux * mesoFreq * 0.95, uy * mesoFreq * 0.82, uz * mesoFreq * 0.95,
        sharpness1 * 0.96, tiltAmp * 0.85, 0.80, (blk.seedSalt & 511) + 19, _cleav1
      );
      evalPlanarCleavage3D(
        ux * microFreq * 0.82 + 4.3, uy * microFreq * 0.78 + 8.1, uz * microFreq * 0.82 + 2.7,
        sharpness2 * 0.95, tiltAmp * 0.65, 0.82, (blk.seedSalt & 511) + 63, _cleav2
      );
      disp += (_cleav1.facet * 0.74 + _cleav2.facet * 0.28) * mesoAmp * dScale;

      // Subtle rock-block joint etching where curve-aligned arch slabs meet!
      if (!blk.isBoulder && !blk.noSeamNotch && _state.dSecondBedrock < 1e8) {
        const seamGap = Math.abs(_state.dBedrock - _state.dSecondBedrock);
        const seamAllowance = S * 0.015 * (0.35 + aperture);
        if (seamGap < seamAllowance) {
          const s = 1 - seamGap / seamAllowance;
          disp += jointNotchDepth * 0.55 * s * s;
        }
      }
    } else if (isKarst) {
      // LIMESTONE KARST DISSOLUTION REGIME:
      // Domain-warped APERIODIC vertical rainwater rillenkarren flutes + razor karst cleavage!
      // (ZERO Cartesian sin(x)+sin(z) waffle grids and ZERO regular horizontal rings!)
      const dScale = blk.detailScale;
      const wX = scalarField3D(wx * mesoFreq * 0.85, wy * mesoFreq * 0.14, wz * mesoFreq * 0.85, warpSalt + 41) * S * 0.028;
      const wZ = scalarField3D(wx * mesoFreq * 0.85 + 9.4, wy * mesoFreq * 0.14 + 3.1, wz * mesoFreq * 0.85 + 5.7, warpSalt + 83) * S * 0.028;

      const kx = (wx + wX) * microFreq * 1.15;
      const kz = (wz + wZ) * microFreq * 1.15;
      const ky = wy * mesoFreq * 0.16; // Stretched 7x vertically so solution runnels stream straight down!

      // Cuspate vertical dissolution ridges: |noise|^0.65 creates sharp razor crests between rounded runnels!
      const n1 = scalarField3D(kx, ky, kz, restSalt + 11);
      const n2 = scalarField3D(kx * 2.05 + 4.7, ky * 1.3 + 1.9, kz * 2.05 + 8.3, restSalt + 47);
      const cusp1 = 0.62 - Math.pow(Math.abs(n1), 0.65);
      const cusp2 = 0.58 - Math.pow(Math.abs(n2), 0.70);

      // Elongated vertical karst fracture fins
      evalPlanarCleavage3D(
        (wx + wX * 0.5) * mesoFreq * 1.05,
        wy * mesoFreq * 0.28,
        (wz + wZ * 0.5) * mesoFreq * 1.05,
        sharpness1 * 1.08, tiltAmp * 0.75, 0.82, (blk.seedSalt & 511) + 29, _cleav1
      );

      // Irregular solution kame/pocket hollows on upper surfaces
      const pitField = scalarField3D(wx * microFreq * 0.75, wy * microFreq * 0.75, wz * microFreq * 0.75, restSalt + 109);
      const solutionPit = Math.max(0, pitField - 0.32) * 0.95;

      disp += (cusp1 * 0.95 + cusp2 * 0.48 + _cleav1.facet * 0.62 + solutionPit * 0.42) * mesoAmp * 1.42 * dScale;
    } else {
      // CRYSTALLINE / JOINTED / FOLIATED REGIME (Alpine Crags, Hero Boulders, Schist Hogbacks, Sea Arches):
      // Full-face 3-tier structural rock cleavage so faces NEVER look like smooth low-poly triangles!
      const dScale = blk.detailScale;
      const phase = (blk.seedSalt & 15) * 1.73;

      const ux = (px * blk.fx0 + py * blk.fx1 + pz * blk.fx2) * aniso[0] * 0.46;
      const uy = (px * blk.fy0 + py * blk.fy1 + pz * blk.fy2) * aniso[1] * 1.25 + phase;
      const uz = (px * blk.fz0 + py * blk.fz1 + pz * blk.fz2) * aniso[2] * 0.72;

      const arrisProx = clamp(1 + _state.secondPlane / edgeZoneWidth, 0, 1);
      const stressZone = clamp(scalarField3D(wx * mesoFreq * 0.48, wy * mesoFreq * 0.48, wz * mesoFreq * 0.48, restSalt) * 1.25 + 0.45, 0, 1);
      // High base floor (0.56) ensures broad boulder & alpine faces always carry crisp stepped rock facets!
      const detailEnvelope = clamp(0.56 + 0.32 * stressZone + 0.28 * arrisProx, 0.52, 1.15);

      if (mesoAmp > 1e-5) {
        // Tier 1: Broad structural bench / ledge steps
        evalPlanarCleavage3D(
          ux * macroCleavFreq + 2.3, uy * macroCleavFreq + 1.1, uz * macroCleavFreq + 4.7,
          sharpness1 * 0.95, tiltAmp * 1.15, 0.78, (blk.seedSalt & 511) + 3, _cleav3
        );
        // Tier 2: Crisp decimeter cleavage scarplets
        evalPlanarCleavage3D(
          ux * mesoFreq, uy * mesoFreq, uz * mesoFreq,
          sharpness1, tiltAmp, 0.80, (blk.seedSalt & 511) + 17, _cleav1
        );
        disp += (_cleav3.facet * 0.58 + _cleav1.facet * 0.78) * mesoAmp * dScale * detailEnvelope;
      }

      if (microAmp > 1e-5) {
        // Tier 3: Fine fracture steps & rough mineral cleavage
        evalPlanarCleavage3D(
          ux * microFreq + 11.3, uy * microFreq + 5.7, uz * microFreq + 3.1,
          sharpness2, tiltAmp * 0.75, 0.82, (blk.seedSalt & 511) + 91, _cleav2
        );
        disp += _cleav2.facet * microAmp * dScale * (0.55 + 0.50 * detailEnvelope);
      }

      if (!blk.isBoulder && !blk.noSeamNotch && _state.dSecondBedrock < 1e8) {
        const seamGap = Math.abs(_state.dBedrock - _state.dSecondBedrock);
        const seamAllowance = S * 0.018 * (0.35 + aperture);
        if (seamGap < seamAllowance) {
          const s = 1 - seamGap / seamAllowance;
          disp += jointNotchDepth * 0.72 * s * s;
        }
      }
    }

    // Boulder-to-boulder contact crevice along boulder-built arches (`desert-arch`, `double-arch`, `sea-arch`)
    if (isArch && blk.isBoulder && _state.dSecondBoulder < 1e8) {
      const boulderSeam = Math.abs(_state.dBoulder - _state.dSecondBoulder);
      const bAllow = S * 0.016 * (0.40 + aperture);
      if (boulderSeam < bAllow) {
        const bs = 1 - boulderSeam / bAllow;
        disp += jointNotchDepth * 0.88 * bs * bs;
      }
    }

    // Vertical organ-pipe rainwater fluting + multi-order Cutler/Claron 360-deg horizontal strata on Spires & Hoodoos!
    if (isSpireOrHoodoo && !blk.isBoulder && !blk.isTalus) {
      const fWx = scalarField3D(wx * mesoFreq * 0.85, wy * mesoFreq * 0.16, wz * mesoFreq * 0.85, warpSalt + 53) * S * 0.020;
      const fWz = scalarField3D(wx * mesoFreq * 0.85 + 6.1, wy * mesoFreq * 0.16 + 2.4, wz * mesoFreq * 0.85 + 3.9, warpSalt + 97) * S * 0.020;
      const flute1 = scalarField3D((wx + fWx) * microFreq * 0.98, wy * mesoFreq * 0.14, (wz + fWz) * microFreq * 0.98, restSalt + 23);
      const flute2 = scalarField3D((wx + fWx) * microFreq * 1.90 + 3.7, wy * mesoFreq * 0.22 + 1.3, (wz + fWz) * microFreq * 1.90 + 5.1, restSalt + 71);
      const organFlute = (0.58 - Math.pow(Math.abs(flute1), 0.66)) * 0.78 + (0.55 - Math.pow(Math.abs(flute2), 0.70)) * 0.36;
      const majorShelf = -tabularBedProfile((wy + warp * 0.38) / (S * 0.058));
      const minorShelf = -tabularBedProfile((wy + warp * 0.38 + S * 0.011) / (S * 0.024));
      disp += (organFlute * 0.80 + majorShelf * 0.68 + minorShelf * 0.36) * mesoAmp * 1.05 * blk.detailScale;
    }

    let dFinal = dMacro + disp;
    if (crackNet && crackNet.evalFineCracks) {
      dFinal += crackNet.evalFineCracks(wx, wy, wz);
    }

    if (groundY !== null && groundY !== undefined) {
      const dGround = groundY - wy;
      if (dGround > dFinal) dFinal = dGround;
    }
    return dFinal;
  }

  function evalFull(wx, wy, wz) {
    return evalFine(wx, wy, wz, fullCtx);
  }

  // Optional 3D Stratigraphic / Fracture Per-Vertex Color Evaluator (calibrated to true PBR rock linear-RGB!)
  let evalRockColor = null;

  if (archetype === 'sedimentary-cliff') {
    // Note: `buildSedimentaryCliff` scales `dipAngle * 0.38` and `height = S * 0.72` — match 1:1 so color beds lock to 3D strata!
    const strikeRad = (params.strikeAngle ?? 8) * DEG2RAD;
    const dipRad = clamp((params.dipAngle ?? 4), 0, 18) * 0.38 * DEG2RAD;
    const stratFrame = buildStrikeDipFrame(strikeRad, dipRad);
    const fy0 = stratFrame.normal[0], fy1 = stratFrame.normal[1], fy2 = stratFrame.normal[2];
    const cliffH = S * 0.72;

    evalRockColor = (wx, wy, wz, nx, ny, nz, ao, cav, outRgb) => {
      const bedWarp = scalarField3D(wx * warpFreq * 1.4, wy * warpFreq * 0.7, wz * warpFreq * 1.4, warpSalt + 19) * S * 0.008;
      const hStrat = (wx * fy0 + (wy - (groundY ?? 0)) * fy1 + wz * fy2) + bedWarp;
      const u = clamp(hStrat / cliffH, 0.0, 1.15);

      const mGrain = scalarField3D(wx * microFreq * 0.65, wy * microFreq * 1.1, wz * microFreq * 0.65, restSalt + 311);
      const mPatch = scalarField3D(wx * mesoFreq * 0.55, wy * mesoFreq * 0.55, wz * mesoFreq * 0.55, restSalt + 419);
      const fineBed = Math.sin(u * 105.0 + mPatch * 0.9);

      let sR, sG, sB;
      if (u < 0.105) {
        // 1. Toe bench and basal notch: muted grey-brown bedrock rather than
        // a saturated orange apron that visually melts away from the wall.
        const darkSeam = smoothstep(0.30, 0.88, fineBed);
        sR = (0.45 * (1 - darkSeam) + 0.30 * darkSeam) + 0.022 * mGrain;
        sG = (0.38 * (1 - darkSeam) + 0.28 * darkSeam) + 0.018 * mGrain;
        sB = (0.30 * (1 - darkSeam) + 0.25 * darkSeam) + 0.016 * mGrain;
      } else if (u < 0.29) {
        // 2. Lower blocky cliff & right seaward spit: interbedded coastal ochre-amber & weathered grey-taupe sandstone!
        const ochreBed = smoothstep(-0.18, 0.28, Math.sin(u * 76.0 + 0.4));
        sR = (0.49 * (1 - ochreBed) + 0.50 * ochreBed) + 0.020 * mGrain;
        sG = (0.46 * (1 - ochreBed) + 0.39 * ochreBed) + 0.018 * mGrain;
        sB = (0.41 * (1 - ochreBed) + 0.28 * ochreBed) + 0.016 * mGrain;
      } else if (u < 0.46) {
        // 3. Lower-middle vertically-jointed calcareous grey-buff beds + thin rusty ironstone seams (`images (1).jpg`!)
        const ironSeam = Math.pow(clamp(Math.sin(u * 62.0 + 0.6), 0, 1), 6.0);
        const shadeAlt = 0.5 + 0.5 * Math.sin(u * 52.0);
        const gR = 0.46 + 0.065 * shadeAlt;
        const gG = 0.44 + 0.060 * shadeAlt;
        const gB = 0.39 + 0.055 * shadeAlt;
        sR = gR * (1 - ironSeam) + 0.49 * ironSeam + 0.020 * mGrain;
        sG = gG * (1 - ironSeam) + 0.37 * ironSeam + 0.018 * mGrain;
        sB = gB * (1 - ironSeam) + 0.26 * ironSeam + 0.016 * mGrain;
      } else if (u < 0.69) {
        // 4. Mid-upper laminated dusty maroon-brown siltstone. Keep it earthy
        // rather than candy pink: lower blue, warmer red/green balance.
        const buffRib = Math.pow(clamp(Math.sin(u * 88.0 + 1.2), 0, 1), 4.5) * 0.50;
        const deepPlum = 0.5 + 0.5 * fineBed;
        const pR = 0.40 + 0.045 * deepPlum;
        const pG = 0.285 + 0.035 * deepPlum;
        const pB = 0.225 + 0.030 * deepPlum;
        sR = pR * (1 - buffRib) + 0.48 * buffRib + 0.020 * mGrain;
        sG = pG * (1 - buffRib) + 0.43 * buffRib + 0.018 * mGrain;
        sB = pB * (1 - buffRib) + 0.34 * buffRib + 0.018 * mGrain;
      } else {
        // 5. Upper weathered coastal tan-grey bluff caprock & russet-stained craggy outcrop blocks
        const russet = clamp(mPatch * 0.85 + 0.28, 0, 1) * clamp(ny * 0.55 + 0.45, 0, 1);
        sR = 0.49 * (1 - russet * 0.45) + 0.48 * (russet * 0.45) + 0.024 * mGrain;
        sG = 0.46 * (1 - russet * 0.45) + 0.34 * (russet * 0.45) + 0.022 * mGrain;
        sB = 0.41 * (1 - russet * 0.45) + 0.25 * (russet * 0.45) + 0.020 * mGrain;
      }

      // Subtle iron-ochre oxidation on vertical cross-joint fracture walls + dark crevice staining
      const jointStain = clamp(mPatch * 0.85 + 0.15, 0, 1) * clamp(1.0 - Math.abs(ny), 0, 1) * 0.18;
      sR = sR * (1 - jointStain) + 0.52 * jointStain;
      sG = sG * (1 - jointStain) + 0.35 * jointStain;
      sB = sB * (1 - jointStain) + 0.21 * jointStain;

      const creviceDark = 1.0 - 0.36 * clamp((1.0 - ao) * 0.85 + cav * 0.55 - 0.20, 0, 1);
      outRgb[0] = Math.pow(clamp(sR * creviceDark, 0.03, 0.85), 2.2);
      outRgb[1] = Math.pow(clamp(sG * creviceDark, 0.03, 0.85), 2.2);
      outRgb[2] = Math.pow(clamp(sB * creviceDark, 0.03, 0.85), 2.2);
    };
  } else if (archetype === 'hoodoo-columns') {
    // Bryce Canyon Claron Formation: calibrated PBR coral-terracotta, salmon-rose, and warm limestone-cream!
    evalRockColor = (wx, wy, wz, nx, ny, nz, ao, cav, outRgb) => {
      const bedWarp = scalarField3D(wx * warpFreq * 1.2, wy * warpFreq * 0.6, wz * warpFreq * 1.2, warpSalt + 29) * S * 0.007;
      const u = ((wy - (groundY ?? 0)) + bedWarp) / (S * 0.88);
      const mGrain = scalarField3D(wx * microFreq * 0.7, wy * microFreq, wz * microFreq * 0.7, restSalt + 521) * 0.018;

      const creamBand = Math.pow(clamp(Math.sin(u * 26.0 + 0.8), 0, 1), 3.6);
      const terraBand = smoothstep(-0.25, 0.45, Math.sin(u * 15.0 - 0.5));

      let sR = 0.64 * (1 - terraBand * 0.35) + 0.55 * (terraBand * 0.35);
      let sG = 0.38 * (1 - terraBand * 0.40) + 0.28 * (terraBand * 0.40);
      let sB = 0.27 * (1 - terraBand * 0.40) + 0.20 * (terraBand * 0.40);

      // Calibrated Claron calcareous cream-limestone marker beds (~#998b7a, never blown-out snow white!)
      sR = sR * (1 - creamBand * 0.68) + 0.60 * (creamBand * 0.68) + mGrain;
      sG = sG * (1 - creamBand * 0.68) + 0.54 * (creamBand * 0.68) + mGrain;
      sB = sB * (1 - creamBand * 0.68) + 0.47 * (creamBand * 0.68) + mGrain;

      const creviceDark = 1.0 - 0.28 * clamp((1.0 - ao) * 0.75 + cav * 0.45 - 0.22, 0, 1);
      outRgb[0] = Math.pow(clamp(sR * creviceDark, 0.03, 0.85), 2.2);
      outRgb[1] = Math.pow(clamp(sG * creviceDark, 0.03, 0.85), 2.2);
      outRgb[2] = Math.pow(clamp(sB * creviceDark, 0.03, 0.85), 2.2);
    };
  } else if (crackNet) {
    evalRockColor = (wx, wy, wz, nx, ny, nz, ao, cav, outRgb) => {
      evalCrackedMonolithColor(crackNet, wx, wy, wz, nx, ny, nz, ao, cav, outRgb);
    };
  }

  /**
   * Rock hardness in [0, 1] of the macro block/curve dominating this point
   * (talus is loose and soft). Used by particle rain erosion so hard caprock
   * resists and soft beds/talus rill first. Falls back to mid-hard rock.
   */
  function evalHardness(wx, wy, wz) {
    evalMacroWithWinner(wx, wy, wz, fullCtx, _state);
    const blk = _state.bestBlock;
    if (!blk) return 0.62;
    if (blk.isTalus) return 0.28;
    return blk.hardness ?? 0.62;
  }

  return {
    detailMargin,
    buildBrickContext,
    evalMacro,
    evalFine,
    evalFull,
    evalRockColor,
    evalHardness,
  };
}
