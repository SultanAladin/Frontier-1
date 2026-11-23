# Vulkan preview, materials and illumination integration









## Cloud-shadowed fog and light shafts — 2026-10-25

- Clarified provenance: inherited four-octave cloud density was ported and its rendering integration improved;
  it was not replaced by a new cloud-shape model. Earlier cloud/ground shadows did not establish air light shafts.
- Added new native/GPU height-fog single scattering, exact camera/light optical depth, HG phase, cloud visibility
  at each volume sample and triangle occlusion. Original point/IES/spot profiles illuminate the fog. Analytic
  arrivals and sampled emitter-to-surface segments receive fog extinction; both lighting paths remain.
- Actual 320×200 captures: `Exhibits/LightCompute/Images/FogSkyComparison.png` (cloud shadow off/on in fog),
  `FogLocalComparison.png` (local-light fog off/on), `FogComparison.png` (original ControlVehicle), plus wind120.
  The sky fixture uses an explicit thinner 800–1100 m broken-cloud slab to expose soft shafts, not a hidden effect.
- 654 numeric camera probes, 390 capture camera probes and 270 native surface-arrival probes. Daylight numeric
  RGB maximum discrepancy 1.14637e-5; local-light 1.04968e-5; capture maximum 5.48751e-5; surface maximum 5.21472e-5.
  Uniform Beer/ambient, near-horizontal stability, bitwise zero density, geometry occlusion, bounded steps,
  dry-history isolation and enable/disable cold equality passed. Focused checks passed ASan/UBSan.
- Sampling is not converged: on 12 numeric rays, view32→128 changed red radiance by up to 0.00311955 and
  shadow16→64 by up to 0.0293056. Remaining: alias/banding, temporal fog, multiple scattering, emissive-volume
  scattering, full inter-bounce/coupled-media transport and final cloud shapes/appearance.
- Thirteen entries, eight SSBOs, 128-byte pushes; no new output slabs or Int64 shader feature. SwiftShader CPU
  type 4 only, not physical-GPU/Windows/real-time acceptance. Existing native Sun/flare/stars remain; Moon planned.
- Reproduction, exact checks and scope: `Exhibits/LightCompute/README.md`, `RunFog.sh`, `Results/Fog.json`.

## Spatial cloud-shadow checkpoint — 2026-10-25

- Matching directional sunlight now integrates cloud extinction at each primary receiver and secondary hit,
  not at one observer position. Exact, reservoir-target and spatial-reuse evaluations use current-position T;
  proposal mass stays unattenuated with support retained. Both analytic and emissive lighting remain.
- Five cases × 433 native surface probes (2,165): maximum RGB discrepancy 8.87267e-6. Secondary transport:
  117 native probes, maximum 7.40833e-8. Each single-Sun case exercises 16,000 temporal and 63,438 spatial
  reuses; maximum exact/reused RGB discrepancy 9.53674e-7. Non-Solar analytic lighting is bitwise unchanged.
- Wind revision matches cold reconstruction. Reproduced an additional cloud-disable stale-history defect,
  then tracked out-of-prefix cloud selection: disable/re-enable now match cold direct/indirect output bitwise.
- Fixed omitted ambient RGB initializers from the previous cloud upload. The strengthened uniform check
  requires nonzero ambient. All 2,230 cube and 396 camera probes and corrected volume captures passed ASan/UBSan;
  maximum composite cube RGB error 8.32318e-6 and camera RGB error 8.63495e-6.
- Four actual 640×400 wind times (0/60/120/180 s), original ControlVehicle and original ground, are retained in
  `Exhibits/LightCompute/Images/CloudShadowVehicle.png`, `CloudShadowGround.png` and `CloudShadowComparison.png`.
  Cameras and vehicle are stationary; wind moves. The overhead direct-only view isolates spatial shadows.
- Final spatial/toggle ASan/UBSan and fresh-sampling checks passed. Legacy Vulkan, indirect reconstruction and
  native CTest 4/4 passed; their exact execution chronology is retained in `Results/CloudShadowRegression.txt`.
  Concurrent full-size software runs hit allocation errors; serial volume checks and resumed final view passed.
- Still twelve entries/eight SSBOs/128-byte pushes; no added slabs or Int64 shader feature. SwiftShader CPU
  type 4, not physical GPU, Windows or real-time acceptance. No new Moon/Sun/flare/star presentation integration.
  Remaining: flat-slab horizon artifacts, approximate ambient, indirect cube seams and finite-Sun penumbrae.
- Detailed evidence and reproduction: `Exhibits/LightCompute/README.md`, `RunClouds.sh`, `Results/CloudShadows.json`.

## Vulkan cloud environment checkpoint — 2026-10-25

- Added GPU cloud density, 64-step per-camera-ray integration, six-step Sun self-shadowing and a cloud radiance
  cube for indirect rays. Native 64-bit noise hash is emulated with paired 32-bit arithmetic; cloud/primary/
  transport SPIR-V requires only Shader capability, not Int64. CPU atmosphere precomputation remains.
- Added optional native/GPU uniform ambient cloud radiance (default zero preserves the earlier model). Sun at
  ground/cloud altitude uses atmospheric transmittance; one observer-to-Sun cloud factor shades local receivers,
  not a spatial cloud-shadow map. Cloud parameters and loop extents are checked before dispatch.
