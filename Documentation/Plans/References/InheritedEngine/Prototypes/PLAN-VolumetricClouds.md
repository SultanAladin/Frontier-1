# PLAN — VolumetricClouds

🧩 A raymarched volumetric cloudscape for Frontier: a Perlin-Worley density field authored by a
weather map + altitude gradients so one renderer produces every cloud type — flat stratus, lumpy
stratocumulus, billowy cumulus, wispy cirrus, and towering **cumulonimbus (thunderheads)** — lit by
Beer-Lambert extinction + a dual-lobe Henyey-Greenstein phase + a powder dark-edge term, self-shadowed
by a sun-facing sample cone, and casting **moving cloud shadows onto the scene** via a top-down
transmittance map. Made real-time (GTX 1060 / Pascal baseline) with a quarter-resolution target,
jittered raymarch, and temporal reprojection. It shares its physics core (Beer-Lambert + HG + the
energy-conserving integral) with [[PLAN-VolumetricFog]].

---

## Provenance (research-backed, adversarially verified 2026-07-26)

Deep-research pass: 22 sources, 25 claims 3-vote verified, **25 confirmed / 0 killed** (unusually
clean — the technique is settled and consistently reported). The canonical approach is **Nubis** by
Andrew Schneider + Nathan Vos (Guerrilla Games).

| Iteration | Source | Contribution |
|---|---|---|
| **Nubis (HZD)** | Schneider, *The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn*, SIGGRAPH 2015 (Advances in Real-Time Rendering) | The bedrock recipe — Perlin-Worley + weather map, **< 2 ms on PS4** |
| **Nubis 2** | Schneider, *Authoring Real-Time Volumetric Cloudscapes with the Decima Engine*, SIGGRAPH 2017 | Regional authoring, animation/transitions, atmosphere integration |
| **Nubis Evolved** | Guerrilla, 2022 | Native 1080p without temporal upscaling |
| **Nubis Cubed** | Schneider, 2023 | Voxel renderer + compressed-SDF sphere-tracing (leaves 2.5D behind) |
| **Implementable recipe** | Högfeldt thesis (Chalmers / Frostbite) | Shader-level end-to-end math — the most directly portable reference |
| **Lighting integral** | Hillaire, *Physically Based Sky, Atmosphere & Cloud Rendering in Frostbite*, SIGGRAPH 2016 | Dual-lobe HG + energy-conserving analytic integration |

---

## Recommendation (locked)

**Build the Nubis 2015/2017 (Perlin-Worley 2.5D) path.** It is the only approach that delivers the full
cloud-type range *and* runs on a 1060. Treat **Nubis3 (voxel + SDF)** as a later upgrade, not the first
build — it is PS5-class and heavy to reproduce.

| Approach | Cloud types | Shadows | Cost (1060) | VRAM | Effort | Verdict |
|---|---|---|---|---|---|---|
| **Nubis 2015/2017 (Perlin-Worley 2.5D)** | Stratus, stratocumulus, cumulus, cirrus, **cumulonimbus (thunder)** via weather-map + altitude gradients | Self-shadow (6-tap cone) + cloud→ground shadow map | 🚩 med (quarter-res + reproject) | 🚩 ~9 MB (128³ + 32³ + 128²) | 🚩 med | 🏆 **Use this** — full type range, 1060-appropriate |
| **Nubis3 (voxel + SDF, 2023)** | Same + finer detail, best quality | Best (decoupled sun-density voxel grid, long-distance) | 🔴 high (PS5-class) | 🔴 high (128³ + voxel grid) | 🔴 high | ⭐ Best-looking, deferred — too heavy for the 1060 floor |

Legend: ✔️ low · 🚩 med · 🔴 high · 🏆 pick · ⭐ aspirational.

---

## Density Field (the noise construction — verified 3-0)

Two-level **"shape then erode,"** three precomputed textures (~9 MB total):

