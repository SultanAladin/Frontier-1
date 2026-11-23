# PLAN — Sun Shadow Clipmap (P6): Virtual Shadow Maps + SMRT Soft Shadows

🧩 Implementation plan for Frontier's sun shadows: an EEVEE-Next-class **software virtual shadow map**
(sparse page atlas, camera-tracked light-space clipmap) sampled by **SMRT** — shadow-map ray tracing —
rather than PCF/PCSS filtering. Targets the NVIDIA Pascal floor (GTX 1060 6 GiB): no hardware ray
tracing, no mesh/task shaders, no ML denoiser.

> **Headline:** port EEVEE-Next's software VSM, not UE5's hardware-assisted one. Two purpose-built
> windows, **not** one shared voxel field — `ToroidalClipmapField` keeps all 8 signatures
> byte-identical and stays the GI probe primitive, while a new 2D **light-space** `SunShadowClipmap`
> hosts the sun pages. Sun shadows are **ray-marched**, never depth-compare-filtered:
> 4 rays × 8 steps = 36 taps/pixel worst case. `ShadowPageAtlas` is `R32_UINT` + `imageAtomicMin`
> (core VK 1.0 — it does **not** need the probed int64 path).

⚠️ This plan supersedes the shadow sketches in `EngineDocs/PLAN-VisibilityRenderer.md` §7 and the
"shared spine" premise in `Documentation/REFERENCE-ToroidalClipmapGI.md`. Where they disagree, this
document is authoritative — see §3.

---

## 1. What SMRT is (and is not)

**SMRT = Shadow Map Ray Tracing.** For each shaded pixel, cast a small fan of rays from the shading
point toward the light and *march each one through the shadow depth pages*, looking for the step where
the ray passes behind the recorded depth. That crossing is an occluder intersection.

| | PCF / PCSS | SMRT |
| - | ---------- | ---- |
| Operation | average N depth **comparisons** in a kernel | **march** a ray, find the depth crossing |
| Penumbra source | kernel width heuristic | ray-fan geometry — falls out of the light's solid angle |
| Contact hardening | needs a separate blocker-search pass | 🟢 free — near occluders are hit on early steps |
| High-res geometry | 🔴 over-blurs (both Epic and Blender rejected PCF for this) | 🟢 holds detail |
| Hardware need | none | none — compute over a depth texture. **No BVH, no RTX.** |

💡 "Rays" here traverse a **depth texture**, not a scene BVH. This is why it runs on Pascal at all.

Two structural limits are inherited and must not be "fixed" in the shading math:

- **Single-layer depth.** A page stores one depth per texel, so the tracer sees only the nearest
  occluder along the light ray. Thin double-walled geometry can leak.
- **Quadratic iteration.** Cost is `RayCount × StepCount`. 4 × 8 = 36 taps worst case (32 marches +
  4 setup); this is the budget knob.

---

## 2. Storage model

Three-level indirection, all verified against EEVEE:

```
world / light space
      ↓  (light basis + level select)
ShadowTileStore          32×32 tiles per level, LODs 0..5   ← which pages are wanted
      ↓  (allocation)
ShadowSamplingTileStore  packed 32-bit words                ← where each tile's page lives
      ↓
ShadowPageAtlas          shared physical page pool, R32_UINT ← the depth itself
```

