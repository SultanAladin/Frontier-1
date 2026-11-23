# RESEARCH — Material Models, State of the Art 2026

🧩 Deep-research companion to `PLAN-UnifiedMaterialModels.md`. Verifies the legacy catalogue against
2026 primary sources, reads Unreal Engine 5.8 shader source directly, and resolves the open questions on
Subsurface, Iridescence, Glass-in-rasterizer, Hair, and über-shader permutation risk.

> **Headline:** the legacy catalogue is sound but **three items are now stale** — Schlick-for-metals is
> superseded by **F82-tint**, **emission is NOT applied last** (it sits below coat + fuzz), and the
> industry has moved from *fixed shading-model lists* toward **slab/closure stacks** (Unreal Substrate,
> OpenPBR). Frontier should adopt the corrected math now and the slab architecture later, not both at once.

---

## 1. Ground truth read from Unreal Engine 5.8 source

Read directly from `C:\Program Files\Epic Games\UE_5.8\Engine\Shaders\Private\` on this machine — not
from documentation or blogs.

### 1.1 Shading model IDs (`ShadingCommon.ush:20-35`)

| ID | Name | ID | Name |
| -- | ---- | -- | ---- |
| 0 | `UNLIT` | 7 | `HAIR` |
| 1 | `DEFAULT_LIT` | 8 | `CLOTH` |
| 2 | `SUBSURFACE` | 9 | `EYE` |
| 3 | `PREINTEGRATED_SKIN` | 10 | `SINGLELAYERWATER` |
| 4 | `CLEAR_COAT` | 11 | `THIN_TRANSLUCENT` |
| 5 | `SUBSURFACE_PROFILE` | 12 | `SUBSTRATE` |
| 6 | `TWOSIDED_FOLIAGE` | 13 | `SUBSTRATE_TOON` |

- `SHADINGMODELID_NUM = 14`, `SHADINGMODELID_MASK = 0xF` — 🔴 **4 bits reserved in the G-buffer.**
  Frontier's 9-10 models fit the same 4-bit budget with room to spare. Adopt the same packing.

### 1.2 The dispatch is a switch — and it is deprecated

`ShadingModels.ush:1137-1168` — `IntegrateBxDF()` is a plain `switch (GBuffer.ShadingModelID)` with one
`case` per model falling through to a `…BxDF()` function. Notably:

```text
// UE_DEPRECATED 5.7 - Deprecated by Substrate - Substrate uses SubstrateEvaluateBSDFCommon() instead
FDirectLighting IntegrateBxDF( FGBufferData GBuffer, ... )
```

- 💡 The switch-on-ID architecture the plan proposes is **exactly what Unreal shipped for a decade** —
  it is proven, not speculative.
- ⚠️ Epic has now **deprecated it** in favour of Substrate closures. Frontier should build the switch
  (simple, proven) but keep the shade entry point behind one function so a slab stack can replace it later.
- `DEFAULT_LIT`, `SINGLELAYERWATER`, and `THIN_TRANSLUCENT` all share `DefaultLitBxDF` — thin glass costs
  **no extra shading code**, only a different blend/pass. Confirms the plan's Glass-as-forward-tail split.

### 1.3 🐞 Unreal's Cloth is NOT Charlie — the legacy doc mismatched it

`BRDF.ush:685-703` holds **both** models, and legacy Cloth uses the first:

| Function | Formula | Used by |
| -------- | ------- | ------- |
| `D_InvGGX(a2, NoH)` | `rcp(PI*(1+4a²)) * (1 + 4a⁴/d²)`, `d = (NoH − a²·NoH)·NoH + a²` | legacy `CLOTH` model |
| `Vis_Cloth(NoV,NoL)` | `rcp(4·(NoL + NoV − NoL·NoV))` | legacy `CLOTH` (= Ashikhmin) |
| `D_Charlie(Roughness, NoH)` | `(2 + 1/r)·pow(Sin2H, 1/(2r)) / 2π` | Substrate sheen / glTF path |
| `Vis_Charlie_L(x, r)` | Estevez & Kulla 2017 rational fit | Substrate sheen |

🔴 **Parameterization trap:** Unreal's `D_Charlie` takes **Roughness directly** (`InvR = 1/Roughness`),
whereas the legacy Frontier doc computes `AlphaG = SheenRoughness²` then `InverseR = 1/AlphaG`. These are
**not the same curve**. Pick one convention and state it at the call site, or sheen will not match glTF
or Unreal. The glTF/KHR_materials_sheen reference uses `alphaG = roughness²`; Unreal's is pre-squared by
the caller. Frontier should follow **glTF** (`alphaG = r²`) for interchange fidelity.

---

## 2. OpenPBR v1.1.1 — two corrections to the legacy plan

Spec v1.1.1 dated **2026-04-17** (ASWF). Now default material in Maya / 3ds Max / Arnold 2026;
Blender 5.x Principled BSDF is **OpenPBR-based**; Substance 3D Painter 2026 ships an OpenPBR beta.

### 2.1 🔴 Slab order — emission is NOT last

```text
ambient medium → FUZZ → COAT → THIN-FILM → BASE (metal | translucent | subsurface | gloss)
                                  ▲
                        EMISSION enters here — BELOW coat and fuzz
```

- The legacy doc says *"Emission: additive radiance applied last, after BRDF and after coat attenuation."*
  🐞 **Wrong per OpenPBR.** Emission is tinted by **both** coat and fuzz. The canonical use case is a TV
  screen with a reflective glass coat and dust fuzz on top — applying emission last would make the dust
  and coat invisible over the glowing screen.
- 📝 Correct order: `Lo = Fuzz + Coat + (1−Fc)·(1−Ffuzz)·(BaseBRDF + Emission)`.
- The sheen slab was **renamed "fuzz" and moved to the top of the stack** so it can represent dust lying
  over both base and coat — a change from the older "sheen is part of the cloth model" framing.

### 2.2 🔴 F82-tint replaces Schlick for metals

OpenPBR uses the **F82-tint** model (Kutz et al. 2021, extending Hoffman 2019), not plain Schlick, for
conductor Fresnel. It reproduces the real reflectivity dip near 82° that Schlick misses.

```text
F_Schlick(μ) = F0 + (1 − F0)(1 − μ)^5
F_82(μ)      = F_Schlick(μ) − b·μ·(1 − μ)^6
where  μ̄ = 1/7  (θ ≈ 81.79° ≈ 82°)
       b  = ( F_Schlick(μ̄) − F(μ̄) ) / ( μ̄·(1 − μ̄)^6 )
