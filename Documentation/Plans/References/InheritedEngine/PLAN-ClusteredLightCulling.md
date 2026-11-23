# PLAN — Clustered Light Culling (Deferred / Forward+)

🧩 The one authoritative plan for Frontier's many-light path: which culling structure to build, why,
what it costs in memory and milliseconds, and in what order to build it. Backs the `P5 clustered
lights` backlog item, which is 🔴 **confirmed absent** — verified, not assumed.

> **TL;DR** — Build **tiles + Z-bins, deferred, scalarized** (the Drobot 2017 / EEVEE Next structure),
> **not** the UE5/Godot 3D froxel grid. Same memory, ~450× the Z resolution, and it drops into the
> existing full-screen visibility resolve with no depth prepass because world position is already
> reconstructed there. Defer the whole stochastic family (MegaLights / ReSTIR / HypeHype) behind TAA.

🔴 **Two corrections to widely-repeated claims** — both verified against primary sources this session:

| Common claim | Reality |
|---|---|
| Z-binning is Sebastian **Aaltonen**'s | **Michal Drobot** (Activision/Infinity Ward), SIGGRAPH 2017. The analysing blog is Sebastian **Sylvan** — name collision. Aaltonen's relevant work is the HypeHype mobile talks |
| UE5 is "clustered deferred" | **Wrong for the opaque path.** UE shades each light individually via traditional deferred; the light grid serves translucency, volumetric fog, reflection environment and Lumen only |
| MegaLights **replaces** clustered culling | It **consumes** it. The mobile port requires `r.Forward.LightLinkedListCulling=1` + `r.Forward.MaxCulledLightsPerCell=32`; Epic *rebuilt* the grid for it (>0.6 ms → <0.2 ms) |

---

## 1. Current state — why this is 🔴 absent

| Where | What exists today |
|---|---|
| `Internal/Graphics/Visibility/Shaders/SurfaceShade.frag:126` | ONE `vec4 LightDirection` push-constant — a single analytic directional light |
| same, `:274-276` | `const vec3 LightColour` · `const float LightIntensity` · `const vec3 AmbientColour` — compile-time constants |
| anywhere | ❌ no light list · ❌ no light buffer · ❌ no culling structure · ❌ no light count |

The shade unit is a **full-screen fragment unit reading a packed `R32_UINT` identity buffer**
(Burns & Hunt 2013 deferred texturing), reconstructing world position analytically via
Möller-Trumbore at `:322-324`. That architecture decides the recommendation below — see §5.

---

## 2. The structural finding — two families, one clear winner

Every shipping engine picks one of two data-structure families. The post-2017 trend is decisive:

| Family | Storage | Z resolution at ~1 MB | Engines |
|---|---|---|---|
| True 3D clusters `O(X·Y·Z)` | per-cell index list | **18 slices** | UE5 · idTech 6 · Godot 4 · HDRP-clustered |
| **Tiles + Z-bins** `O(X·Y + Z)` | 2D tile bitmask + 1D Z LUT | **8096 slices** (+32 KB) | **COD:IW · EEVEE Next · Unity URP Forward+** |

💡 Drobot's own published table: `240×135` tiles cost 1036 KB **either way**; adding Z-bins buys
**8096 Z subdivisions for 32 KB**, against 18 for the equivalent-memory clustered grid. That single
comparison is the whole argument, and it is why every redesign after 2017 chose the second family.

---

## 3. Engine survey — forward+ AND deferred, as shipped

