# Material glint research and budget proposal

**Current user decision (2026-10-25): cellular retained for automotive paint.** Earlier Glinty/default proposals
are historical, not the selected implementation direction. [Native material renders](../../../Exhibits/LightCompute/MaterialScene.md).


**2026-10-19 · Research plus isolated benchmark. Production implementation/default switch remain on hold.**

Local software-renderer results: [executed comparison](../../../Exhibits/TextureStudio/GlintComparison.md).
The benchmark authorization supersedes the earlier research-only restriction, not the production hold.

Keep both evaluation choices. The requested future default is particle microfacets for new materials; retain
existing cellular flakes for compatibility. These papers do not describe interchangeable particle-sprite engines.
A 25 MB memory cap does not establish an FPS target or require allocating 25 MB.

## Research comparison

Suitability ranking concerns our environment-lit material preview, not measured speed or universal image quality.

| Reference | Mechanism and evidence | Material-preview suitability |
|---|---|---|
| Glinty 2025 [1] | Implicit point process; GGX/Beckmann and sampling | First default candidate |
| Chermain 2020 [3, 4] | Normalized NDFs; compact multiscale dictionary | Lighting restrictions |
| Loubet 2020 [2] | Slope-space integration; specular path sampling | Later transport reference |
| Current cellular paint | Object-space property generation before lighting | Retain compatibility |

**Recommendation:** investigate Glinty first when implementation is authorized. Preserve cellular evaluation as an
explicit alternative. Do not replace the existing presets or assume these methods produce identical images.

## What the papers actually establish

- **Glinty [1]:** facets are represented by a point process on an implicit multiscale grid, designed to locate
  highlight-producing facets. The project describes memory- and precomputation-free evaluation, GGX/Beckmann,
  anisotropy, per-particle colour and importance sampling. This does not mean the whole renderer uses no memory,
  or that a frame has constant cost regardless of pixels, lights or layers. No explicit particle buffer is required.
- **Chermain [3, 4]:** the published implementation uses a 384 KiB dictionary. Its abstract's broader claim is
  less than 1 MiB. Section 7.6 states the original method does not support GGX, explicit normal-map control of
  its patch NDFs, or area/environment illumination through importance sampling. The authors leave that sampling
  scheme to future work. Do not silently attribute later extensions to this original 2020 implementation.
- **Loubet [2]:** the main problem is robust light-path sampling through one specular reflection/refraction,
  including caustics. Its related glint application handles explicit high-resolution normal microstructure and
  GGX slope integrals. This is not evidence for a budgeted real-time particle-sprite renderer.
- None of these three references establishes a complete hair/fibre renderer. Velvet/sheen response, visible
  fibres, fibre shadowing and geometry remain separate features; shared masks/density inputs do not unify their
  scattering models automatically.

## Published performance, not Frontier measurements

Chermain Table 2 / section 7.5 [4]: NVIDIA RTX 2080, OpenGL forward shading, 1920×1080; timings include overdraw.

| Scene | Glint method, ms/frame | Cook–Torrance baseline, ms/frame |
|---|---:|---:|
| Sponza | 3.0 | 0.7 |
| Screen plane | 2.5 | 0.4 |
| Car (2 CV) | 9.2 | 2.0 |
| Parquet | 6.4 | 0.6 |
| Brushed aluminium | 1.7 | 0.4 |

The car/parquet cases use three point lights; timings depend on coverage, roughness and relative microfacet area.
These are complete timings for those particular test renderings, not isolated shader instruction timings or
predictions for Frontier. They demonstrate real-time feasibility, not that glints are cheaper than ordinary PBR.

Glinty's project/Adobe pages were accessible, but both the linked author PDF and the Aalto PDF mirror failed to
fetch with HTTP 500 through the available retrieval tool. Its detailed benchmark tables were therefore not
verified. Do not attach an invented millisecond figure or speed-up ratio to it. Loubet's abstract/introduction
and Chermain's abstract/performance/limitations sections were inspected; this is not a claim of full reproduction.

**Current forecast:** plausible interactive material-preview use, but no defensible Frontier FPS or faster-than-
cellular claim yet. GPU, resolution, affected screen area, light sampling, coats and temporal quality all matter.

## Existing Frontier source audit

Inspected, not modified or newly benchmarked:

- `Engine/Render/Shader/AutomotivePaint.slang`: `EvaluatePaintCell` searches 3×3×3 = 27 cells per flake layer.
  `EvaluatePaintSurface` calls it once per configured flake layer. Three layers mean 81 cell visits per shaded
  paint pixel, plus warp/noise, palette, derivatives and relief. Supported source layers are 1–4, hence 27–108.
- Its derivative footprint computes a resolved-detail blend, but the cell loop still runs before that blend.
  This source observation is not a compiler disassembly, hardware instruction count or measured bottleneck.
