# PLAN — VolumetricFog

🧩 A physically based, animated volumetric fog subsystem for Frontier: a camera-frustum-aligned
froxel (frustum-voxel) 3D field, lit once by compute so the ray-march is independent of light count,
integrated near→far with Beer-Lambert transmittance, self-shadowed for god-rays, and advected by
curl-noise so the fog genuinely *moves* (not a screen-space fake). It couples to an analytic
aerial-perspective atmosphere for the far field. GTX 1060 / Pascal is the baseline.

---

## Provenance (research-backed, adversarially verified 2026-07-26)

This plan is the synthesis of a deep-research pass (23 sources, 25 claims 3-vote verified). The
architecture below is production-proven in Frostbite, Unreal, Flax, Godot, and RDR2 — it is the
industry-consensus real-time approach, not one vendor's trick.

| Foundation | Source |
|---|---|
| Froxel pipeline (lighting accumulated once in a frustum-aligned 3D texture) | Wronski, *Volumetric Fog*, SIGGRAPH 2014 (Assassin's Creed IV) |
| Physically based unification (cascaded extinction volume, real participating media) | Hillaire, *Physically Based & Unified Volumetric Rendering in Frostbite*, SIGGRAPH 2015 |
| Radiative-transfer + phase-function math | Hillaire, *Real-time Volumetric Rendering Course Notes*, Revision 2013 |
| Aerial-perspective atmosphere (precomputed LUTs) | Bruneton & Neyret 2008 (CGF 27(4)) → Hillaire, *A Scalable and Production Ready Sky and Atmosphere*, EGSR 2020 (CGF 39(4)) |
| Voxelize-and-raymarch at shipping scale | Bauer (Rockstar), *Advances in Real-Time Rendering*, SIGGRAPH 2019 (RDR2) |

A single-file WebGL2 demo (`VolumetricFogDemo.html`, repo root) already implements the full physics
(Beer-Lambert, dual-use HG phase, curl-noise advection, sun self-shadow, aerial perspective) as a
fullscreen march. Its tuned constants (density, `g`, curl, height falloff) are the reference look and
should be ported into the froxel constants below — the *math* is identical, only the *where* differs
(per-pixel march → 3D-field compute + composite).

---

## The Math (settled physics — do not re-derive)

```text
extinction        SigmaT   = SigmaA + SigmaS                 // absorption + scattering
transmittance     T(d)     = exp(-SigmaT * d)                // Beer-Lambert
step integration  L_step   = InScatter * (1 - exp(-SigmaT*ds)) / SigmaT   // energy-conserving
accumulate        L       += Transmittance * L_step ; Transmittance *= exp(-SigmaT*ds)
phase (HG)        p(cos,g) = (1 - g*g) / (4*pi * (1 + g*g - 2*g*cos)^1.5)   // g in [-1,+1], forward>0
```

- `g` (anisotropy) is the "Scattering Distribution" dial: 0 isotropic, →1 forward-scatter toward the
  sun (the god-ray glow). Two HG lobes summed give a truer fog response.
- Compositing is **premultiplied-alpha**, front-to-back, early-out when `Transmittance` drops below a
  threshold (~0.02).

---

## Target Pipeline (four compute passes + composite)

```text
 1 Inject density/extinction  ->  2 Inject in-scatter (per light)  ->  3 Integrate near->far  ->  4 Composite
 (curl-noise advected field)     (sun self-shadow -> god-rays)        (running transmittance)     (over scene by depth)
        write RGBA froxel               accumulate per froxel             write scattering+T         sample at pixel depth
```

- **Froxel field** — a frustum-aligned 3D texture; cells small near camera, growing with distance via
  an exponential Z distribution. Suggested start resolution **160 × 90 × 64** (tune on the 1060).
- **Decoupling caveat (verified):** the light-count independence applies **only to pass 3** (the single
  integration march). Pass 2 injection still scales with light count; shadowed point/spot lights
  cost ~3× (Epic). Sun is the cheap common case.
- **Temporal reprojection is mandatory, not optional** — the field is low-res and aliases badly.
  Reproject the previous sequence's field with per-sequence sub-voxel jitter, blend ~7% history.
  Known artifact: fast-moving lights leave lighting trails.

---

## Movement (advection — the "it actually moves" requirement)

Density is evaluated in pass 1, so animation lives there:

- **Curl-noise** of an FBM potential → divergence-free swirl (no sources/sinks, looks like real air).
- **Wind offset** — translate the noise sample position by `Wind * SequenceTime` each sequence.
- **Multi-octave FBM** for detail; `smoothstep` the field into clumps; multiply by an exponential
  **height falloff** so fog pools low and thins with altitude.

Frontier already owns curl-noise and clipmap re-centring machinery (RadianceMarch / GDF), so the
advection layer is largely re-wiring, not new infrastructure.

---

## Performance (GTX 1060 / Pascal baseline)

Epic benchmarks the froxel approach at ~1 ms (PS4 High) to ~3 ms (GTX 970 Epic, ~8× voxels); cost is
**dominated by field resolution**. A 1060 sits between those points → a few ms at moderate resolution.

⚠️ Those are ~2017 Maxwell/GCN numbers, **not a direct 1060 measurement** — tune resolution and Z
distribution and **measure empirically** (mirror the `RenderExtension` `…Validation` discipline).

Levers, cheapest first: lower Z slices → shorter far reach → fewer light-march taps → half-res
integration + upsample (what the demo's next step would be anyway).

---

## Naming Ledger (authoritative — extend in the same spirit; propose new identifiers before use)

📝 Banned here: `system`, `mesh`, `stage` (a GPU unit is a `…Pass`), `Frame` (a frame is a temporal
**Sequence**), `Manager`/`Handler`/`Data`/`Node`/`Kind`. Suffixes `…Pass`, `…Field`, `…Profile`,
`…Enabled`, `…Condition`, `…Division`, `…Validation` follow the strict rules.

| Concern | Name |
|---|---|
| Subsystem coordinator (owns passes, drives the field) | `VolumetricFogDivision` |
| The frustum-aligned 3D field container | `FroxelField` |
| Pass ①: evaluate density + extinction into the field | `FogDensityInjectionPass` |
| Pass ②: accumulate in-scattered light per froxel | `FogScatteringInjectionPass` |
| Pass ③: integrate near→far, write scattering + transmittance | `FogIntegrationPass` |
| Pass ④: composite the integrated field over the scene | `FogCompositePass` |
| Secondary sun self-shadow march | `SunTransmittanceMarch` |
| Analytic far-field atmosphere | `AerialPerspectiveField` |
| Per-froxel payload (scattering RGB + extinction A) | `ScatteringExtinctionProfile` |
| Advected density evaluator (curl-noise + wind + FBM) | `EvaluateFogDensity` |
| Height-falloff term | `HeightFalloffProfile` |
| HG phase evaluator | `EvaluatePhaseHenyeyGreenstein` |
| Anisotropy dial | `ScatteringDistribution` |
| Extinction = absorption + scattering | `ExtinctionCoefficient` (`AbsorptionCoefficient` + `ScatteringCoefficient`) |
| Running transmittance during integration | `AccumulatedTransmittance` |
| Temporal reprojection of the prior field | `FogTemporalReprojection` |
| History blend weight | `TemporalBlendProfile` |
| Sub-voxel jitter offset | `SubVoxelJitterProfile` |
| Field resolution triple | `FroxelExtent` {X,Y,Z} |
| Exponential Z distribution | `DepthDistributionProfile` |
| Per-sequence time (frame time banned) | `SequenceTime` |
| Standalone test app + folder | `VolumetricFogValidation` |
| Build define | `FRONTIER_VOLUMETRIC_FOG_VALIDATION` |
| Initialize / per-sequence / finalize | `InitializeVolumetricFog` / `SynthesizeFogSequence` / `FinalizeVolumetricFog` |

Suggested home: `Engine/Internal/Graphics/Renderer/Volumetric/` (`FroxelField/`, `Passes/`, `Shaders/`,
`AerialPerspective/`), coordinator `VolumetricFogDivision.{h,cpp}` — one component per folder, `.h`+`.cpp`.

---

## Phase Roadmap

| Phase | Deliverable | State |
|---|---|---|
| **P0** | WebGL2 reference demo — full physics as a fullscreen march; lock the look/constants | ✔️ DONE (`VolumetricFogDemo.html`) |
| **P1** | `FroxelField` + `FogDensityInjectionPass` + `FogIntegrationPass` + `FogCompositePass`; one directional (sun) light, constant density; prove the field end-to-end | ☐ |
| **P2** | `EvaluatePhaseHenyeyGreenstein` in injection → god-rays; `SunTransmittanceMarch` self-shadow | ☐ |
| **P3** | Curl-noise + wind advection in `EvaluateFogDensity`; `HeightFalloffProfile` | ☐ |
| **P4** | `FogTemporalReprojection` + `SubVoxelJitterProfile` — kill the low-res aliasing | ☐ |
| **P5** | `FogScatteringInjectionPass` multi-light (point/spot, shadowed) | ☐ |
| **P6** | `AerialPerspectiveField` (Bruneton→Hillaire LUTs) + composite seam (no double-counted extinction) | ☐ |
| **P7** | `VolumetricFogValidation` app + 1060 timing capture; tune `FroxelExtent` / `DepthDistributionProfile` | ☐ |

---

## Open Questions (carried from research — resolve during build)

1. Concrete `FroxelExtent`, Z distribution, and `TemporalBlendProfile` for best quality/perf on a 1060.
2. Exact curl-noise / Perlin-Worley + wind + FBM recipe (medium-confidence in research — validate visually).
3. Multiple scattering beyond single-scatter: Frostbite approximation vs Hillaire 2020 multi-scatter LUT.
4. **Compositing seam** — combining `AerialPerspectiveField` with `FroxelField` without double-counting
   extinction, and the depth/transmittance handoff. Least-documented, trickiest integration point.

---

## Source Reading Order

1. Wronski 2014 (froxel foundation) — `bartwronski.com/publications/`
2. Hillaire / Frostbite 2015 — `ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite`
3. Hillaire Revision 2013 course notes (the math worked) — `patapom.com/topics/Revision2013/`
4. Hillaire 2020 sky/atmosphere + MIT source — `github.com/sebh/UnrealEngineSkyAtmosphere`
5. Bruneton & Neyret 2008 — `inria.hal.science/inria-00288758`
6. RDR2, Bauer 2019 — `advances.realtimerendering.com/s2019/`
7. Reference code — `deepwiki.com/diharaw/volumetric-fog`