| Engine | Ships | Tile | Z slices | Z law |
|---|---|---|---|---|
| **UE5** (default) | deferred — ⚠️ grid does NOT shade opaque | 64×64 | 32 | exponential |
| **UE5** (forward) | opt-in Forward+ | 64×64 | 32 | exponential |
| **EEVEE Next** (4.2+) | forward, tiles+Zbin | **adaptive ≥32** | **4096** | **linear** |
| **Godot 4** | clustered **forward** | 32×32 | 32 | linear |
| **Unity HDRP** | **FPTL tiled for deferred**; clustered for forward/transparent | 16 / 32 / 64 | 64 | geometric, **per-tile adaptive base** |
| **idTech 6** (DOOM) | clustered forward, **CPU-built** | 16×8×24 grid | 24 | logarithmic |
| **COD:IW** | tiles+Zbin — **explicitly rejected clustered** | 8×8 | 8096 | linear |
| **Frostbite** | tiled deferred (the 2011 origin) | ⚠️ unverified | — | — |

**Z-law verdict:** linear is fine *if* you can afford thousands of bins (EEVEE 4096, COD 8096).
Exponential/logarithmic is a workaround for being stuck at 18–32 slices. Godot's linear-at-32 is the
weakest distribution surveyed. HDRP's per-tile adaptive log base is the most sophisticated.

