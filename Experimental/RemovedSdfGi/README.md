# 🧩 `Experimental/RemovedSdfGi/` — SDF / GI Experiment Record

> **TL;DR**: the SDF shadow, SDF GI, and emissive-GI experiments are **not lost**. They were removed from
> `Engine/` by commit `63db120` ("Remove GI and SDF subsystems and unify direct OpenPBR/MatCap world rendering",
> 2026-11-20 00:29:46 UTC) — not by any later commit. This directory keeps a permanent, self-contained copy of
> every removed source file, its generated proof images, and a one-command restore script.

---

## 1. What was removed, and by which commit

| Question | Answer |
| :--- | :--- |
| Who removed `Exhibits/CleanShadowAndGiCandidates.h` and `Exhibits/CleanEmissiveShadowValidation.cpp`? | Commit **`63db120`** — "Remove GI and SDF subsystems and unify direct OpenPBR/MatCap world rendering" |
| When? | **2026-11-20 00:29:46 UTC** — about 4 h 40 min *before* this session's commits |
| Did this session remove them? | **No.** The session commits (`3ca5ab0` proof snapshots, `daed305` Fluid/Particle/ForceField/VectorField retirement) delete **zero** files matching `Sdf`, `DistanceField`, `GlobalIllumination`, `CleanEmissive*`, `CleanShadow*`, or `VoxelSdf` |
| Was the removal limited to `Engine/`? | **No.** `63db120` deleted the exhibits, their candidate headers, the evidence PNGs, and the path-tracer exhibit alongside the engine subsystems |
| Is the work recoverable? | **Yes, byte-exact** — the objects are still in the repository history and are copied here |

Deletion scope of `63db120` (114 paths, summarised):

- `Engine/DistanceFieldSpace/` — `SignedDistanceVolume.{h,cpp}`, `VoxelSdfClipmap.{h,cpp}`, `ToroidalDistanceClipmap.h`, `SobolDirections.h`, `README.md`
- `Engine/GeometricRaster/GlobalIllumination/` — `DeterministicIntegrators.h`, `DynamicIrradianceCacheSequence.{h,cpp}`, `IrradianceLatticeSequence.{h,cpp}`, `WeightedPathReplaySequence.{h,cpp}`
- `Engine/Shaders/` — `GlobalIllumination/{DistanceEvaluation,VoxelSdfClipmapCompute,DynamicIrradianceCache,WeightedPathReplay,WeightedTemporalFilter}.slang`, `GeometricRaster/SignedDistanceGridCompute.slang`, `DirectLighting/ContactClipShadow.slang`
- `Exhibits/` — `CleanEmissiveGiCandidates.h`, `CleanEmissiveGiValidation.cpp`, `CleanShadowAndGiCandidates.h`, `CleanEmissiveShadowValidation.cpp`, `VoxelSdfClipmapGiValidation.cpp`, and the whole `Exhibits/DeterministicPathTracer/` report
- `Exhibits/Images/CleanEmissiveGi/`, `Exhibits/Images/CleanEmissiveShadows/` — the rendered proof sheets

---

## 2. What this directory contains

| Path | Contents |
| :--- | :--- |
| `Source/Exhibits/*` | The five removed exhibit files, byte-exact from `63db120^` |
| `Source/Engine/DistanceFieldSpace/*` | The seven removed SDF engine files |
| `Source/Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h` | Deterministic quadrature / sampling integrators |
| `Source/Engine/Shaders/*` | The three removed SDF/GI `.slang` units |
| `Source/Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.{h,cpp}` + `ParticleBuffers.h` | The FCT1 vehicle loader that every SDF/GI exhibit depends on for its `ControlVehicle` triangle set (paths preserved so the source still compiles when restored) |
| `Source/BakingSdf/*` | Archived Baking Studio 3D Volume SDF baker (`GpuSdfBaker.{h,cpp}`, `BakeVolumeSdfCompute.slang`, `ExtractSdfSectionCompute.slang`, `10-BakingWorkspaceSplitSdf.png`) retired during Phase 0 of the World Rendering Redo (`WorldRenderingRedoPlan.md`) |
| `Source/Exhibits/DeterministicPathTracer/` | The removed path-tracer report (`README.md`, `Code/`, `Results/`, `Images/`) |
| `Evidence/CleanEmissiveGi/*` | 10 proof PNGs + `validation_summary.json` |
| `Evidence/CleanEmissiveShadows/*` | 6 proof PNGs + `shadow_validation_summary.json` |
| `Restore.py` | Re-materialises every source file at its original path (never overwrites a differing file without `--force`) |
| `Restore.patch` | The same restoration as a `git apply` patch |

