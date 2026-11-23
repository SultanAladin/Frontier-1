# PLAN — Unified Material & Shading Models

🧩 The one authoritative plan for Frontier's material system: ~22 authored channels across 9 shading
models (Unreal/Blender parity), how they render, and in what order to build them. Merges the two
verified legacy catalogues (`PhysicallybasedShadingReport.md`, `ShdingModels.md`) with the render
architecture in `PLAN-UnifiedRenderEngine.md` + `PLAN-VisibilityRenderer.md`.

> **TL;DR** — All models live in ONE über-shader; the shading model is a per-pixel branch chosen by a
> specialization constant, evaluated in a single deferred compute shade (~0.67 ms measured, GTX 1060).
> Only Glass/transmission renders in a separate forward tail. Emissive and Iridescence are NOT passes:
> emissive is a shared additive channel, iridescence is a Fresnel-term modifier.

🔴 **Read `RESEARCH-MaterialModels2026.md` alongside this plan.** It verified this catalogue against
OpenPBR v1.1.1, Unreal 5.8 source, the ratified KHR extensions and their reference GLSL, and found **23
corrections** — see §7 below. Where the two documents disagree, the research document wins.

---

## 1. Source provenance

| Source | Legacy location | Holds |
| ------ | --------------- | ----- |
| `PhysicallybasedShadingReport.md` | `RetiredProject/.retired/Docs/` | Verified 6-model catalogue + shared core |
| `ShdingModels.md` | `RetiredProject/.retired/Docs/` | Superset: adds Subsurface, Iridescence, Matcap, EON diffuse → 8 models |
| `PLAN-UnifiedRenderEngine.md` | `EngineDocs/` | Channel G-buffer spine; live path = forward, ~7 flat channels |
| `PLAN-VisibilityRenderer.md` | `EngineDocs/` | Deferred material-classify → per-material compute shade |
| `FrontierMemoryArchive.md` | root | Measured timing: deferred `DirectPBR` shade = 0.67 ms |

🔴 The two `.retired/Docs` files are the authoritative math — every formula traced to OpenPBR v1.1.1,
Walter 2007, Heitz 2014, Conty–Kulla 2017, Belcour–Barla 2017, and the ratified KHR glTF extensions,
verified June 2026. The render plans are the authoritative architecture. This document merges them; the
core needs no new research.

---

## 2. Unified channel set (~22, Unreal/Blender parity)

Shared = every model. Model-defining = allocated only when that model is selected.

| #  | Channel                          | Type / range        | Models              | Origin |
| -- | -------------------------------- | ------------------- | ------------------- | ------ |
| 1  | BaseColor                        | linear RGB          | all                 | glTF 2.0 |
| 2  | Roughness                        | scalar 0-1          | all opaque          | glTF 2.0 |
| 3  | Metallic                         | scalar 0-1          | Standard            | glTF 2.0 |
| 4  | Reflectance / Specular           | scalar 0-1          | Standard            | Frostbite / KHR_specular |
| 5  | Normal                           | tangent map         | all                 | glTF 2.0 |
| 6  | AmbientOcclusion                 | scalar 0-1          | all (shared)        | glTF 2.0 |
| 7  | Emissive color                   | linear RGB          | all (shared)        | glTF 2.0 |
| 8  | Emissive strength                | scalar (HDR > 1)    | all (shared)        | KHR_emissive_strength |
| 9  | DiffuseRoughness                 | scalar 0-1          | Standard (EON)      | OpenPBR / Portsmouth 2024 |
| 10 | Anisotropy                       | scalar −1..1        | Metal/Aniso         | Disney/Filament |
| 11 | AnisotropyRotation               | rad                 | Metal/Aniso         | Filament |
| 12 | ClearCoatWeight                  | scalar 0-1          | Clearcoat           | KHR_clearcoat |
| 13 | ClearCoatRoughness               | scalar 0-1          | Clearcoat           | KHR_clearcoat |
| 14 | ClearCoatNormal                  | tangent map (opt)   | Clearcoat           | KHR_clearcoat |
| 15 | SheenColor                       | linear RGB          | Cloth               | KHR_sheen |
| 16 | SheenRoughness                   | scalar 0-1          | Cloth               | KHR_sheen |
| 17 | Transmission                     | scalar 0-1          | Glass               | KHR_transmission |
| 18 | IOR                              | 1.0-2.5             | Glass/Clearcoat     | KHR_ior |
| 19 | Thickness                        | scalar ≥ 0          | Glass/SSS           | KHR_volume |
| 20 | AttenuationColor + Distance      | RGB + scalar [cm]   | Glass               | KHR_volume |
| 21 | ScatterDistance (MFP) + unit scale| RGB [cm] + [units/cm] | Subsurface        | Christensen–Burley 2015 |
| 21b| Texturing + transmission mode    | 2 enums + RGB tint  | Subsurface          | HDRP / UE Burley |
| 22 | Iridescence factor (tex **R**)   | scalar 0-1          | modifier: Standard+Metal | KHR_iridescence |
| 22b| Iridescence IOR + thk min/max (tex **G**) | 1.3 def / nm | modifier: Standard+Metal | KHR_iridescence |