```

- 💡 Called "**tint**" because the edge colour is applied *multiplicatively* — so **white is a universal
  default that reduces exactly to standard Schlick**. That makes it a drop-in: ship white, get today's
  look; artists tint the edge only when they want the real metal dip.
- ⚠️ The original (non-tint) F82 exhibits **glowing, non-energy-conserving edges** in a white-furnace
  test and needs clamping. Use the **tint** variant.
- ⚠️ 🔴 **F82-tint conflicts with thin-film iridescence** (OpenPBR issue #79): F82-tint computes
  reflectivity directly without going through a complex IOR, so the optical **phase shift** that Airy
  interference needs is not available. Issue #225 tracks the related metal-under-coat problem.
  **Consequence for Frontier: Metal + Iridescence cannot both be exact.** Choose per material — either
  F82-tint metal (no iridescence) or Schlick-IOR metal (iridescence works). Document the limitation
  rather than shipping a silently wrong combination.
- Blender's **Conductor BSDF** exposes exactly this: an `F82` Fresnel type plus a `Physical Conductor`
  type taking measured IOR + extinction. **EEVEE only supports F82**, converting physical inputs
  internally — a useful precedent for a real-time engine.
- Parameter tables: `portsmouth/F82-tint-generator` and `peterkutz/metal-colors` derive F82-tint
  parameters from measured spectral complex IOR. Use these instead of hand-authored f0 triples.

---

## 3. Unreal Substrate — the cost model that decides whether to copy it

Substrate replaced the fixed shading-model list with "slabs of matter." **Production-ready and default
as of UE 5.7**; docs now at 5.8.

| Budget (`r.Substrate.BytesPerPixel`) | Layers/pixel | Intended target |
| ------------------------------------ | ------------ | --------------- |
| **80 (default)** | 3-4 | most platforms |
| 160 | 7-8 | complex hero materials; **doubles** Substrate-buffer bandwidth |
| 40 | 2 | Steam Deck / mobile |

- **Overflow is graceful, not fatal:** when a material exceeds closure-count or bytes-per-pixel, slabs are
  **merged by parameter blending** until it fits. Debug via `Window > Substrate`, the *Material count* and
  *Material classification* view modes.
- Substrate runs a **material classification pass after the base pass** to make the lighting pass more
  efficient — 💡 **this is precisely the P4 MATERIAL CLASSIFY stage in Frontier's plan.** Independent
  confirmation that classify-then-shade is the right architecture.
- **Adaptive vs Blendable G-buffer:** Adaptive = full fidelity, ~**15% longer cook time**, DBuffer decals
  only, SM6 platforms. Blendable = **one feature per pixel** (highest priority wins), works on SM5.
  Anisotropy can be enabled independently of other features in Blendable.
- ⚠️ Epic's own guidance: *if a material is expressible in a legacy shading model without hacks, use the
  legacy system* — the slab overhead is not justified "for consistency."

🔴 **Verdict for Frontier:** do **not** build Substrate-style slabs now. A GTX 1060 with a ~24 MiB channel
G-buffer cannot afford 80 B/px of closure storage on top of the existing GI budget. Build the 4-bit
switch (§1), keep the classify pass (already planned), and treat slabs as a post-1.0 architecture.

---

## 4. Blender EEVEE-Next — the closest analogue, and its warning

EEVEE-Next moved from forward to a **G-buffer deferred pipeline**.

- 🟢 **Arbitrary number of BSDFs supported without major performance impact** — raytracing and SSS are no
  longer limited to one BSDF node. Direct validation of the many-models-one-shade thesis.
- 🟢 **All render passes render at once**; no multiple geometry passes (except cryptomatte). Legacy EEVEE
  had to re-render the sample with different uniforms per material pass.
- 🔴 ⚠️ **The warning:** *"the G-buffer's optional layers take a very large amount of compile time"* —
  detecting unused layers and removing them is on Blender's own low-hanging-fruit list (issue #145347).
  Deferred light evaluation was originally one shader evaluating **all closures for all lights**, which
  *"drastically increase[d] shader complexity"* (issue #129268); the fix is splitting light types across
  separate shaders.
- ⚠️ AOV clearing is required because of deferred layering — an object in layer 1 can write an AOV while
  an object in layer 2 occludes it without writing the same AOV.
- ⚠️ NPR caveat: `Shader to RGBA` is **incompatible with the deferred pipeline**; such materials are
  treated as alpha-blended. Frontier's Matcap/CAD-preview path is analogous — keep it out of the
  deferred shade or accept the same restriction.

---

## 5. Über-shader permutation risk — the real threat to the plan

The plan leans on specialization constants. That is correct but has a documented failure mode.

| Concern | Fact | Mitigation |
| ------- | ---- | ---------- |
| When does specialization happen? | **Pipeline-creation time only** — the sole point Vulkan drivers compile shaders | Precompile the finite combination set before the render loop; swap pipelines, never respecialize per frame |
| Frequently-changing constants | Each change forces an **expensive recompile** — spec constants are useless for these | Keep such parameters in push constants / UBOs, not spec constants |
| Descriptor layouts | 🚩 Spec constants **cannot modify descriptor set layouts** | Design one layout that covers all models up front |
| Compile-time explosion | Combinatorial across blend state, targets, MSAA, vertex layout — hundreds of pipelines per logical shader | 4-bit model ID → bounded set; avoid multiplying by unrelated state |
| First-run stutter | Unavoidable without a warm cache | Serialize `VkPipelineCache` to disk (two `vkGetPipelineCacheData` calls) and reload next run |
| Cache eviction | 🐞 NVIDIA has **deleted** first-launch caches once the driver shader-cache size limit was exceeded (raised 128 MB → 1024 MB in driver 460) | Keep the engine's own cache file; do not rely on the driver cache alone |
| Cache-miss fallback | Uber shader with dynamic branching, compiled up front, used until the specialized pipeline finishes (the D3D12 PSO-cache and Dolphin approach) | Ship one dynamic-branch fallback variant |
| `VK_EXT_graphics_pipeline_library` | ⚠️ Fast-linked unoptimized pipelines can cost **~50% in the general case, with worse outliers**; vendors advise keeping per-frame unoptimized work **below ~10%** | Do not rely on fast linking; replace with optimized pipelines ASAP |
| Driver flattening | 🐞 Drivers have been known to peek at UBO values and silently flatten branches — shaders look fast until a driver that doesn't do this | Never benchmark on one vendor only |

🔴 **Action:** cap the specialization axis at the **4-bit model ID plus a small fixed feature mask**.
Verify pipeline count stays in the low hundreds, and serialize the pipeline cache from the first milestone
(M2) — retrofitting it after the permutation set grows is far harder.

---

## 6. Hair / fur — a genuinely separate model, correctly deferred

Not in the legacy catalogue, and it should stay out of the near-term plan.

- **Marschner et al. 2003** decomposes single-fibre scattering into **R / TT / TRT** lobes (reflection,
  transmission-transmission, transmission-reflection-transmission), plus a measured ideal-specular
  forward-scattering behaviour the model itself does not predict.
- **Zinke et al. 2008 dual scattering** splits multiple scattering into global + local, accelerated by a
  **deep opacity map**; ≥ **2 orders of magnitude** faster than unbiased path tracing while preserving
  photorealism in most settings. This is the real-time standard.
- Unreal exposes it as `SHADINGMODELID_HAIR` with graph inputs **Base Color, Scatter, Specular, Roughness,
  Emissive, Tangent, Backlit, AO, Pixel Depth Offset** — note **Tangent, Scatter, Backlit** replace the
  usual Normal/Metallic slots.
- Recent work: *Real-time LOD Strand-based Rendering* (EGSR 2025) — hair-card LOD causes **discontinuity
  in dynamics and appearance** at the strand→card transition; uses elliptical thick hairs with an
  aggregated BCSDF instead. *Real-Time Hair Rendering with Hair Meshes* (SIGGRAPH 2024) generates geometry
  on the fly to cut storage/bandwidth. Industry practice remains **hair cards** (Koh & Huang 2001 lineage)
  with dual specular highlights (primary + tinted secondary) and a transmission term.
- 🔴 **Verdict:** hair needs its own **geometry pipeline** (strands/cards, sorted transparency, deep
  opacity maps) far more than it needs a BRDF slot. Out of scope for a CAD/DCC engine's first material
  release. Record in `Backlog.md`; do not budget a channel for it now.

---

## 7. Subsurface scattering — verified against Unreal 5.8, HDRP, and EEVEE-Next source

🔴 **Verdict: the legacy plan's "screen-space separable SSS" is one generation out of date.** All three
major engines converged on **Christensen–Burley normalized diffusion**, evaluated as a **screen-space disk
gather with bilateral reweighting**. Jimenez separable SSSS is now the *legacy/cheap* path, not the target.

### 7.1 Technique landscape

| Technique | Model | Cost shape | Status 2026 |
| --------- | ----- | ---------- | ----------- |
| **Burley normalized diffusion** | `R(r) = (e^(−r/d) + e^(−r/3d)) / (8πdr)`; 2 params (volume albedo A, shape s) | 1 setup + 1 non-separable disk gather | 🟢 **Current standard** — HDRP, UE5 Burley, EEVEE-Next |
| Jimenez separable SSSS (CGF 2015) | separable approximation of the 2D profile | 2 × 1D convolutions, 7 taps | legacy; deterministic, cheapest credible |
| Penner pre-integrated (GPU Pro 2) | pre-blurred diffuse BRDF → 2D LUT on (N·L, curvature) | **zero extra dispatches**, in-shader | alive as the forward / low-end tier |
| OpenPBR subsurface slab | authoring + interchange spec (random-walk on the reference side) | — | the spec you *map onto* Burley, not an algorithm |

💡 EEVEE-Next's `burley_setup()` is the whole albedo → shape conversion, and it is trivially portable:

```text
s = 1.9 − A + 3.5·(A − 0.8)²        // Burley eq. (6), albedo → shape
l = 0.25 · (1/π) · radius
d = l / s
```

⚠️ EEVEE deviates from Unity's 2018 formulation by precomputing a **weight-profile texture** instead of
per-primary weights, so it supports **per-pixel AND per-channel radius**. Worth copying.

### 7.2 Channel additions — the legacy plan's channel 21 is under-specified

Channel 21 (`SubsurfaceColor + ScatterDistance`) is necessary but **not sufficient**. Burley additionally
needs:

| Parameter | Type | Unit | Why it is mandatory |
| --------- | ---- | ---- | ------------------- |
| Surface albedo A | RGB 0-1 | `[-]` | 🔴 **Must equal BaseColor** or energy breaks — reuse channel 1, do not author separately |
| Max screen radius | scalar | `[px]` | Cull: sub-pixel footprint ⇒ skip SSS entirely |
| World unit scale | scalar | `[units/cm]` | The profile is in physical units; without it radii are meaningless |
| Texturing mode | enum | `[-]` | *post-scatter* (albedo applied once at exit — scans/photogrammetry) vs *pre+post-scatter* (blurs albedo, softer) |
| Transmission mode | enum | `[-]` | *thin* (one shared shadow fetch) vs *thick* (max of baked thickness and shadow-map thickness) |
| Transmission tint | RGB | `[-]` | back-lit ear/nostril colour |

💡 **Dual specular lobe** (`Roughness₀` soft, `Roughness₁` tight, `LobeMix`) buys more perceived skin
quality per ALU than extra SSS taps. Cheap. Recommend adding it with the SSS model, not later.

🔴 **Do not expose Gaussian weights.** Unity explicitly rejected the 2-Gaussian model's 7 degrees of
freedom as not artist-authorable — that rejection *is* the reason Burley was adopted.

### 7.3 Render structure — 🔴 the visibility buffer already solves the hardest part

SSS does need its own dispatches, but only **two**, and neither is full-screen once tile-classified:

```text
P5   MATERIAL SHADE      → write diffuse (transmitted) radiance to its OWN target, separate
                           from specular; do both transmission events here            [compute]