- 2,230 native cube probes: maximum T error 1.186e-5, RGB error 1.06147e-5. 396 direct camera-ray probes: maximum
  RGB error 9.02517e-6. Uniform Beer-Lambert, ambient closed form, clear-path bitwise equality, wind and bounded
  march checks pass. Full cloud cases passed ASan/UBSan; native CTest 4/4, recovered/native sanitizer checks,
  clustered ControlVehicle, legacy Vulkan and indirect-reconstruction regressions also pass.
- Actual clear/cloud/wind/3° low-Sun readbacks: `Exhibits/LightCompute/Images/CloudWeather.png`, `CloudSkyWeather.png`
  and `CloudComparison.png`. Fixture Sun angles/irradiance and camera are explicit, not observer/time ephemeris.
- Twelve compute entries, eight SSBOs, push ABI 128 B; cloud cache adds 221,200 B. Device remains SwiftShader CPU
  type 4; no physical-GPU/real-time/Windows claim. Existing native Sun/flare/stars and both light paths remain.
- Remaining cloud appearance: flat-slab/regular-step horizon structure, approximate ambient, indirect cube seams
  and spatial shadowing. Moon, GPU atmosphere and night/catalogue work remain in `Documentation/EnvironmentTodo.md`.

## Vulkan indirect reconstruction checkpoint — 2026-10-25

- Recovered saved branch from GitHub checkpoint `0a4560c2` after sandbox reset; restored pinned ignored tools.
- Added reprojection, current-neighborhood RGB clamp, capped temporal mean and surface-compatible 5×5 spatial
  filtering of indirect only. Current visible emission and sky misses stay separate; direct histories are unchanged.
- Isolated emission at 160×100: independent 16-seed reference, eight updates; RGB RMSE 0.0103498 → 0.0058573,
  **43.41% lower**. This is not a full-resolution moving-quality or equal-cost speed result. Independent native
  spatial kernel: 210 probes, maximum RGB discrepancy 4.8961e-8.
- Six full-resolution moving vehicle/camera poses exercise reuse and the pose-3 light reset. Emission revision and
  camera cut match cold reconstruction bitwise; all sources off is black. Visible emission is preserved; extracted
  direct output differs by at most 3.8147e-6 from final float arithmetic.
- Actual readbacks: `Exhibits/LightCompute/Images/TransportFiltered.png` and
  `TransportReconstructionComparison.png`. Legacy exact/cluster/reservoir/static-history, native primary and
  emissive-estimator checks passed; focused ASan/UBSan numeric/spatial/reset checks passed.
- Eleven compute entries, eight SSBOs, push constants 128 B; 21 float4 history/output slabs (86.016 MB at 640×400).
  SwiftShader CPU type 4 only. Clamping/smoothing are biased; moving emitter/shadow changes are not exhaustively
  revalidated. Physical RTX/GTX/Windows, glossy transport, GPU sky/clouds and Moon rendering remain open.
- Evidence/reproduction: `Exhibits/LightCompute/README.md` and `RunTransportReconstruction.sh`. Historical missing
  private scratch digests are explicitly labelled after recovery, not presented as current-byte verification.

## Vulkan emissive-area sampling checkpoint — 2026-10-24

- GitHub preservation verified before work: local/remote `3dfbc2d3` matched and the working tree was clean.
- Explicit area/luminance sampling over 1,130 emissive triangles replaces hemisphere-hit emission only. Analytic
  point/IES/spot/directional secondary shading and sky misses remain on the hemisphere route; visible emission
  is unchanged. Both lighting paths remain; no emissive clustering or authored proxy-energy accounting claimed.
- 225 native probes, 16 area draws: mean RGB error 7.2128e-9, max 1.36771e-6. Against independent 8,192-draw
  native reference probes, emissive RMSE 0.206886 → 0.00906289 (**95.62% lower**). This is not equal-cost speedup,
  a full-image quality statistic, or universal unbiasedness evidence.
- Six moving poses preserve current indirect bitwise versus fresh evaluation. Geometry-CDF rebuilds do not
  invalidate direct-light history; the changed analytic light at pose 3 still does. Probability ticks preserve
  dim emitter support; invalid CDF entries are rejected; empty-emitter and disabled-environment cases pass.
- Actual readbacks: `Exhibits/LightCompute/Images/EmissiveComparison.png` and `EmissiveMotion.png`. Coarse displaced
  bright footprints are substantially removed; residual grain remains. Indirect filtering is not implemented.
- Legacy exact/cluster/reservoir/history, native primary/cosine transport and new ASan/UBSan checks passed.
  Device remains SwiftShader CPU type 4. RTX/GTX/Windows acceptance, glossy transport and GPU environment are open;
  native Sun/stars/flare remain unchanged and Moon remains planned.
- Detailed source/device/sampling scope and reproduction: `Exhibits/LightCompute/README.md`, `RunEmissive.sh`.

## Vulkan one-bounce diffuse transport checkpoint — 2026-10-24

- `TraceSurfaceTransport` traces full-BVH secondary rays from Vulkan-generated camera surfaces. Secondary hits
  evaluate material emission and shadowed analytic point/IES/spot/directional lighting; misses sample a shared
  native-atmosphere cube. The primary response is diffuse; direct GGX lighting remains intact.
- 3,603,424 secondary rays; 901 independent double-reference receiver probes give mean RGB error 2.08237e-8,
  maximum 4.6271e-6. Analytic-only, emission-only and environment-only routes all contribute; isolated sum agrees
  within 8.44442e-6. All sources off is exactly black. Proxy-versus-emissive physical accounting remains open.