---

## 3. Shading models (the BRDF / BSDF / GlossBSDF / Glass / Emissive extension)

Each row is a closed feature set — a glass material never allocates a sheen channel. This is Unreal's
Default Lit / Cloth / Clear Coat split and Blender's separate BSDF nodes, unified over one über-shader.

| Model              | Lobe (replaces / adds)                         | Beyond base PBR  | Blender analog |
| ------------------ | ---------------------------------------------- | ---------------- | -------------- |
| Standard opaque    | Lambert **or EON** diffuse + Cook–Torrance GGX | base             | Principled |
| Metal (conductor)  | GGX only, no diffuse, chromatic f0             | base             | Principled metallic=1 |
| Anisotropic        | anisotropic GGX + Smith                        | brushed/machined | Anisotropic BSDF |
| Clearcoat          | 2nd GGX lobe (Kelemen V) over base             | car paint, lacquer | Clearcoat / Coat |
| Cloth / Sheen      | **Charlie NDF replaces Cook–Torrance**         | velvet, satin    | Sheen / Velvet BSDF |
| Glass (BTDF)       | **Walter 2007 refractive BTDF + Beer–Lambert** | transmission     | Glass BSDF / Transmission |
| Subsurface         | **Burley disk gather** (2 compute dispatches)   | skin, wax, marble| Subsurface Scattering |
| Iridescence        | **Airy Fresnel replaces Schlick** (modifier)   | soap, oil, anodized | Thin-Film |
| Matcap (non-PBR)   | baked sphere, no light loop                    | CAD preview      | Solid/MatCap view |
| Emissive           | additive `Lo += EmissiveColor·Strength`        | **shared feature, not a model** | Emission BSDF |

🔴 Emissive is not its own pass or model — a shared channel kept > 1.0 so it survives into bloom.
Iridescence is not a lobe — it swaps the Fresnel term inside Standard/Metal.

⚠️ **Corrected slab order** (OpenPBR v1.1.1 — supersedes "emission applied last"):

```text
ambient medium → FUZZ → COAT → THIN-FILM → BASE (metal | translucent | subsurface | gloss)
                                  ▲
                        EMISSION enters here — BELOW coat and fuzz, tinted by both

Lo = Fuzz + Coat + (1 − Fc)·(1 − Ffuzz)·(BaseBRDF + Emission)
```

⚠️ **Metal Fresnel = F82-tint, not plain Schlick.** White edge tint reduces exactly to Schlick, so it is a
drop-in default. 🔴 F82-tint and thin-film iridescence are **mutually exclusive in exact form** — F82-tint
has no complex IOR, so the Airy phase term is unavailable. Confirmed from the other direction by the
ratified iridescence spec: its `Fresnel0ToIor` is exact for dielectrics only and **assumes metal κ = 0.0**.
See research §2.2 and §8.2.

---

## 4. How they render — together, not per-pass

🔴 All opaque models render in ONE deferred compute shade. This is the point of the visibility-buffer
architecture the render plans already chose.

```text
Per-frame render spine (PLAN-VisibilityRenderer §2, deferred path):
  ┌─ P1  RASTER/VISIBILITY   geometry → id + depth only (NO material reads)
  ├─ P4  MATERIAL CLASSIFY   tile-count materials → per-material pixel lists      [compute]
  ├─ P5  MATERIAL SHADE      ONE dispatch per material-id; über-shader branches
  │                          on ShadingModel spec-constant → GGX/Charlie/Airy/…   [compute]  ← 0.67 ms
  ├─ P5a SSS PREPARE         repack radiance+id, footprint, tile list + indirect args [compute] ┐ SSS
  ├─ P5b SSS CONVOLVE        indirect over tagged tiles: Burley disk gather        [compute] ┘ only
  ├─ P7  INDIRECT (GI/AO/IBL) feeds the same shade point
  ├─ P9  REFRACTION SOURCE   ONE half-res scene-colour copy + mip chain (~5.5 MiB) [copy+blit]
  └─ P10 TRANSPARENCY TAIL   Glass/transmission ONLY, per-mesh sorted + blended    [raster forward]
```

🔴 **One copy per frame, not per draw** — refractors therefore do **not** refract each other. This is
exactly what HDRP, Godot and stock UE5 ship; glass layering is unsolved in every engine (research §9.5).
Skip OIT at this tier: per-pixel sorted OIT spends **70-95%** of its time in the sort.