| Texture | Extent | Channels | Role |
|---|---|---|---|
| `BaseShapeField` | 128³ | 4 | ch1 = Perlin-Worley; ch2-4 = three Worley octaves. Worley channels summed, **multiplied** into the Perlin channel |
| `DetailErosionField` | 32³ | 3 | three Worley octaves — **subtracted at the edges** to carve wispy detail |
| `CurlMotionField` | 128² | 3 | non-divergent (fluid) swirl that advects the detail — the "it moves" layer |

**Weather map** (`WeatherProfileField`, 2D, tiling): `R = coverage`, `G = precipitation`, `B = cloud type`.

**Density assembly** (Högfeldt Algorithm 4.1, verified):

```text
Density  = Coverage                         // WeatherProfileField.r
Density *= AltitudeSignal(weather.gb)       // where in the layer this height sits
Density *= EvaluateBaseShape                // BaseShapeField (Perlin-Worley * Worley)
Density -= EvaluateDetailErosion            // DetailErosionField carves edges
Density *= AltitudeGradientProfile          // per-type vertical shaping (below)
Density  = clamp(Density, 0, 1]
```

---

## Cloud Types (one renderer, authored by weather + altitude gradients)

Cloud type is **not** a separate renderer — it is the weather map's `B` value selecting/blending
**altitude gradients**. The three low presets are blended by a 0–1 type value; cumulonimbus is a
dedicated override.

| Cloud | Altitude gradient | Weather-map / gradient authoring |
|---|---|---|
| **Stratus** (flat haze layer) | `StratusGradientProfile` — low, thin, spread | type ≈ 0.0, low coverage, low band |
| **Stratocumulus** (lumpy layer) | `StratocumulusGradientProfile` | type ≈ 0.5, medium coverage |
| **Cumulus** (puffy fair-weather) | `CumulusGradientProfile` — tall base, billowy top | type ≈ 1.0, medium coverage, mid band |
| **Cumulonimbus (THUNDER)** | `CumulonimbusGradientProfile` — **separate override**, full vertical column + anvil top | high coverage + high precipitation (G) + full-height gradient |
| **Cirrus** (wispy high) | high thin band, detail-heavy | separate high band, low density |

The three low profiles blend through `MixAltitudeGradients()`; **cumulonimbus overrides** (it needs the
full vertical extent + anvil). Precipitation (`G`) darkens the base and drives the thunderhead look.

---

## Lighting & Phase Math (verified 3-0 — shared with the fog plan)

```text
extinction    T          = exp(-SigmaT * d)                              // Beer-Lambert
phase (HG)    p(cos,g)   = (1 - g*g) / (4*pi * (1 + g*g - 2*g*cos)^1.5)  // Henyey-Greenstein
dual-lobe     p2         = lerp(p(cos,g0), p(cos,g1), alpha)             // Frostbite: g0=0.8, g1=-0.5, alpha=0.5
integrate     Sint       = (S - S*exp(-SigmaT*D)) / SigmaT               // energy-conserving (Hillaire)
```

- **Dual-lobe HG** — one forward lobe (g0=0.8) + one back lobe (g1=−0.5) gives the silver-lining +
  fill look. Defaults are artist-tunable, not strict physics.
- **Powder / "Beer's-Powder"** — Schneider's view-dependent dark-edge term recreating the
  powdered-sugar darkening on cloud edges. HG was chosen over Cornette-Shanks (cost) and full Mie
  (banding).
- **Energy-conserving integration is the single most important correctness detail** — the naive
  `S = T·L` accumulation over-darkens and makes brightness depend on step length; store extinction,
  integrate analytically.
- **Primary march**: 64–128 samples along the view ray, early-out when transmittance < ~0.01.

---

## Shadows (both requested kinds)