- Six moving frames retain current indirect/emission bitwise versus fresh evaluation, including the light-change
  reset. Actual readbacks: `Exhibits/LightCompute/Images/TransportMotion.png` and `TransportComparison.png`.
- Complete static/moving direct/primary regression and focused ASan/UBSan transport execution passed. Device is
  SwiftShader CPU type 4. Eight compute entries, eight SSBOs, 128-byte push constants; no GPU/Windows speed claim.
- Atmosphere is CPU-precomputed into six 48×48 float RGB cube faces, not CPU per-pixel lighting and not a GPU
  atmosphere/cloud port. Native Sun, flare and stars remain unchanged; Moon remains planned.
- Fixed 16-direction diffuse quadrature visibly creates coarse illumination footprints. Indirect denoising,
  emissive importance sampling, glossy/multi-bounce transport, environment appearance and physical RTX/GTX/Windows
  integration remain open. Numeric agreement with the finite quadrature is not a convergence/quality proof.
- Detailed reproduction, checks and source/device limits: `Exhibits/LightCompute/README.md` and transport reports.

## Moving Vulkan-primary history checkpoint — 2026-10-24

- Six authentic vehicle/camera poses now generate surfaces and rigid-motion metadata on Vulkan with CPU raster
  disabled and all receiver channels poisoned. Persistent temporal/spatial reservoirs and reconstruction execute
  over those generated receivers. Actual animation: `Exhibits/LightCompute/Images/PrimaryMotion.png`.
- Moving RMSE sum 0.651907 → 0.286237, **56.09% lower**; fixture quality, not equal-cost speed or GPU performance.
  Native motion projection agrees within 0.00012207 px. Uploaded per-pose rotation coefficients fix a measured
  shader trigonometry discrepancy; no CPU per-pixel visibility or motion buffer is supplied.
- Native ray/shading checks at all six poses; zero hit/miss disagreement. Eleven triangle identities differ at
  edges or within float-depth tolerance. Worst all-pixel RGB outlier is 0.960438; this remains a recorded
  robustness limitation. Matched-facet and all-pixel errors are separately reported, not conflated.
- Camera-cut versus cold restart, light-off invalidation and current facet-emission refresh pass bitwise checks.
  Full retained history, clustered lighting and primary clipping/fallback regressions pass.
- Device is still SwiftShader CPU type 4. This is direct plus visible material emission against black, not GPU
  indirect/emissive-source transport, sun/sky/cloud/Moon integration, physical RTX or Windows acceptance.
- Detailed evidence and reproduction: `Exhibits/LightCompute/README.md`, `Results/PrimaryMotion.txt` and JSON.

## Vulkan camera-ray visibility checkpoint — 2026-10-24

- Executed `TracePrimarySurfaces` on the authentic ControlVehicle, with native raster scene setup/render skipped
  and all 36 receiver floats poisoned before dispatch. Geometry, camera, material and light export remains CPU work.
- Independent double-precision rays: 225,214 hits / 30,786 misses, zero hit/miss or first-triangle disagreement;
  maximum relative depth error 5.00174e-6. Native barycentrics, BRDF, imported IES profiles and full-BVH shadows
  give mean RGB error 2.03235e-6, maximum 6.93947e-5. All-light and clustered output is identical.
- Near/far, nearest overlapping surfaces, all-miss and 256 forced-stack fallback checks passed. Exact shared-edge
  ties in the synthetic fixture are classified individually; nearest depths and shaded values remain unchanged.
- `Exhibits/LightCompute/Images/Primary.png` is the actual Vulkan readback, not an illustration. Device remains
  SwiftShader CPU type 4; no physical GPU, RTX query, Windows or production viewport proof is claimed.
- Seven shader entries / eight SSBOs / 128-byte push constants; private exhibit archive FLC3. Complete existing
  static/moving history regression and native sanitizer/CTest suite rerun successfully after the shared ABI change.
- This checkpoint is direct plus material emission against a black environment. Both analytic point/IES/spot and
  emissive lighting remain. GPU indirect transport, environment integration and moving GPU-primary history remain
  next integration gates. Preserve existing sun/lens flare/stars; Moon remains in `Documentation/EnvironmentTodo.md`.
- Reproduction, scope and measured results: `Exhibits/LightCompute/README.md`, `Results/Primary.txt` and manifest.

## Recommendation

**Implement 1 CAD → 2 MatCap → 3 preview PBR → 4 full materials/shaders, sequentially on shared Vulkan/Slang.**
This sequence is authorized. The user requested MatCap next despite the still-open native CAD host gates.
Do not make CAD depend on a visibility
buffer, PBR materials, ray tracing or ReSTIR. Keep both painting applications and reference collections intact.
Use one shared rendering foundation, not four independent renderer implementations.

Prefer **B's resource/culling structure** for the later illumination port, with selected features/tests from A.
Neither branch establishes a GPU-speed winner, supplies hardware ray tracing, or is a finished production GI solution.

## Current implementation boundary — CAD, MatCap and preview PBR offscreen

- `Engine/Render/SurfaceRasterization` now separates shared device programs, shared resident geometry and independent
  image targets. Viewports/thumbnails reuse the same pipeline pair and uploaded geometry; neither belongs to UI.
- Six `.slang` entries compile through an explicitly supplied portable compiler; reflection checks 112-byte CAD
  and 128-byte MatCap push ABIs and vertex locations; MatCap also checks texture/sampler slots. Both require only
  core SPIR-V Shader capability, not optional draw parameters or wide lines.
