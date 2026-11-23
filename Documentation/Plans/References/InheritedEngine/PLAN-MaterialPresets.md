# PLAN — Named Material Presets → Model Mapping & GI Slot-In

🧩 The **preset layer** over `PLAN-UnifiedMaterialModels.md`: the artist-facing library of ≥10 named
materials (Fabric, Plastic, Metal, Clearcoat, Glass, Emissive/LED…), each resolved to one existing
shading model + a locked channel set with 2-3 variations — plus the contract for how presets slot into
`REFERENCE-ToroidalClipmapGI.md` given GI has **no channels and no code yet**.

> **TL;DR** — A material preset is **not** a new shading model. It is a **named channel-default bundle**
> over an existing 4-bit model ID. 12 preset families map onto 8 models (well inside the 4-bit / 14-ID
> budget). Presets touch the GI path in exactly **one** place — what each surface *contributes to* and
> *receives from* the SH probe field — a 4-role, model-agnostic contract addable at P7 with **zero new
> channels**.

🔴 **Read `PLAN-UnifiedMaterialModels.md` + `RESEARCH-MaterialModels2026.md` alongside this.** They own
the shading math (9 models, ~22 channels, F82-tint, slab order, SSS/iridescence/glass), verified against
OpenPBR v1.1.1 / UE 5.8 / ratified KHR. **No math is re-derived here** — this plan is authoring ergonomics
and the GI contract only.

---

## 1. Where this sits — the three-layer stack

| Layer | Owns | Doc |
| ----- | ---- | --- |
| **Presets** (this plan) | Named library: Fabric, Plastic, Metal… → model + channel defaults + variations | ← this file |
| **Models** | 9 shading models, ~22 channels, the BRDF/BSDF math, F82-tint, slab order | `PLAN-UnifiedMaterialModels.md` |
| **GI** | Toroidal-clipmap SH probe field + horizon-scan (all 🔴 not started) | `REFERENCE-ToroidalClipmapGI.md` |

💡 Presets are pure authoring ergonomics: pick a model, lock sensible channel defaults, hide the channels
that model doesn't use. This is Blender's Principled-BSDF-with-presets and Unreal's material-instance
pattern — **one master, many named instances** — not a new closure per name.

⚠️ **Current code reality (verified 2026-07-29).** The live path is the Visibility renderer
(`Internal/Graphics/Visibility/` — id + depth, cull, hardware + software raster, resolve). **No** channel
G-buffer, material shade, or shading-model code is live yet — `MaterialProperties.h` / `ChannelGBuffer` /
`SurfaceForward` / `DeferredShadePass` sit in the **retired** `Render/Surface/` + `Render/Illumination/`
trees, not compiled. So presets land at **M2–M4** of the model plan's build order, never before it.

---

## 2. What a preset IS (the data contract)

A preset is a small record — **no new shader code**:

```text
SurfacePreset {
  Name              "Brushed Aluminium"
  ShadingModelId    METAL             // 4-bit ID — the ONLY thing the über-shader branches on
  FeatureMask       ANISOTROPY        // small fixed feature bits within that model
  ChannelDefaults { BaseColor = .91,.91,.92   Roughness = .35   Anisotropy = .7   … }
  HiddenChannels  { Transmission, Sheen*, ClearCoat* }   // UI reveals only the rest
  ContributionRole  OPAQUE_DIFFUSE    // §6 — one of 4 GI roles (derivable, see §6)
}
```

🔴 **Preset → model is many-to-one.** "Plastic", "Rubber", "Ceramic", and a car-paint base all ride the
**Standard** model; they differ only in channel defaults + which coat/sheen feature bits are on. A preset
never adds a shading model, a channel, or a pipeline permutation.

📝 Naming: the library record is a `SurfacePreset`; the store is a `SurfacePresetLibrary`; the GI tag is a
`ContributionRole` (a role **by rendering function**, not an OO "role"). These slot under the model plan's
revived `Render/Surface/`.

---

## 3. The material library — ≥10 named families, with variations

🎯 12 families → 8 models. **Bold** = base preset; the variation column lists its 2-3 authored variants.