| Shadow | What it delivers | How | Cost |
|---|---|---|---|
| **Self-shadow** (`SunConeTransmittance`) | Dark undersides, bright tops — clouds read as 3D solids | **6 samples in a cone toward the sun** (5 near + 1 far to catch distant-cloud shadowing), Beer-Lambert along the light ray | inside the march |
| **Cloud → ground** (`CloudShadowProjectionPass`) | Moving cloud shadows sweep across terrain/scene | render cloud transmittance into a **top-down `CloudShadowField`** map; sample it when shading the scene | 🚩 one extra low-res pass |
| **Decoupled sun-density** (Nubis3, deferred) | Long-distance shadows, ~40% cheaper march | precompute summed sun-direction density into a voxel grid, amortized over 8 sequences | 🔴 deferred to the Nubis3 upgrade |

For thunderheads specifically, **self-shadow + `CloudShadowProjectionPass`** together give the dark
storm underside with shadows racing across the ground.

---

## Performance (GTX 1060 / Pascal baseline — verified, with caveat)

| Lever | Effect |
|---|---|
| Quarter-resolution cloud target | march far fewer pixels |
| **Vos 1-of-16-per-4×4-block + reprojection** | the headline win: **~20 ms → ~2 ms** on the source hardware |
| Jittered start (Van der Corput) + temporal blend (~5% history) | recovers detail from sparse sampling |
| Dual cheap/expensive stepping | big cheap steps until a cloud is hit → full detail → step back after empty samples |
| Early-out | stop when transmittance < ~0.01; skip pixels high-transmittance last sequence |

Combined (arXiv 1609.05344): sparse sampling + jitter + temporal resolve + analytic integration
reaches usable quality at **~1/16 the steps**.

⚠️ **Hardware caveat (verified):** every shipped ms figure is console-class — HZD ~2 ms is **PS4/GCN**;
Nubis Evolved / Nubis Cubed (2.2–4 ms @ 960×540) are **PS5-class (~RX 6700 tier, materially stronger
than a 1060)**. **No source measures a native 1060.** Budget **quarter-res + reprojection + aggressive
early-out** and expect clouds to take a meaningful frame fraction. Build the 2015/2017 + Högfeldt
recipe, not Nubis3.

---

## Naming Ledger (authoritative — extend in the same spirit; propose new identifiers before use)

📝 Banned here: `system`, `mesh`, `stage` (a GPU unit is a `…Pass`), `Frame` (a frame is a temporal
**Sequence**), `Kind`, `Node`, `Manager`/`Handler`/`Data`, family words. Suffixes `…Pass`, `…Field`,
`…Profile`, `…Enabled`, `…Condition`, `…Division`, `…Validation` follow the strict rules.

| Concern | Name |
|---|---|
| Subsystem coordinator | `VolumetricCloudDivision` |
| Low-frequency base 3D noise (128³, 4-ch) | `BaseShapeField` |
| High-frequency erosion 3D noise (32³, 3-ch) | `DetailErosionField` |
| Curl motion 2D noise (128²) | `CurlMotionField` |
| Weather authoring map (coverage/precip/type) | `WeatherProfileField` |
| Per-type vertical shaping | `AltitudeGradientProfile` (`Stratus…` / `Stratocumulus…` / `Cumulus…` / `Cumulonimbus…`) |
| Blend low presets by type value | `MixAltitudeGradients` |
| Density evaluator (assembly above) | `EvaluateCloudDensity` |
| Base-shape sampler | `EvaluateBaseShape` |
| Detail-erosion sampler | `EvaluateDetailErosion` |
| View-ray march pass | `CloudRaymarchPass` |
| HG phase evaluator | `EvaluatePhaseHenyeyGreenstein` |
| Dual-lobe anisotropy dials | `ForwardScatterProfile` / `BackScatterProfile` |
| Powder dark-edge term | `PowderEdgeProfile` |
| Sun self-shadow cone march | `SunConeTransmittance` |
| Cloud→ground shadow render pass | `CloudShadowProjectionPass` |
| Top-down cloud shadow map | `CloudShadowField` |
| Energy-conserving accumulation | `AccumulatedTransmittance` |
| Quarter-res + reproject resolve | `CloudTemporalReprojection` |
| 4×4 sparse update selector | `SparseUpdateProfile` |
| History blend weight | `TemporalBlendProfile` |
| Jitter offset | `RaymarchJitterProfile` |
| Per-sequence time (frame banned) | `SequenceTime` |
| Standalone test app + folder | `VolumetricCloudValidation` |
| Build define | `FRONTIER_VOLUMETRIC_CLOUD_VALIDATION` |
| Initialize / per-sequence / finalize | `InitializeVolumetricCloud` / `SynthesizeCloudSequence` / `FinalizeVolumetricCloud` |