- Executed native C++/Vulkan readback on **SwiftShader (CPU), not Windows or hardware GPU**: orthographic/perspective,
  thumbnail, front/rear edge occlusion, near/eye clipping, cancelled upload retry and retained resource lifetimes.
- Fixture is two authored blocks in Exhibits, with explicit edges; it is not a CAD decoder, Boolean union or preset.
- C++ source, executable validation and captured images: `Exhibits/Renderer`. Portable compiler binary and SPIR-V
  outputs stay ignored; provenance/reflection reports and real PNG readbacks are retained.
- Still required for CAD acceptance: existing-device native presenter/image registration; orbit/pan/zoom and window
  resize/DPI; validation layers and Windows/hardware runs; document geometry/identity and thumbnail cache integration;
  shared-edge cleanup, transform/precision coverage and fenced retirement across all presentation readers.
- MatCap now uses the same resident geometry and sized targets, with one shared optional pipeline and immutable
  sampled lookup residency. View-space inverse-transpose normals, texture orientation/filtering, scale/shear/reflection,
  CAD round trips, texture changes, depth, thumbnail reuse and source-scope finalization are pixel-checked.
- The normal-calibration sphere is analytical test geometry, not a CAD document or creation preset. Captures and code
  are in `Exhibits/Renderer`. Native presenter/Windows/validation-layer acceptance remains open for both modes.
- Preview PBR adds an optional fourth shared pipeline, immutable scattering uniform residency and a shared texture
  interface. `ScatteringStratum` names the evaluated angular-scattering unit; `PreviewScattering` orders up to eight.
  Opaque strata mix evaluated GGX/diffuse by coverage; dielectric coatings reflect and attenuate scattering beneath
  using Schlick entrance/exit Fresnel and Beer absorption. This is a shared-normal preview approximation, not the
  full Unreal material implementation, parameter interpolation, or a completed material compiler.
- PBR includes directional illumination, deterministic cosine/GGX mixture quadrature of an analytical sky, planar
  sRGB albedo sampling, exposure, ACES-fit tone mapping and sRGB display encoding. No HDR archive/prefilter,
  authored UVs, normal mapping, refracted transport, multiple scattering or subsurface transport is claimed.
- The earlier HTML browser displays real native offscreen readbacks through `ServeRenderer.py`; one persistent
  Vulkan device retains four programs and each fixture geometry across material/mode requests. Original reference
  samples remain explicitly WebGL. Browser evidence does not close native editor/presenter acceptance gates.
- Authored opaque/coating materials now compile strict TOML to typed mip textures and a 1504-byte immutable uniform (original 1216-byte prefix plus automotive/preview fields).
  `MaterialSurface.slang` adds authored UV/tangent normals, ORM and radiance, using the shared scattering equations.
  The earlier HTML applies/imports/exports authored source and displays native compilation diagnostics/readbacks.
  This is the bounded material slice, not a general shader-expression compiler or every scattering model.
- Authored strata now have independent texture-array slices, UVs and normal strengths, rotated anisotropic GGX
  and optional exact dielectric Fresnel. Environment proposals/PDFs match each directional lobe. These additions
  do not add scene refraction, SSS, complex-IOR conductors or multiple-scattering compensation.
- Native document material assignment/presentation and advanced transport remain pending. No GI/Render settings, native sample presets,
  project-format migrations or painting integration are introduced by this work.

## Pinned references — keep A and B distinct

| Reference | Branch | Pinned commit | Exhibit |
| --- | --- | --- | --- |
| A | `arena/b55d93c0-slate` | `8a7004983b10c079564becb932ae9b444716b9c4` | VehicleProvingGrounds |
| B | `arena/d984ba1b-slate` | `c552cb26bf2be710baaefe64b830fe6db28f7ae7` | IlluminationHost |

A includes `c005f6e0be68254efc6315e53fa8ac5bd7ec8105` and its garden follow-up `8a70049`.
These branches diverged: A is not simply an upgrade over B. The shared native Engine and painting-reference files
have identical Git blob identities between these two pinned trees. Their exhibit code differs.

Preserved under `Experimental/SlateReference/`: both complete renderer exhibits, the complete 30-file Next/React
painting UI app, standalone paint prototypes, native C++ Engine sources and their supporting tools/build recipes.
These are pinned source snapshots, not nested Git repositories or a promise that the imported apps are production-ready.
See `Exhibits/RendererPlanning/Results/SlateSources.json` for all 1,149 source identities and SHA-256 fingerprints.

## Do we need a visibility raster?

**Ordinary visibility/depth rasterization: yes. A full visible-triangle ID buffer: not for the first CAD renderer.**
The latter stores surface/triangle identities and reconstructs shading attributes later. It is distinct from normal
GPU rasterization and from the acceleration structures used by ray tracing.

Legend: effort/cost ✔️ low, 🚩 medium, 🔴 high; suitability has more ⭐ = better for the first CAD milestone.
Costs are engineering expectations, not measured GPU timings.

| Approach | Effort | Extra work/storage | CAD-first suitability | Decision |
| --- | --- | --- | --- | --- |
| Hardware raster, depth + colour, topology-edge overlay | ✔️ | ✔️ | ⭐⭐⭐⭐⭐ | Start here |
| Attribute/G-buffer then deferred shading | 🚩 | 🚩 | ⭐⭐⭐ | Add only for a consumer that needs it |
| Full triangle-ID visibility buffer + attribute reconstruction | 🔴 | 🚩 | ⭐⭐ | Benchmark later, not a prerequisite |

