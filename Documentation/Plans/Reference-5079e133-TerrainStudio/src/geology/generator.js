/**
 * Unified Procedural Rock & Cliff Synthesis Pipeline Orchestrator.
 */

import { createRng } from '../core/prng.js';
import { evaluateSparseSdf } from '../core/sparse-sdf.js';
import { convertMeshToSdf } from '../core/mesh-to-sdf.js';
import { extractDualContourMesh } from '../core/dual-contour.js';
import {
  removeFloatingIslands,
  taubinSmoothWeldedMesh,
  decimateMeshQEM,
  buildGameReadyAttributes,
  buildPreviewMeshAttributes,
} from '../core/mesh-post.js';
import {
  resolveParams,
  QUALITY_LEVELS,
  FORMATION_PRESETS,
  normalizeErosionSdfResolution,
} from './presets.js';
import { createTectonicField } from './tectonics.js';
import { buildMacroFormation } from './formations.js';
import { createGeologicalSdfEvaluator } from './surface-detail.js';
import { applySdfErosionSuite } from './sdf-erosion-suite.js';
import { computeChemicalStains } from './chemical-staining.js';

function summarizeSdfProcess(stats) {
  if (!stats) return null;
  const eroded = stats.erodedVolume ?? stats.dissolvedVolume ?? 0;
  return {
    particles: stats.particles ?? stats.droplets ?? 0,
    particlesRequested: stats.particlesRequested ?? stats.droplets ?? 0,
    steps: stats.steps ?? 0,
    erodedM3: Number(eroded.toFixed(3)),
    depositedM3: Number((stats.depositedVolume ?? 0).toFixed(3)),
    elapsedMs: stats.elapsedMs ?? 0,
    ...(stats.directionMode
      ? {
          directionMode: stats.directionMode,
          directionRangeDeg: stats.directionRangeDeg ?? 0,
          directionCycles: stats.directionCycles ?? 0,
          directionSamplesDeg: [...(stats.directionSamplesDeg || [])],
          directionSectorOffsetsDeg: [...(stats.directionSectorOffsetsDeg || [])],
          directionSectorCounts: [...(stats.directionSectorCounts || [])],
          directionSectorShares: [...(stats.directionSectorShares || [])],
        }
      : {}),
    ...(stats.floodParticles !== undefined
      ? {
          floodParticles: stats.floodParticles ?? 0,
          windParticles: stats.windParticles ?? 0,
          abrasionEvents: stats.abrasionEvents ?? 0,
          depositionEvents: stats.depositionEvents ?? 0,
          wallEvents: stats.wallEvents ?? 0,
          floorEvents: stats.floorEvents ?? 0,
          potholeEvents: stats.potholeEvents ?? 0,
        }
      : {}),
  };
}

function buildErosionPreviewMesh(sdf, groundY, anchor, maxTriangles, preserveWorldXZ = false) {
  const dc = extractDualContourMesh(sdf, {
    qefRegularization: 0.0085,
    rootSteps: 3,
  });
  const cleaned = removeFloatingIslands(
    dc.positions,
    dc.indices,
    groundY ?? 0,
    sdf.voxelSize * 3.5
  );
  if (!cleaned.indices.length || !cleaned.positions.length) return null;

  let positions = cleaned.positions;
  let indices = cleaned.indices;
  const triCount = indices.length / 3;
  if (maxTriangles > 0 && triCount > maxTriangles) {
    const decimated = decimateMeshQEM(positions, indices, maxTriangles / triCount);
    positions = decimated.positions;
    indices = decimated.indices;
  }

  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < positions.length; i += 3) {
    const x = positions[i], y = positions[i + 1], z = positions[i + 2];
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }
  if (!Number.isFinite(minX)) return null;

  // Reuse the first frame's placement anchor for every preview, so watching
  // erosion does not look like the asset itself is sliding around.
  // When `preserveWorldXZ` is enabled (interactive 3D spline rigging), keep X/Z pinned to world space!
  const useAnchor = anchor || {
    shiftX: preserveWorldXZ ? 0 : -0.5 * (minX + maxX),
    shiftY: -minY,
    shiftZ: preserveWorldXZ ? 0 : -0.5 * (minZ + maxZ),
  };
  const mesh = buildPreviewMeshAttributes(positions, indices, { uvScale: 0.35 });
  for (let i = 0; i < mesh.positions.length; i += 3) {
    mesh.positions[i] += useAnchor.shiftX;
    mesh.positions[i + 1] += useAnchor.shiftY;
    mesh.positions[i + 2] += useAnchor.shiftZ;
  }

  return {
    mesh,
    dimensions: [
      Number((maxX - minX).toFixed(2)),
      Number((maxY - minY).toFixed(2)),
      Number((maxZ - minZ).toFixed(2)),
    ],
    anchor: useAnchor,
  };
}