PNG payloads were re-encoded with zlib level-9 DEFLATE (decode-identical) to keep this record at ~8 MB.

---

## 3. Restore

```bash
# Inspect first (default): lists what would be written, changes nothing
python3 Experimental/RemovedSdfGi/Restore.py

# Materialise every removed source + evidence file at its original path
python3 Experimental/RemovedSdfGi/Restore.py --apply

# Or via git
git apply Experimental/RemovedSdfGi/Restore.patch
```

Restoring the source files alone is **not** enough to rebuild the three exhibits — `63db120` also reshaped
the surrounding engine (see §4). `Restore.py --apply` is intended for reference, verification, and as the
starting point for a full re-integration.

---

## 4. Why the exhibits do not compile against the current engine as-is

The removed exhibits depend on engine surfaces that `63db120` changed in the same commit:

| Dependency | State today |
| :--- | :--- |
| `WorldRenderEngine::DdgiRaytracing` / `SdfGlobalIllumination` | **Removed** — the enum now has only `MatCap` and `OpenPbrDirect` |
| `WorldSceneSpecification::{SceneSdf, ClipmapConfig, DynamicSdfObjects, PointLights, LatticeConfig, OptionalFluid}` | **Removed** — the struct keeps `ClusterInstances`, `MaterialRegister`, `BounceSummaries`, `SceneTree`, `PointLights` (now `DirectPointLightSpec`), `Sun`, `AreaLights`, `SkyRadiance`, `Atmosphere` |
| `TieredWorldRenderSequence` GI passes (`RenderIndirectDiffuse`, lattice/cache sequences) | **Removed** — header shrank by 80 lines |
| `Engine/ParticleSpace/` (FCT1 loader host) | **Removed** by this session (`daed305`) — the loader itself is restored here under `Source/` |
| `Engine/Shaders/GlobalIllumination/*.slang` | **Removed** — restored here under `Source/` |
| `ExternalPackages/stb/stb_image_write.h` | Not fetched in a clean checkout — run `python3 Automation/PrepareDependencies.py` |

Every other include the exhibits need is still present (`MathConstants.h`, `TieredWorldRenderSequence.h`,
`AnalyticPrimitives.h`, `BlackbodyEmission.h`, `OpenPbrSurfaceScattering.h`, `AxisCoordinates.h`,
`LinearColors.h`, `WideBoundingTree.h`, `PolygonNetwork.h`).

Re-integration therefore means one of:

1. **Full restore** of the SDF/GI engine surface (`git revert 63db120`, then re-apply this session's Fluid/
   Particle retirement on top), or
2. **Exhibit-only port**: keep the current dual-engine renderer, move `VoxelSdfClipmap` + `DeterministicIntegrators`
   back into `Engine/`, and have the exhibits drive their GI themselves instead of through
   `TieredWorldRenderSequence` engine modes.

Options 1 and 2 both need the `ControlVehicle` FCT1 loader, which is why it is preserved here in full.

---

## 5. The standalone C++ SDF demo still builds and runs

`Experimental/SdfLightingDemoCpp/` was **not** touched by `63db120` or by this session. It is a dependency-free
C++20 sphere tracer (analytical SDF soft shadows, emissive LTC panel, SDF cone GI + AO with bilateral denoise,
multi-bounce SDF reflections, sun/moon/starlight) and it still renders all five engines:

```bash
cd Experimental/SdfLightingDemoCpp
make render     # writes output_engines/01_matcap.png .. 08_path_tracer_night.png
make verify     # fast 480x270 sweep with the same five engines
```

It shares no code with the removed exhibits, so it is the fastest way to keep the SDF lighting experiments alive
while the in-engine GI is out.