A visibility buffer can reduce repeated expensive material evaluation and attribute bandwidth in suitable scenes.
For a small CAD preview it can instead add attachment traffic, indirection and reconstruction work. An RG32_UINT
identity target alone occupies about 15.8 MiB at 1920 × 1080 before depth, motion, coverage, MSAA or history.
There is no universal efficiency win. Hardware RT also does not require this buffer.

The native Slate visibility implementation is useful reference, not a finished dependency: its fragment shader
writes partition/triangle identities and occupancy; comments explicitly mark motion and cutout handling as absent.
Adopt stable identities and camera-relative precision now; defer its entire visibility machinery until warranted.

## A versus B — source-based comparison

Legend: suitability has more ⭐ = better for the later integration foundation; ranking is architectural, not FPS.

| Concern | A | B | Integration choice |
| --- | --- | --- | --- |
| Shared native C++ engine | Same pinned source | Same pinned source | No engine-level winner |
| Light binning | 16-entry cell lists; counts overflow and drops excess assignments | 32-bit cell masks for 32 lights; outside-grid all-light fallback | B for current bounded scene; scalable lists later |
| Light/bin buffer allocation | 84,016 bytes including light records | 6,176 bytes including light records | B uses less storage here, not proven faster |
| Glass | World-traced refracted leg; one interface, fixed 1.4 m skip | Additional screen-space backdrop/refraction step with fixed 0.5 m offset | Neither is final physical transmission |
| Material records | 48 bytes; dielectric IOR derived from F0 | 64 bytes; explicit IOR with shared stride constant | Explicit semantics; validate one new shared ABI |
| Surface identity | Packed with surface attributes; screen-trace mode suppresses derez glow | Separate identity attachment, with corresponding effect-target fix | Preserve explicit identity, use integer IDs natively |
| Triangle rays | Compute/software two-level hierarchy, reflections toggle | Compute/software two-level hierarchy, reflections toggle | Neither is Vulkan hardware RT |
| ReSTIR GI | Distance-field + surface-card gather/reuse | Distance-field + surface-card gather/reuse | Validate estimator before replacing visibility |
| Timing tools | Detailed copyable snapshot, including zero-duration work | 120-frame configuration matrix with summaries | Combine both; add warm-up and native GPU timestamps |
| Foundation suitability | ⭐⭐⭐ | ⭐⭐⭐⭐ | B structural base; A feature/test donor |

A bounded CPU reproduction with 32 huge-reach lights retained 16 per cell in A and all 32 in B.
A counted 16,384 dropped light/cell assignments in that test. This is not a frame-time benchmark.
B's mask is not a general solution for thousands of lights; it must grow into tested overflow-safe lists or multiple
words before the light count grows. Both approaches build their bins on the CPU in these exhibits.

Do not blindly transplant A's fixed transmission skip, B's screen-space refraction addition, half-float identities,
fixed-size traversal stacks, or unexamined reservoir weights into the production path. Test thin/thick interfaces,
entry/exit intersections, energy conservation, occlusion, capacity exhaustion and identity precision explicitly.

Local replay: **A 64/64, B 46/46 Node tests pass** after restoring the pinned Jolt dependency. Counts describe these
exact trees and commands, not equivalent test coverage. Earlier 58/58 and Dawn-clean statements are historical
source/user reports; Dawn and real-GPU rendering were not rerun for this planning review.

## Shared rendering shape

```text
Editor / Project Browser / standalone painting host
                       |
          shared Vulkan device + resource lifetime
                       |
       persistent geometry + camera + render target
                       |
      +----------------+-----------------+
      |                |                 |
 CAD surface/edges   MatCap          shared material evaluation
                                        |
                               preview PBR / full PBR
                                        |
                           optional ray visibility + ReSTIR GI
```

The primary painting reference is now **TextureStudio / Alloy** from `streamlinkinbox/Frontier`, pinned at
`773b2f5a2672ae530accff000d65d779e9059b66`, preserved in full under `Experimental/TextureStudio`.
The earlier Slate React/HTML painting references were the wrong application; they remain secondary references only.
The standalone painter and the embedded Frontier workspace must use the same document, material evaluation,
texture residency, undo and shader code. Separate application hosts are allowed; a second independent material
implementation or an iframe standing in for native integration is not the goal.

### Common rules

- Engine owns reusable Vulkan rendering, geometry residency, shading and materials. UI owns viewport controls.
- Extend the existing Vulkan presentation at a narrow offscreen-submission/image-registration boundary. Do not
  construct a second Vulkan device merely to show a browser thumbnail, or expose mutable device ownership to UI.
- Keep original geometry uploads resident; update transforms/camera separately. Rebase large translations in host
  precision before narrowing; handle non-uniform normal transforms correctly.
- Render offscreen, synchronize writes to shader sampling, then display the image through the existing ImGui adapter.
  Retire old images/descriptors only after all in-flight work completes. No per-frame device-idle waits.
- Compile `.slang` to SPIR-V. Share geometry/material declarations, validate layout and use reflected bindings.
  Keep authored descriptions in TOML; shader source is source code, not serialized material content.