- `Engine/Render/Shader/MaterialSurface.slang`: paint properties are evaluated before environment integration.
  The environment loop uses 8 or 32 samples per proposal; there is one cosine proposal plus one per stratum.
  Two strata in the 32-sample mode produce 96 proposal samples, before direction-rejection branches.
- A true light/view-dependent glint NDF may be evaluated inside lighting integration, unlike the existing
  once-per-pixel cellular property generation. Replacing cells alone may therefore increase total work.
  Glint importance sampling and its PDF must agree with BRDF evaluation and the existing mixture estimator.
- Existing `Exhibits/TextureStudio/Results/ProceduralPaintChecks.json` records pixel-change correctness, not
  hardware timings. Native control evidence explicitly reports hardware/Windows execution false. Software-
  Vulkan wall-clock results would not predict a desktop GPU speed-up, so none are presented as such here.

Source fingerprints at review:

```text
AutomotivePaint.slang c973b9836e49afbed482296f6eeebe49ed9d9ca9594ad345341209dc8454d398
MaterialSurface.slang 95506ec0f2cc73351cb4a11ab1d6d3cbaec6cfde511d35f8d5a92da36506feb5
SurfaceMaterial.cpp   31e9ee3365277b0436b4f250ec2e205ff4354bd9baa3a100964c777b3e97d104
```

## Proposed 25 MB budget

Scope below is a recommendation, not implemented policy. MB means **25,000,000 bytes**, not 25 MiB.

| Interpretation | Consequence | Proposal |
|---|---|---|
| Shared feature cap | Shared across active materials | Recommended: 25 MB |
| 25 MB per material | Ten materials could reserve 250 MB | Not the default |
| Mandatory particle allocation | Wastes memory for an implicit method | Do not allocate merely to fill the budget |

Charge caches/dictionaries, optional explicit facets, indices, scratch and overlapping in-flight versions to the
same feature allocation cap, including known allocation alignment. Exclude unrelated preview targets/material
textures and report those separately. Unknown driver/pipeline overhead means this is not a whole-process VRAM
cap. Show used/limit, cache rebuilds, overflow and the active evaluator. No hidden density loss on overflow.

If a later explicit-facet mode needs stored particles, these are arithmetic upper bounds, not Glinty requirements:

| Hypothetical bytes/facet | 25 MB, no overhead | 20 MB after reserving 5 MB for other feature allocations |
|---|---:|---:|
| 32 | 781,250 | 625,000 |
| 48 | 520,833 | 416,666 |
| 64 | 390,625 | 312,500 |

Actual capacity depends on the chosen layout, allocation granularity, indices and retirement overlap. Persistent
CPU source/history, texture inputs and visible fibre geometry have separate accounting; do not multiply caches
by channel or silently consume the flake allowance for hair. Implicit Glinty may need very little of this cap.

A separate work/quality limit is necessary: pixel footprint/LOD, angular filtering, light samples and temporal
stability. An example future acceptance target is at most 2 ms **incremental** glint GPU cost at 1080p on an
agreed desktop GPU. That is a proposed target, not a prediction or commitment; total frame time remains separate.

## Integration with the requested material layers/components

```text
Material
├─ Clearcoat: coverage, IOR, roughness, absorption/thickness
├─ Middle coat
│  └─ Flakes: evaluator + density/size/orientation/tint inputs + masks
└─ Basecoat: colour/roughness/normal and procedural components
```

- Future evaluator selector: **Particle microfacets** (default for new flake components) / **Cellular** (existing).
  Keep old material interpretation unchanged until explicit conversion. Store authoring choices/seed in TOML;
  caches are derived resources. No schema, shader or selector was added in this research turn.
- Gradient/noise/image/Pattern components can drive compatible density, size, tint, roughness and orientation
  inputs. A glint model is a scattering feature, not simply another albedo overlay or arbitrary export channel.
- Preserve binder tint, burial, palette/ramp/single modes, orange peel and coating attenuation. Initial research
  must test their mapping; the papers do not prove compatibility with our current source parameters.
- Both methods can exist in different layers, or use cellular colour variation with particle-microfacet specular
  response. Do not add two full-strength responses for the same facet population: that double-counts energy.
- Coats must attenuate underlying responses consistently. Dense/distant facets need a filtered aggregate limit,
  not hard disappearance. Seeds must remain stable during camera motion and across colour/normal/depth outputs.
- Fibre geometry/sheen can coexist through masks and material layers, but require their own shading, sampling,
  LOD, self-shadowing and budget decisions. These papers alone do not authorize a shared particle simulator.

## Deferred verification before choosing a production default

1. Review the full Glinty manuscript, reference shader and applicable code licences; do not assume paper access
   grants a shader-code licence. Confirm GGX/Beckmann, filtering and sampling conventions before translation.