| #  | Material family | Model (4-bit ID) | Feature bits | Defining channels beyond base | Variations |
| -- | --------------- | ---------------- | ------------ | ----------------------------- | ---------- |
| 1  | **Plastic** | Standard | — | Roughness ≈ .4, Metallic = 0, Reflectance ≈ .5 | Glossy (r=.1) · Matte/ABS (r=.7) · Translucent (thin transmission) |
| 2  | **Metal** | Metal | F82-tint | chromatic f0 = BaseColor, Roughness, **no diffuse** | Polished (r=.05) · Brushed → §3 row 3 · Oxidized (r=.6 + AO) |
| 3  | **Brushed / Anisotropic metal** | Anisotropic | ANISO | Anisotropy, AnisotropyRotation | Radial-brushed · Linear-brushed · Machined |
| 4  | **Clearcoat / Car paint** | Clearcoat | COAT | ClearCoatWeight, ClearCoatRoughness, (ClearCoatNormal) | Car paint (metallic base) · Lacquered wood (dielectric base) · Wet coat |
| 5  | **Fabric / Cloth** | Cloth (Charlie) | SHEEN | SheenColor, SheenRoughness (glTF αG = r²) | Velvet (high sheen) · Satin (tight sheen) · Denim/Cotton (rough diffuse + low sheen) |
| 6  | **Glass** | Glass (BTDF) | TRANSMIT + VOLUME | Transmission, IOR, Thickness, Attenuation | Clear (thin) · Frosted (rough) · Tinted volumetric (attenuation) |
| 7  | **Ceramic / Porcelain** | Standard | COAT (opt) | BaseColor light, Roughness low, thin coat | Glazed (coat) · Bisque/matte (no coat) · Enamel |
| 8  | **Rubber** | Standard | — | BaseColor dark, Roughness ≈ .8, Reflectance low | Matte rubber · Tire (aniso hint) · Silicone (soft SSS-lite) |
| 9  | **Skin / Wax / Marble** | Subsurface (Burley) | SSS | ScatterDistance, transmission mode, dual-spec | Skin · Wax · Marble (deep MFP) |
| 10 | **Iridescent** | Standard / Metal + modifier | IRIDESCENCE | iridescenceFactor (tex R), thk min/max (tex G), IOR | Soap bubble · Anodized metal · Beetle / oil-slick |
| 11 | **Emissive / LED** | Emissive (shared channel) | EMISSIVE | EmissiveColor, EmissiveStrength (HDR > 1) | LED panel · Neon tube · Glowing screen (emissive **under** coat) |
| 12 | **Matcap / CAD preview** | Matcap (non-PBR) | MATCAP | baked sphere tex, no light loop | Clay · Metal-studio · Normal / checker debug |

🔴 **Coverage:** 12 families ≥ the requested 10, each with 2-3 variations = **~34 authorable presets** over
**8 model IDs** — inside the 4-bit / 14-ID budget with room to spare. Emissive (LED) is a **shared channel,
not a model**; LED = high EmissiveStrength kept > 1.0 so it survives into bloom.

⚠️ **Preset-specific traps carried from research (do not re-derive):**

- **Fabric** αG convention = glTF `r²`, stated at the call site (research §1.3) — *not* Unreal's
  pre-squared `D_Charlie`, or sheen won't match glTF.
- **Iridescent metal** is mutually exclusive with F82-tint in exact form (research §2.2 / §8.2). Preset
  rule: an iridescent-metal preset silently falls back to **Schlick-IOR** metal, documented in its tooltip.
- **Emissive / LED** enters **below** coat + fuzz in the slab (research §2.1) — a glowing screen *under* a
  glass coat, never "added last."
- **Skin / SSS** requires TAA; until TAA lands the preset ships on **Jimenez separable**, not Burley
  (research §7.5).

---

## 4. Full channel table — which preset authors which channel

Rows = the ~22 model channels. ● = authored default in the preset · ○ = optional / variation-only · · = hidden.