- CAD, MatCap, preview PBR and full PBR are selectable modes/feature tiers, not copies of geometry or asset loading.
- Dirty thumbnails render on geometry/material/camera/size changes; batch requests and reuse cached GPU images.
- Specify scene-linear versus display-referred colour and perform the output transfer exactly once.
- No GUI GI/renderer controls are introduced into the Control Centre by this plan.

## Dependency order and acceptance gates

| Order | Deliverable | Acceptance before proceeding |
| --- | --- | --- |
| 0 | Rendering contracts + narrow Vulkan host boundary | Geometry/identity/target ownership; shader compilation and reflection; validation enabled |
| 1 | **CAD only** | Real topology edges, hidden-surface depth, orthographic/perspective, capture, resize/DPI and zero validation errors |
| 2 | MatCap | Offscreen core checked; native-host acceptance still open; same geometry/targets and normal-transform contract |
| 3 | Preview PBR | Shared material subset, GGX/direct light/environment, textures and colour management; no mandatory GI |
| 4 | Full materials + shader integration | Explicit closures/features, layering, alpha, transmission policy, shared ABI and safe compile failure |
| 5 | Painting app: standalone and embedded | Real strokes/UV projection, masks/layers, undo, material browser, persistence round trips |
| 6 | Native ray visibility + reference integration | Capability detection, BLAS/TLAS build/refit, hardware-ray and fallback agreement, no leaks |
| 7 | ReSTIR GI + reflection integration | Validated weighting, reuse/rejection, denoising, material edits, lighting changes and reference-image comparisons |
| 8 | Scale/optimization review | Hardware A/B timings justify any visibility buffer, GPU culling or further specialization |

Stages 5 and 6 may proceed independently after the material contracts stabilize. GI must see the same evaluated
materials as rasterization and painting; it must not silently use a separate approximate material identity table.
Do not delay the CAD milestone for the rest of this roadmap.

### CAD milestone — first implementation gate

- Depth-tested opaque surfaces followed by boundary/crease/authored curve edges. Hide tessellation diagonals in CAD
  mode; offer triangle-wire inspection separately later. Never label triangle soup as faithful CAD topology.
- Prefer an explicit topology-edge list and screen-space expanded strokes with antialiased coverage. Do not require
  optional wide-line support, geometry shaders, or `fillModeNonSolid` merely to draw CAD edges.
- Depth bias/edge visibility must match the chosen depth convention; do not copy bias signs blindly with reversed Z.
- Support orbit/pan/zoom, orthographic/perspective, resize and DPI. X-ray/hidden-edge display is an explicit later option.
- Start with a small real source geometry fixture in Exhibits, not a production creation preset. Blank remains the
  only native project preset. Geometry decoding must stay explicit; no fake successful CAD import.
- Check shared-edge deduplication, boundary/crease masks, transformed instances, near-plane clipping, very large/small
  coordinates, thin features, GPU image readback, thumbnail/viewer agreement and resource retirement.
- Record GPU/CPU times at thumbnail and viewport extents plus increasing geometry counts on named hardware.
  Success means correct images and bounded resource use first; no FPS guarantee is made without those measurements.

### Material, shader and painting preparation

The imported C++ project contains real material declarations, layer sequences, processing/dirty keys, image import,
texture upload/export and a Slang thumbnail shader. Inspect/reuse those seams rather than porting the whole editor.
That thumbnail shader is a simplified sphere/highlight calculation, not evidence of a complete production PBR path.
Preview and full PBR should share core BRDFs and material meaning while differing in enabled effects/sample budgets.

TextureStudio is the correct app: real texture layers, digital painting, packed UVs, masks, materials/patterns/stamps
and baking workspaces. Its existing project export is `.texture.json`, not an established `.pigment` painting format.
Its original compiled self-contained app is served on port 8774; local smoke checks cover the real teapot and
Lit/Wireframe switching. Native Vulkan conversion and full functionality validation are still future work.

The previously selected Slate React app contains layer/inspector UI and some simulated painting operations. `PaintWorkshop.html` contains
actual Three.js/WebGL/Canvas painting interactions and depends on a remote Three.js module. Neither is already
Frontier's native Vulkan painting application, and copying its source does not complete its integration.

The reference `.pigment` codec is a typed binary fixed-dielectric declaration: name, base colour, roughness, IOR and
one dielectric boolean. It explicitly excludes image/layer processing. It cannot currently save a complete painting
session. Before implementation, define versioned authored material/layer/texture references and recovery/undo rules,
using the project's approved TOML authoring policy. Reconcile the existing binary `.pigment` through an explicit
compatibility decision, not a silent overwrite or a new format invented while writing a browser.

Standalone and embedded hosts must round-trip the same content; shader/material edits invalidate the correct atlas
regions and GI history. Thumbnails use the same material evaluation but must not require full-scene ReSTIR to load.

### ReSTIR and ray tracing preparation

Use the source exhibits as behavioural references, not prevalidated native hardware-RT code. Port WGSL algorithms
into the shared Slang/Vulkan implementation. Start with a deterministic direct/path reference before temporal reuse.
Then validate candidate PDFs and reservoir weights, temporal reprojection, disocclusion/material rejection, spatial
reuse visibility, denoising and reset conditions against that reference. A reservoir implementation is not made
unbiased or production-ready merely by retaining the ReSTIR name.

