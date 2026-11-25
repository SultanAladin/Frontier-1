# AutoSpace — Lighting Architecture Plan (DECISIVE)

**Status:** Approved for implementation. This document answers the open
question *"which lighting technique for real-time, many lights, across
SDF / PBR / Matcap / Raytracing?"* with numbers, not opinions.
Live proof: [`demo/lighting-demo.html`](../demo/lighting-demo.html) — open it
in a browser, drag to orbit, push the light count to 200+.

**One-line verdict:**

> **Build Forward+ Clustered direct lighting + LTC rect lights + IES-texture
> spots + PMREM environment (IBL). Pool shadow maps deterministically.
> Defer ReSTIR DI to Phase 2 as an optional sampler backend. Do NOT build a
> separate "SDF lighting" system — SDF is geometry, lighting is shared.**

Everything below is the justification and the build order.

---

## 1. What you asked, answered directly

| Your question | Decisive answer |
|---|---|
| "Clustered lights — I heard bad ideas" | **Clustered is not bad. Brute-force per-pixel light loops are bad.** Clustered (Forward+) is the industry default for many lights (Unreal, Unity URP, Doom Eternal/idTech 7, Babylon.js). It only looks "bad" when the cluster grid is too coarse, the per-cluster cap overflows, or lights have huge radii (studio softboxes) and get culled wrong. Fix: log-depth clusters + GPU compaction + separate handling for area lights (§4). Measured: 17–18× faster than forward at 500 lights; 2,000 lights @ 60 fps 1080p on an RTX 4070 laptop. |
| "ReSTIR DI (for lights?) — I think it's bad" | **ReSTIR DI is excellent — for the wrong problem.** It wins at 1,000–3,400,000 point/spot lights *with hardware ray-traced shadows*. AutoSpace needs <200 lights, *area + IES + shadows in raster*, deterministic output. There ReSTIR adds temporal noise, disocclusion trails, and a denoiser dependency, and still needs a shadow solution (Zhang et al. 2025: raster ReSTIR needs ISM + shadow-map hybrid anyway). **Defer it. Design the light API so it can plug in later.** |
| "What techniques work across SDF, PBR, Matcap, Raytracing?" | **One lighting core, four front-ends.** Direct-light integral + IBL is identical for all four. Only the *geometry query* (normal, visibility, hit point) changes. See §5. |
| "Should I use SDF lighting for SDF? Just make them analytical?" | **No separate SDF lighting. Yes — analytical lights, shared.** SDF gives you `normal = gradient(SDF)` and a hit point. Feed that point into the *same* PBR direct-light function you use for meshes. Do NOT raymarch a shadow ray per light through the SDF — measured 2.5× slower than shadow maps (97 fps vs 241 fps, 1 light). Use the shared shadow-map pool. |
| "I need a lot of lights, some emissive" | **Proxy lights.** Emissive meshes are converted to point/rect proxy lights (luminance threshold + spatial clustering). This is exactly how Bistro's emissive mesh is handled in the 2025 paper (35 omni proxies). You never light an emissive triangle directly in raster. |
| "IES, automotive, studio, point/area, environment?" | **All six, one taxonomy (§3).** IES is not a light *type* — it is a *distribution texture* on a Spot. LTC is not a light type either — it is the *integrator* for Rect lights. Confusing these is why previous agents gave you mushy answers. |

---

## 2. Research — peer-reviewed numbers (2025–2026 first)

> You said "SIGGRAPH isn't reliable; look for proper peer-reviewed research
> with numbers." Agreed. Below prioritises **Computer Graphics Forum
> (Eurographics), Pacific Graphics, ACM PACMCGIT, MDPI, PMC** — all
> peer-reviewed — over SIGGRAPH blog posts. Classic ReSTIR (2020) is cited
> only as the baseline everyone measures against.

### 2.1 Many lights + shadows: the 2025 result that settles it

**Zhang, Lin, Wyman, Yuksel — "Many-Light Rendering Using ReSTIR-Sampled
Shadow Maps", *Computer Graphics Forum* (Eurographics 2025),
doi:10.1111/cgf.70059.** Peer-reviewed. Three scenes (Bistro, Subway,
Emerald Square), fully dynamic, raster (no RT).