| Channel | Plastic | Metal | Aniso | Coat | Fabric | Glass | Ceramic | Rubber | Skin | Irid | Emiss | Matcap |
| ------- | :-----: | :---: | :---: | :--: | :----: | :---: | :-----: | :----: | :--: | :--: | :---: | :----: |
| BaseColor | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ○ | · |
| Roughness | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | · | · |
| Metallic | ● | ●=1 | ●=1 | ○ | ·=0 | ·=0 | ·=0 | ·=0 | ·=0 | ○ | · | · |
| Reflectance | ● | · | · | ● | ○ | · | ● | ● | ● | ● | · | · |
| Normal | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ○ | ○ |
| AmbientOcclusion (shared) | ● | ● | ● | ● | ● | ○ | ● | ● | ● | ● | ○ | · |
| DiffuseRoughness (EON) | ○ | · | · | ○ | ● | · | ○ | ● | ● | ○ | · | · |
| Anisotropy (+ Rotation) | · | ○ | ● | ○ | · | · | · | ○ | · | · | · | · |
| ClearCoat {Weight, Rough, Normal} | · | · | · | ● | · | · | ○ | · | · | · | · | · |
| Sheen {Color, Roughness} | · | · | · | · | ● | · | · | · | · | · | · | · |
| Transmission | ○ | · | · | · | · | ● | · | · | · | · | · | · |
| IOR | ○ | · | · | ● | · | ● | ● | · | ○ | ● | · | · |
| Thickness | ○ | · | · | · | · | ● | · | · | ● | · | · | · |
| Attenuation (Color + Distance) | · | · | · | · | · | ● | · | · | ○ | · | · | · |
| ScatterDistance (+ modes) | · | · | · | · | · | · | · | ○ | ● | · | · | · |
| Iridescence (factor R / thk G / IOR) | · | ○ | · | ○ | · | · | · | · | · | ● | · | · |
| Emissive {Color, Strength} | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ● | · |
| Matcap sphere texture | · | · | · | · | · | · | · | · | · | · | · | ● |

📝 This table **is** the "full channels" deliverable: every named material's complete authored surface and —
critically — its **hidden** set, which is what keeps the property panel bloat-free (a Glass preset never
shows a Sheen slider). The user's requested effects map as: **Refraction** → Glass (Transmission + IOR +
Thickness); **Anisotropy** → Aniso / Brushed rows; **Reflection / Fresnel** → Reflectance + F82-tint (Metal)
or Fresnel-mix (dielectrics); **Flakes** → an iridescence / coat variation, deferred (§7).

---

## 5. Integration into the current pipeline

Where each piece attaches to the **live** visibility spine — a slot-in at the model plan's M2–M4, not a
rewrite:

```text
LIVE TODAY:   P1 raster/visibility (id + depth) → InstanceCull → resolve id buffer
                     │
MODEL PLAN ADDS:     ├─ M1  Channel G-buffer (4 targets, ~24 MiB @ 1080p)
                     ├─ P4  MATERIAL CLASSIFY  → per-material-ID pixel lists      [compute]
                     └─ P5  MATERIAL SHADE     → über-shader, branch on 4-bit ShadingModelId
                               ▲
THIS PLAN ADDS:  ── the preset resolves to (ShadingModelId, FeatureMask, channel defaults).
                    The shader only ever sees the resolved model ID + channels — it has no concept
                    of "Fabric", only CLOTH + a Sheen channel.
```

🔴 **Key claim — a preset costs nothing extra in the shade loop.** It flattens to a model ID + channel
values; the specialization axis stays at the 4-bit model ID + a small fixed feature mask (research §5
permutation discipline). Presets do **not** multiply the pipeline count.

Where preset code physically lands (per `FolderStructure.md`):

- Preset records + library → revive `Render/Surface/MaterialProperties.h` from retired; add
  `SurfacePreset.h` + a `SurfacePresetLibrary`.
- Preset picker UI → `Interface/Components/` — a `SurfacePropertyPanel` (the model plan's M7
  `SurfacePropertyInterface`); the dropdown reveals only the preset's non-hidden channels.

⚠️ **OPEN DECISION — resolve at load-time, at runtime, or both.** Deferred by user (2026-07-29): both paths
are wanted — **runtime** resolution for live authoring (TexturePainting edits a preset and re-shades), and
**load-time flatten** for baked game content (a shipped scene carries resolved channels, no preset lookup).

| Path | When preset → (model ID, channels) happens | Fits | Cost |
| ---- | ------------------------------------------ | ---- | ---- |
| **Load-time flatten** | asset load / bake | shipped game scenes, `Codex` archive | zero runtime; no preset in the shader |
| **Runtime resolve** | authoring edit | TexturePainting, live preset tweaks | one indirection through a material table via the vbuffer id |

📝 Reconciliation (to confirm later): keep the `SurfacePreset` record as the **authoring-time** source of
truth; a live editor resolves it per-edit; the `Codex` serializer **bakes** the resolved channel block into
the shipped document. Same record, two consumers — decide the exact split at M7.

---

## 6. GI slot-in — the part with no channels yet

🔴 The real question: *how do materials fit the GI when GI has no channels?* Answer — **GI needs no
per-material channels of its own.** The SH probe field (`ToroidalClipmapField`, already a live stub in
`EngineContext/SpatialAcceleration/`) is populated from the **existing shade output**; each surface's
relationship to GI is fully determined by its **model ID + channels that already exist**. Presets add
exactly **one enum**: `ContributionRole` (4 roles), and even that is derivable from `ShadingModelId` +
`EmissiveStrength`.