Use Vulkan acceleration structures and ray queries where supported for the first hardware-RT visibility route;
consider a full RT pipeline only when shader needs justify it. Keep capability-negotiated alternatives and explicit
unsupported diagnostics for non-RT devices. CAD and MatCap remain operational without RT extensions.
Screen-space tracing remains optional assistance, not a substitute for off-screen geometry or correct world tracing.
Surface-card/distance-field approximations remain deliberate quality choices, not assumptions inherited by every mode.

## Evidence and boundaries

- Pinned sources: `Exhibits/RendererPlanning/Results/SlateSources.json`.
- Local Node results: `ReferenceChecks.json`, `A-Tests.tap`, `B-Tests.tap` in that Results directory.
- Reproducible light-bin comparison: `Exhibits/RendererPlanning/Code/CompareReferences.mjs` and its JSON result.
- No new native renderer, shader, GUI renderer controls or format migration was implemented in this planning turn.
- No hardware timing, Windows/Vulkan execution, Dawn rerun, native scene capture, commit, push or Actions is claimed.


## Native ControlVehicle rerender after recovery — 2026-10-24

The recovered project and audit are saved in GitHub checkpoint `12344d0b` on the session branch.
`Exhibits/VehicleReference` newly renders the original 25,068-facet ControlVehicle in native C++, with the
original material table, Sun, atmospheric sky, clouds and three rigid poses. This is a CPU reference run,
not recovery of the missing Vulkan/Slang implementation or a physical GPU result.

Continued renderer work fixes off-camera shadow casters: scene-wide `TriangleVisibilitySpace` replaces
primary-frustum-only shadow geometry. The old renderer fails the targeted off-camera regression; the repair
passes it, 8,192 exhaustive long-double interval comparisons and the existing 18 renderer checks.
The recovered illumination/indirect/environment suites also pass 188/188 checks.

Next integration remains the native Vulkan/Slang execution route, with scene-wide geometry visibility and
independent references retained. The new CPU capture does not re-establish the former approximately 60%
integrated-renderer completion claim. GPU execution, moving history across poses, indirect vehicle baking and
Windows runtime still require their own evidence. Moon requirements remain in `Documentation/EnvironmentTodo.md`.


## Unsigned triangle-distance lighting — 2026-10-24

The actual 25,068-facet ControlVehicle now bakes a 64×32×24 unsigned distance volume per pose, with conservative
Lipschitz interpolation strides and exact triangle hit refinement. Native CPU environment MIS proposals now
replace occluded sky with shadowed direct-lit/emissive facet radiance: a one-bounce RGB reference, not multibounce
radiosity. Three matched field-off/on pairs are retained in `Exhibits/VehicleReference`.

Independent long-double proximity/ray tests, actual saved-vehicle bake probes and RGB transport checks pass.
The disabled controls remain identical to the preceding committed captures on this Linux configuration.
Bright reflections remain visibly noisy at the current sampling rate; estimator convergence/variance filtering
and geometry-light proposal sampling need validation before quality sign-off. Vulkan/Slang integration,
physical GPU execution, motion history across poses and Windows runtime are still separate acceptance work.
All source, tests and native proof must be committed and pushed to the session branch with `[skip ci]`.

## Conservative native analytic clustering — 2026-10-24

Both pinned prototypes cluster analytic lights; no distinct native emissive-mesh clustering implementation was
found. A drops excess cell assignments, while B retains its bounded 32-light set. Both use synthetic angular
profiles rather than importing measured IES. Keep the pinned references and existing emissive transport.

`LightInfluenceSpace` now supplies conservative native RIS candidates with global reservoir IDs, normalized local
proposal mass and exhaustive overflow/outside-grid fallback. Point, oriented spot and an explicit Type-C LM-63
subset execute in the CPU reference. Profiles are relative-peak intensity shapes, not automatic lumen calibration;
the test fixture is synthetic. Separate mesh-emitter clustering is optional future area-emitter sampling, and a
shared-fixture double-counting policy is not supplied by simply retaining both emission and analytic proxies.

`Exhibits/ClusteredLighting` contains the audit, native tests and matched 36-light ControlVehicle captures:
identical display RGB, 8,107,704 versus 2,682,703 candidate-list visits. This is no GPU timing or speedup claim.
Independent membership probes, fresh-RIS statistical comparison, IES/spot checks, inactive-proxy emission,
4/4 CTest targets, 188 recovered core checks and focused sanitizers pass. Six prior vehicle/distance captures
were rerun with unchanged controls. Four-frame static reuse is finite, not a motion-history correctness proof.
The native Slang/Vulkan, RTX/BVH, moving-history/filtering and Windows/device acceptance phase remains outstanding.

## Executed Vulkan analytic lighting — 2026-10-24

`Exhibits/LightCompute` now executes the shared `LightInfluence.slang` through real Vulkan compute on SwiftShader
CPU device type 4. It builds conservative light cells and evaluates point/spot/Type-C profile lighting, GGX,
full-scene triangle-BVH shadow visibility and fresh RIS. Native primary raster and residual sky/indirect/emissive
lighting remain CPU inputs. This is not the production editor viewport or a complete GPU renderer.

The original ControlVehicle matches the double-precision native reference to maximum linear RGB error 6.44028e-5;
183 display pixels differ by one code step. Vulkan global/clustered/forced-capacity output buffers are identical.
The 97-light/256-seed sampling reference, forced triangle-stack fallback and offscreen caster checks pass.
Compiled Slang, explicit device identity, pinned dependencies, actual readbacks and source hashes are retained.
No physical GPU, hardware RT, GPU temporal/spatial reuse, moving history, GPU filtering or Windows execution is
claimed. Next integrate GPU reservoirs/reprojection/filtering with independent exact references; preserve the
hardware ray-query and shader triangle-BVH routes as explicit later device acceptance requirements.