⚠️ **Vulkan-specific:** EEVEE's scalarization (`simd_min` / `simd_broadcast_first`) is `#ifdef
GPU_METAL` **only** — on the Vulkan backend it currently forgoes wave-uniform reduction, needing
`GL_KHR_shader_subgroup_ballot` + `_arithmetic` or Vulkan 1.1. Godot *does* use real subgroups
(`subgroupOr` merges Z bits across a wave before a single `atomicOr`). Since scalarization was worth
**12%** in Drobot's ablation, follow **Godot's** approach here, not EEVEE's.

---

## 4. 🔴 Decision table — what to use and why

**Cost = % of a 16.6 ms (60 fps) frame.** Accuracy: 🔴 = **best**. Cost / VRAM / Effort: ✔️ = **low = better**.

| Option | Cost (GPU) | VRAM | Accuracy | Effort | Lights | TAA needed | Quality | Rank |
|---|---|---|---|---|---|---|---|---|
| **Tiles + Z-bins, deferred, scalarized** | ✔️ | ✔️ | 🔴 | 🚩 | ~10⁴ | no | ⭐⭐⭐⭐⭐ | 🥇 🏆 |
| Tiles + Z-bins, forward+ | ✔️ | ✔️ | 🔴 | 🔴 | ~10⁴ | no | ⭐⭐⭐⭐ | 🥈 |
| 3D froxel clusters (UE5/Godot) | 🚩 | 🚩 | 🚩 | 🚩 | ~10³ | no | ⭐⭐⭐ | 🥉 |
| FPTL fine-pruned tiled (HDRP) | 🚩 | ✔️ | 🔴 | 🔴 | ~10³ | no | ⭐⭐⭐⭐ | 🏅 |
| Plain 2D tiled (no Z) | 🔴 | ✔️ | ✔️ | ✔️ | ~10² | no | ⭐⭐ | 🏳️ |
| Stochastic tile sampling (MegaLights / HypeHype) | 🚩 | 🚩 | 🔴 | 🔴 | ~10⁵ | 🔴 **yes** | ⭐⭐⭐⭐⭐ | 🏴 blocked |
| Light BVH / light tree | 🔴 | 🚩 | 🚩 | 🔴 | ~10⁵ | 🔴 **yes** | ⭐⭐⭐ | 🏴 blocked |
| Single directional (today) | ✔️ | ✔️ | — | ✔️ | 1 | no | ⭐ | 🏁 current |

⭐ **Recommendation: tiles + Z-bins, deferred, scalarized.** Reasons, in order of weight:

1. **Same memory as clustered, ~450× the Z resolution** (§2). Not a tradeoff — a strict improvement.
2. **It composes with the existing shade unit for free.** Olsson 2012's key property: the cluster
   lookup is a double indirection where each fragment computes its own cluster id — *independent of
   forward vs deferred, only the source of position/Z changes*. `SurfaceShade.frag:324` already holds
   exact world position, so **no depth prepass is needed**.
3. **Deferred over forward+** because the shade unit is already a single full-screen unit over a
   packed id buffer. Forward+ would mean re-plumbing light iteration into every draw — 🔴 higher
   effort for no gain on this architecture.
4. **It is where every post-2017 redesign landed** — COD:IW, EEVEE Next, Unity URP Forward+.
5. **No TAA dependency**, unlike every stochastic option (§6).

🏷️ *Tagged for M-series render work; sequenced in §7.*

---

## 5. Why the visibility-buffer architecture changes the answer

✔️ **Clustering drops in essentially unchanged** — per §4 reason 2.

✔️ **Already tile-coherent.** HypeHype's per-tile sampling exists to fix wave divergence in exactly
this kind of full-screen unit. The shade unit has no per-material branching to untangle first.

📝 **Literature gap, stated plainly:** there is **no** peer-reviewed paper on the visibility-buffer +
many-lights combination. It lives entirely in engine talks and practitioner blogs. Do not search for
a citation that does not exist. Related: UE5's Nanite **resolves its visibility buffer into a
G-buffer and then lights that** — it does not light directly from the visibility buffer.

⚠️ **Two blockers already recorded in `EngineDocs/Backlog.md`:**

| Blocker | Consequence |
|---|---|
| `Graphics.lib` does not compile (`InstanceOrigins`, Backlog §"`Graphics.lib` does not compile") | 🔴 no lighting work is verifiable in-app until this clears |
| Flat geometric normals (`SurfaceShade.frag:339`) | many-light shading on faceted normals reads as per-facet banding, magnifying the existing limitation |

---

## 6. Resource usage

### 6.1 Memory — the structure itself

Tile bitmask = `TileCountX · TileCountY · ceil(LightCount / 32)` words. Z LUT = `BinCount · 4` B
(two 16-bit min/max light ordinals per bin). **Independent of tile count** — that is the point.

| Resolution | Tile | Tiles | Lights | Bitmask | Z LUT (4096 bins) | **Total** |
|---|---|---|---|---|---|---|
| 1920×1080 | 8×8 | 240×135 | 1024 | 4.05 MB | 16 KB | **4.07 MB** |
| 1920×1080 | 16×16 | 120×68 | 1024 | 1.02 MB | 16 KB | **1.04 MB** |
| 1920×1080 | 32×32 | 60×34 | 1024 | 261 KB | 16 KB | **277 KB** |
| 2560×1440 | 16×16 | 160×90 | 1024 | 1.80 MB | 16 KB | **1.82 MB** |
| 2560×1440 | 32×32 | 80×45 | 1024 | 461 KB | 16 KB | **477 KB** |
| 2560×1440 | 16×16 | 160×90 | 256 | 461 KB | 16 KB | **477 KB** |

Comparison at matched memory — the §2 argument in concrete numbers:

| Structure | Storage | Z resolution |
|---|---|---|
| Tiled only, 240×135 | 1036 KB | 1 |
| **Tiled + Z-bin, 240×135** | **1036 KB + 32 KB** | **8096** |
| Clustered, 60×32×18 | 1106 KB | 18 |

Plus the light store itself: a 64-byte light record × 1024 lights = **64 KB**. Negligible.

📝 **Tile-size guidance (Wicked Engine, corroborated by EEVEE):** use **16×16 or 32×32 for culling**
but **8×8 for the shading unit** — they need not match. Start at 32×32; it is 277 KB at 1080p and the
cheapest thing to widen later.

### 6.2 Time — measured, from primary sources

**COD:IW ablation, PS4 1080p, 256-bit array** — the cleanest four-way comparison published:

| Config | Opaque | Occupancy |
|---|---|---|
| Base tile | 5.7 ms (100%) | ~3 |
| + Z-bin | 5.2 ms (91%) | ~3 |
| Scalarized | 5.1 ms (88%) | 4.3 |
| **Scalarized + Z-bin** | **4.6 ms (80%)** | 4.3 |

Second scene (Hangar Fire): 9.00 ms → **7.65 ms (15% saved)**.

💡 **Scalarization is worth 12% on its own** and is nearly free to add — see the Vulkan note in §3.

**Wicked Engine 2.5D depth bitmask** — best cost/benefit datapoint in the entire survey:

| Config | Cull | Shade |
|---|---|---|
| 2D culling | 0.53 ms | 10.72 ms |
| **2.5D depth bitmask** | **0.64 ms** | **7.64 ms** |

**+0.11 ms of culling buys ~3 ms of shading.** ⚠️ GPU and resolution not stated in the source — treat
the ratio as sound and the absolutes as indicative.

**Grid build cost (UE5, thousands of lights):** >0.6 ms → **<0.2 ms** after splitting into coarse +
main units with HZB culling of occluded cells. Epic's own caveat: "this likely could be optimized
further."

**Budget for Frontier** — target ≤ 0.3 ms cull at 1440p / ~1024 lights, on the §6.1 32×32 config.

### 6.3 What the blocked options would cost

For reference only — 🏴 blocked on TAA, see §7.

| System | Platform | Lights | Cost |
|---|---|---|---|
| MegaLights | PS5 1080p, 1 spp, async off | 941 area, **all shadowed** | **5.51 ms** total, replacing *all* direct lighting (sampling 0.7 / SS trace 0.47 / HW RT 1.35 / shade 0.55 / light list 0.05 / denoise 0.96) |
| MegaLights mobile | Mali-G1 (12 core), UE 5.7 | ~195 point | **41 fps** vs **8 fps** traditional deferred; 2.35 M vs 57.0 M rays (~24×) |
| HypeHype STB | $99 phone → high-end PC | scene-independent | reservoirs in 4×4 px regions — **1080p needs only a 60×36 target** |

---

## 7. Build order

| Phase | Deliverable | Gate |
|---|---|---|
| **P0** | 🔴 clear the `InstanceOrigins` compile break | `Graphics.lib` links; app runs |
| **P1** | Light record store + N analytic point/spot lights, **no culling** — brute-force loop in the shade unit | visual correctness vs the single-light reference |
| **P2** | Tile bitmask build unit (compute) + consumption in the shade unit | identical image to P1, faster at N > ~32 |
| **P3** | Z-bin LUT (linear bins, 4096) + `zbin_mask` range narrowing | identical image; measure against the §6.2 budget |
| **P4** | Scalarization via `GL_KHR_shader_subgroup_ballot` + `_arithmetic` | ~12% expected (Drobot); confirm on target hardware |
| **P5** | 2.5D depth bitmask | +~0.1 ms cull, expect multi-ms shade win |
| **P6** | 🏴 stochastic tier — **blocked on TAA** | revisit only once TAA lands |

📝 **Naming** (per `SKILL-Naming.md` — `Pass` and `stage` are banned; render units take a mechanism
suffix): the culling compute unit is a `…Partition` / `…Submission`, the consumption side lives in the
existing shade unit. Candidate names to gate before use: `LightTilePartition` (builds the bitmask),
`LightDepthBinTable` (the Z LUT), `LightRecordStore` (the light array). Run the §0 6-gate test at
authoring time — do **not** reuse these unchecked.

⚠️ **Do not build P6 before TAA.** Every stochastic path requires temporal accumulation. This is the
same constraint the Backlog's SSS entry already records for Burley — one missing dependency, two
blocked features.

---

## 8. Verified negative results

Stated so they are not re-researched later. These are 🔴 **confirmed absences after targeted search**,
not gaps in the search:

- ❌ **No peer-reviewed 2024–2026 paper on tiled/clustered light binning.** The problem is considered
  solved-enough; research energy moved to stochastic sampling.
- ❌ **No paper on visibility-buffer + many-lights** (see §5).
- ❌ SIGGRAPH 2026 Advances and I3D 2026 contain **no** light-culling or deferred-shading talk.

💡 **What did happen instead:** two production systems (UE5 MegaLights, HypeHype) independently
converged on the same architecture — **keep the clustered grid as a spatial acceleration structure,
then stochastically sample a fixed number of lights from it per tile.** The grid became the
*candidate generator* rather than the thing you iterate. This is why §4 recommends building the grid
now: it is the prerequisite for the stochastic tier, not a detour around it.

**Epic's documented case against full ReSTIR** (the most decision-relevant material found): needs
2–3× more traces than 1 spp; candidate sampling evaluates ~20% of the light list per pixel; BRDF
weighting ignores shadowing so occluded lights dominate; and *"ReSTIR is not sample guiding"* — it
repeats discrete samples, and *"denoisers don't like correlation… sampling and denoising improvements
partially cancel each other."* Against light trees: no visibility term, LOD merging caused leaking,
per-frame GPU rebuild "doesn't translate well to GPUs."

---

## 9. Sources

Primary sources preferred; source-quoting analyses flagged where they substitute for gated repos.

| Source | Venue / year | Holds |
|---|---|---|
| Drobot, *Improved Culling for Tiled and Clustered Rendering* | SIGGRAPH 2017 Advances | 🔴 **the Z-bin origin** — algorithm, memory table, PS4 ablation |
| Olsson, Billeter, Assarsson, *Clustered Deferred and Forward Shading* | HPG 2012 | the foundation; the forward/deferred composability property (§5) |
| Burns & Hunt, *The Visibility Buffer* | JCGT 2(2), 2013 | Frontier's shade architecture; already discusses Forward+ tile culling |
| Sousa et al., idTech 6 / DOOM | SIGGRAPH 2016 | `Z = Near·(Far/Near)^(slice/N)`; 16×8×24; CPU-built |
| Mikkelsen, FPTL | GPU Pro 7 | HDRP's tiled-deferred fine pruning |
| Blender EEVEE Next source | 4.2+ | `CULLING_ZBIN_COUNT 4096`, `LIGHT_CHUNK 256`, adaptive tile size, ⚠️ Metal-only scalarization |
| Godot 4 `cluster_render.glsl` / `cluster_store.glsl` | — | 32×32, `subgroupOr` before `atomicOr`, min/max Z range |
| Turánszki (Wicked Engine), 2.5D depth bitmask | 2018 | the §6.2 cull/shade tradeoff; 8×8-for-shading guidance |
| Narkowicz & Costa, *MegaLights* | SIGGRAPH 2025 Advances | PS5 timings; the anti-ReSTIR argument; grid rebuild 0.6→0.2 ms |
| Lempiäinen, *Stochastic Tile-Based Lighting in HypeHype* | SIGGRAPH 2025 Advances | closest match to this architecture; per-tile not per-pixel |
| Zhang, Lin, Wyman, Yuksel, *Many-Light Rendering Using ReSTIR-Sampled Shadow Maps* | CGF 44 / EG 2025 | 🔴 **the one stochastic path that works WITHOUT ray tracing** — 10–20 full-res maps replace 210–611; <1.0 ms overhead |
| Tokuyoshi et al., *Hierarchical Light Sampling with Accurate Spherical Gaussian Lighting* | SIGGRAPH Asia 2024 | modern light-tree baseline; 1.4× faster |
| Lin, Kettunen, Wyman, *ReSTIR PT Enhanced* | I3D 2026 | 2–3× faster; duplication maps attack the correlation Epic cited |
| Chen et al., *LightOpt* | SIGGRAPH 2026 | orthogonal — *reduce* light count 18–52% at authoring time |

⚠️ **Unverified, flagged:** UE5 cvar defaults and `GetLightGridZParams` constants (Epic's repo is
account-gated; figures came from source-quoting analyses); Frostbite's DX11 tile size; Wicked Engine's
GPU/resolution for the §6.2 absolutes. 📝 Ouyang 2021 ReSTIR GI was **not** independently confirmed —
verify before citing.

🔴 **If a future session finds this document disagrees with measured numbers on Frontier's own
hardware, the measurement wins.** Every figure here is from another engine on other hardware.