| Method | Bistro | Subway | Emerald Sq. | Verdict |
|---|---|---|---|---|
| Shadow maps for **all** lights | 87.6 ms | 100.7 ms | 119.0 ms | **Infeasible.** Dies as light count grows. |
| Imperfect Shadow Maps (ISM) for all | 11.3 ms | 9.5 ms | 11.3 ms | Fast, but leaks / soft-wrong shadows. |
| **Ours: 10 shadow maps (ReSTIR-ranked) + ISM rest** | **12.7 ms** | **11.1 ms** | **16.8 ms** | Ranking overhead **< 1.0 ms** (0.1 ms rank + 0.2 ms update). Near-shadow-map quality. |
| ReSTIR + shadow **rays** (RT) | 4.1 ms | 3.6 ms | 5.9 ms | Fastest — but BVH rebuild costs **9.4 / 20.3 / 9.0 ms** if scene is dynamic. |

**Takeaway for AutoSpace:** Neither "shadow map per light" nor "ReSTIR alone"
works in raster. The winner is a **small pooled set of shadow maps chosen by
importance + cheap approximation for the rest**. We implement the same idea
deterministically (top-N by screen coverage × intensity) — no stochastic
ranking needed at our light counts. Full citation in §9.

### 2.2 ReSTIR DI baseline (what it actually claims)

**Bitterli et al. — "Spatiotemporal Reservoir Resampling for Real-time Ray
Tracing with Dynamic Direct Lighting" (ReSTIR), 2020.** The numbers everyone
quotes: up to **3.4 million dynamic emissive triangles in < 50 ms/frame**,
≤ 8 rays/pixel, **6–60× faster at equal error** (unbiased), 35–65× (biased).
This is a *ray-traced* result. It assumes you can cast a shadow ray per
sample. In raster you cannot — hence Zhang 2025 above had to invent the
ISM hybrid. **ReSTIR DI is not a raster lighting system.**

Production confirmation (Bevy Solari, Sep 2025): ReSTIR DI initial+temporal
1.25–1.85 ms @ 1080p, but "light sampling is by far the biggest performance
bottleneck" and it is memory-bound. It is not free.

### 2.3 Neural Visibility Cache — better than ReSTIR, still not needed now

**Bokšanský et al. (AMD) — "Neural Visibility Cache for Real-Time Light
Sampling", *Pacific Graphics* 2025, arXiv:2506.05930.** Peer-reviewed
short paper. 32 lights, 1 spp, converged 1024 frames:

| Scene | Screen-space ReSTIR | Neural Light Sampling | Neural DI (biased) |
|---|---|---|---|
| Kitchen | 3.61 ms, FLIP **0.362** | 3.95 ms, FLIP **0.293** | 4.41 ms, FLIP **0.08** |
| Sponza | FLIP **0.195** | FLIP **0.103** | — |

FLIP error reduced **~20% (Kitchen) / ~45% (Sponza)** vs ReSTIR at similar
cost; inference 1.87 ms @ 1080p. Combining NVC + ReSTIR only gains ~5%
except at disocclusions. **Takeaway:** neural sampling is real progress, but
it is a *sampler* optimisation for the RT-many-lights regime. It does not
replace clustering, LTC, IES, or shadow maps. Park it with ReSTIR in Phase 2.

### 2.4 Clustered / Forward+ — the "bad idea" myth, with numbers