## Executed Vulkan history and moving reconstruction — 2026-10-24

Shared Slang now executes temporal/spatial reservoir reuse, previous-surface reprojection checks, current
visibility reevaluation, temporal moments and two edge-aware à-trous iterations. Native export supplies stable
surface tokens and rigid previous-position/normal/projection inputs. Exact byte comparison of light/profile
updates resets history; current residual sky/indirect/emissive radiance bypasses old direct-light colour history.

`Exhibits/LightCompute` retains a lossless six-pose Vulkan animation, fresh/reconstructed comparison and GPU
history rejection map. After sixteen static updates RMSE falls 69.08%; over five moving updates mean per-image
RMSE falls 56.04% against exact current lighting. A colour change at pose 3 resets all history. Material, normal,
position and camera-cut tests reject stale inputs; light-off and residual-refresh results exactly match current
references. Unfiltered reused reservoirs independently match exact energy within fixture sampling error.

Device remains SwiftShader CPU Vulkan, not physical GTX/RTX or Windows. The reconstruction is deliberately
biased, sampled visibility rejection is not an all-light change detector, and remaining shadow/specular noise
prevents final quality sign-off. Primary raster and sky/indirect remain CPU; production viewport, deforming
motion, hardware RT and physical-device/Windows acceptance remain separate integration work.

## Fog shadow quadrature refinement — 2026-10-25

Two fixed-camera ControlVehicle/wind fixtures compare 16/32/64/128 shadow samples at 32 view midpoints.
Actual 160×100 readbacks and 936 independent native probes are retained in `Exhibits/LightCompute`.
64-step full-image RGB RMSE against finite 128-step quadrature decreased 96.57% / 96.39% versus 16;
the exhibit preset is now 64. This is 4× shadow samples, not a speed or full-convergence claim.
`Results/FogSampling.json`, `Images/FogSamplingComparison.png` and `RunFogSampling.sh` retain/reproduce it.
Earlier 320×200 evidence remains unchanged at 16 steps. Historical medium prose was incorrect: source and
captures actually used 0.0015/m and 450 m, not 0.0007/m and 650 m. Metadata corrected, images unchanged.
View marching/aliasing, temporal fog, coupled/multiple scattering, final cloud appearance, Moon and physical
GPU/Windows acceptance remain open; preserve both lighting paths and existing Sun/flare/star code.

## Emissive-volume single scattering — 2026-10-25

Shared Slang fog now optionally gathers emissive triangle radiance, not just emitter-to-surface attenuation.
Area/luminance CDF samples use solid-angle conversion, HG phase, triangle visibility and analytic source-to-volume
extinction. Both lighting types remain. Fog[15] selects 0–64 samples; default 0 preserves older presets.
Actual 160×100 original-ControlVehicle/ground captures add an explicit 2×2 m calibration panel (not a new model
or hidden source boost). Isolated emissive-only and combined analytic/emissive volume lighting are retained in
`Exhibits/LightCompute/Images/FogFacetComparison.png` and `FogFacetCombined.png`. 468 capture and 448 sanitizer
native camera probes passed; capture maximum RGB error 1.59192e-5. A 131,072-sample native rectangle integral
matches independent analytic solid angle within 0.0446%. Cold/zero-density/empty-CDF equality, surface isolation,
geometry occlusion and invalid inputs checked; external-driver leak checks disabled.
Reproduce serially with `RunFogFacets.sh`; see exhibit README and `Results/FogFacets.json` for exact scope.
Emissive-volume single scattering is no longer wholly absent, but remains opt-in and finitely sampled. Multiple
scattering, coupled inter-bounce transport, temporal fog, final clouds, Moon and physical GPU/Windows/viewport
acceptance remain open. Sun/flare/stars are unchanged. No speed or real-time claim.

## Working cloud budget and material source decision — 2026-10-25

User requested lower cloud sampling for iteration. Current working fog scenes use cloud32 and fog-shadow32,
self-shadow6 and view32. This supersedes the former 64-shadow working preset. High-budget fixtures remain
explicit reference tools; `RunCloudPreview.sh` defaults to low-only, `COMPARE=1` requests reference renders.
Actual two-wind 160×100 comparison RMSE to finite64: 0.00207406 / 0.00187121; appearance inspected and accepted
for iteration, not final quality. Fresh Release/native tests passed, no fresh sanitizer or hardware-speed claim.
See `Exhibits/LightCompute/README.md` and `Results/CloudBudget.json` / `CloudPreview.json`.

Reuse materials from SultanAladin/Frontier arena/5e581235-frontier, **except rewrite glints/flakes/glitter**.
Pinned source audit at be1d0a8e45d39738f37ccf711522a745e326e149: 452/455 selected files already identical,
including all 399 material archive files and the material/paint shaders. Three newer upstream vector-gradient
files use a different ABI; no partial import. Full layered material evaluation still needs scene integration.
Existing glint research and executed adapter comparison are retained; no physical-GPU winner or completed
rewrite is implied. Licensing-safe implementation, sampling/PDF consistency and temporal/energy checks remain.
Audit/reproduction: `Exhibits/TextureStudio/MaterialReuse.md` and `Code/MaterialReuseAudit.py`.