P5a  SSS PREPARE         → 8×8 CS: repack radiance + object-id, compute pixel footprint,
                           atomicAdd tile coords → tile buffer + DispatchIndirect args [compute]
P5b  SSS CONVOLVE        → indirect CS over tagged tiles ONLY: disk gather, bilateral
                           reweight, normalize weights to 1, write back                [compute]
P6   COMPOSITE           → += specular
```

| Target | Format | Res | 1080p cost |
| ------ | ------ | --- | ---------- |
| SSS radiance (array: 0 = direct, 1 = indirect) | R11G11B10F | full | ≈ 16.6 MiB |
| Object id | R16_UINT | full | ≈ 4.1 MiB |
| Tile coords + indirect args | SSBO, packed `uvec2×16` | — | negligible |

🟢 **This is Frontier's best-case scenario.** The vbuffer already carries per-pixel instance ids — which is
*exactly* the `object_id` Burley needs for leak rejection (`if (samp.sss_id != object_id) continue;`). That
one line is the fix Blender 4.2 was praised for ("no longer leaks between objects"); Frontier gets it free.
Likewise P4 material-classify replaces the hand-built stencil hierarchy Unity needed.

⚠️ **Full resolution — do not go half-res.** Counter-intuitive but documented: the bilateral upsample plus
depth/normal refetch eats most of the saving while destroying pore/wrinkle detail, and the kernel is
*already* a blur so the losses compound. Unreal states the same result — with 64-bit scene colour,
**full-res is usually faster than checkerboard** (fewer texture fetches).

Sampling scheme (all offline precompute, so its cost is irrelevant):

- ① Importance-sample radial distance by the profile's own PDF (it is normalized, so it *is* a valid PDF).
  The CDF is not analytically invertible → invert numerically by **Halley's method**, offline.
- ② Importance-sample the **longest-radius channel** (red, for skin) for best variance reduction.
- ③ Fibonacci / golden-angle for the polar angle; store `xy` = disk position, `z` = 1/pdf.
- ④ Per-pixel rotation of the pattern (EEVEE: `interleaved_gradient_noise` × golden angle) — 🔴 EEVEE
  deliberately **limits rotation magnitude "to avoid too much cache misses."**
- ⑤ Weight = `burley_eval(d, r) · pdf_inv` where **r is the true 3D Euclidean distance** between entry and
  exit view-space positions, *not* the 2D disk distance.
- ⑥ **Normalize accumulated weights to sum to 1** — this is the trick that avoids computing per-sample
  surface area analytically.

| Engine | Sample count |
| ------ | ------------ |
| Unity HDRP | 0 (sub-pixel) / **21** (medium) / **55** (large); PS4 shipped capped at 21 |
| EEVEE-Next | `sample_len = 16` hardcoded (`SSS_SAMPLE_MAX 64` is buffer capacity only); `SSS_BURLEY_TRUNCATE 16.0` |
| Jimenez separable | 7 per 1D pass |

### 7.4 Measured cost — and the GTX 1060 projection

| Source | Measured | Conditions |
| ------ | -------- | ---------- |
| Jimenez CGF 2015 | **0.489 ms** | 1080p, Radeon HD 7970, 2 × 7 taps, blur only |
| Jimenez tech report 2012 | **1.05 ms** | 1080p, **GTX 580**, multi-light |
| Unity SIGGRAPH 2018 | **1.16 ms** | PS4, 21 samples, CS doing convolve **+** diffuse/specular merge |
| Unity HDRP | high-quality tier ≈ **2.5×** default | — |
| Practitioner all-in report | ~2 ms blur + 1-2 ms resolves + mask prepass | real-world integration |

🚩 **GTX 1060 6 GB projection: budget ~2 ms at 1080p**, bandwidth-bound not ALU-bound. The 580 → 1060 gap
is ~2.5-3× shader throughput, so full-res Burley at ~21 taps over a couple of on-screen heads lands in the
**1.0-2.0 ms** band. Because the convolve is an *indirect* dispatch over tagged tiles, cost scales with SSS
**screen coverage** — a distant character is nearly free. VRAM ≈ **21 MiB**, trivial against 6 GB.

### 7.5 🔴 Pascal-specific traps

| Trap | Fact | Mitigation |
| ---- | ---- | ---------- |
| **FP16 is a trap** | cc 6.1 (all GTX 10-series) runs FP16 at **1/64 rate**; the op is vec2 ⇒ ~**1/128 per lane**. Only GP100/Jetson got full rate | Use FP16 for **storage/bandwidth only** (R11G11B10F targets, half samplers). All math in FP32. Pascal *does* have full-rate INT8/DP4A |
| Bandwidth ceiling | GP106: 1280 cores, 10 SMs, **192 GB/s** over 192-bit GDDR5, only **1.5 MiB L2** | Mandatory LDS tile cache: radiance + linear depth with a **2-texel border**; order threads on a **Z-order (Morton) curve** |
| Shared-memory cap | 96 KB/SM but blocks capped at **48 KB**, NVIDIA advises **≤ 32 KB/block** | Size the LDS tile to stay under 32 KB |
| Large kernels thrash texture cache | Unity's own stated limitation; with 1.5 MiB L2 this bites harder than the GCN they measured on | Clamp max screen radius; ship the 0/21/55 sample LOD — 🔴 **non-negotiable on this card** |
| Async compute | Pascal rebalances at **SM** granularity via preemption (~0.1 ms), not GCN per-CU concurrency | Batch into **few long dispatches**. Two is already right. Do not port console code assuming free overlap |
| 🔴 **Burley needs TAA** | Unreal states Burley **"requires Temporal Anti-Aliasing to display properly"**; it importance-samples the *profile*, not the *lighting* | Frontier has no TAA yet ⇒ **prefer Jimenez separable (deterministic 7-tap) until TAA lands**, or use Penner |
| Bilateral weighting not optional | Without it background bleeds onto foreground; reported artifacts trace to `FOLLOW_SURFACE` being disabled | Reject on object id **and** weight by true 3D distance — both, not either |
| Banding at large radii | Documented by Unreal under extreme lighting | Limited-magnitude per-pixel pattern rotation |
| Missing-data artifacts | Occluded neighbours (nose over cheek) have no data — same class as SSAO/SSR; Unreal: "a gray outline appears when non-SSS Materials occlude SSS Materials" | Inherent to screen space; accept |
| 🟢 Metallic-slot conflict **avoided** | Unreal *and* Unity both repurpose the metallic G-buffer slot for SSS profile data, losing metallic on SSS materials | Frontier fetches params from a material table via the vbuffer ⇒ **no channel sacrifice** |
| Penner curvature caveat (if used as low-end tier) | `ddx/ddy` are constant per triangle ⇒ edge discontinuities, worsened by skinning; baked curvature maps are wrong under deformation | Pick your poison; practitioners preferred baked maps |

### 7.6 Engine ground truth

| Engine | Path |
| ------ | ---- |
| Unreal 5.8 | `Subsurface Profile` + **Enable Burley** is the high-end path; Jimenez retained for compatibility. 🚩 In **5.7 Substrate is production-ready and default-on for new projects** — SSS became a *slab property* (`Sub-Surface Type` = Wrap / Two-Sided Wrap / **Diffusion**). Diffusion **without** a profile drives scattering from DiffuseAlbedo + MFP directly and **is blendable**; profiles are not |
| Unity HDRP | Burley diffusion profiles, 0/21/55 sample LOD, hand-built stencil hierarchy |
| EEVEE-Next | Burley per the Golubev SIGGRAPH 2018 talk, + weight-profile texture for per-channel radius. **Not** random-walk (that is Cycles only) |

🔴 **EEVEE-Next is the closest architectural match to Frontier** — Vulkan, compute-based, no hardware RT,
Pascal-era support. Read it for structure; ⚠️ it is **GPL** — do not copy code.

### 7.7 Recommendation

Implement **Burley normalized diffusion, full-res, two compute dispatches, tile-classified via indirect
dispatch**, reusing the vbuffer ids for leak rejection. Ship sample-count LOD from day one. 🔴 Treat TAA as
a **prerequisite** — until it exists, ship Jimenez separable as the default and keep **Penner
pre-integrated** in reserve as the zero-dispatch forward/low-end tier.

⚠️ This supersedes the plan's M6 line item: SSS is no longer "screen-space separable" but "Burley disk
gather," and it adds **P5a/P5b** to the render spine plus two render targets to M1's budget.

---

## 8. Iridescence — verified against the ratified spec and reference GLSL

🟢 `KHR_materials_iridescence` is **"Complete, Ratified by the Khronos Group"** (Board, 2022-07-22), based
on Belcour & Barla 2017 (TOG 36(4) a65). Forbidden alongside `KHR_materials_pbrSpecularGlossiness` and
`_unlit`. Reference asset: `IridescentDishWithOlives`. Shipping in Filament, three.js, Babylon.js, Blender
Cycles (PR #118477), Unity HDRP, and OpenPBR (`thin_film_weight/thickness/ior`).

⚠️ **Belcour–Barla IS already the real-time approximation** — it analytically pre-integrates the spectral
response so RGB engines match a spectral reference. It is **not** a spectral Airy loop; the reference GLSL
truncates the Fourier series at **m = 1..2** (two dirac pairs + DC term). There is no "cheaper analytic
form" to find; the only real saving is a LUT.

### 8.1 Authored parameters (exact, from the ratified spec)

| Property | Type | Default | Notes |
| -------- | ---- | ------- | ----- |
| `iridescenceFactor` | number | `0.0` | 0 ⇒ extension is a no-op |
| `iridescenceTexture` | textureInfo | — | 🔴 **R** channel, linear. `iridescence = factor · texture.r` |
| `iridescenceIor` | number | `1.3` | film IOR |
| `iridescenceThicknessMinimum` | number | `100.0` | `[nm]` |
| `iridescenceThicknessMaximum` | number | `400.0` | `[nm]` |
| `iridescenceThicknessTexture` | textureInfo | — | 🔴 **G** channel, linear |

```text
thickness = mix(ThicknessMinimum, ThicknessMaximum, iridescenceThicknessTexture.g)
```

- 🐛 **Common authoring bug:** R = intensity, G = thickness — *different channels of the same texture*.
- Absent thickness texture ⇒ implicitly 1.0 ⇒ thickness = **Maximum** everywhere.
- `Minimum` **MAY** exceed `Maximum` — the spec explicitly permits inversion.
- Practical ranges (Blender PR #118477): soap bubble — base IOR 1.0, film IOR 1.33, 10-1000 nm; dove neck
  feather — film IOR 1.55 (keratin), 400-600 nm over an absorbing volume. ⚠️ Keep films ≤ a few thousand nm
  or the BRDF's "light exits where it enters" assumption breaks.

### 8.2 🟢 Confirms the plan: modifier, not a lobe

```text
metal_brdf      → mix(conductor_fresnel(f0, spec), spec · iridescent_fresnel(…), strength)
dielectric_brdf → mix(fresnel_mix(base_ior, base, spec), rgb_mix(base, spec, iridescent_f), strength)
```

🔴 **Energy conservation uses the max colour component**, exactly as `KHR_materials_specular`:
`rgbAlphaMax = max(r,g,b); Lo = (1 − rgbAlphaMax)·base + rgbAlpha·spec`. Using `1 − rgbAlpha`
component-wise **inverts colours and gains energy** — a silent, easy bug.

⚠️ Confirms research §2.2's exclusivity finding from the other direction: `IorToFresnel0` /
`Fresnel0ToIor` are exact **for dielectrics only**; **metal extinction κ is assumed 0.0**, and Schlick is
used so S/P polarisation is never split. TIR (`cosTheta2Sq < 0`) returns `vec3(1.0)`; out-of-gamut
negatives are clamped via `max(I, vec3(0.0))`.

### 8.3 Cost — and why to bake a LUT

Instruction count **counted directly from the ratified reference GLSL** (`iridescence.glsl`, 100 lines):
2 × `evalSensitivity`, ~8 `cos`, ~8 `exp`, ~15 `sqrt`, 2 `pow5`, 2 mat3 XYZ→Rec709, 4 branches.

| Option | Cost | Accuracy | Varying thickness | Metals | Rank |
| ------ | ---- | -------- | ----------------- | ------ | ---- |
| **2D LUT** `(cosθ₁ × thickness) → RGB`, baked offline via TMM + CIE | ✔️ 1 fetch | 🚩 | 🟢 (thickness is an axis) | 🟢 (bake per f0 set) | 🥇 🏆 |
| 1D LUT at fixed thickness | ✔️ 1 fetch, 64 px | ✔️ | 🔴 no | 🔴 unsupported | 🥈 |
| Reference `evalIridescence` GLSL | 🚩 | 🔴 best | 🟢 | 🚩 κ = 0 | 🥉 |
| Kneiphof 2019 prefiltered moments | 🚩 +0.13 ms | 🔴 | 🟢 | 🟢 | 🏅 |

| Published measurement | Figure |
| --------------------- | ------ |
| BB17 vs Kneiphof 2019, IBL, Beethoven bust (α = 0.25, d = 550 nm) | **3.49 ms vs 3.62 ms** — marginal overhead |
| Liu 2024 LUT + 3 polygonal lights, dragon @1080p, RTX 2080 Ti | **2.58 ms/frame** |

🚩 **Pascal projection:** GP106 runs `cos`/`exp` on the SFU at **1/4 rate**. ~16 transcendentals + 15 sqrt
≈ **80-120 shader cycles/pixel** atop the base BRDF ⇒ roughly **1.5-3 ms for a full-screen iridescent
surface @1080p**; negligible for a hero object under ~20% coverage. A LUT collapses it to ~4 cycles + fetch
latency. ⚠️ A LUT **fixes the IOR** — keep the analytic path only if `iridescenceIor` must be authorable
per material.

⚠️ Two quality traps: ① evaluating the iridescent term in **only the specular direction** under prefiltered
IBL gives oversaturated colours and bright grazing-angle halos on rough surfaces (this is what Kneiphof
fixes); ② LUT **angle-axis resolution matters** — phase rate-of-change grows without bound at grazing
angles, so undersampling yields banding instead of the correct graceful whitening. Prefilter mips on θ.

---

## 9. Glass / transmission in a rasterizer — what every engine actually does

🟢 **The plan's approach is correct and universal**: copy scene colour → offset UV by the refracted ray's
exit point → sample a mip chosen by roughness. 🔴 **Thickness is always baked or approximated, never
traced.** 🔴 **Layering is unsolved everywhere.**

### 9.1 Engine survey

| Engine | Mechanism | Source buffer | Thickness | Layering |
| ------ | --------- | ------------- | --------- | -------- |
| UE5 legacy | translucent blend; Refraction Method = IOR / **Pixel Normal Offset** / 2D Offset | scene-colour copy, RGBA16F | n/a | per-mesh sort; `r.OIT.SortedPixels` (DX12-only, experimental) |
| UE5 Substrate | rough refraction — Roughness **and** Thickness blur the layer beneath | screen-space | slab param | `RefractionCoverageMode` blends sharp/blurred |
| UE5 Thin Translucent | single-pane, Fresnel-driven, no volume | — | none | — |
| EEVEE-Next 4.2+ | *Screen Space Refraction* → renamed **Raytraced Transmission**; SS ray march, no BSDF-count limit | depth + colour | **Thickness socket**; Sphere or Slab; optional *Thickness From Shadow* | needs dithered method; Blended mode drops SS refraction |
| Filament | `refractionMode: none\|cubemap\|screenspace`, `refractionType: solid\|thin` | cubemap (IBL, "significantly more efficient") or SS colour | `thickness` / `microThickness` | — |
| Unity HDRP | Sphere / Planar(Box) / Thin (**hard-coded 5 mm**); proxy raycast vs probe volume | `_ColorPyramidTexture` (mips = downscale+blur) | Thickness + map | 🔴 pyramid is **opaque-only, pre-refraction** ⇒ refractors invisible through refractors |
| three.js | separate scene render per transmissive object | `transmissionSamplerMap` | `thickness` | drei replaces mip blur with multi-tap noise so it *can* see other refractors |
| Godot 4 | `textureLod(screen_texture, ref_ofs, ROUGHNESS*4.0)` | BackBufferCopy | — | 🔴 docs: "cannot refract onto itself or onto other transparent materials" |

### 9.2 The canonical technique (glTF Sample Renderer reference)

```text
① refract at entry, scale by thickness in local space
   refractionVector = refract(−v, normalize(n), 1.0 / ior)
   modelScale.{x,y,z} = length(modelMatrix[{0,1,2}].xyz)      // rotation-independent
   transmissionRay = normalize(refractionVector) · thickness · modelScale