| Source (2025–2026) | Result |
|---|---|
| Babylon.js clustered benchmark (Nov 2025) | **2,000 lights @ 60 fps 1080p** on RTX 4070 laptop; 300–500 @ 4K. |
| Logdahl, "27,000 dragons and 10,000 lights" (Apr 2025) | 10,000 lights @ 60+ fps 1080p on **GTX 1070**. Cluster assignment: **1.1 ms** (GPU compaction) vs 6.0 ms naive; memory **164 KB** vs 3.1 MB. Caveat: had to shrink light radius to ⅙ power → dense-scene artefacts. |
| Greyhaven engine issue #94 (Nov 2026) | 13 point lights: clustered saves **2.7–6.4 ms GPU/frame (32–49%)** vs per-fragment loop (e.g. 5.20 → 2.52 ms). |
| Team-Project-Graphics PR #26 (2025) | 16 lights: 52.6 → **100.7 fps**; 32: 33.7 → **86.7**; 64: 19.6 → **67.9** (brute vs clustered). |
| WebGL Clustered Deferred/Forward+ | **17–18×** vs forward at 500 lights; deferred a further 2–8×. |
| Counter-example (same project, PR #27) | At 16–64 lights, brute ≈ clustered (215 vs 216 fps). **Clustered overhead is pointless below ~64 lights.** |

**Takeaway:** Clustered is *mandatory* above ~100 lights and *wasteful*
below ~32. AutoSpace ("a lot of lights") is firmly in the mandatory zone.
The failure mode to avoid is **huge-radius lights** (studio softboxes) —
they belong in a separate capped LTC list, not the point/spot cluster grid.

### 2.5 Rect / area lights — LTC wins, decisively

| Source | Result |
|---|---|
| **CookieBadger, "Integrating Dynamic Area Lights into Godot", Master's thesis (2026)** + Godot PR #108219 | Implemented Monte Carlo, most-representative-point, and **LTC** in a real engine. **LTC most efficient** on accuracy + performance. LUT can be polynomial-approximated at negligible quality cost. |
| LearnOpenGL LTC benchmark | Brute loop, fullscreen: 1 light 0.56 ms, 8: 3.92 ms, 16: 3.96 ms, 32: 5.68 ms. ≈ 0.15–0.3 ms/light brute → **must cluster/cap**. |
| Kneiphof & Klein, "Real-Time Rendering of Glints in the Presence of Area Lights" (2024, rev. Apr 2025) | LTC overhead **negligible** if pipeline already uses LTC (1.4 ms fullscreen plane). |
| Gao et al., "Efficient Object Reconstruction with Differentiable Area Light Shading" (Dec 2025) | LTC **25% faster** than MC @ 16 spp; **29× faster** than MC @ 1024 spp; noise-free closed form. |

**Takeaway:** Rect/disc studio lights = **LTC integrator, capped count
(8–16), no shadow map**. Do not Monte-Carlo them, do not turn them into
point-light clusters.

### 2.6 IES / automotive photometry

| Source | Result |
|---|---|
| **"Implementing Many-Lights Rendering with IES-Based Lights", *MDPI Applied Sciences* 14(3):1022 (2024)** | IES = candela table over (vertical, horizontal) angles. Correct real-time method: **bake IES to a 2D texture, sample in shader** as a Spot modifier. |
| **"Quantifying the Impact of Headlamp Light Distribution…", *PMC* (May 2026)** | 16 production headlamps, full-sphere IES, inverse-square → lux. 1 lx / 3 lx contours to **650 m**. Headlight = Spot + IES, physical units. |
| Qt "Poor Man's Volumetric Light" (Mar 2026) | Production pattern: `sampleIES()` via 3D texture lookup (θ, φ) → intensity. Same pattern we use for direct lighting. |
| Unreal / Blender / KeyShot docs | IES is a **modifier on Point/Spot**, multiplier 1.0 = physical. Never a standalone light type. |

**Takeaway:** Headlight/taillight/streetlamp = **Spot + IES texture
(candela)**. One texture lookup per light per pixel. Cheap. Physical.

### 2.7 Environment / IBL

PMREM (prefiltered mipmapped radiance environment map) + split-sum BRDF LUT
is the universal standard: **1 texture sample per pixel** for specular IBL,
1 for diffuse. No per-light cost. Without it, PBR and Matcap look dead even
with perfect direct lights. Bake offline (or on import), sample at runtime.
SDFGI (Godot-style) is a *GI* system, not a direct-light replacement:
Sponza 128³ SDF = 85 fps / 11.8 ms, 512³ = 60 fps / 16.7 ms. Park GI until
direct lighting is correct.

### 2.8 SDF shadows — do not raymarch per light

**"RTSDF: Real-time Signed Distance Fields for Soft Shadow Approximation in
Games" (arXiv:2210.06160):** 1 directional light, SDF raymarch shadows =
**97 fps** vs shadow maps **241 fps**. SDF raymarching is for *geometry*
(primary rays, GI probes), not for per-light visibility. Reuse the shared
shadow-map pool.

### 2.9 What we are NOT using (and why)

| Technique | Why not now |
|---|---|
| ReSTIR DI / GI | RT-many-lights regime. Needs shadow rays + denoiser. Phase 2 hook only. |
| Markov Chain Mixture Models (CGF 2023) | 15–16 ms @ 1080p on RTX 2080 Ti (8 spp). Competitive but complex screen-space vMF; solves a problem we don't have yet. |
| Neural Visibility Cache / Neural Importance Sampling (2025) | Real gains (FLIP −20–45%) but adds online training + inference. Phase 2, after ReSTIR hook. |
| Deferred rendering | Breaks SDF/Matcap flexibility, MSAA cost, G-buffer bandwidth. Forward+ keeps material freedom. |
| Shadow map per light | 87–119 ms at scale (Zhang 2025). Pool + importance selection instead. |
| Monte Carlo area lights | 29× slower than LTC at equal quality. |

---

## 3. Light taxonomy (the only six you need)

Physical units throughout: **candela (cd)** for intensity, **lux** for
illuminance, ACES tone mapping, exposure control. "Intensity 1.0" is banned
— every light declares a unit.

| # | Type | Use in AutoSpace | Integrator | Shadow | Count budget |
|---|---|---|---|---|---|
| 1 | **Directional** | Sun / studio key, moon (Lobby C) | Analytic N·L + GGX | CSM (2–4 cascades) | 1–2 |
| 2 | **Point** | Bulbs, emissive proxies, interior fills | Analytic inverse-square + GGX | Pooled cube atlas (shared) | ≤ 256 clustered |
| 3 | **Spot** | Studio spots, taillights (no IES) | Analytic cone + GGX | Pooled 2D atlas | ≤ 128 clustered |
| 4 | **Spot + IES** | **Headlights**, streetlamps, downlights | Spot + **IES texture lookup** (cd) | Pooled 2D atlas | ≤ 32 (IES subset) |
| 5 | **Rect Area** | **Studio softboxes**, light panels | **LTC** (diffuse + specular) | None (LTC + optional 1 RT shadow ray) | ≤ 16, separate list |
| 6 | **Environment (IBL)** | Studio HDRI, showroom, night sky | **PMREM + split-sum** (1 sample) | None (baked / probe) | 1 global + probes |

Disc/sphere/tube lights are **not** separate types — approximate as Rect
(LTC) or Point. Free-form/Bezier LTC exists but is out of scope.

**Emissive meshes → proxy lights:** threshold luminance, cluster emissive
triangles spatially, emit one Point (small) or Rect (large panel) proxy per
cluster. Rebuild when emissive set changes, not per frame.

---

## 4. Architecture

```
                        ┌─────────────────────────────────────────────┐
                        │              LIGHT REGISTRY (CPU)           │
                        │  type, transform, color, cd/lux, IES id,    │
                        │  radius, shadow flag, importance score      │
                        └──────┬──────────────┬──────────────┬────────┘
                               │              │              │
                    ┌──────────▼──┐   ┌───────▼───────┐  ┌───▼──────────────┐
                    │ CLUSTER GRID│   │  LTC LIST     │  │  ENV / PMREM     │
                    │ Point+Spot  │   │  Rect only    │  │  HDRI → PMREM    │
                    │ log-depth   │   │  cap 16       │  │  + BRDF LUT      │
                    │ GPU compact │   │  no shadowmap │  │  1 sample/pixel  │
                    └──────┬──────┘   └───────┬───────┘  └────────┬─────────┘
                           │                  │                   │
                    ┌──────▼──────────────────▼───────────────────▼──────┐
                    │         SHARED SHADING CORE (GPU)                  │
                    │  PBR: GGX + LTC + IES + IBL + ACES                 │
                    │  Matcap: same lights, BRDF override                │
                    │  Shadow pool: CSM + atlas (top-N importance)       │
                    └──────▲──────────────────▲───────────────────▲──────┘
                           │                  │                   │
              ┌────────────┴───┐   ┌──────────┴────┐   ┌──────────┴─────┐
              │ MESH front-end │   │ SDF front-end │   │ RT front-end   │
              │ raster / DXR   │   │ raymarch      │   │ (Phase 2)      │
              │ P,N,V,albedo   │   │ N=∇SDF, hit P │   │ cluster list = │
              └────────────────┘   └───────────────┘   │ ReSTIR pool    │
                                                       └────────────────┘
```

### 4.1 Forward+ Clustered (Point + Spot)

- View frustum → 3D cluster grid (e.g. 16×9×16, log-depth slices).
- GPU assignment with **compaction** (1.1 ms / 164 KB @ 10k lights, Logdahl
  2025) — never naive CPU loop.
- Per-cluster light list; fragment loops only its cluster's lights.
- **Overflow policy:** per-cluster cap (e.g. 64) + importance sort
  (intensity / distance²). Overflow lights are *dropped with a debug
  heatmap*, never silently wrap. The demo shows this heatmap live.
- **Huge-radius lights** (radius > cluster size) go to a separate
  "global list" evaluated per pixel (max 8), not the cluster grid.

### 4.2 LTC Rect list (separate, capped)

- Max 16 rect lights evaluated per pixel, sorted by importance.
- LTC diffuse + specular LUTs (Heitz). Polynomial LUT approximation
  allowed (Godot thesis: negligible quality loss).
- No shadow maps. Optional: single shadow ray in RT mode (Phase 2).

### 4.3 IES spots

- IES file → bake to 2D texture: `u = φ/2π, v = θ/π → candela`.
- Shader: `intensity = texture(iesTex, uv) * color * falloff`.
- Headlight preset: narrow beam + horizontal cutoff (asymmetric φ).
  Downlight preset: wide cosine. Uniform preset: flat cone.
- The demo's polar plot is generated from the *same* function the shader
  uses — what you see is what you ship.

### 4.4 Shadow pool (deterministic top-N)

- Directional → CSM.
- Spot/Point → **atlas pool** (e.g. 8× 1024² slots, or 4× cube).
- Each frame, rank shadow-casting lights by
  `importance = screenCoverage × intensity / distance²`
  (Zhang 2025 ranking idea, deterministic version). Top-N get shadow maps;
  rest get ISM-style cheap visibility (distance + N·L bias) or no shadow.
- No flicker: hysteresis on ranking (a light must beat the incumbent by
  10% to swap slots).

### 4.5 Environment

- HDRI → PMREM cubemap (GGX prefilter, mip = roughness) + irradiance +
  BRDF LUT, baked on import.
- Diffuse IBL: irradiance sample × albedo / π.
- Specular IBL: PMREM sample (mip = roughness) × split-sum BRDF.
- Lobby presets swap the HDRI; probes (Phase 3) localise it.

---

## 5. One core, four front-ends (SDF / PBR / Matcap / Raytracing)

The direct-light integral is identical in all modes:

```
Lo = Σ_lights  f_BRDF(N,V,L) · L_i · visibility · geometryTerm   +   IBL(N,V)
```

| Mode | What changes | What does NOT change |
|---|---|---|
| **PBR (mesh)** | Raster/RT hit → P, N, albedo, roughness | Light list, LTC, IES, IBL, shadow pool |
| **SDF** | Raymarch hit → P, **N = ∇SDF**, albedo/roughness from SDF id | Everything else. Visibility = shadow-map sample, **not** SDF raymarch per light. |
| **Matcap** | BRDF → matcap lookup `f(N,V)` | Light list still drives IBL + tint; direct lights become matcap tint. |
| **Raytracing (Phase 2)** | Hit via BVH; shadow rays replace shadow-map samples for pooled lights | Cluster list becomes the ReSTIR candidate pool. No API break. |

**SDF analytical lights?** Yes — the lights *are* analytical (closed-form
point/spot/LTC/IES). The SDF only supplies the shading point and normal.
There is no "SDF lighting model" to invent.

---

## 6. The 3 lobbies (lighting rigs, no UI)

Same core, three rig presets. Each lobby is a *lighting test scene* first;
UI comes later and is out of scope for this plan.

| Lobby | Rig | What it proves |
|---|---|---|
| **A — Studio** | 4–8 Rect LTC softboxes + PMREM studio HDRI + 1 Directional key (CSM) | Area-light quality, soft shadows, car-paint reflections. |
| **B — Showroom** | 20–60 Point/Spot (emissive proxies) + PMREM showroom + pooled shadows | Many-light clustering, emissive proxies, shadow-pool ranking. |
| **C — Night / Beam** | 2× Spot+IES headlights (650 m beam) + Directional moon + dark PMREM | IES photometry, lux falloff, cutoff correctness. |

Each lobby ships with a **budget card**: light counts per type, shadow-pool
size, expected ms (from §2 numbers), and a debug view (cluster heatmap,
shadow-slot list, IES polar plot).

---

## 7. Build order

| Phase | Deliverable | Exit criteria |
|---|---|---|
| **0 — Demo (this PR)** | `demo/lighting-demo.html`: SDF raymarch + clustered vs brute vs ReSTIR-sim, LTC rect, IES spots, PMREM-style env, Matcap toggle, live FPS + heatmap + polar plot | User can reproduce every number in §2 in-browser. |
| **1 — Core** | Light Registry + Forward+ cluster grid (GPU compaction) + LTC rect list + IES bake + PMREM bake + shadow pool (CSM + atlas, top-N) + PBR/Matcap shading core. Lobbies A–C rigs. | 200 lights @ 60 fps 1080p on target GPU; no cluster overflow artefacts; IES lux matches reference within 5%. |
| **2 — RT hook** | Shadow rays for pooled lights; cluster list exposed as ReSTIR candidate pool; optional ReSTIR DI backend behind a flag. | RT mode matches raster within FLIP < 0.1; ReSTIR off by default. |
| **3 — GI** | SDFGI / DDGI probes for indirect. Direct lighting untouched. | Indirect only; direct-light numbers unchanged. |

**Explicit non-goals for Phase 1:** ReSTIR, neural caches, Markov-chain
sampling, deferred rendering, per-light shadow maps, Monte Carlo area lights.

---

## 8. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Cluster overflow in dense showroom | Per-cluster cap + importance sort + debug heatmap (demo shows it). Increase grid / shrink radius before raising cap. |
| Softbox (huge radius) culled wrong | Rect lights never enter cluster grid — separate LTC list. |
| IES units wrong (too bright/dark) | Candela in, lux out, exposure + ACES. Validate against 1 lx / 3 lx contours (PMC 2026). |
| Shadow-pool flicker | Hysteresis + temporal ranking smoothing. |
| SDF shadow cost blowup | Shadow-map sampling only. SDF raymarch for primary rays + GI probes. |
| ReSTIR noise if enabled early | Off by default; Phase 2 flag; requires denoiser + temporal reuse. |

---

## 9. References

1. Zhang, Lin, Wyman, Yuksel. *Many-Light Rendering Using ReSTIR-Sampled
   Shadow Maps.* Computer Graphics Forum (Eurographics 2025).
   doi:10.1111/cgf.70059.
2. Bitterli et al. *Spatiotemporal Reservoir Resampling for Real-time Ray
   Tracing with Dynamic Direct Lighting (ReSTIR).* 2020. Baseline:
   3.4M emissive tris < 50 ms, 6–60× equal-error.
3. Bokšanský et al. (AMD). *Neural Visibility Cache for Real-Time Light
   Sampling.* Pacific Graphics 2025. arXiv:2506.05930. FLIP 0.362 → 0.293
   (Kitchen), 0.195 → 0.103 (Sponza).
4. Logdahl. *27,000 dragons and 10,000 lights: GPU-Driven Clustered Forward
   Renderer.* Apr 2025. 1.1 ms / 164 KB cluster assignment @ 10k lights.
5. Babylon.js Forum. *Clustered Forward+ benchmark.* Nov 2025. 2,000 lights
   @ 60 fps 1080p (RTX 4070 laptop).
6. CookieBadger. *Integrating Dynamic Area Lights into the Real-Time
   Rendering Pipeline of the Godot Engine.* Master's thesis, 2026.
   Godot PR #108219. LTC > Monte Carlo / MRP.
7. Kneiphof & Klein. *Real-Time Rendering of Glints in the Presence of Area
   Lights.* 2024, rev. 2025. LTC overhead negligible.
8. Gao et al. *Efficient Object Reconstruction with Differentiable Area
   Light Shading.* Dec 2025. LTC 25% faster than MC@16spp, 29× than MC@1024.
9. *Implementing Many-Lights Rendering with IES-Based Lights.* MDPI Applied
   Sciences 14(3):1022, 2024.
10. *Quantifying the Impact of Headlamp Light Distribution on Automotive
    Camera Perception.* PMC, May 2026. 16 IES headlamps, 1/3 lx to 650 m.
11. *RTSDF: Real-time Signed Distance Fields for Soft Shadow Approximation
    in Games.* arXiv:2210.06160. SDF shadows 97 fps vs shadow maps 241 fps.
12. Dittebrandt et al. *Markov Chain Mixture Models for Real-Time Direct
    Illumination.* Computer Graphics Forum, 2023. 15–16 ms @ 1080p (2080 Ti).
13. Heitz. *Real-time Polygonal-Light Shading with Linearly Transformed
    Cosines.* 2016. LTC LUTs (vendored reference tables).

---

## 10. How to verify this plan (5 minutes)

1. Open `demo/lighting-demo.html`.
2. Set **Method = Brute force**, lights = 128. Note FPS.
3. Set **Method = Clustered**, same lights. Note FPS + heatmap.
4. Set **Method = ReSTIR-sim (1 candidate)**. See the noise — that is why
   ReSTIR is Phase 2, not Phase 1.
5. Toggle **IES profile** Headlight / Downlight / Uniform. Watch the polar
   plot and the beam on the ground change.
6. Toggle **Matcap** and **SDF/Mesh**. Same lights, different front-end —
   proving the shared core.
7. Push lights to 200+. Clustered holds; brute collapses. That gap is the
   entire argument for Forward+.