| Question                        | Answer |
| ------------------------------- | ------ |
| All opaque models in one pass?  | Yes. Specialization constants gate one über-shader. |
| Each model its own pass?        | No — except Glass. |
| Why is Glass separate?          | It samples the already-shaded opaque framebuffer offset by the refracted direction; must run after opaque resolve → forward tail. |
| Performant?                     | Measured 0.67 ms for the whole deferred PBR shade on a GTX 1060. Cost lives in GI (~9-12 ms), not shading. |
| Emissive pass?                  | None — additive term inside the shade. |
| Iridescence pass?               | None — Fresnel-term modifier over Standard/Metal. |

💡 Why classify-then-shade beats naive branching: a single `if(model)` per pixel causes warp divergence
(every lane pays for every branch). The classify pass sorts pixels by material first, so each dispatch
shades one model with zero divergence — Unreal-style many-models-one-frame at near-single-model cost.

---

## 5. Performance / speed ranking

Cost = relative ALU per shaded pixel in P5. **Accuracy: 🔴 = best.** Cost/VRAM/Effort: more = worse.

| Model                        | Cost (ALU) | Extra VRAM        | Accuracy | Effort | Quality    | Rank  |
| ---------------------------- | ---------- | ----------------- | -------- | ------ | ---------- | ----- |
| Matcap                       | ✔️         | ✔️                | ✔️       | ✔️     | ⭐⭐        | 🏁    |
| Metal                        | ✔️         | ✔️                | 🔴       | ✔️     | ⭐⭐⭐⭐    | 🥇 🏆 |
| Standard (Lambert)           | ✔️         | ✔️                | 🔴       | ✔️     | ⭐⭐⭐⭐    | 🥇    |
| Standard (EON diffuse)       | 🚩         | ✔️                | 🔴       | 🚩     | ⭐⭐⭐⭐⭐  | 🥈    |
| Clearcoat                    | 🚩         | ✔️                | 🔴       | 🚩     | ⭐⭐⭐⭐⭐  | 🥈    |
| Anisotropic                  | 🚩         | ✔️                | 🔴       | 🚩     | ⭐⭐⭐⭐    | 🥉    |
| Cloth (Ashikhmin V)          | 🚩         | ✔️                | 🚩       | 🚩     | ⭐⭐⭐      | 🥉    |
| Cloth (Charlie V + DFG LUT)  | 🔴         | 🚩 (DFG blue ch.) | 🔴       | 🔴     | ⭐⭐⭐⭐⭐  | 🥈    |
| Iridescence (2D LUT)         | ✔️ 1 fetch | 🚩 (LUT, fixed IOR)| 🚩      | 🚩     | ⭐⭐⭐⭐⭐  | 🥈    |
| Iridescence (analytic BB17)  | 🔴 1.5-3 ms| ✔️                | 🔴       | 🔴     | ⭐⭐⭐⭐⭐  | 🥉    |
| Subsurface (Burley, ~2 ms)   | 🔴         | 🔴 (~21 MiB, 2 tgts)| 🔴     | 🔴     | ⭐⭐⭐⭐    | 🥉    |
| Glass (BTDF, separate tail)  | 🔴         | 🚩 (~5.5 MiB ½-res)| 🚩 (raster)| 🔴   | ⭐⭐⭐      | 🥉    |

⭐ Build-order recommendation: Standard + Metal first (~90% of surfaces, byte-for-byte port of the
shipping forward math), then Clearcoat + Cloth + Glass, then Anisotropic, then Subsurface + Iridescence
last (heaviest, most niche). Matcap ships alongside Standard for the CAD viewport.

---

## 6. Build order

```text
M0  Port SurfaceForward math verbatim → shared core .glsl (GGX/Smith/Schlick/EON/multiscatter/split-sum IBL)
M1  Channel G-buffer (PLAN-UnifiedRenderEngine C1): 4 targets, ~24 MiB @ 1080p
M2  Deferred SHADE dispatch (C5 / P5): Standard + Metal, spec-constant gated. A/B pixel-identical to today.  ← gate
M3  Material-classify (P4): per-material pixel lists → zero-divergence multi-model shade
M4  + Clearcoat, + Cloth (Ashikhmin default), + Anisotropic
M5  Glass forward tail (P9+P10): ½-res fb-copy + mips, Slab thickness, Beer–Lambert, per-mesh sort
M6  + Subsurface (Burley disk gather, P5a+P5b, +2 targets), + Iridescence (2D LUT Fresnel modifier)
M7  SurfacePropertyInterface (ImGui): ShadingModel dropdown reveals only that model's channels
```

Each step lands behind a default-OFF toggle and must be A/B-identical to the prior path before flipping.