② exit point → NDC → UV
   ndc = proj · view · vec4(position + transmissionRay, 1.0)
   uv  = (ndc.xy / ndc.w + 1.0) / 2.0
③ sample the copy at a roughness-derived LOD
   lod = log2(framebufferWidth) · applyIorToRoughness(roughness, ior)
④ Beer–Lambert, then tint by baseColor
⑤ f_diffuse = mix(f_diffuse, f_specular_transmission, transmissionFactor)   ← replaces the diffuse lobe
```

🔴 The load-bearing mapping — and a trap:

```text
applyIorToRoughness(r, ior) = r · clamp(ior·2 − 2, 0, 1)
```

At `ior == 1.0` this clamps to **0**, so **roughness produces no blur at all**. IOR 1.5 gives the default
amount of microfacet refraction. 🟢 Confirms the plan's step ⑤: transmission is not an extra lobe — it
*replaces the diffuse lobe proportionally*.

### 9.3 Artifacts and their shipped mitigations

| Artifact | Cause | Mitigation as shipped |
| -------- | ----- | --------------------- |
| Foreground bleeding / ghosting (HL2 speech bubble, F.E.A.R. pistol, DOOM shotgun tip) | already-drawn foreground sits in the copy | depth-test the refracted UV; fall back to unperturbed `SCREEN_UV` when sampled depth is nearer than the refractor |
| Off-screen UVs | refracted ray exits the frustum | clamp (most) · **mirror** (Quantum Break) · cubemap fallback (HDRP *Screen Weight Distance* fades SS→probe) · edge-fade (EEVEE) |
| Mip popping / "chunky fat pixels" / flicker | discrete LOD from a low-res screen-aligned buffer | keep the buffer power-of-two; raise resolution at low roughness; **replace mip blur with multi-tap noise** (drei) |
| Hard edges between differing depths | blurring a buffer that mixes depths; degrades badly above roughness ≈ 0.2 | separable blur chain instead of raw mips; depth-aware weighting |
| Aliasing | refraction applied before AA resolve | apply late (DOOM folds heat distortion into the tonemap) or MSAA both buffers (Crysis 3) |
| TAA smearing | distorted colour no longer aligns with depth | emit correct motion vectors; blur may still be wrong |
| Flat-surface IOR chaos (UE cafe windows) | scene colour read with a constant offset | **Pixel Normal Offset** (offset from pixel− vertex normal). 🔴 Needs a normal map — default (0,0,1) ⇒ zero refraction |
| Closer objects bleeding in at acute angles | — | UE *Refraction Depth Bias* |

💡 Production blur references: **DOOM frosted glass** = 5-level downscale chain (1920→60) + separable X/Y
blur on levels 2-5 = **8 blur passes**, gaussian approximating a GGX lobe. **Dishonored 2** injects
distortion vectors into the **half-res motion-blur velocity buffer** — smooth refraction essentially free.
**HL2 / F.E.A.R.** used no filtering at all: a sharp 1024² copy under a 1080p framebuffer.

### 9.4 Thin-walled vs volumetric — `KHR_materials_volume` (🟢 ratified)

| Property | Type | Default | Notes |
| -------- | ---- | ------- | ----- |
| `thicknessFactor` | number | `0` | 🔴 **mesh coordinate space**; `0` ⇒ thin-walled |
| `thicknessTexture` | textureInfo | — | **G** channel `[0,1]`, multiplied by factor |
| `attenuationDistance` | number | `+Infinity` | mean free path, 🔴 **world space** |
| `attenuationColor` | number[3] | `[1,1,1]` | colour white light becomes at that distance |

⚠️ **Deliberate unit mismatch**: thickness in mesh space, attenuation distance in world space. Medium
properties are **not texturable** (homogeneity assumed). `doubleSided` has no effect on a volume boundary.
Volumetric use requires a **closed / manifold** mesh. Must be paired with `KHR_materials_transmission`.

```text
σs = 0 (scattering out of scope) ⇒ σt = σa = −log(c)/d
T(x) = e^(−σt·x) = c^(x/d)
⇒ transmittance = pow(attenuationColor, vec3(transmissionDistance / attenuationDistance))
```

📝 `attenuationDistance == 0.0` is the sentinel for +∞ ⇒ no attenuation. Base colour tints uniformly *at
the boundary* ("as if covered with a coloured transparent foil"); attenuation is **path-length dependent**,
so colour becomes shape-dependent. Thin-surface absorption in `KHR_materials_transmission` is just
`1.0 − baseColor`. At `metallicFactor = 1.0`, `transmissionFactor` is ignored.

🔴 **Five ways to get thickness — none of them trace:**

| Method | Used by | Accuracy |
| ------ | ------- | -------- |
| **Baked thickness map** (AO bake along −N; dark = thin) | glTF-recommended; factor = bbox longest edge | 🚩 lossy — sampled at entry, true path length is angle-dependent |
| **Sphere** (object = sphere tangent at entry, radius = thickness) | Filament `solid`, HDRP `Sphere`, EEVEE `Sphere` | 🚩 good for convex/round |
| **Slab** (two parallel planes) | Filament `thin`, HDRP `Planar`, EEVEE `Slab` | ✔️ correct for panes, grass blades |
| Fixed constant | HDRP `Thin` = 5 mm, no override | ✔️ |
| Shadow-map derived | EEVEE *Thickness From Shadow* | 🔴 EEVEE's own devs call it **imprecise, underestimating, with ugly fringes** — avoid |

💡 Filament's split, concretely: a 1 m-radius hollow sphere with a 1 mm wall ⇒ `thickness = 1`,
`microThickness = 0.001`. 📝 glTF says ray tracers should **ignore** the thickness texture and use traced
distance — but must still read `thicknessFactor` to tell thin-walled from volumetric.

### 9.5 🔴 Multiple glass layers — no engine solves this

The failure modes are structural, not bugs: Godot — a refractor behind another transparent is
**invisible**; HDRP — the colour pyramid is opaque-only, so refractors are invisible through refractors;
UE — per-mesh sorting only unless OIT is on. The **glTF spec's own compliance floor** is *"aim to display at
least opaque objects through a transmissive material"*, with extra transparent layers explicitly optional.

| Approach | Cost | Accuracy | VRAM | Rank |
| -------- | ---- | -------- | ---- | ---- |
| **Sort meshes back-to-front + depth pre-pass** | ✔️ 1.37 ms @1080p | ✔️ fails on intersections/cycles | ✔️ | 🥇 🏆 **for a 1060** |
| Per-draw copy before each refractor (HL2 / Crysis 3 / DOOM) | 🚩 N copies + N renderpass switches | 🚩 correct layering, foreground ghosting | 🚩 | 🥈 |
| MBOIT 4-moment 16-bit @960×560 | 🚩 2.4 ms | 🚩 | 🚩 ~20 MiB | 🏅 |
| Weighted Blended OIT | ✔️ 1 geometry pass | ✔️ needs per-scene weight tuning, occlusion leakage | ✔️ | 🏅 |
| Per-pixel sorted OIT (`r.OIT.SortedPixels`) | 🔴 sort = **70-95%** of total at high depth complexity | 🔴 | 🔴 unbounded | 🥉 |
| MBOIT 4-moment 32-bit | 🔴 8.1 ms | 🚩 | 🔴 | 🏴 |
| Depth peeling | 🔴 **123 ms** vs 12-16 ms approximations | 🔴 ground truth | 🔴 linear in layers | 🏴 reference only |

🔴 **Interplay of Light's verdict, still current: refraction of transparent surfaces remains unsolved by all
OIT methods.** OIT fixes blending *order*, not the refraction *source buffer*. Latest production art is
Activision **AVBOIT** (Adaptive Voxel-Based OIT, SIGGRAPH 2025) — built precisely because nothing existing
met Call of Duty's accuracy+perf bar.

### 9.6 Cost on a GTX 1060

```text
opaque → [copy scene colour] → [build mip/blur chain] → transparent (sorted)
```

| Item | Cost (derived, 192 GB/s @ ~75% eff.) | VRAM @1080p |
| ---- | ------------------------------------ | ----------- |
| RGBA8 copy (16.6 MB traffic) | ~0.11 ms | 8.3 MiB (~11 MiB with mips) |
| RGBA16F copy (33 MB traffic) | ~0.22 ms | 16.6 MiB (~22 MiB with mips) |
| Half-res (960×540) RGBA16F + mips | ~0.06 ms | **~5.5 MiB** |
| Mip chain generation | +0.05-0.08 ms | +⅓ base |
| DOOM-style 5-level + 8 separable blurs | measurably more — a *feature*, not a default | — |
| Per-draw copies × N | 🔴 N × above **plus barrier / renderpass bubbles** — on a 10-SM part these stalls often exceed the copy's bandwidth cost | — |

⭐ **Recommendations for M5:**

- ① **Half- or quarter-res refraction source.** Refraction destroys high-frequency detail anyway; HL2
  shipped 1024² under 1080p, Dishonored 2 used half-res. 4× bandwidth saving, usually invisible.
- ② **One copy per frame, not per draw.** Sort refractors per-mesh and accept that they don't refract each
  other — exactly what HDRP, Godot and stock UE5 ship. 🔴 **Skip OIT entirely at this tier.**
- ③ **Slab thickness + baked thickness map**; Sphere mode for hero props. Do **not** attempt shadow-map
  thickness.
- ④ **Bake the iridescence 2D LUT** unless `iridescenceIor` must be per-material authorable.
- ⑤ ⚠️ With prefiltered IBL + iridescence, expect oversaturated grazing-angle halos on rough surfaces
  unless Kneiphof's moment prefilter is adopted (+0.13 ms measured).

---

## 10. Corrections ledger — what changes in the plan

| # | Item | Legacy plan said | Corrected | Severity |
| - | ---- | ---------------- | --------- | -------- |
| 1 | Emission order | "applied last, after coat attenuation" | Sits **below** coat + fuzz; is tinted by both | 🔴 |
| 2 | Metal Fresnel | Schlick, f0 = BaseColor | **F82-tint** (white edge ⇒ reduces to Schlick) | 🔴 |
| 3 | Metal + Iridescence | integrate freely as a modifier | 🔴 **Mutually exclusive in exact form** — F82-tint lacks the phase term Airy needs | 🔴 |
| 4 | Sheen parameterization | `alphaG = SheenRoughness²` | Correct for glTF; **differs from Unreal's pre-squared `D_Charlie`** — state convention at call site | 🚩 |
| 5 | Cloth reference impl | "Unreal-class cloth = Charlie" | Unreal legacy cloth = **`D_InvGGX` + `Vis_Cloth`**, not Charlie | 🚩 |
| 6 | Sheen naming | "sheen is the cloth model's lobe" | OpenPBR renames it **fuzz** and moves it **top of stack** (dust over coat) | 🚩 |
| 7 | Model-ID budget | unspecified | **4 bits / 14 IDs** (Unreal-proven); Frontier's 10 fit | ✔️ |
| 8 | Pipeline cache | not mentioned | 🔴 Serialize `VkPipelineCache` from M2, not later | 🚩 |
| 9 | Hair | absent | Correctly absent — needs a geometry pipeline, not a channel | ✔️ |
| 10 | SSS integrator | "screen-space separable" (Jimenez) | **Burley normalized diffusion**, disk gather + bilateral reweight — separable is now the *legacy* tier | 🔴 |
| 11 | SSS prerequisite | none stated | 🔴 Burley **requires TAA**; Frontier has none ⇒ ship Jimenez until TAA lands | 🔴 |
| 12 | SSS resolution | unstated (half-res assumed cheaper) | **Full-res**; half-res upsample costs more than it saves and destroys pore detail | 🚩 |
| 13 | SSS render structure | "offscreen blur" | **P5a prepare + P5b indirect convolve** over tile-classified pixels; +2 targets (~21 MiB) to M1 | 🚩 |
| 14 | SSS surface albedo | separate `SubsurfaceColor` channel | 🔴 Volume albedo A **must equal BaseColor** or energy breaks — reuse channel 1 | 🔴 |
| 15 | Pascal FP16 | unstated | 🔴 cc 6.1 runs FP16 at **1/64 rate** (~1/128 per lane on vec2) — storage only, never math | 🔴 |
| 16 | Iridescence texture channels | unstated | 🐛 intensity = **R**, thickness = **G** of the *same* texture — easy silent bug | 🚩 |
| 17 | Iridescence energy conservation | unstated | 🔴 Use `max(r,g,b)`; component-wise `1 − rgbAlpha` **inverts colours and gains energy** | 🔴 |
| 18 | Iridescence integrator | analytic Belcour–Barla | 🚩 **Bake a 2D LUT** `(cosθ × thickness) → RGB` unless `iridescenceIor` must be authorable — 1.5-3 ms ⇒ one fetch on Pascal | 🚩 |
| 19 | Transmission roughness→blur | unstated | 🔴 `r · clamp(ior·2 − 2, 0, 1)` — at **IOR 1.0 roughness produces no blur at all** | 🔴 |
| 20 | Glass refraction source | full-res fb copy | 🚩 **Half- or quarter-res** — refraction destroys high-frequency detail anyway; 4× bandwidth saving (~22 → ~5.5 MiB) | 🚩 |
| 21 | Glass layering | unstated | 🔴 **Unsolved in every engine.** One copy/frame + per-mesh sort; refractors do not refract each other. Skip OIT at this tier | 🔴 |
| 22 | Glass thickness | `Thickness` channel, unqualified | Thickness is **baked or approximated, never traced**: baked map + **Slab** (Sphere for hero props). 🔴 Avoid shadow-map thickness | 🚩 |
| 23 | Volume units | unstated | ⚠️ `thicknessFactor` is **mesh space**, `attenuationDistance` is **world space** — deliberate mismatch; medium properties are **not texturable** | 🚩 |

---

## 11. Open items still unresolved

- 🚧 Whether Frontier's Matcap path can live inside the deferred shade or must sit outside it (the
  EEVEE `Shader to RGBA` problem, §4).
- 🚧 Exact pipeline-permutation count once the feature mask is fixed — measure at M2.
- 🚧 Whether TAA lands before M6; it gates which SSS integrator ships as default (§7.5).

🟢 Subsurface (§7), iridescence (§8) and glass-in-rasterizer (§9) are now all independently verified against
ratified specs, reference GLSL, and shipping engine source. No research gaps remain in the model catalogue.

---

## 12. Sources

**Specifications**
- [OpenPBR Surface spec (ASWF)](https://academysoftwarefoundation.github.io/OpenPBR/) · [reference .mtlx](https://github.com/AcademySoftwareFoundation/OpenPBR/blob/main/reference/open_pbr_surface.mtlx)
- [OpenPBR Surface, DigiPro paper (DOI 10.1145/3744199.3744632)](https://dl.acm.org/doi/10.1145/3744199.3744632) · [OpenPBR: Novel Features and Implementation Details (arXiv 2512.23696)](https://arxiv.org/pdf/2512.23696)
- [OpenPBR issue #79 — thin-film with F82-tint](https://github.com/AcademySoftwareFoundation/OpenPBR/issues/79) · [issue #225 — metal under coat](https://github.com/AcademySoftwareFoundation/OpenPBR/issues/225)
- [Hoffman, Generalization of Adobe's Fresnel Model, Rev 1.1 (2023)](https://renderwonk.com/publications/wp-generalization-adobe/gen-adobe.pdf)
- [portsmouth/F82-tint-generator](https://github.com/portsmouth/F82-tint-generator) · [peterkutz/metal-colors](https://github.com/peterkutz/metal-colors)

**Unreal Engine**
- Local source: `C:\Program Files\Epic Games\UE_5.8\Engine\Shaders\Private\{ShadingCommon,ShadingModels,BRDF}.ush`
- [Shading Models in Unreal Engine (5.8)](https://dev.epicgames.com/documentation/unreal-engine/shading-models-in-unreal-engine)
- [Overview of Substrate Materials (5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine) · [Substrate Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/substrate-materials-in-unreal-engine)
- [Substrate, SIGGRAPH 2023](https://advances.realtimerendering.com/s2023/2023%20Siggraph%20-%20Substrate.pdf)
- [Adding a new Shading Model (Hoffman)](https://medium.com/@lordned/ue4-rendering-part-6-adding-a-new-shading-model-e2972b40d72d) · [The Deferred Shading Pipeline](https://medium.com/@lordned/unreal-engine-4-rendering-part-4-the-deferred-shading-pipeline-389fc0175789)

**Blender**
- [Principled BSDF manual](https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/principled.html) · [Metallic/Conductor BSDF](https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/metallic.html) · [Metallic BSDF PR #114958](https://projects.blender.org/blender/blender/pulls/114958)
- [EEVEE-Next tracker #93220](https://projects.blender.org/blender/blender/issues/93220) · [#129268 deferred lighting efficiency](https://projects.blender.org/blender/blender/issues/129268) · [#145347 deferred material compile time](https://projects.blender.org/blender/blender/issues/145347)
- [EEVEE render passes](https://developer.blender.org/docs/features/eevee/render_passes/)

**Vulkan / permutations**
- [Utilizing Specialization Constants](https://docs.vulkan.org/samples/latest/samples/performance/specialization_constants/README.html) · [Pipeline Cache guide](https://docs.vulkan.org/guide/latest/pipeline_cache.html) · [Pipeline Management](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html)
- [VK_EXT_graphics_pipeline_library proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_graphics_pipeline_library.html)
- [The Shader Permutation Problem, Part 2 (MJP)](https://therealmjp.github.io/posts/shader-permutations-part2/) · [Dolphin Ubershaders](https://dolphin-emu.org/blog/2017/07/30/ubershaders/)
- [NVIDIA forum — pipeline cache eviction](https://forums.developer.nvidia.com/t/pre-compiled-shader-cache-cleaned-by-nvidia-driver-causing-stutter-when-launching-vulkan-apps/336196)

**Subsurface scattering**
- [Efficient Screen Space Subsurface Scattering, SIGGRAPH 2018 — Golubev / Unity](https://www.advances.realtimerendering.com/s2018/Efficient%20screen%20space%20subsurface%20scattering%20Siggraph%202018.pdf)
- [Separable Subsurface Scattering — Jimenez et al., CGF 2015](https://www.cg.tuwien.ac.at/research/publications/2015/Jimenez_SSS_2015/) · [tech report RR-02-12](https://graphics.unizar.es/papers/s4_techreport12.pdf) · [iryoku/separable-sss](https://github.com/iryoku/separable-sss)
- [Subsurface Profile Shading Model (UE 5.8)](https://dev.epicgames.com/documentation/unreal-engine/subsurface-profile-shading-model-in-unreal-engine) · [Substrate production pipeline, UE 5.7](https://www.strayspark.studio/blog/substrate-materials-production-pipeline-ue5-7)
- [Unity HDRP Diffusion Profile reference](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.4/manual/diffusion-profile-reference.html) · [HDRP Subsurface Scattering](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Subsurface-Scattering.html)
- EEVEE-Next source: `eevee_subsurface.hh` · [`eevee_subsurface_shared.hh` (`burley_setup`, `SSS_SAMPLE_MAX`)](https://github.com/blender/blender/blob/main/source/blender/draw/engines/eevee/eevee_subsurface_shared.hh) · [PR #107407](https://projects.blender.org/blender/blender/pulls/107407) · [issue #122571 (16-sample hardcode)](https://projects.blender.org/blender/blender/issues/122571) · [4.2 LTS EEVEE notes](https://developer.blender.org/docs/release_notes/4.2/eevee/)
- [MJP — An Introduction To Real-Time Subsurface Scattering](https://therealmjp.github.io/posts/sss-intro/) · [GPU Gems ch.16](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-16-real-time-approximations-subsurface-scattering) · [Pre-Integrated Skin Shading (Simon's Tech Blog)](http://simonstechblog.blogspot.com/2015/02/pre-integrated-skin-shading.html) · [The Order: 1886 material pipeline](https://blog.selfshadow.com/publications/s2013-shading-course/rad/s2013_pbs_rad_notes.pdf)

**Iridescence**
- [KHR_materials_iridescence spec](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_iridescence/README.md) · [PR #2027](https://github.com/KhronosGroup/glTF/pull/2027) · reference GLSL `glTF-Sample-Renderer/source/Renderer/shaders/iridescence.glsl`
- [Belcour & Barla 2017 project page](https://belcour.github.io/blog/research/publication/2017/05/01/brdf-thin-film.html) · [HAL hal-01518344](https://hal.science/hal-01518344v2) · [Unity blog write-up](https://blog.unity.com/technology/a-practical-extension-to-microfacet-theory-for-the-modeling-of-varying-iridescence)
- [Kneiphof, Golla & Klein 2019, CGF 38(4) — prefiltered moments](https://www.semanticscholar.org/paper/Real%E2%80%90time-Image%E2%80%90based-Lighting-of-Microfacet-BRDFs-Kneiphof-Golla/96cc88a553e8762237a90daa4e67e59690517d16)
- [Liu et al. 2024, Precomputed Monomial-Gaussians (CGF 14991)](https://onlinelibrary.wiley.com/doi/10.1111/cgf.14991) · [Gu et al. 2024, CAVW — TMM baked to texture](https://onlinelibrary.wiley.com/doi/10.1002/cav.2289)
- [DerSchmale/threejs-thin-film-iridescence (1D LUT)](https://github.com/DerSchmale/threejs-thin-film-iridescence) · [Blender Cycles PR #118477](https://projects.blender.org/blender/blender/pulls/118477) · [Drobot & Micciulla 2017, multilayered](https://www.activision.com/cdn/research/s2017_pbs_multilayered_slides_final.pdf)

**Glass / transmission / OIT**
- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md) · [KHR_materials_volume](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_volume/README.md) · reference GLSL `glTF-Sample-Renderer/.../{ibl,punctual,functions}.glsl`
- [Froyok — Refracting Pixels (HL2 / DOOM / Crysis 3 / Quantum Break / Dishonored 2 teardown)](https://www.froyok.fr/blog/2024-12-refraction/) · [GPU Gems 2 ch.19](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation)
- [Filament Materials — refractionMode / refractionType](https://google.github.io/filament/main/materials.html) · [Filament PBR doc](https://google.github.io/filament/Filament.md.html)
- [UE Pixel Normal Offset](https://dev.epicgames.com/documentation/unreal-engine/refraction-using-pixel-normal-offset-in-unreal-engine?lang=en-US) · [UE Using Refraction](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-refraction-in-unreal-engine)
- [EEVEE material settings — Thickness Mode](https://docs.blender.org/manual/en/latest/render/eevee/material_settings.html) · [EEVEE raytracing settings](https://docs.blender.org/manual/en/latest/render/eevee/render_settings/raytracing.html) · [PR #113401 Split Thickness Approximation](https://projects.blender.org/blender/blender/pulls/113401)
- [HDRP refraction models](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@13.1/manual/refraction-models.html) · [HDRP colour pyramids](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@12.0/manual/Custom-Pass-buffers-pyramids.html)
- [Godot screen-reading shaders](https://github.com/godotengine/godot-docs/blob/master/tutorials/shaders/screen-reading_shaders.rst) · [Godot proposal #9580 — depth-guarded refracted UV](https://github.com/godotengine/godot-proposals/discussions/9580)
- [three.js PR #21884](https://github.com/mrdoob/three.js/pull/21884) · [drei MeshTransmissionMaterial](https://github.com/pmndrs/drei-vanilla/blob/main/src/materials/MeshTransmissionMaterial.ts)
- [Interplay of Light — OIT Endgame (perf table)](https://interplayoflight.wordpress.com/2022/07/10/order-independent-transparency-endgame/) · [Vulkan depth-peeling sample](https://docs.vulkan.org/samples/latest/samples/api/oit_depth_peeling/README.html) · [Activision AVBOIT 2025](https://research.activision.com/publications/2026/adaptive-voxel-based-order-independent-transparency) · [Weighted Blended OIT](http://casual-effects.blogspot.com/2014/03/weighted-blended-order-independent.html)

**Pascal / GTX 1060**
- [GTX 1060 architecture (TechPowerUp)](https://www.techpowerup.com/review/nvidia-geforce-gtx-1060/2.html) · [Tom's Hardware review](https://www.tomshardware.com/reviews/nvidia-geforce-gtx-1060-pascal,4679.html) · [Pascal memory & cache hierarchy](https://www.bodunhu.com/blog/posts/pascal-gpu-memory-and-cache-hierarchy/)
- [FP16 support on GTX 1060/1080 (NVIDIA forums)](https://forums.developer.nvidia.com/t/fp16-support-on-gtx-1060-and-1080/53256) · [Pascal FP16/FP32/INT8 performance (Beyond3D)](https://forum.beyond3d.com/threads/pascal-fp16-fp32-int8-performance.58323/) · [Pascal async compute explored](https://www.eteknix.com/pascal-gtx-1080-async-compute-explored/)

**Hair**
- [Dual scattering approximation (Zinke 2008)](https://dl.acm.org/doi/10.1145/1360612.1360631) · [Real-time LOD Strand-based Rendering (EGSR 2025)](https://sites.cs.ucsb.edu/~lingqi/publications/paper_egsr25_hairlod.pdf) · [arXiv 2405.10565](https://arxiv.org/abs/2405.10565)
- [High-Performance Real-Time Implicit Strand-Based Hair Rendering (arXiv 2607.04230)](https://arxiv.org/pdf/2607.04230)