```text
P5   MATERIAL SHADE   ── writes direct radiance + BaseColor + Normal + Roughness (already in G-buffer)
        │
P7a  horizon-scan GI/AO  ── reads G-buffer Normal + depth (HiZ). Needs NO new channel.
P7b  GiProbeField (SH)   ── INJECT: each shaded texel's outgoing diffuse radiance → nearest SH probe
        │                   GATHER: every opaque surface reads irradiance(Normal) back from the field
P7c  composite           ── Lo += albedo · SH_irradiance(N) · AO
```

The **4 contribution roles** — the whole preset ↔ GI contract:

| ContributionRole | Presets | Injects into SH field? | Gathers from SH field? |
| ---------------- | ------- | ---------------------- | ---------------------- |
| `OPAQUE_DIFFUSE` | Plastic, Fabric, Ceramic, Rubber, Skin | ✔️ diffuse albedo · radiance | ✔️ diffuse irradiance |
| `METALLIC_SPECULAR` | Metal, Aniso, Clearcoat | 🚩 weak (little diffuse) | ✔️ via reflection probe / IBL specular, **not** SH diffuse |
| `TRANSMISSIVE` | Glass | 🔴 no — handled in the forward tail | ✔️ reads opaque-resolved framebuffer, not SH |
| `EMISSIVE` | Emissive / LED | ✔️✔️ **strong** — LEDs are GI light sources | — (self-lit) |

🔴 **The one signal GI genuinely gains from the preset layer:** knowing an **Emissive / LED** surface is a
**light emitter into the probe field**, not merely a bright pixel. That is the single new signal — and it is
already carried by the existing `EmissiveStrength` channel (> 1.0 ⇒ inject into SH). So even the GI hook
needs **no new channel** — only the rule *"route emissive radiance into the probe-injection step."*

💡 Why this is clean: Frontier is deferred with a channel G-buffer, so GI reads BaseColor / Normal /
Roughness that **every** model already writes — GI is model-agnostic by construction. Presets don't extend
the G-buffer for GI; they only tag which of the 4 roles a surface plays, and that tag is derivable from
`ShadingModelId` + `EmissiveStrength` alone. **GI integration = 4 rules, 0 new channels.**

⚠️ **OPEN DECISION — GI-hook scope.** Deferred by user (2026-07-29): whether to (a) define the 4-role
contract now as interface-only and wire it when P7 GI actually lands (all GI is 🔴 not started), or (b) also
draft the emissive-injection compute step ahead of the rest of GI. Recommendation pending that call:
**contract-only** until P5c/P7 exist, since there is no probe field to inject into yet.

---

## 7. Build order + deferred work

```text
Presets ride the model plan's milestones — not a separate track:
  M2  Standard + Metal shade live  ──► presets: Plastic, Metal, Rubber, Ceramic, Emissive/LED, Matcap
  M4  + Clearcoat, Cloth, Aniso    ──► presets: Clearcoat/Car-paint, Fabric family, Brushed metal
  M5  Glass forward tail           ──► presets: Glass family
  M6  Subsurface + Iridescence     ──► presets: Skin/Wax/Marble, Iridescent family
  M7  SurfacePropertyPanel         ──► preset dropdown + per-preset channel reveal/hide; resolve the §5 open decision
  P7  GI probe field lands         ──► wire the 4 ContributionRoles (emissive-inject is the only new line); resolve the §6 open decision
```

🚧 **Deferred to `Backlog.md`** (the flake / anisotropy-refraction extras raised in the request):

- **Flakes** (metallic-flake car paint, glitter) — a procedural / normal-perturbation variation on
  Clearcoat; needs a flake normal map or procedural node, not a channel. Post-1.0.
- **Refraction anisotropy** (stretched refraction through brushed glass) — needs an anisotropic BTDF; the
  Glass tail ships isotropic first.
- **Reflection anisotropy** — already covered by the Anisotropic model (M4); no separate work.
- **Hair / fur** — already deferred by the model plan (a separate geometry pipeline, not a channel).

---

## 8. Open decisions (owner: user)

| # | Decision | Options | Status |
| - | -------- | ------- | ------ |
| 1 | Preset resolution timing (§5) | load-time flatten · runtime resolve · **both** | 🚧 both wanted; exact split at M7 |
| 2 | GI-hook scope (§6) | contract-only · also draft emissive-inject | 🚧 deferred to P7 |