| Quantity | Value | Source |
| -------- | ----- | ------ |
| `SHADOW_PAGE_RES` | **256** | EEVEE `eevee_defines.hh` (⚠️ the 128 in prior Frontier notes is UE5's number) |
| `SHADOW_TILEMAP_RES` | 32×32 tiles | EEVEE |
| `SHADOW_TILEMAP_LOD` | 5 (LODs 0..5 = 6 levels) | EEVEE |
| Atlas format | `R32_UINT` | `imageAtomicMin` needs uint; core VK 1.0 |
| Atlas clear identity | `0xFFFFFFFF` | atomic-min identity |

🔴 **Page ownership is three-state** (free / cached / used), not two. A cached page retains its depth
across images and is reclaimed only under pressure — this is what makes a scrolling camera cheap.

⚠️ **Pool exhaustion is the failure mode to design against.** When allocation outruns capacity,
shadows vanish *in whole blocks* (a page either exists or does not). Mitigation: a `PageCensus`
readback that warns when `PageUsedCount > ShadowPageCapacity`, read one image stale (§4, C6).

---

## 3. 🔴 The clipmap substrate ruling — why the 3D field is NOT reused

### 3.1 The ruling

> **A separate 2D light-space `SunShadowClipmap` is built. `ToroidalClipmapField` is not modified at
> all** — all 8 public signatures stay byte-identical, and it remains the GI irradiance-probe
> primitive for P7b. The two share only **header-only** integer addressing helpers
> (`ToroidalAddressing.h`).

### 3.2 The one fact that decides it

The deciding factor is **coordinate frame, not dimensionality.**

| Structure | Addressed in | Basis behaviour |
| --------- | ------------ | --------------- |
| Sun shadow pages | **light space** — `tilemap_uv = lP.xy - clipmap_origin`, where `lP = SunView * P` | 🔴 **rotates with the sun** |
| GI probe voxels | **world space** — `ResolveCameraCell` floors world XYZ (`ToroidalClipmapField.h:110`) | 🟢 rigidly world-axis-aligned |

There is **no assignment of meanings to `XCell`/`YCell`/`ZCell` that makes a world-aligned lattice
light-aligned.** Unifying them would require adding a `LatticeBasis` rotation member, which poisons
every world-aligned consumer.

💡 This conclusion was reached independently by two fit studies, including one *assigned to argue for
generalisation*, which concluded its own position was wrong.

### 3.3 The "shared spine" premise is false — the docs are wrong, not the code

⚠️ `REFERENCE-ToroidalClipmapGI.md` and `PLAN-VisibilityRenderer.md` §7.3/§8.3 promise that one
toroidal spine serves both shadows and GI. That promise does not survive contact with the light basis.
The honest scope of sharing is **two integer helpers plus one span solver** — nothing more.

Also verified: `EvaluateClipmapScroll` in `ToroidalClipmapField.cpp` **already** contains the
generic 3-axis loop (`AxisDeltas[3]`, `SpanMin[3]`/`SpanMax[3]`). So there is no extraction work to
do, and `ToroidalAddressing` must be **header-only** — two `Build.bat` files
(`ClipmapFieldValidation`, `TriangleCellOverlapValidation`) compile `ToroidalClipmapField.cpp`
standalone, so a new sibling `.cpp` in that folder would break them.

### 3.4 Residual risk

🚩 **Dual-scroll divergence.** Two windows now scroll on different rules (one world-tracked, one
light-tracked). They can disagree about what is resident. Accepted, because they have **no shared
consumer**: shadows read the atlas, GI reads the probe field.

---

## 4. Per-image pipeline

### 4.1 Placement

🔴 Every shadow GPU step lands in the **offscreen preamble**, before the colour scope opens — the
atlas is a storage image and the colour scope has no attachment for it.

```
RecordPreamble  (RenderExtension.cpp:987 — OUTSIDE any rendering scope)
 ├── 🚧 C1–C6  CPU sun-window advance   ← at the TOP, before early cull
 ├── ① early cull · ② visibility raster (heads + floor) · ③ depth → sampled
 ├── ④ HiZ reduce · ⑤ late cull + re-raster · ⑥ pick copy · ⑦ id → sampled
 └── 🚧 S1…S8  the shadow chain
RecordSequence  (:1253 — INSIDE the open colour scope)
 ├── IntegrateClipmapField :1533   ← GI field only; NOT the sun window
 ├── sky · grid · visibility resolve
 └── surface shade :1578  ← 🚧 consumes shadows
```

⚠️ `IntegrateClipmapField` stays at `:1533`. It has no P6 consumer; moving it would perturb the P5c
gate.

🔴 `RecordPreamble` needs the observer position, today computed in `RecordSequence`. Call
`EvaluateObserverPosition` at the head of the preamble, cache it, and have `RecordSequence` read the
cache. `DriveViewportCamera` must also move ahead of the preamble or both consumers read a camera one
image stale.

### 4.2 CPU steps

| Step | Operation |
| ---- | --------- |
| C1 | `SolveSunShadowBasis` from `SolarDirection` → orthonormal light frame (Gram-Schmidt against world +Z; fall back to +X within 1e-3 of vertical) |
| C2 | `EvaluateSunShadowScroll` per level → `TileShift` + up to **2** exposed L-strips (2D) |
| C3 | `IntegrateSunShadowResidency` → toroidal permutation + strip-only dirty marking |
| C4 | Whole-window invalidation on light rotation / level-range change → `WholeWindowDirtyCondition` |
| C5 | Swap `PreviousCasterTransform` ← `CurrentCasterTransform`; rebuild `CasterBoundsStore` |
| C6 | Read the stale `PageCensus`; warn on over-subscription |

⚠️ C4 is a **separate coarse path** from C2's scrolling. Do not merge them. Trigger:
`dot(SolarDirectionThisImage, SolarDirectionPrevious) < cos(1e-4)`.

### 4.3 GPU steps

| # | Unit | Type | Dispatch | Writes |
| - | ---- | ---- | -------- | ------ |
| **S1** | `MarkVisibleShadowPages` | compute | `ceil(W/8) × ceil(H/8)`, local 8×8 | `ShadowTileStore` — `atomicOr` used-bit |
| **S2** | `ShadowTileTagInscription` | **raster** | instanced **sphere proxy** (⚠️ not OBB — see below), 2 sub-draws (previous + current bounds) | `ShadowTileStore` — `atomicOr` `Update` only |
| **S3** | `ShadowTileLevelPropagate` | compute | 🔴 **one `1×1×1` dispatch per level, ascending, barrier between** (not `1×1×LevelCount`), local **32×32 = 1024** | `atomicOr` `Used\|Coarse`, up LODs 1..5 |
| **S4** | `ShadowPageFree` | compute | `1 × 1 × LevelCount`, local 32×32 | `PageCachedRing`, `PageFreeHeap` |
| **S5** | `ShadowPageAllocate` | compute | `1 × 1 × LevelCount`, local 32×32 | `ShadowSamplingTileStore`, `ShadowPageRenderTable`, `PageCensus` |
| **S6** | `ShadowPageClear` | compute | **indirect**, local 32×32 | atlas ← `0xFFFFFFFF` |
| **S7** | `SunShadowDepthRasterization` | **raster** | **indirect**, one draw per dirty-page rect | atlas via `imageAtomicMin` |
| **S8** | `ShadowPageAtlasReadTransition` | barrier | — | atlas GENERAL → SHADER_READ_ONLY |

🔴 **S1 reads the id buffer, not the depth sentinel.** Where nothing rasterized, depth holds the clear
value and reconstruction yields a far-plane point that over-marks distant tiles. `VisibilityImage`'s
`0xFFFFFFFF` sentinel is unambiguous and survives a future reverse-Z switch; the depth clear
convention does not.

🔴 **S2 is deliberately raster, not compute.** Upstream: *"done in 2 pass of this same shader. One for
past object bounds and one for new object bounds"* — so both vacated and newly-occupied tiles go
dirty. Three subtleties:

| Detail | Rationale |
| ------ | --------- |
| `CULL_FRONT` | avoids **double-tagging** (front/back faces cover identical tiles). Not a coverage guarantee. |
| `OutPosition.z *= 1e-5` | **this** guarantees coverage — pancaking flattens far/behind geometry into the frustum. Legal because fragment depth is irrelevant when the shader only writes an SSBO. |
| Conservative raster | **emulated in the vertex shader** (expand projected bounds ~1 px; NDC pixel = `2.0/32`). No `NV_conservative_raster`. |

🔴 **S2 rasterizes a SPHERE, not an OBB, and the "instanced OBB" wording above was unbuildable.** The
only bounds producer in the pipeline is `InstanceCull`'s `PartitionCullRecord`, which carries a
world-space bounding **sphere** (`SphereXYZ` + `SphereRadius`) plus a normal cone — there is no OBB
anywhere to instance, and a sphere has no orientation to derive one from. The substitution is
conservative in the safe direction: a sphere's light-space silhouette is a circle of exactly
`SphereRadius` under *every* sun angle, so the quad never **under**-covers. It over-covers a thin or
elongated caster, costing redundant redraws — never a missed update, which would be a stale shadow.
An OBB path would need a new bounds producer (unbuilt).

🔴 **S2's "previous bounds" sub-draw still has no data source.** `SuzanneSceneInstance` carries no
previous transform and the cull upload runs only in the scene-load branch, so today only the
*current* sub-draw can be issued and **a caster that MOVES will not invalidate the page it left
behind**. The shader is already correct for both sub-draws and needs no change when the data lands.

⚠️ S3's local size 32×32 = 1024 is **exactly** Pascal's `maxComputeWorkGroupInvocations`. Zero
headroom — never add a Z dimension to the *local* size (Z lives in the dispatch).

🔴 **S3 cannot be one `1×1×LevelCount` dispatch, and the original row said otherwise.** Level *N*'s
inherited demand must be visible when *N+1* is processed, or LOD 0's demand stops at LOD 1 instead of
reaching LOD 5. Vulkan guarantees no ordering between workgroups and `barrier()` synchronizes only
*within* one, so a single dispatch runs all six levels concurrently and propagates one level deep,
**nondeterministically**. The ordering must live in the recorder: one dispatch per level, ascending,
with a `vkCmdPipelineBarrier` (`SHADER_WRITE → SHADER_READ`) between consecutive dispatches;
`gl_WorkGroupID.z` selects the level.

⚠️ **`ShaderPlan.ps1` now tracks the shader include graph.** These are the tree's first shaders to
`#include` anything, and the old gate compared source mtime to output mtime only — so editing
`ShadowTileStore.glsl` left every dependent `.spv` looking up to date and the **old** binary shipped,
carrying stale bit values against the new C++ ones. Fixed with `glslc -MD` depfiles (`<output>.spv.d`,
git-ignored). Verified: touching only the include recompiles exactly the 4 dependents and skips the
other 44; deleting it fails all 4 loudly with exit 1 rather than shipping the stale module.

---

## 5. The SMRT shader

Per shaded pixel:

1. Build a **uniform solid-angle cone** around the light direction —
   `sample_uniform_cone(random_2d, cos(SunAngularRadius))`. ⚠️ This is a cone, **not** a light-space
   disc; a disc mis-shapes the penumbra.
2. For each of `ShadowRayCount` rays: march `ShadowStepCount` steps through the pages, comparing
   ray depth against stored depth to find the crossing.
3. Average the per-ray occlusion → `SunVisibility ∈ [0,1]`.

| Constant | Value | Note |
| -------- | ----- | ---- |
| `ShadowRayCount` | 4 | EEVEE's default dropped to **1 ray × 6 steps** after PR #121317; 4 × 8 is chosen here for a still image without TAA |
| `ShadowStepCount` | 8 | |
| Taps worst case | **36** | 32 marches + 4 setup |

🔴 **Every bias is a multiple of one texel radius.** `ShadowTexelRadius` derives from the level's
world texel size; three stacked receiver offsets (normal, slope, depth) are each expressed in those
units, so a level change rescales all of them coherently.

⚠️ **fp16 collapses these offsets.** Keep the tracer's position math in fp32.

💡 No denoiser. With no TAA in Frontier, ray/step counts are the only quality knob — which is why 4×8
is the starting point rather than EEVEE's 1×6.

---

## 6. 🔴 Four hard prerequisites

| # | Blocker | Verified evidence | Consequence |
| - | ------- | ----------------- | ----------- |
| **B1** | ✔️ **CLEARED (P6.0)** — was OFF | `VulkanHost.cpp` now probes `vkGetPhysicalDeviceFeatures` and sets `EnabledFeatures.fragmentStoresAndAtomics` when advertised, recording the verdict on `Host.FragmentStoresAndAtomicsEnabled` | S2 **and** S7 may now write from a fragment shader. On a device lacking the bit the flag stays false and those passes must bypass — requesting it unconditionally would fail `vkCreateDevice` outright and take the whole renderer down, not just the shadows |
| **B2** | ✔️ **CLEARED (P6.3a)** — was discarding the floor | The `discard` is replaced by a branched fetch from the floor's own b5/b6/b7; only an *unbindable* floor still discards | 🔴 The floor is the surface shadows must land on — it is now a genuinely lit surface in the radiance target rather than a skipped partition range |
| **B3** | Linear-HDR intermediate | shade writes `EncodeSrgb(TonemapPbrNeutral(...))` straight to `B8G8R8A8_UNORM` | shadows must attenuate **linear** radiance, before the operator |
| **B4** | Shader target-env registration | `Automation/ShaderPlan.ps1` exists; auto-discovers `Internal/**/Shaders/*.{vert,frag,comp}` | a wrong env yields an **unloadable** module |

### 6.1 B1 — the probe

🔴 This is **not** the int64 path. It is a core `VkPhysicalDeviceFeatures` bit, universally supported
on Pascal, that Frontier simply never turned on.

```cpp
// VulkanHost.h — beside ShaderImageInt64AtomicsEnabled
bool FragmentStoresAndAtomicsEnabled = false;   // [-] - fragment-stage SSBO / storage-image writes (S2 tag, S7 atlas raster)

// VulkanHost.cpp — in the EnabledFeatures block, after geometryShader
VkPhysicalDeviceFeatures SupportedFeatures = {};
vkGetPhysicalDeviceFeatures(Host.PhysicalDevice, &SupportedFeatures);
Host.FragmentStoresAndAtomicsEnabled = SupportedFeatures.fragmentStoresAndAtomics == VK_TRUE;
if (Host.FragmentStoresAndAtomicsEnabled)
    EnabledFeatures.fragmentStoresAndAtomics = VK_TRUE;
```

⚠️ One bit covers both stores and atomics in the fragment stage.
`vertexPipelineStoresAndAtomics` is **not** required — neither vertex shader writes.

### 6.2 B2 — getting the plane shaded so shadows are visible

Verified comment: *"The floor's geometry lives in a different vertex/index buffer than the heads',
which this pass does not bind."*

| Route | Mechanism | Verdict |
| ----- | --------- | ------- |
| ① **Bind the floor as a second mesh channel** — b9/b10 floor vertex/index, b11 floor instances; branch on `Partition >= FloorPartitionBase` to pick the pair; drop the `discard`; gate the record on `HeadsPresent \|\| FloorPresent` | 2–3 new SSBO bindings + one `if` around the fetch | 🥇 **RULED** |
| ② Shadow the floor inside `GroundGridPass` | the grid is an analytic ground plane, not the checkered floor mesh — wrong surface | ✖️ rejected |
| ③ Merge floor + head geometry at load | touches the decoder, the partition-base scheme, and `InstanceCullSubmission`; breaks the disjoint-range identity contract | ✖️ rejected → Backlog |

⚠️ The floor's instance records already carry `MaterialId`, so `SurfacePresetTable` works unchanged.
**This is a visible pixel change independent of shadows** — which is exactly why it lands as its own
phase (P6.3a) with that A/B as its gate.

### 6.3 B4 — target env per new shader

🔴 **Ruling: no `$TargetEnvForShader` entry is added for any P6 shader.** All ten sit on the
conservative `vulkan1.0` default. Per the script's own comment, that means a new shader "can only fail
LOUDLY at compile time rather than quietly producing a binary the driver refuses to load." That is the
desired failure mode. `imageAtomicMin` on `r32ui` is core GLSL 4.20 / SPIR-V 1.0.

⚠️ `ShadowTracing.glsl` is an `#include` body, **not** a compile target. `ShaderPlan.ps1`'s
`-Include '*.vert','*.frag','*.comp'` already excludes `.glsl` — but the extension choice is
load-bearing and must stay `.glsl`.

⚠️ `ShaderPlan.ps1` writes each `.spv` **beside its source**; the per-target `Build.bat` copies it
into `<exe>/Shaders`. `RenderExtensionValidation/Build.bat` needs a copy line for
`Internal/Graphics/Shadow/Shaders/*.spv`.

---

## 7. Phase order and gates

Each phase has one gate. 🔴 = blocking prerequisite for everything after it.

| Phase | Work | Gate |
| ----- | ---- | ---- |
| **P5.9a** | ✔️ Swapchain → `_SRGB`; delete the manual 2.2 gamma | 🟢 done — image unchanged except correct low-end; two `pow` calls gone |
| **P5.9b** | ✔️ Linear HDR target + single resolve; **sky and shade** write linear; `Exposure` into the push block | 🟢 **done — completed 2026-07-30, after being marked 🟢 prematurely once.** 🔴 **The defect the premature mark hid:** `RadianceResolve.frag:7-8` claimed *"Both now write linear radiance and only this pass maps"* while `SurfaceShade.frag` still called `TonemapPbrNeutral` and `SkyDome.frag` still applied its own ACES-ish curve — both into the linear target the resolve maps again, so **every scene pixel was tone-mapped twice** (`RecordSurfaceShadeInscription` at `RenderExtension.cpp:1527` records inside the radiance scope opened at `:1497`; neither call was guarded). Worse, the sky curve ended in `clamp(Mapped, 0, 1)`, **destroying all >1.0 sky radiance before it reached the float target** — the sun disc and bright horizon headroom the HDR target exists to hold, unrecoverable by any downstream exposure. Both curves are now removed; `RadianceResolve.frag:79` is the **only** tone map left in the shader tree (verified by grep). ✔️ Verified on device: 180 frames + clean shutdown, validation layer active, **zero VUID**, log structurally identical to the P6.3b baseline apart from the intended change (the 4 unconsumed-vertex-attribute cautions are pre-existing and unrelated). ⚠️ Two corrections to the row as planned: the **grid does NOT write linear** (display-referred, draws after the resolve per the two-scope decision), and `SkyDome`'s `Push.Exposure` is now **deliberately unused** — exposure is applied once at the resolve, so applying it in the dome too would scale sky against geometry. 📝 The matcap branch is display-referred by nature and must not be mapped, so it now writes through `InvertPbrNeutral` — the analytic inverse — letting the resolve's forward curve cancel it. Exact only under the PBR Neutral operator; see the ⚠️ in that branch and the Backlog row for the F5-bypass caveat. ✔️ Gate wording (critique **G16**) **ruled**, and the previous *"identical for opaque"* phrasing **withdrawn as false**: PBR Neutral subtracts its black-point offset *before* the early-out, so no non-black pixel survives unchanged, and with P5.9a's pure-2.2 → piecewise-sRGB change the opaque delta reaches ~35 code values. The gate is stated against **newly-blessed goldens, never against the old pipeline** — full ruling + sources in `EngineDocs/Backlog.md`. ✔️ **G16 SUPERSEDED 2026-07-30 — the goldens are no longer required, and the replacement is stronger.** A golden image can only report *"different"*, which was already known; it cannot report *"correct"*. Because the tone map is a **pure function of one pixel** (no neighbourhood, no history, no view dependence), its correctness is decidable analytically. `_ClaudeScratch/tmp/ToneMapProbe.cpp` now gates it with **27/27 checks and no image, no GPU, and no reference files**: the shader's inlined constants are proven equal to the Khronos spec's own `F90 / Ks / Kd` parameterization to **1.1e-16** (an *independent* derivation, so a mistyped constant diverges), 6.25 is shown to be exactly `1/(4·F90)`, and the spec's documented properties are verified directly — pure `-0.04` subtraction throughout `[0.08, 0.8]`, gradient continuity across both knees, monotonicity over `[0, 100)`, boundedness below 1.0 to 1e12, black→black, and no channel-order swap. 🔴 The decisive addition is an **IDEMPOTENCE** assertion: a tone map satisfies `f(f(x)) ≠ f(x)`, so a second application is detectable algebraically — **298 of 299 samples shift >1 code, always darker**. Had that assertion existed, the double map would have failed on its first run instead of shipping behind a header comment claiming it was fixed. The probe also **parses the three shaders** and asserts structurally that only the resolve maps, that no inverse remains, and that the sky writes unclamped — so the regression cannot return silently |
| **P6.0** | ✔️ 🔴 Probe + enable `fragmentStoresAndAtomics` | 🟢 done — `VulkanHost::FragmentStoresAndAtomicsEnabled` queried via `vkGetPhysicalDeviceFeatures` and enabled only when advertised; reported at bring-up beside the int64 notice. ⚠️ Reported from the HOST (what the device actually turned ON), not from `HardwareFeatureProfile` (what the hardware merely *can* do) — the two can disagree and only the former makes a fragment-stage write legal |
| **P6.1** | ✔️ `SunShadowClipmap` — 2D light-space window + header-only `ToroidalAddressing.h` | 🟢 done — **29/29 CPU checks pass**, max **2** strips observed across 441 scroll cases, wrap round-trips over -200..200. `Graphics/Build.bat` globs `*.cpp` recursively, so the new `Shadow/` unit needed no registration. ⚠️ Three corrections to the row as planned: `RefreshSunShadowClipmap` commits every level **before** refreshing `Basis` (refreshing first makes the rotation test compare the sun against itself, so C4 never fires); C4 is kept a **separate coarse path** that reports `WholeWindowDirtyCondition` with **zero** strips rather than a strip solve; and the false *"shared spine"* claim at `ToroidalClipmapField.h:8` was corrected — that field is world-space and serves P7b only |
| **P6.2** | 🟡 `ShadowPageAtlas` + page pool (S4/S5 **CPU mirror**) | 🟡 **partial — pool + atlas image done, gate met; S6 compute clear deferred to P6.4.** 45/45 CPU checks: at rest 192 pages used, **0 starved, 0 evicted**, census sums to capacity across 40 images of 400-request oversubscribed churn. 🔴 **Q3's missing numbers are now fixed by ruling, not research:** `ShadowPageCapacity` = **256** (4096² R32_UINT atlas, 64 MiB, 16×16 pages of 256²) against **6144** addressable tiles — a **24:1 oversubscription**, so eviction is the steady state, not an edge case. 🔴 Reclaim is **coarsest-LOD-first, oldest-first within a level**, and the scan **stops at the requesting level** so a coarse LOD can never take a finer one's page (without that bound LOD 4/5 thrash each other every image). Strict LOD-agnostic LRU was rejected — it can evict a near page mid-pan and punch a visible hole in front of the viewer. (User ruling, 2026-07-30.) ⚠️ A `Used` page is unreclaimable by *any* policy, so the LOD priority decides only who loses first among **cached** pages; genuine exhaustion still needs the census warning. 📝 **Amended in P6.3b:** the scan-stops-at-the-requesting-level bound alone starved a walking camera, because it also barred a level from reclaiming its own abandoned pages. A same-level last-resort fallback now runs after the coarse scan, guarded on `LastUsedImage < ImageOrdinal` — see the P6.3b row and the `Backlog.md` entry for why that guard is what keeps the anti-thrash property |
| **P6.3a** | ✔️ 🔴 Bind the floor in the shade pass; drop the `discard` | 🟢 code complete — floor vertex/index/instance on **b5/b6/b7** (not the sketched b9-b11; 5-7 were free), fetch branches on `Partition >= FloorPartitionBase`, and the ordinal is **rebased** by that base before indexing `FloorInstances`. ✔️ **Gate confirmed visually** — the plane is shaded and walkable. 📝 It renders near-white *by design* (`SurfacePresetTable` preset 0 authors 0.88/0.88/0.90), **not** for want of UVs: the shade pass binds no albedo texture at all and never reads `TexU`/`TexV`. Left white at the user's direction |
| **P6.3b** | 🟡 Wire the window + atlas into `RenderExtension`; C1-C4/C6 advance per image | 🟡 **partial — the window now advances on a real device every image and the gate is met, but S1/S2/S3 are NOT written; the row's original scope was split.** Gate evidenced three independent ways: 11/11 motion checks (origin scrolls with the camera, ≤2 strips per step, a fixed sun never trips the whole-window path, LOD 0 : LOD 5 scroll ratio ~32:1), 45/45 atlas checks re-run after the eviction fix, and real-device bring-up — 180 images + clean teardown under the validation layer, **zero VUID, zero leaks**. 🔴 **Deviation from §4.1:** the plan places `DriveViewportCamera` *ahead* of the preamble; it is deliberately **not** moved, because the preamble's shade reconstructs geometry from the id buffer the raster already wrote and must read the camera that wrote it. Solved instead by caching the observer in the preamble (`CachedObserverPosition` / `ObserverCacheSeeded`) for `RecordSequence` to reuse. 🔴 **The motion probe found a real starvation bug** the P6.2 churn probe could not: the coarser-only reclaim scan meant a level could never reclaim **its own** stale pages, so a walking camera settled at `used=72 cached=184 free=0` and starved every later request permanently. Fixed with a same-level last-resort fallback guarded on `LastUsedImage < ImageOrdinal`; both probes green after, and `0=129 / 5=127` confirms the coarsest-first ruling survives intact. ⚠️ The instrumentation traces the **toroidal origin**, not residency — the producer that marks a tile valid is the S7 raster (P6.4), so vacancy sits at 6144/6144 by construction until then and is not a defect |
| **P6.3c** | S1/S2/S3 — tile marking and propagation (split out of P6.3b) | requested tiles match the camera's near-field footprint; propagation reaches every coarser level. ✔️ Fork C **ruled: reuse** (see §8). 🔴 **S2's second sub-draw is RETAINED — an earlier recommendation to drop it was wrong and is recorded here so it is not re-proposed.** The reasoning that killed it ("no previous-transform history exists, so both sub-draws rasterize identical geometry — real cost, zero coverage") mistook the pair for a *rasterization* concern. Upstream it is **cache invalidation**: EEVEE tags *"the tiles it was in and is now into"* in two passes over `bounds_buf.previous()` / `.current()`, both raising `SHADOW_DO_UPDATE` (`eevee_shadow_tag_update.bsl.hh:8-11`, `eevee_shadow.cc:940-951`). Dropping it does not save cost — it **strands stale shadows behind every moving caster**, because this atlas caches pages across images by design. 🔴 Prerequisite the plan never listed: a **staleness flag distinct from `Used`** (`ShadowPageOwnership` cannot express "exists but wrong"); a page renders only when `is_used && do_update`. ✔️ **This prerequisite is now DONE (2026-07-30), ahead of S1/S2/S3** — `ShadowPageRecord::ContentStale` plus `InvalidateShadowPagesInSphere` / `InvalidateShadowTileContent` / `MarkShadowPageRendered` / `ShadowPageNeedsRender`, census extended with `PageStaleCount` + `PageRenderCount`, **69/69 CPU checks** (45 pre-existing + 24 new) and a 180-image device run whose log is **byte-identical** to the P5.9b baseline — expected, since nothing calls the tag pass until S2 is its consumer. 🔴 It is a **bit orthogonal to `Ownership`, not a fourth state**, because the two have different lifetimes: `Ownership` is recomputed every image by `OpenShadowPageImage`, so a merged encoding would be wiped by the `Used`→`Cached` demotion and a page tagged stale in image N would read clean in image N+1 with nothing redrawn. ⚠️ S2 must call the tag pass **twice per changed caster** (previous **then** current bounds) and must **not** clear staleness itself — only the S7 raster may, via `MarkShadowPageRendered`. 📝 The performance worry is inverted: the flag is what *enables* skipping, so a static scene measures `PageRenderCount == 0` and dispatches nothing. 📝 Tag direction is **both**: receiver/view-driven from the depth buffer decides which pages *exist*, caster-driven from the OBBs decides which are *stale* — different flags, not one. 📝 Propagation is a separate bottom-up compute pass in current Blender (finest LOD tagged, then fanned out by `atomicOr`), not an inline per-LOD loop. ✔️ **`ShadowTileStore` + `Shadow/Shaders/ShadowTileStore.glsl` NOW EXIST (2026-07-30), 62/62 CPU checks.** The virtual table is one 32-bit word per tile (6144 words = 24 KiB): bits 0..7 are the only bits marking may `atomicOr`, bits 8..31 are the allocator's packed page index, and the masks are asserted disjoint so a marking pass cannot corrupt a live mapping. The per-image reset **clears demand but preserves `Update`** — the same lifetime asymmetry as `ContentStale`, one level up. ⚠️ **Externally corroborated:** Blender's own developer docs independently describe the same pass order and the same three-state O(1) allocator, and add a mechanism this plan did not list — **Tile Masking**, *"untag the lower LOD tiles that are completely overlapped by higher LOD tiles"* — now implemented as `MaskRedundantShadowTiles`. 🔴 **Two real bugs were caught by probing rather than by review, both invisible to structural tests.** ① Enumerating a level's window as `Origin + Slot` instead of **`Slot - Origin`**: `ToroidalOrigin` is stored in ADD form (the *negated* corner, `SunShadowClipmap.h:22`), so the wrong form still covers all 32 physical slots exactly once — bijection, in-range and store-vs-atlas agreement **all pass** — while naming a different set of light tiles (`[16..47]` vs `[-16..15]`), whose halved ancestors fall outside the coarse window: propagation reached **0/32** coarse tiles instead of 32/32. The GLSL had the mirror-image defect (subtracting the origin where the CPU adds it), so `ValidateShadowTileStoreLayout` now asserts that operator's **sign** as text, since no compiler can see it. ② Masking tested the `Coarse` bit alone to mean "inherited", but **`Direct` and `Coarse` legitimately coexist** — a distant receiver samples a coarse tile itself while nearer geometry propagates into the same tile — so it masked a page a receiver was actively reading, punching a hole at exactly the distance coarse LODs serve. Fixed by giving direct demand its **own bit** propagation never raises; masking now requires `Coarse && !Direct`. 📝 The remaining S1/S2/S3 work is the three shader bodies (`MarkVisibleShadowPages.comp`, `ShadowTileTagInscription.{vert,frag}`, `ShadowTileLevelPropagate.comp`) plus their dispatch in `RecordPreamble` |
| **P6.4** | S7 depth raster into the atlas | atlas depth matches a CPU ray-cast of the floor plane |
| **P6.5** | SMRT tracer in the shade pass | 🟢 **shadows appear on the plane** |
| **P6.6** | Bias tuning | zero acne on the floor; contact-correct at object bases |

⚠️ P6.3a precedes every floor-based gate. Without it the heads self-shadow and **nothing lands on the
plane** — with the rest of the chain perfectly correct.

---

## 8. Open forks

| Fork | Question | Default |
| ---- | -------- | ------- |
| A | Ray/step counts without TAA | 4 × 8; revisit if TAA lands |
| B | S2 raster vs. compute tagger | raster (matches upstream); compute is the B1 fallback |
| C | `CasterBoundsStore` producer — reuse `InstanceCullSubmission` bounds or compute fresh | ✔️ **RULED: reuse** (2026-07-30). No `CasterBoundsStore` is authored. `UploadInstanceCullRecords` already fits one `PartitionCullRecord` per instance as the mesh-local sphere **transformed by that instance's `Model`** (`RenderExtension.cpp:919-929`) — i.e. the world-space per-instance sphere S2 needs already exists, device-local, in `InstanceCull.RecordBuffer`. Capacity becomes `RecordCapacity` = **1024** (`RenderExtension.cpp:755`), which retires the plan's unbacked 128-caster estimate and its missing growth policy. 📝 The record is at `Visibility/SurfacePartition.h:52` — an earlier note in this plan cited `Render/Surface/SurfacePartition.h`, which **does not exist**; the line number was right, the path was wrong. ⚠️ A **sphere** marks a conservative tile rectangle, so an elongated caster over-marks; accepted for the first cut because the bounds are free, and the cost is bounded by the same 24:1 oversubscription the pool already handles. 📝 Upstream this is a deliberate shape split, not an oversight: casters are tagged from an **OBB** (`ObjectBounds.bounding_corners`, world-space origin corner + 3 edge vectors), and spheres appear only where the bounded thing is roughly isotropic — the opaque receiver tags a *point* (`radius == 0`), the volume receiver a sphere, and `ObjectBounds.bounding_sphere` serves as a validity sentinel plus one coarse local-light reject. So the sphere reuse is a **knowing accuracy trade for a free producer**; if over-marking ever shows up in the census, the upgrade path is an OBB per caster, not a tighter sphere |
| D | Blue-noise tile source — generated at init, baked header, or asset | 🚧 unresolved; needed for P6.6 |

---

## 9. Evidence quality

Research: 83 agents, 69 claims extracted, adversarially verified by skeptics that fetched real Blender
source (`eevee_shadow_tracing_lib.glsl`, `eevee_shadow.cc`, `eevee_defines.hh`,
`eevee_shadow_tilemap_lib.glsl`).

| | Count |
| - | ----- |
| Upheld | 45 |
| 🔴 Refuted or downgraded | **24** |

Corrections this plan carries, so they are not re-introduced:

- `SHADOW_PAGE_RES` is **256**, not 128 (128 is UE5's).
- EEVEE's step default is **6 with ray count 1** after PR #121317 — an earlier "7 samples" claim was refuted.
- `Automation/ShaderPlan.ps1` **exists** — the "manual `glslangValidator` hand-step" claim was wrong twice.
- The shade push block is **112 B** today, not 104.
- `IntegrateClipmapField` is at `:1533` inside `RecordSequence`, **not** the preamble.
- `EvaluateClipmapScroll` **already** has the generic 3-axis loop.

🚧 Two research gaps remain, disclosed rather than papered over:

- **Q3 (page allocation / GPU tilemap indirection)** — lost to an agent stall. §2 and §4.3 S4/S5 are
  reconstructed from the surviving evidence, not from a dedicated study.
- **`reuse-3d` fit option** — lost to a tool failure. Refuted transitively by §3.2's frame argument,
  so the §3.1 ruling is not in doubt.