---

## 7. Corrections

Carried forward from the legacy docs:

- 🐞 **Anisotropy remap:** use linear `αt = α(1+aniso)`, `αb = α(1−aniso)`. The `sqrt(1−0.9·aniso)`
  variant is inconsistent with the aniso-GGX normalization — rejected.
- 🐞 **Water reflectance:** old table listed 0.2; correct value from IOR 1.33 is f0 ≈ 0.02 →
  reflectance ≈ 0.35.

New from `RESEARCH-MaterialModels2026.md` (2026-07-28) — full 23-row ledger in that document's §10:

| Item | Corrected to | Severity |
| ---- | ------------ | -------- |
| Emission order | Below coat + fuzz, tinted by both — **not** applied last | 🔴 |
| Metal Fresnel | **F82-tint** (Kutz 2021); white edge ⇒ reduces to Schlick | 🔴 |
| Metal + Iridescence | **Mutually exclusive in exact form** — document the limitation | 🔴 |
| Sheen `alphaG` | Follow glTF (`alphaG = r²`); Unreal's `D_Charlie` is pre-squared — differs | 🚩 |
| Cloth reference | Unreal legacy cloth = `D_InvGGX` + `Vis_Cloth`, **not** Charlie | 🚩 |
| Sheen → fuzz | OpenPBR renames it **fuzz**, moves it to top of stack (dust over coat) | 🚩 |
| Model ID packing | **4 bits / 14 IDs**, matching Unreal's `SHADINGMODELID_MASK 0xF` | ✔️ |
| Pipeline cache | 🔴 Serialize `VkPipelineCache` to disk from **M2**, not later | 🚩 |
| SSS integrator | **Burley disk gather**, not separable — separable is now the legacy tier | 🔴 |
| SSS + TAA | 🔴 Burley **requires TAA**; until it lands ship Jimenez separable as default | 🔴 |
| SSS albedo | Volume albedo **must equal BaseColor** — reuse channel 1, never author separately | 🔴 |
| Pascal FP16 | 🔴 cc 6.1 runs FP16 at **1/64 rate** — storage/bandwidth only, never math | 🔴 |
| Iridescence energy | 🔴 Conserve with `max(r,g,b)`; component-wise `1 − rgbAlpha` **inverts colour + gains energy** | 🔴 |
| Iridescence channels | 🐛 intensity = tex **R**, thickness = tex **G** — same texture, different channels | 🚩 |
| Iridescence integrator | **Bake a 2D LUT** (cosθ × thickness → RGB) — 1.5-3 ms ⇒ one fetch. LUT fixes the IOR | 🚩 |
| Transmission blur | 🔴 `r · clamp(ior·2 − 2, 0, 1)` — at **IOR 1.0 roughness gives NO blur** | 🔴 |
| Glass source buffer | **Half-res** copy, one per frame (~5.5 MiB); refractors don't refract each other | 🚩 |
| Glass thickness | Baked map + **Slab** (Sphere for hero props). 🔴 Never shadow-map-derived thickness | 🚩 |
| Volume units | ⚠️ `thicknessFactor` = **mesh** space, `attenuationDistance` = **world** space | 🚩 |

⚠️ **Permutation discipline:** cap the specialization axis at the 4-bit model ID plus a small fixed
feature mask. Specialization happens only at pipeline-creation time; anything that changes per frame
belongs in push constants. Blender hit large G-buffer compile times on exactly this axis.

⚠️ **Do not build Substrate-style slabs yet.** Unreal's default budget is 80 B/px for 3-4 layers; a
GTX 1060 already carries a ~24 MiB channel G-buffer plus the GI budget. Build the switch; keep the shade
entry point behind one function so slabs can replace it post-1.0.

🚧 **Hair/fur is deliberately excluded** — it needs its own geometry pipeline (strands/cards, deep opacity
maps, sorted transparency), not a channel slot. Record in `Backlog.md`.

---

## 8. Residual caveats to budget

- ⚠️ XeGTAO ships as HLSL with no official Vulkan port and is unmaintained — budget a self-maintained
  HLSL → GLSL/SPIR-V fork for the AO channel.
- ⚠️ EON multiscatter constants and albedo-scaling coat/sheen layering omit a few percent of energy at
  grazing angles — fine for the Vulkan preview; expect a small mismatch vs a path-traced reference.
- ⚠️ Screen-space SSS is the rasterizer-practical integrator; the analytic Christensen–Burley R(r) is its
  kernel, not a closed-form shading term. 🟢 The vbuffer's instance ids double as the leak-rejection
  `object_id`, so Frontier avoids both Blender's pre-4.2 inter-object leaking and Unreal/Unity's sacrifice
  of the metallic G-buffer slot. Full detail + Pascal traps → research §7.