2. Capture current cellular baseline on actual Windows/Vulkan hardware: GPU timestamps, CPU/present, p50/p95,
   device/driver, resolution, lighting proposals, strata, coverage and seeds. No idle-FPS or software-GPU claim.
3. Compare equal scenes: cellular, implicit particle microfacets and their controlled combination. Exercise 1–4
   flake layers, single/multiple coats, point/studio/environment lighting, zoom/pan and roughness/density extremes.
4. Report full-frame and incremental shader cost, residency/peak allocations, rebuilds and quality differences.
   Pixel-identical output is not expected. Test energy, coloured/anisotropic facets, seam stability and aliasing.
5. Test 25 MB overflow, many materials and in-flight retirement without stalls/use-after-free. Reject or visibly
   reduce cache quality rather than silently changing the authored density. Confirm supported platforms.
6. Only then enable the requested particle-microfacet default for new components and retain cellular selection.

**No material implementation, shader edits, new particle allocation, benchmark port, Actions, package or release
was started. Pattern's existing approved backlog continues separately; this document does not mark it complete.**

## Sources

- [1] Kemppinen, Paulin, Thonat, Thiery, Lehtinen and Boubekeur, 2025, *Evaluating and Sampling Glinty NDFs
  in Constant Time*, DOI `10.1145/3763282`.
  <https://perso.telecom-paristech.fr/boubek/papers/Glinty/>
  Accessible corroborating abstract: <https://research.adobe.com/publication/glinty/>.
- [2] Loubet, Zeltner, Holzschuch and Jakob, 2020, *Slope-Space Integrals for Specular Next Event Estimation*,
  DOI `10.1145/3414685.3417811`. <https://rgl.epfl.ch/publications/Loubet2020Slope>
  Linked paper: <https://d38rqfq1h7iukm.cloudfront.net/media/papers/Loubet2020Slope.pdf>.
- [3] Chermain, Sauvage, Dischler and Dachsbacher, 2020, *Procedural Physically based BRDF for Real-Time
  Rendering of Glints*, DOI `10.1111/cgf.14141`.
  <https://igg.unistra.fr/People/chermain/real_time_glint/>
- [4] Chermain preprint, especially Table 2 and sections 7.5–7.6:
  <https://igg.unistra.fr/People/chermain/assets/pdf/Chermain2020Procedural.pdf>.
- Failed Glinty PDF retrievals: author `Glinty.pdf` linked from [1] and Aalto mirror linked at
  <https://aaltodoc.aalto.fi/items/10f84cdd-c742-4167-a358-129b96b3fa06>.

Sources accessed 2026-10-19. No paper/video/shader assets were copied into the repository.

## User direction and source audit — 2026-10-25

The user explicitly wants to reuse the linked material code/library **except glints/flakes/glitter, which
need rewriting**. This is the implementation direction, not an instruction to redesign all material presets.
The existing research and adapter benchmark above were found and reviewed; they are not a completed rewrite.
`Exhibits/TextureStudio/MaterialReuse.md` records pinned upstream identity (452/455 compared files identical,
including all material archive files and optical material/paint shaders). The three differences are newer
vector-gradient authoring/ABI changes, not a glint replacement. Do not import them piecemeal.
Proceed with a licensing-safe reference and layered-material integration/validation; no production-default
switch is justified until the new evaluation, PDFs, energy and temporal behaviour have been checked.

## Requested two-render comparison — 2026-10-25

[GlintPair](../../../Exhibits/TextureStudio/GlintPair.md) now supersedes the historical adapter as the
requested visual comparison: exactly two sand/metallic-paint/snow PNGs, primary/repeat software Vulkan
elapsed queries and isolated host-memory snapshots. It uses an author-demo-derived Glinty mirror;
full-file identity and commercial permission remain unverified. Both captures repeat byte-identically.
Cellular is unchanged and remains legacy. No production default, full native integration, temporal/PDF
acceptance or physical-GPU speed claim follows from this isolated exhibit.

[Native material evaluation](../../../Exhibits/TextureStudio/MaterialEnergy.md) has resumed using the
existing scattering sequence: 24 finite/nonnegative/reciprocal fixtures, three white-furnace excesses
(maximum 1.36977433). Keep the existing material library; investigate those energy findings before
claiming physically correct layered scene integration. Both analytic and emissive lighting remain required.

## User selection — cellular retained, 2026-10-25

The user selected **cellular for metallic automotive paint** after inspecting the paired renders; Glinty may
remain useful for finer detail but is not the current replacement direction. This supersedes the earlier
rewrite/default-selection proposal, without deleting the research or its permission limits.
[Native material scene](../../../Exhibits/LightCompute/MaterialScene.md) now binds Racing Green cellular
clearcoat, rubber, thin transmitting glass, BVH mirrors and plastic depth-screen reflections, and reuses the
C++ shaderball/cloth geometry. It is a bounded integration, not full PBR-library or hardware acceptance.