Suggested home: `Engine/Internal/Graphics/Renderer/Volumetric/Clouds/` (`Noise/`, `Passes/`,
`Shaders/`, `Shadow/`), coordinator `VolumetricCloudDivision.{h,cpp}` — one component per folder,
`.h`+`.cpp`. Shares the physics core with `Engine/Internal/Graphics/Renderer/Volumetric/` (fog).

---

## Phase Roadmap

| Phase | Deliverable | State |
|---|---|---|
| **P0** | Precompute `BaseShapeField` (128³) + `DetailErosionField` (32³) + `CurlMotionField` (128²); write to cache | ☐ |
| **P1** | `CloudRaymarchPass` — base-shape only, constant coverage, Beer-Lambert transmittance; prove the march | ☐ |
| **P2** | `WeatherProfileField` + `AltitudeGradientProfile` blend → stratus/stratocumulus/cumulus/cirrus; **cumulonimbus override** | ☐ |
| **P3** | Lighting: dual-lobe `EvaluatePhaseHenyeyGreenstein` + `PowderEdgeProfile` + `SunConeTransmittance` self-shadow; energy-conserving integral | ☐ |
| **P4** | Detail erosion + `CurlMotionField` advection (wind over `SequenceTime`) → moving, wispy clouds | ☐ |
| **P5** | Quarter-res target + `RaymarchJitterProfile` + `CloudTemporalReprojection` + `SparseUpdateProfile` (1-of-16) — hit the 1060 budget | ☐ |
| **P6** | `CloudShadowProjectionPass` → `CloudShadowField`; sample it when shading the scene → moving cloud shadows | ☐ |
| **P7** | Atmosphere seam — apply aerial-perspective / sky LUT + order cloud vs [[PLAN-VolumetricFog]] fog composite (open question #1) | ☐ |
| **P8** | `VolumetricCloudValidation` app + 1060 timing capture; tune resolution / reprojection cadence | ☐ |
| **P9** *(deferred)* | Nubis3 upgrade — compressed-SDF sphere-trace + decoupled sun-density voxel grid | ⏸ deferred |

---

## Open Questions (carried from research — resolve during build)

1. **Cloud ↔ atmosphere ↔ froxel-fog integration** — the coupling (aerial-perspective LUT application,
   cloud-vs-fog composite order, transmittance handoff) is confirmed to *exist* in Nubis/Frostbite but
   no source gave the exact mechanism. **This is the seam that ties this plan to [[PLAN-VolumetricFog]].**
2. Concrete 1060 step counts, texture resolutions, and reprojection cadence — all measured figures are
   console-class; validate empirically (mirror the `…Validation` discipline).
3. Per-sequence SDF generation/update cost for animated clouds on the deferred Nubis3 path.

---

## Source Reading Order

1. Högfeldt thesis (Chalmers/Frostbite) — most implementable, shader-level — `cse.chalmers.se/~uffe/xjobb/RurikHögfeldt.pdf`
2. HZD slides — `slideshare.net/guerrillagames/the-realtime-volumetric-cloudscapes-of-horizon-zero-dawn`
3. Frostbite PB Sky/Atmosphere/Cloud deck (dual-lobe + energy-conserving integral) — `slideshare.net/DICEStudio/physically-based-sky-atmosphere-and-cloud-rendering-in-frostbite`
4. arXiv 1609.05344 — sparse-sampling optimization paper
5. Nubis Cubed PDF — the deferred voxel/SDF path
6. Reference code — Meteoros (Vulkan): `github.com/AmanSachan1/Meteoros` · Project Marshmallow: `github.com/mccannd/Project-Marshmallow`
