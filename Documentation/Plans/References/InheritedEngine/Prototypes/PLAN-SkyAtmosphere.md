# PLAN — Sky / Atmosphere (Hillaire 2020) in RenderExtensionValidation

**Status:** approved to build (2026-07-26). Model locked: **Hillaire 2020 only**.
**Renders in:** `Binaries/RenderExtensionValidation` (app → `SynthesizeOutputSequence`).
**Source home:** new live module `Internal/Render/Sky/` (folds into `Render.lib` via the auto-glob).
**Mines from archive:** `Archive/DormantAtmosphere/` — its *shaders + constants* only (physics is real); its
C++ is a hollow shell (see Root Cause) and is **not** revived as-is. Archive is left in place (copy, never move-delete).

---

## Sky model decision

| Criterion | **Hillaire 2020** (chosen) | Wilkie–Hosek 2021 | Bruneton–Neyret 2008 |
|---|---|---|---|
| Type | Precomputed LUT chain | O(1) analytic fit | Precomputed 4D LUT |
| Night / day→night | ✅✅✅ | ❌ | ✅✅ |
| Aerial-perspective haze | ✅✅✅ | ❌ | ✅✅ |
| Per-frame cost | ~0.4 ms | ~0.1 ms | ~0.4 ms |
| VRAM | ~4.3 MB | ~256 KB | ~8 MB+ |
| Boot bake | ~12–15 ms | 0 ms | heaviest |
| Build-from-current | finish gap | fresh | heaviest |
| Industry use | Unreal, Godot | Blender legacy | ancestry |

Hillaire: only candidate with day→night + haze, matches the research pick, reuses existing shaders/constants.

---

## Root cause of the previous "I don't know how to fix it"

The archive looks 60% built but is a **hollow shell that compiles and renders nothing real**:

| Archive file | Reality |
|---|---|
| `AtmosphereSettings.h` | ✅ Real Bruneton earth constants — **reuse** |
| `Transmittance.comp`, `Inscatter.comp`, `PhaseFunctions.glsl` | ✅ Real shader math — **reuse/adapt** |
| `SkyAtmosphere.frag` | ⚠️ Fake — line 102 `TODO: fetch transmittance`; substitutes `exp(-Altitude*0.1)`; marches 32×10 km = 320 km through a 6360 km planet → garbage |
| `AtmosphericScatteringPass.cpp/.h` | ⚠️ Every `Create*Lut` / `Initialize*Pipelines` / `Record*Bake` is an empty `TODO` — allocates no image, dispatches no shader; also uses `class`+global-singleton (violates struct+free-function) and depends on VMA (not linked here) |
| `SkyAtmospherePass.*`, `SolarSourcePass.*`, `SolarSource.frag` | ❌ One-line `// Placeholder` |

So the sky-dome draw pass and sun-disc pass never existed, and the one frag that exists never samples the LUTs.

---

## Approach — mirror the GroundGridPass idiom (not the archived class)

`Internal/Render/Grid/GroundGridPass.{h,cpp}` is the template: struct + free functions, raw Vulkan
(no VMA — `VulkanHost.Allocator` is nullptr), SPIR-V loaded from a `FRONTIER_*_SHADER_DIR` baked path,
recorded into the substrate's already-open dynamic-rendering scope. The sky adds one thing the grid
lacks: **descriptor sets** (to sample the LUT textures) and **image/UBO allocation** (first in this layer —
a small self-contained `FindMemoryType` + image-create helper lives inside the sky module).

Integration seam (`RenderExtension.cpp:222 SynthesizeOutputSequence`): the substrate clears→opens
dynamic rendering→calls `RecordSequence`. Sky records **first** (fills the whole framebuffer), grid
records over it. Boot bakes run once in `InitializeRenderExtension`.

---

## Module layout (new, under `Internal/Render/Sky/` — auto-globbed into Render.lib)

```
Internal/Render/Sky/
  SkyAtmosphere.h            struct SkyAtmospherePass, SkyAtmosphereConstants, free fns
  SkyAtmosphere.cpp          init (bake LUTs) / record sky+sun / finalize
  AtmosphereProfile.h        earth constants + std140 UBO mirror (from AtmosphereSettings.h)
  Shaders/
    Transmittance.comp       transmittance LUT (adapt archive)
    MultiScatter.comp        isotropic multi-scatter LUT (adapt archive Inscatter)
    SkyView.frag  + .vert    sky-view LUT bake (fullscreen tri) — REAL LUT sampling
    SkyDome.frag  + .vert    per-frame sky dome: sample sky-view LUT along view ray
    SolarDisc.frag           sun disc with limb + transmittance tint
    AtmosphereCommon.glsl     phase fns + shared UBO block
```

`Render/Build.bat`: add a `FRONTIER_SKY_SHADER_DIR` define + `glslc` lines mirroring the grid block.
No source-list edit (the `.cpp` glob picks up `Sky/`).

---

## Build sequence (all in this pass)

1. **Constants + UBO** — `AtmosphereProfile.h` from `AtmosphereSettings.h`; std140 UBO, persistent-mapped.
2. **LUT resources** — allocate transmittance 256×64, multi-scatter 32×32, sky-view 192×108 (RGBA16F);
   samplers + descriptor sets; `FindMemoryType` helper.
3. **Boot bakes** — record transmittance → multi-scatter → sky-view once, with image barriers; submit on
   the graphics queue in `Initialize`; log ms + VRAM to a trace file.
4. **Fix the real bug** — `SkyView` shader samples transmittance LUT correctly and marches in correct
   km-space (this is the `exp()` fake replaced).
5. **Sky-dome pass** — per-frame fullscreen-triangle: reconstruct view ray from inverse-view-projection
   (same source the grid uses), sample sky-view LUT, output radiance. Records first in the sequence.
6. **Sun disc** — `SolarDisc.frag`: angular-radius disc along `SolarDirection`, transmittance-tinted, soft limb.
7. **Sun-move rebake** — re-bake sky-view only when `SolarDirection` changes (dirty gate).
8. **Outliner UI** — from `Documentation/Mockups/AtmosphereOutliner.html`: sun elevation/azimuth, turbidity,
   exposure, layer toggles → drive `SkyAtmosphereConstants` / UBO.
9. **Build + verify** — `RenderExtensionValidation/Build.bat` via PowerShell; confirm the window shows a
   real gradient sky + sun, day→night as elevation drops.

## Naming compliance
GPU units end `…Pass`; `SkyAtmosphereField`/`…Pass` (no `System`); verbs Evaluate/Decode/Resolve/Record/
Construct/Initialize/Finalize; PascalCase constants, no `k-`. Sun vector = `SolarDirection`.

## Guardrails
- Never delete the archive — copy shader math out, leave the tree.
- A/B by toggle where it touches shared code; sky is additive here (new module only).
- Pascal / GTX-1060 floor: RGBA16F sampling + fullscreen triangles only; no compute-only-storage tricks.

## Follow-on roadmap (after core sky lands)
1. **Sky (this doc)** — Hillaire core chain: transmittance + multi-scatter + sky-view + sky-dome + sun disc.
   Aerial-perspective froxel deferred (no scene geometry in the validation app yet).
2. **Fog** — height/distance fog integrated with the atmosphere transmittance; froxel/aerial-perspective
   volume becomes worthwhile here once it fades over geometry. Same `SolarDirection` + LUTs feed it.
3. **Clouds** — volumetric cloud layer (raymarched), lit by the same sun + sky-view radiance so it stays
   consistent with the atmosphere; composits under the sky dome, over fog.