export function generateRockFormation(rawParams = {}, onProgress = null, onPreview = null) {
  const t0 = performance.now();
  const params = resolveParams(rawParams);
  const rng = createRng(params.seed);

  if (onProgress) onProgress(0.03, 'Synthesizing tectonic stress & joint sets...');
  const tectonics = createTectonicField(params, rng.fork('tectonics'));

  if (onProgress) onProgress(0.08, 'Sculpting 3D structural rock blocks & curves...');
  const macro = buildMacroFormation(params, tectonics, rng.fork('macro'));

  // Compute tight bounding box around all positive blocks & 3D arch curves
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  const allPositives = macro.curves ? [...macro.blocks, ...macro.curves] : macro.blocks;
  for (let i = 0; i < allPositives.length; i++) {
    const b = allPositives[i];
    if (b.minX < minX) minX = b.minX;
    if (b.minY < minY) minY = b.minY;
    if (b.minZ < minZ) minZ = b.minZ;
    if (b.maxX > maxX) maxX = b.maxX;
    if (b.maxY > maxY) maxY = b.maxY;
    if (b.maxZ > maxZ) maxZ = b.maxZ;
  }

  if (!Number.isFinite(minX)) {
    minX = -2; minY = 0; minZ = -2;
    maxX = 2; maxY = 3; maxZ = 2;
  }

  const qSpec = QUALITY_LEVELS[params.quality] || QUALITY_LEVELS.high;
  const formationPreset = FORMATION_PRESETS[params.archetype] || FORMATION_PRESETS['cliff-wall'];
  const evaluator = createGeologicalSdfEvaluator(macro, params, rng.fork('detail'));
  const pad = evaluator.detailMargin + params.scale * 0.025;
  const bounds = {
    minX: minX - pad,
    minY: (macro.groundY !== null ? macro.groundY : minY) - pad * 0.6,
    minZ: minZ - pad,
    maxX: maxX + pad,
    maxY: maxY + pad,
    maxZ: maxZ + pad,
  };

  const maxSpan = Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY, bounds.maxZ - bounds.minZ);

  // SDF erosion bake controls. The generator API keeps the historical
  // analytic-SDF default for compatibility; Rock & Cliff Studio explicitly
  // selects the mesh source so the exact placement mesh is converted back
  // to an erosion SDF at an independent, user-selected resolution.
  const rainAmount = params.rainErosionAmount ?? 0;
  const windAmount = params.windErosionAmount ?? params.windAmount ?? 0;
  const thermalAmount = params.thermalErosionAmount ?? params.thermalAmount ?? 0;
  const cryoAmount = params.cryoErosionAmount ?? params.cryoAmount ?? 0;
  const chemicalDissolutionAmount =
    params.chemDissolutionAmount ?? params.chemicalDissolutionAmount ?? 0;
  const slotCanyonPath = params.archetype === 'slot-canyon' ? (macro.slotCanyon || null) : null;
  const slotFloodAmountRaw = params.slotFloodAmount ?? params.flashFloodAmount;
  // Raw API callers that only set rain/wind still get the slot's defining
  // through-flow process; Studio and the CLI pass an explicit amount.
  const slotFloodAmount = slotCanyonPath
    ? Math.max(0, Math.min(1, Number(
        slotFloodAmountRaw ?? (0.72 * rainAmount + 0.28 * windAmount)
      ) || 0))
    : 0;
  const basePreviewOnly = Boolean(params.basePreviewOnly);
  const preserveWorldXZ = Boolean(params.preserveSplineWorldSpace);
  const anySdfErosion =
    !basePreviewOnly && (
      rainAmount > 0.001 || windAmount > 0.001 || thermalAmount > 0.001 ||
      cryoAmount > 0.001 || chemicalDissolutionAmount > 0.001 || slotFloodAmount > 0.001
    );
  const requestedErosionResolution = normalizeErosionSdfResolution(
    params.erosionSdfResolution ?? params.sdfErosionResolution ?? 0
  );
  const sdfErosionSource =
    String(params.sdfErosionSource || 'analytic').toLowerCase() === 'mesh' ? 'mesh' : 'analytic';
  const sdfEdgeProtection = params.sdfEdgeProtection ?? params.rainEdgeProtection ?? 0.7;
  const erosionPreviewEnabled = !basePreviewOnly && typeof onPreview === 'function';
  const erosionPreviewFrames = Math.max(
    1,
    Math.min(16, Math.round(params.erosionPreviewFrames ?? params.liveErosionPreviewFrames ?? 6))
  );
  const erosionPreviewMaxTriangles = Math.max(
    0,
    Math.round(params.erosionPreviewMaxTriangles ?? 65000)
  );
  let erosionPreviewAnchor = null;
  let erosionPreviewFramesEmitted = 0;
  let erosionPreviewError = null;

  // In analytic mode, an explicitly requested bake resolution can be the
  // first (and only) SDF evaluation. In mesh mode, the source formation is
  // evaluated at the normal quality, contoured, and then that exact mesh is
  // voxelized at the bake resolution below.
  // In `basePreviewOnly` mode, cap voxel axis resolution (~72-84) so spline edits synthesize interactively!
  const previewCapVoxels = Math.max(48, Math.round(Number(params.previewMaxAxisVoxels) || 80));
  const sourceMaxAxisVoxels = basePreviewOnly
    ? (params.quality === 'draft' ? qSpec.maxAxisVoxels : Math.min(qSpec.maxAxisVoxels, previewCapVoxels))
    : (anySdfErosion && sdfErosionSource === 'analytic' && requestedErosionResolution > 0
        ? requestedErosionResolution
        : qSpec.maxAxisVoxels);
  const sourceVoxelSize = maxSpan / sourceMaxAxisVoxels;

  let sdf = evaluateSparseSdf(bounds, sourceVoxelSize, evaluator, onProgress);
  const sourceGridDims = [sdf.nx, sdf.ny, sdf.nz];
  const sourceGridMaxAxis = Math.max(...sourceGridDims);
  let meshToSdfStats = null;
  let erosionSuiteStats = null;
  let rainStats = null;

  if (anySdfErosion) {
    if (sdfErosionSource === 'mesh') {
      if (onProgress) onProgress(0.655, 'Extracting source mesh for SDF conversion...');
      const sourceDc = extractDualContourMesh(sdf, {
        qefRegularization: 0.014,
        rootSteps: 2,
      }, onProgress
        ? () => onProgress(0.655, 'Extracting source mesh for SDF conversion...')
        : null);
      const sourceClean = removeFloatingIslands(
        sourceDc.positions,
        sourceDc.indices,
        macro.groundY ?? 0,
        sourceVoxelSize * 3.5
      );
      if (sourceClean.indices.length >= 3) {
        if (onProgress) onProgress(0.66, 'Converting placement mesh → erosion SDF...');
        sdf = convertMeshToSdf(sourceClean, {
          maxAxisVoxels: requestedErosionResolution || sourceGridMaxAxis,
          bandVoxels: 4,
          onProgress: onProgress
            ? (f, stage) => onProgress(0.66 + 0.015 * f, `Mesh → SDF: ${stage}`)
            : null,
        });
        meshToSdfStats = sdf.conversionStats;
      }
    }

    const erosionGridMaxAxis = Math.max(sdf.nx, sdf.ny, sdf.nz);
    erosionSuiteStats = applySdfErosionSuite(sdf, {
      rainAmount,
      rainDroplets: params.rainDroplets ?? Math.round(erosionGridMaxAxis * 48),
      slotFloodAmount,
      slotCanyon: slotCanyonPath,
      slotFloodParticles: params.slotFloodParticles ?? params.flashFloodParticles,
      slotWindParticles: params.slotWindParticles,
      slotSedimentSupply: params.slotSedimentSupply ?? params.slotSediment ?? 0.62,
      windAmount,
      windParticles: params.windParticles ?? params.windErosionParticles,
      windAzimuthDeg: params.windAzimuth ?? params.windAzimuthDeg ?? 35,
      windShiftMode: params.windShiftMode ?? params.windDirectionMode ?? 'fixed',
      windShiftRange: params.windShiftRange ?? params.windShiftRangeDeg ?? 55,
      windShiftCycles: params.windShiftCycles ?? 1,
      windShiftSectors: params.windShiftSectors ?? 5,
      windTurbulence: params.windTurbulence ?? 0.45,
      windStreamlining: params.windStreamlining ?? 0.65,
      windSandSupply: params.windSandSupply ?? 0.65,
      windFluting: params.windFluting ?? 0.65,
      windCurtainHeight: params.windCurtainHeight ?? 0.42,
      windSpeed: params.windSpeed ?? 0.62,
      thermalAmount,
      thermalParticles: params.thermalParticles ?? params.thermalErosionParticles,
      thermalReposeAngleDeg: params.thermalReposeAngle ?? params.thermalReposeAngleDeg ?? 34,
      cryoAmount,
      cryoParticles: params.cryoParticles ?? params.cryoErosionParticles,
      chemicalDissolutionAmount,
      chemicalDroplets: params.chemDissolutionDroplets ?? params.chemicalDroplets,
      edgeProtection: sdfEdgeProtection,
      hardnessAt: evaluator.evalHardness || null,
      dipAngle: params.dipAngle ?? 14,
      strikeAngle: params.strikeAngle ?? 22,
      jointDensity: params.jointDensity ?? 0.55,
      cleavageSharpness: params.cleavageSharpness ?? 0.68,
      groundY: macro.groundY ?? -Infinity,
      rng,
      seedSalt: params.seed,
      onProgress: onProgress
        ? (f, stage) => onProgress(0.68 + 0.08 * f, stage)
        : null,
      previewFrames: erosionPreviewFrames,
      onPreview: erosionPreviewEnabled
        ? (info) => {
            try {
              const built = buildErosionPreviewMesh(
                sdf,
                macro.groundY ?? 0,
                erosionPreviewAnchor,
                erosionPreviewMaxTriangles,
                preserveWorldXZ
              );
              if (!built) return;
              erosionPreviewAnchor = built.anchor;
              erosionPreviewFramesEmitted++;
              onPreview({
                ...info,
                mesh: built.mesh,
                dimensions: built.dimensions,
                lithology: formationPreset.lithology,
                triangles: built.mesh.indices.length / 3,
              });
            } catch (err) {
              // A preview is observational only: never let contouring a
              // snapshot destroy an otherwise valid final erosion bake.
              erosionPreviewError = err?.message || String(err);
            }
          }
        : null,
    });
    rainStats = erosionSuiteStats.rain;
  }

  const dc = extractDualContourMesh(sdf, {
    qefRegularization: anySdfErosion ? 0.0085 : 0.012,
    rootSteps: anySdfErosion ? 3 : 2,
  }, onProgress
    ? (f, stage) => {
        const t = Math.min(1, Math.max(0, (f - 0.66) / 0.14));
        onProgress((anySdfErosion ? 0.76 : 0.66) + 0.08 * t, stage);
      }
    : null);

  if (onProgress) onProgress(0.84, 'Filtering islands & relaxing surface...');
  const cleaned = removeFloatingIslands(
    dc.positions,
    dc.indices,
    macro.groundY ?? 0,
    sdf.voxelSize * 3.5
  );

  const preset = FORMATION_PRESETS[params.archetype] || FORMATION_PRESETS['cliff-wall'];
  const surfacePositions = taubinSmoothWeldedMesh(
    cleaned.positions,
    cleaned.indices,
    basePreviewOnly ? 1 : (anySdfErosion ? 1 : 2),
    macro.groundY ?? 0,
    { creaseAngleDeg: Math.min(34, params.creaseAngle ?? 30) }
  );

  const rawTriCount = cleaned.indices.length / 3;
  const decimated = !basePreviewOnly && params.decimation < 0.98
    ? decimateMeshQEM(surfacePositions, cleaned.indices, params.decimation)
    : { positions: surfacePositions, indices: cleaned.indices };

  if (onProgress) onProgress(0.92, 'Baking volumetric AO, curvature & normals...');
  const finalMesh = buildGameReadyAttributes(
    decimated.positions,
    decimated.indices,
    sdf,
    {
      creaseAngleDeg: params.creaseAngle ?? 68,
      uvScale: 0.35,
      evalRockColor: evaluator.evalRockColor || null,
    }
  );

  // Compute exact final mesh AABB & ground Y=0.
  // When `preserveSplineWorldSpace` is enabled, keep X/Z pinned in world space so 3D splines never drift!
  let fMinX = Infinity, fMinY = Infinity, fMinZ = Infinity;
  let fMaxX = -Infinity, fMaxY = -Infinity, fMaxZ = -Infinity;
  const pos = finalMesh.positions;
  for (let i = 0; i < pos.length; i += 3) {
    const x = pos[i], y = pos[i + 1], z = pos[i + 2];
    if (x < fMinX) fMinX = x; if (x > fMaxX) fMaxX = x;
    if (y < fMinY) fMinY = y; if (y > fMaxY) fMaxY = y;
    if (z < fMinZ) fMinZ = z; if (z > fMaxZ) fMaxZ = z;
  }

  const shiftX = preserveWorldXZ ? 0 : -0.5 * (fMinX + fMaxX);
  const shiftY = Number.isFinite(fMinY) ? -fMinY : 0;
  const shiftZ = preserveWorldXZ ? 0 : -0.5 * (fMinZ + fMaxZ);
  for (let i = 0; i < pos.length; i += 3) {
    pos[i] += shiftX;
    pos[i + 1] += shiftY;
    pos[i + 2] += shiftZ;
  }

  // Chemical weathering stains (opt-in; Rock Studio enables on full bakes, skipped in fast base preview):
  // dissolved Fe/Cu routed down the finished mesh by steepest descent,
  // depositing rust/patina along real drainage lines. Attribute-only --
  // geometry is untouched, so this runs after decimation on the final mesh.
  let chemStats = null;
  const chemAmount = params.chemAmount ?? 0;
  if (!basePreviewOnly && chemAmount > 0.001) {
    if (onProgress) onProgress(0.97, 'Chemical weathering (iron & copper stain flow)...');
    const cs = computeChemicalStains(finalMesh, {
      amount: chemAmount,
      iron: params.ironContent ?? 0.55,
      copper: params.copperContent ?? 0.18,
      seed: params.seed,
    });
    finalMesh.stains = cs.stains;
    chemStats = cs.stats;
  } else {
    finalMesh.stains = null;
  }

  const elapsedMs = Math.round(performance.now() - t0);

  if (onProgress) onProgress(1.0, 'Ready');

  return {
    params,
    lithology: preset.lithology,
    mesh: finalMesh,
    stats: {
      archetypeName: preset.name,
      basePreviewOnly,
      preserveSplineWorldSpace: preserveWorldXZ,
      worldBounds: Number.isFinite(fMinX)
        ? {
            minX: Number((fMinX + shiftX).toFixed(3)),
            maxX: Number((fMaxX + shiftX).toFixed(3)),
            minY: 0,
            maxY: Number((fMaxY - fMinY).toFixed(3)),
            minZ: Number((fMinZ + shiftZ).toFixed(3)),
            maxZ: Number((fMaxZ + shiftZ).toFixed(3)),
          }
        : null,
      blockCount: macro.blocks.length,
      cutCount: macro.cuts.length,
      faultCount: tectonics.faults.length,
      voxelSize: Number(sdf.voxelSize.toFixed(4)),
      sourceVoxelSize: Number(sourceVoxelSize.toFixed(4)),
      sourceGridDims,
      meshToSdf: meshToSdfStats ? { ...meshToSdfStats } : null,
      sdfErosion: erosionSuiteStats
        ? {
            source: sdfErosionSource,
            requestedMaxAxisVoxels:
              requestedErosionResolution || (sdfErosionSource === 'mesh' ? sourceGridMaxAxis : sourceMaxAxisVoxels),
            gridDims: [sdf.nx, sdf.ny, sdf.nz],
            voxelSize: Number(sdf.voxelSize.toFixed(4)),
            chemicalDissolution: summarizeSdfProcess(erosionSuiteStats.chemicalDissolution),
            wind: summarizeSdfProcess(erosionSuiteStats.wind),
            rain: summarizeSdfProcess(erosionSuiteStats.rain),
            slotFlood: summarizeSdfProcess(erosionSuiteStats.slotFlood),
            cryo: summarizeSdfProcess(erosionSuiteStats.cryo),
            thermal: summarizeSdfProcess(erosionSuiteStats.thermal),
            previewFrames: erosionPreviewFramesEmitted,
            previewError: erosionPreviewError,
            elapsedMs: erosionSuiteStats.elapsedMs,
          }
        : null,
      rainErosion: rainStats
        ? {
            droplets: rainStats.droplets,
            steps: rainStats.steps,
            erodedM3: Number(rainStats.erodedVolume.toFixed(3)),
            depositedM3: Number(rainStats.depositedVolume.toFixed(3)),
            elapsedMs: rainStats.elapsedMs,
          }
        : null,
      chemicalStaining: chemStats,
      gridDims: [sdf.nx, sdf.ny, sdf.nz],
      activeBricks: sdf.activeBrickCoords.length / 4,
      rawTriangles: rawTriCount,
      triangles: finalMesh.indices.length / 3,
      vertices: finalMesh.positions.length / 3,
      removedIslands: cleaned.removedIslands,
      dimensions: [
        Number((fMaxX - fMinX).toFixed(2)),
        Number((fMaxY - fMinY).toFixed(2)),
        Number((fMaxZ - fMinZ).toFixed(2)),
      ],
      elapsedMs,
    },
  };
}
