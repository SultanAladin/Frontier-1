# PLAN — Production PBR Channels

🧩 Completing the PBR channel set end to end: joining the fourteen AUTHORED channels
(`ChannelSlotTable`) to the fourteen SHADED preset records (`SurfacePresetTable` + `SurfaceShade.frag`),
supplying the texture data both halves lack, and correcting the Fresnel energy accounting. Sequenced so
every step is visible on screen before the next begins.

> **TL;DR** — Two disconnected "14"s exist: an authoring panel that allocates no atlas, and a shade pass
> whose every channel is a flat per-material constant. Neither can show a textured surface because the
> vertex stream carries **zeroed texcoords, absent normals, and no tangent at all**. Fix the stream
> first (P0), then Fresnel energy (P1, zero dependencies), then the atlas → sampling → consumer chain.

🔴 Subordinate to `PLAN-UnifiedMaterialModels.md` and `RESEARCH-MaterialModels2026.md`. Where this plan
and the research document disagree, **the research document wins** (that plan's own §7 ruling).

---

## 1. The two channel sets, as they stand

|                | A — Authoring                                          | B — Shading                                                      |
| -------------- | ------------------------------------------------------ | ---------------------------------------------------------------- |
| Site           | `Executables/Validation/ChannelPropertyValidation/ChannelSlotTable.{h,cpp}` | `Internal/Graphics/Scene/SurfacePresetTable.{h,cpp}` + `Internal/Graphics/Visibility/Shaders/SurfaceShade.frag` |
| The fourteen   | 14 channel slots across 5 RGBA8 atlases                | 14 preset records (floor + 13 heads)                             |
| Reality        | ImGui store only — no atlas allocated, `StrokeCount` is a bare `int`, zero device resources | Flat per-material constants in a 96-byte std140 UBO         |
| Joined?        | 🔴 **No code path connects them.**                     |                                                                  |

### 1.1 A — the fourteen authored channels

Row order is the panel's draw order (`ChannelSlotTable.cpp:40`).

| #  | Channel           | Atlas · component      | Editor  | Span    |
| -- | ----------------- | ---------------------- | ------- | ------- |
| 1  | Base Colour       | Colour · RGB           | Colour  | —       |
| 2  | Metallic          | Material · R           | Scalar  | 0–1     |
| 3  | Roughness         | Material · G           | Scalar  | 0–1     |
| 4  | Height            | Material · B           | Scalar  | 0–1     |
| 5  | Normal            | *no storage · derived* | Derived | —       |
| 6  | Opacity           | Material · A           | Scalar  | 0–1     |
| 7  | Emissive          | Emissive · RGB         | Colour  | —       |
| 8  | Ambient Occlusion | Emissive · A           | Scalar  | 0–1     |
| 9  | Anisotropy        | Reflect · R            | Scalar  | 0–1     |
| 10 | Anisotropy Angle  | Reflect · G            | Scalar  | 0–360°  |
| 11 | Clearcoat         | Reflect · B            | Scalar  | 0–1     |
| 12 | Refraction Index  | Reflect · A            | Scalar  | 1–3     |
| 13 | Sheen             | Scatter · RGB          | Colour  | —       |
| 14 | Subsurface        | Sheen · RGB            | Colour  | —       |

⚠️ Two rows break the 0–1 assumption every other scalar shares: Anisotropy Angle spans 0–360°, and
Refraction Index floors at 1.0. Normalise against the slot's own span, never against unit range.

### 1.2 B — consumption gap in the shade pass

| Consumed today (flat constant)                                                   | 🔴 Authored in A, no shader consumer | 🔴 In neither |
| -------------------------------------------------------------------------------- | ------------------------------------- | ------------- |
| BaseColour.rgb + .a · Roughness · Metallic · Reflectance · Emissive + strength · SheenColour · SheenRoughness · CoatWeight · CoatRoughness · IridescenceIor · IridescenceThickness | Height · **Normal** · Opacity (outside Glass) · **Ambient Occlusion** · Anisotropy · Anisotropy Angle · Refraction Index · Subsurface-as-colour | Tangent frame · DFG / multiscatter LUT · F82-tint metal Fresnel · Fresnel energy split |

---

## 2. 🔴 Root blocker — the vertex stream, not the channel table

Three facts kill every texture-driven channel before it starts. Adding atlases without fixing the
stream first produces **nothing on screen**.

| Fact                                  | Evidence |
| ------------------------------------- | -------- |
| **Texcoords are zeroed at bake**      | `Internal/Graphics/Scene/ReferenceGeometryAsset.cpp:7` — "stride-32 RenderVertex (texcoord zeroed)". No `uv` / `texcoord` scan exists anywhere in the file. |
| **Normals are optional and absent**   | `ReferenceGeometryAsset.cpp:144` — `NormalsPresent = Normals.size() == VertexCount*3`; the reference asset records `normals = []`. The shader therefore derives a flat facet normal at `SurfaceShade.frag:558`. |
| **No tangent in the format**          | stride-32 = position@0, normal@12, texcoord@24. `SurfacePresetTable.h:12` states Anisotropic is *deliberately absent* for exactly this reason. |

💡 This is the mechanical cause of "the effect cannot be seen": there is no UV to sample a texture
with, and no tangent frame to orient a normal map or point an anisotropic lobe along.

⚠️ The stride-32 `RenderVertex` layout is **hand-copied into four shaders** (`SurfaceShade.frag`,
`VisibilityRaster.vert`, `SoftwareRasterization.comp`, `ComponentOverlay.frag`) plus a host
`static_assert`. A copy that falls behind still compiles and still validates — it merely strides by the
wrong size, so vertex N reads the tail of vertex N-1. Every mirror changes in ONE edit.

---

## 3. Fresnel — present in form, missing its energy accounting

`FresnelSchlick` already exists; what is absent is the energy bookkeeping around it.

| Piece                                              | Site                      | State |
| -------------------------------------------------- | ------------------------- | ----- |
| `FresnelSchlick(F0, VoH)` — RGB                    | `SurfaceShade.frag:292`   | ✔️ used by the specular lobe |
| `FresnelSchlickScalar(F0, VoH)`                    | `SurfaceShade.frag:298`   | ✔️ used by the coat at fixed f0 = 0.04 |
| f0 = `0.16·Reflectance²`, `mix(f0, base, Metallic)` | `SurfaceShade.frag:605`   | ✔️ |
| Diffuse attenuated by `(1 − F)`                    | `SurfaceShade.frag:638`   | 🔴 absent — diffuse and specular both take full light, so the surface GAINS energy |
| DFG split-sum / multiscatter compensation          | —                         | 🔴 absent — rough metals lose energy and read too dark |
| **F82-tint** metal Fresnel (Kutz 2021)             | —                         | 🔴 absent — plain Schlick; research §2.2 marks this a 🔴 correction |
| `Fresnel0ToIor`                                    | —                         | 🔴 absent — the Refraction Index channel has no consumer at all |

🔴 F82-tint and exact thin-film iridescence are **mutually exclusive**: F82-tint carries no complex IOR,
so the Airy phase term is unavailable, and the ratified iridescence spec assumes metal κ = 0. Keep the
existing ramp on the iridescent path and apply F82-tint only where iridescence is off.

---

## 4. Build order

Ordered by dependency, each step visible on screen before the next begins.

| Step   | Work | Why here | Visible result |
| ------ | ---- | -------- | -------------- |
| **P0** | **Vertex stream.** Scan `"texcoords"` / `"normals"` in the bake; widen stride-32 → **stride-48** (position@0, normal@12, **tangent@24** as 4 floats with w = bitangent sign, texcoord@40). Update all four shader mirrors + the host `static_assert` in one edit. | 🔴 Unblocks every texture channel and the whole anisotropic family | Smooth normals — Chrome / Clearcoat stop being faceted mosaics |
| **P1** | **Fresnel energy.** ① diffuse `× (1 − F)` ② DFG split-sum LUT for multiscatter compensation ③ F82-tint metal Fresnel ④ `Fresnel0ToIor` so Refraction Index becomes live. Pure shader math. | The one step with **zero** upstream dependency — lands immediately | Metals brighten correctly; grazing angles read physical |
| **P2** | **Procedural channel atlas prepass.** One compute unit generating the five RGBA8 atlases from the generator vocabulary already catalogued in `ChannelSlotTable.cpp:62` (Perlin, Voronoi, brushed-metal streaks, curvature wear). Real Roughness / Metallic / Height / AO / Normal variation. | Makes the effect visible with no asset pipeline | Surfaces stop being uniform colour |
| **P3** | **Shader sampling seam.** Bind the five atlases into the reserved set; sample per channel, mixing texture over the preset constant so an unbound channel falls back to today's look **exactly**. | The seam every later channel plugs into | Textured PBR on screen |
| **P4** | **Normal + Height + AO consumers.** Tangent-space normal map against P0's frame; parallax from Height; AO multiplying the **ambient fill only**, never the direct light. | The three channels carrying most perceived detail | Real surface relief |
| **P5** | **Anisotropy + Angle.** Anisotropic GGX with `αt = α(1+a)`, `αb = α(1−a)`. | Needs P0's tangent | Brushed / machined metal |
| **P6** | **Paint deposit → atlas.** Replace `StrokeCount:int` with a real deposit into the P2 atlases, so a painted stroke changes what P3 samples. | The A↔B join that does not currently exist | Hand-painted channels render |
| **P7** | **Disk loader** (KTX2 / PNG + mips) replacing a procedural map per channel at the P3 seam. | Deferred by design — the seam makes it a drop-in | Authored map sets |

🐞 **Anisotropy remap correction** (plan §7): use linear `αt = α(1+aniso)`, `αb = α(1−aniso)`. The
`sqrt(1 − 0.9·aniso)` variant is inconsistent with the aniso-GGX normalization — rejected.

---

## 5. Guard rails

- 🔴 **No tonemap and no tone-map inverse in `SurfaceShade.frag`.** It writes linear scene radiance;
  `RadianceResolve.frag` is the single site applying exposure and the PBR Neutral curve. A clipped
  highlight is an upstream or downstream fault — never a second operator here.
- 🔴 **`SurfaceShadeConstants` and the frag's `ShadeConstants` byte-match with no diagnostic.** Any push
  block change touches both in ONE edit, or every later scalar reads from the wrong offset.
- 🔴 **Force a `.spv` rebuild after editing any `.glsl`.** `ShaderPlan.ps1` keys on source mtime only, so
  an edited include ships a stale binary silently.
- ⚠️ Every step lands behind a **default-OFF toggle** and must be A/B-identical to the prior path before
  flipping (`PLAN-UnifiedMaterialModels.md` §6).
- ⚠️ AO multiplies the **ambient/indirect** term only. Folding it into direct light double-darkens
  contact regions the sun shadow already handles.
- ⚠️ Sheen `alphaG` follows glTF (`alphaG = r²`); Unreal's `D_Charlie` is pre-squared and differs.

---

## 6. Open scope notes

- 🚧 P0 is invasive by nature — a stride change across four hand-written shader mirrors, the bake, and the
  host assert. It is nonetheless the gate: no texture channel can render before it.
- 🚧 Anisotropy is currently documented as *deliberately absent* in `SurfacePresetTable.h:12`. P0 removes
  that justification; update the header comment when P5 lands so the note does not go stale.
- 🚧 `Refraction Index` (channel 12) and `Subsurface` (channel 14) have no `SurfacePresetParameters` field
  at all — they need record slots added, not merely a consumer.
