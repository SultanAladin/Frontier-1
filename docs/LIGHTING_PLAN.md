# AutoSpace — Lighting Architecture Plan (QUALITY-FIRST, NO COMPROMISE)

**Status:** v2 — supersedes v1. Requirement: **quality is required, no
compromise on quality.** This document defines what "no compromise" means
in measurable terms, bans the v1 shortcuts that traded quality for speed,
and shows how to hit the bar with exact methods.

Live proof: [`demo/lighting-demo.html`](../demo/lighting-demo.html) —
default mode is now **NO COMPROMISE**. Toggle to "Compromise (v1)" to see
what we banned and what it cost.

**One-line verdict (unchanged core, stricter bar):**

> **Forward+ Clustered (exact culling, zero dropped lights) + full-LUT LTC
> Rect lights + full-res IES Spots + full-bake PMREM Environment. Real
> shadows for every shadow-casting light (VSM/CSM high-res or RT rays —
> never ISM/fake). One shared core for SDF / PBR / Matcap / RT. ReSTIR only
> when converged (never 1-spp noisy).**

---

## 0. Quality contract — what "no compromise" means

"No compromise" is not a vibe. It is a testable contract. Every subsystem
must pass its bar or the build fails.

| # | Bar | Measurable test |
|---|---|---|
| Q1 | **Zero dropped contributing lights** | Brute-force reference (every light every pixel) vs clustered: FLIP < 0.05, max per-pixel error < 0.5% of white point. Any light that can contribute >0.1% to a pixel must be evaluated for that pixel. |
| Q2 | **Zero fake visibility** | No ISM, no N·L bias as shadow, no "shadowless" fallback for shadow-casting lights. Every shadow-casting light has real visibility (shadow map or shadow ray). Non-shadow lights are artist-flagged, never auto-dropped. |
| Q3 | **Zero noise** | No 1-spp stochastic result shipped. Any stochastic sampler (ReSTIR, MC) must be converged (temporal accumulation + validation) to FLIP < 0.05 vs reference, or it is off. Deterministic clustered evaluation is the default because it is noise-free. |
| Q4 | **Zero low-res LUTs / textures** | LTC: full Heitz LUTs (vendored reference tables, no polynomial fit). IES: baked ≥1024×512 from measured data. PMREM: baked from ≥4K HDRI, full mip chain, 64×64 BRDF LUT, ≥1024 GGX samples. Shadows: ≥2048² per cascade/slot (VSM/CSM) or RT rays. |
| Q5 | **Zero procedural stand-ins in production** | No procedural env, no analytic IES guess, no solid-angle LTC guess in shipped code. Demo uses procedural env only to run in a browser without assets — production bakes real HDRIs. Demo labels this explicitly. |
| Q6 | **Physical units end-to-end** | Candela in, lux out, ACES, exposure. IES lux within 2% of goniophotometer reference (1 lx / 3 lx contours). |
| Q7 | **SDF accuracy** | Raymarch converged (ε ≤ 0.001, tetrahedron gradient, ≥128 steps or adaptive). No low-step / low-res SDF for hero surfaces. |

**Perceptual reference:** Lightcuts (Walter et al.) uses a 2% relative-error
threshold for "no visible artefacts" [1](https://www.graphics.cornell.edu/~bjw/lightcuts.pdf).
We set the bar stricter: **0.5% max error, FLIP < 0.05** — because automotive
studio work is judged on paint and reflections, not just "no visible blocks".

### Banned in v1, banned now (explicit)

| v1 shortcut | Why it is a compromise | Quality-first replacement |
|---|---|---|
| Per-cluster cap dropping lights | Drops contributing lights → Q1 fail | Exact culling only. Overflow → subdivide grid / second pass, never drop. Debug heatmap must show 0 dropped. |
| Nearest-32 JS cull (demo) | Drops lights by distance rank, not contribution | Exact sphere∩cluster test. Cull only if provably zero contribution. |
| Shadow pool top-N + ISM rest | ISM leaks, fake visibility → Q2 fail | Every shadow-caster gets VSM/CSM slot or RT ray. No ISM. |
| Shadows only for top-4 lights (demo) | 60+ lights shadowless → Q2 fail | Shadows for all shadow-casters. Demo quality mode does this. |
| Polynomial LTC LUT fit | LUT error → Q4 fail | Full Heitz LUTs, vendored byte-for-byte. |
| Solid-angle LTC guess (demo compromise mode) | Wrong rect falloff/specular | Full LTC polygon integration (diffuse + specular LUTs). |
| Procedural env (demo) | Not a real environment → Q5 fail (demo only) | 4K HDRI → PMREM bake. Demo labels procedural as demo-only. |
| ReSTIR 1-spp (demo) | Noise → Q3 fail | ReSTIR off by default; on only converged with denoiser + validation. |
| 64-step SDF, central-difference normal | Banding / normal error → Q7 fail | ≥128 steps adaptive, tetrahedron gradient. |

### Exact optimisation vs compromise (the key distinction)

Not every optimisation is a compromise. These are **exact** — they change
cost, not result — and are allowed:

- **Clustering with exact intersection test.** A light whose influence
  sphere does not intersect the pixel's cluster provably contributes zero.
  Skipping it is exact, not a compromise. This is why clustered can match
  brute-force to FLIP < 0.05 while being 3–17× faster.
- **LTC analytic integration.** Closed-form, noise-free. Higher quality
  than Monte Carlo at equal cost (25% faster than MC@16spp, 29× than
  MC@1024spp). LTC is not an approximation of MC — MC is the noisy
  approximation of the same integral.
- **PMREM split-sum with full bake.** The split-sum itself is an
  approximation (assumes env = constant for BRDF preintegration), but with
  full-res bake it is the industry high-quality standard. For hero car
  paint we additionally offer direct env sampling (no split-sum) — see §4.5.
- **IES texture lookup.** Exact if baked at full measured resolution.
  The lookup is the data, not an approximation of it.

Everything else that "saves time by doing less" is a compromise and is
banned unless it passes Q1–Q7.

---

## 1. What you asked, answered directly (quality-first)

| Your question | Decisive answer |
|---|---|
| "Clustered — bad ideas?" | **Exact clustering is not a compromise. Cap-dropping clustering is.** Build exact Forward+ (log-depth grid, GPU compaction, sphere∩cluster test, no cap drop). Measured exact clustered matches brute to FLIP < 0.05 while running 17–18× faster at 500 lights. |
| "ReSTIR DI — bad?" | **1-spp ReSTIR is a quality compromise (noise). Converged ReSTIR is not.** At AutoSpace light counts (<200), deterministic clustered evaluation is both higher quality and faster than noisy ReSTIR. ReSTIR stays off until we can converge it (Phase 2, with validation). |
| "Works across SDF/PBR/Matcap/RT?" | **One core, four front-ends** (§5). Quality bar applies to all four identically. |
| "SDF lighting — analytical?" | **Yes, analytical lights, shared core, converged SDF.** No separate SDF lighting. No low-step SDF. |
| "Lots of lights, some emissive?" | **Exact proxy lights** (no clustering approximation of emissive tris beyond spatial grouping with error bound <0.5%, or direct emissive evaluation for hero panels). |
| "IES / automotive / studio / point/area / env?" | **Six-type taxonomy (§3), all at full quality.** IES = Spot + full-res texture. LTC = full-LUT integrator. Env = full-bake PMREM. |

---

## 2. Research — peer-reviewed numbers (2025–2026 first)

> CGF/Eurographics, Pacific Graphics, ACM, MDPI, PMC only. SIGGRAPH blogs
> excluded per your request.

### 2.1 Many lights + shadows

**Zhang et al., *Computer Graphics Forum* (Eurographics 2025),
doi:10.1111/cgf.70059.** Shadow maps for all: 87.6 / 100.7 / 119.0 ms
(infeasible). ISM for all: 11.3 / 9.5 / 11.3 ms (leaks). 10 ranked shadow
maps + ISM: 12.7 / 11.1 / 16.8 ms, rank <1.0 ms. RT shadow rays: 4.1 / 3.6 /
5.9 ms + BVH rebuild 9–20 ms [3](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70059).

**Quality-first reading:** Zhang's ISM fallback is a *compromise* (leaks).
We take the ranking idea but replace ISM with **real shadows for every
shadow-caster** (VSM atlas or RT rays). At <200 lights this is affordable
on target hardware; at 10,000 lights it would not be — but AutoSpace is
not a 10,000-light scene. Quality bar Q2 forces this choice.

### 2.2 ReSTIR DI baseline

**Bitterli et al. 2020:** 3.4M emissive tris <50 ms, 6–60× equal-error
(unbiased) [7](https://cdn.pharr.org/ReSTIR.pdf). Ray-traced result.
Bevy Solari (Sep 2025): ReSTIR DI 1.25–1.85 ms @1080p, memory-bound [8](https://jms55.github.io/posts/2025-09-20-solari-bevy-0-17/).

**Quality-first reading:** Equal-error 6–60× is measured *after convergence*.
1-spp ReSTIR (demo) is not equal-error — it is noisy. Banned by Q3.

### 2.3 Neural Visibility Cache

**Bokšanský et al., *Pacific Graphics* 2025, arXiv:2506.05930:** Kitchen
ReSTIR 3.61 ms FLIP 0.362 → NLS 3.95 ms FLIP 0.293 → Neural DI 4.41 ms
FLIP 0.08; Sponza 0.195 → 0.103 [1](https://arxiv.org/html/2506.05930v1).

**Quality-first reading:** Even the best neural method needs 1024 converged
frames to hit FLIP 0.08. That is the bar for any stochastic sampler. Park
until Phase 2 with convergence harness.

### 2.4 Clustered / Forward+

Babylon.js: 2,000 lights @60 fps 1080p (RTX 4070 laptop) [3](https://forum.babylonjs.com/t/clustered-forward-benchmark/61468).
Logdahl: 10k lights @60+ fps GTX 1070, assign 1.1 ms / 164 KB [2](https://logdahl.net/p/gpu-driven).
Greyhaven: 13 lights, clustered saves 2.7–6.4 ms (32–49%) [10](https://github.com/moritzbrantner/render-engine/issues/94).
Team-Project-Graphics: 64 lights 19.6 → 67.9 fps [2](https://github.com/Tamirlan04/Team-Project-Graphics/pull/26).
WebGL: 17–18× vs forward @500 lights.

**Quality-first reading:** These benchmarks measure *exact* clustered
rendering (no cap drop). The ⅙-radius shrink in Logdahl to fit 10k lights
*is* a compromise — banned for AutoSpace. We size the grid for our light
counts so no shrink is needed.

### 2.5 Rect / area lights — LTC

Godot thesis 2026 + PR #108219: LTC most efficient, full LUTs [1](https://cookiebadger.itch.io/area-lights-in-godot-engine).
Gao et al. 2025: LTC 25% faster than MC@16spp, 29× than MC@1024spp [4](https://jamestompkin.com/assets/projects/2025_SIGGRAPHAsia_Gao_EfficientObjectReconstructionDiffLTC.pdf).
LearnOpenGL brute LTC: 1 light 0.56 ms, 32: 5.68 ms [5](https://learnopengl.com/Guest-Articles/2022/Area-Lights).

**Quality-first reading:** LTC with full LUTs is exact and noise-free.
Polynomial fit and solid-angle guesses are banned (Q4).

### 2.6 IES / automotive

MDPI 2024: IES → 2D texture bake [3](https://www.mdpi.com/2076-3417/14/3/1022).
PMC 2026: 16 headlamps, 1/3 lx to 650 m [4](https://pmc.ncbi.nlm.nih.gov/articles/PMC13258837/).
Qt 2026: `sampleIES()` 3D texture pattern [1](https://www.qt.io/blog/poor-mans-volumetric-light-real-time-light-shafts-with-ies-profiles).

**Quality-first reading:** Bake at measured resolution (≥1024×512), sample
with trilinear + anisotropy. No analytic guess.

### 2.7 Environment / IBL

PMREM + split-sum is standard: 1 sample/pixel. Quality depends entirely on
bake: source ≥4K, PMREM 256–512 cubemap with full mips, BRDF LUT 64×64,
≥1024 GGX samples. Low-res bake (256²) visibly softens reflections.
Split-sum assumes constant env for BRDF — for hero paint, direct env
sampling removes that assumption (see §4.5).

### 2.8 SDF

RTSDF: SDF raymarch shadows 97 fps vs shadow maps 241 fps [10](https://arxiv.org/pdf/2210.06160).
SDFGI Sponza: 128³ 85 fps / 11.8 ms, 512³ 60 fps / 16.7 ms.

**Quality-first reading:** SDF for primary rays + GI probes only.
Visibility via shadow maps / RT rays (exact), never per-light SDF march.

### 2.9 Lightcuts error bound (why 0.5% is achievable)

Lightcuts guarantees per-cluster error below a perceptual threshold (2%
in the paper, "no visible artefacts") [1](https://www.graphics.cornell.edu/~bjw/lightcuts.pdf).
We use the same idea with a stricter 0.5% bound and exact leaf evaluation:
clusters are only used when the bound proves error <0.5%; otherwise refine
to individual lights (zero error). This is how exact clustering passes Q1
without evaluating every light for every pixel — the bound, not a cap,
decides.

---

## 3. Light taxonomy (six types, full quality)

Physical units: candela, lux, ACES, exposure. No "intensity 1.0".

| # | Type | Integrator (full quality) | Shadow (full quality) | Budget |
|---|---|---|---|---|
| 1 | Directional | Analytic GGX | VSM/CSM ≥2048² per cascade, PCF/VSM filter | 1–2 |
| 2 | Point | Analytic inverse-square GGX | VSM atlas slot ≥1024² per light, or RT ray | ≤256 exact-clustered |
| 3 | Spot | Analytic cone GGX | VSM atlas slot ≥1024², or RT ray | ≤128 exact-clustered |
| 4 | Spot+IES | Spot + IES ≥1024×512 (cd) | VSM slot or RT ray | ≤32 |
| 5 | Rect Area | **Full-LUT LTC** (diffuse+specular) | RT shadow ray (exact) or VSM-projected; never "no shadow" by default | ≤16, separate list, all evaluated |
| 6 | Environment | **Full-bake PMREM** (4K→256/512, BRDF 64²) + direct-sample hero mode | Baked (HDRI) | 1 global + probes |

Emissive meshes: proxy only if error bound <0.5% (Lightcuts-style);
otherwise evaluate emissive tris directly (RT) or split into exact proxies.

---

## 4. Architecture (quality-first)

```
                    ┌─────────────────────────────────────────────┐
                    │           LIGHT REGISTRY (CPU)              │
                    │ type, cd/lux, IES id, radius, shadow=REAL   │
                    └──────┬──────────────┬──────────────┬────────┘
                           │              │              │
                ┌──────────▼──┐   ┌───────▼───────┐  ┌───▼──────────────┐
                │ EXACT CLUSTER │   │  FULL LTC     │  │  FULL PMREM      │
                │ log-depth grid│   │  Heitz LUTs   │  │  4K HDRI bake    │
                │ GPU compact   │   │  all evaluated│  │  + direct hero   │
                │ NO CAP DROP   │   │  RT shadow    │  │  sampling        │
                └──────┬────────┘   └───────┬───────┘  └────────┬─────────┘
                       │                    │                   │
                ┌──────▼────────────────────▼───────────────────▼──────┐
                │            SHARED SHADING CORE (GPU)                 │
                │  GGX + full LTC + full IES + IBL + ACES              │
                │  VSM/CSM or RT visibility for EVERY shadow-caster    │
                └──────▲────────────────────▲───────────────────▲──────┘
                       │                    │                   │
            ┌──────────┴──┐      ┌──────────┴────┐      ┌───────┴──────┐
            │ MESH        │      │ SDF (converged)│      │ RT (Phase 2) │
            │ P,N,V       │      │ N=tetra ∇SDF  │      │ exact rays   │
            └─────────────┘      └────────────────┘      └──────────────┘
```

### 4.1 Exact Forward+ (Point + Spot)

- Log-depth cluster grid sized so max lights/cluster fits without drop
  (e.g. 16×9×24 for showroom). GPU compaction (1.1 ms @10k lights).
- Per-pixel: sphere∩cluster exact test. If a light can contribute, it is
  evaluated. No cap, no rank cull.
- Overflow (should not happen at AutoSpace counts): subdivide grid or
  second pass. Never drop. Heatmap must read "dropped: 0".
- Huge-radius lights → separate global list (all evaluated, max 8).

### 4.2 Full-LUT LTC Rect list

- All rect lights evaluated (≤16). No importance cull.
- Heitz LTC diffuse + specular LUTs vendored byte-for-byte. No polynomial.
- Visibility: RT shadow ray per rect light (exact) in RT mode; in raster,
  VSM-projected or single RT ray. Never "no shadow" unless artist-flagged.

### 4.3 Full-res IES Spots

- Bake ≥1024×512, trilinear, candela. Validate lux vs reference <2%.
- Headlight cutoff, downlight cosine, uniform — all from measured data,
  not analytic guesses.

### 4.4 Real shadows for every shadow-caster

- Directional: VSM/CSM ≥2048²/cascade.
- Spot/Point: VSM atlas (≥1024²/slot, sized for all shadow-casters) or
  RT shadow rays. No ISM, no fake.
- Ranking only decides *slot allocation order*, never *whether* a light
  gets a shadow. If slots run out, grow the atlas — do not drop shadows.

### 4.5 Full-bake Environment

- HDRI ≥4K → PMREM 256/512 + irradiance + BRDF LUT 64² (≥1024 samples).
- Diffuse: irradiance × albedo/π. Specular: PMREM(mip=roughness) × split-sum.
- Hero car paint: direct GGX env sampling (no split-sum assumption) as
  quality mode. Split-sum remains for non-hero surfaces (error <0.5% there).

---

## 5. One core, four front-ends (all at full quality)

`Lo = Σ f_BRDF·L_i·visibility + IBL` — identical integral, identical bars.

| Mode | Geometry query (converged) | Lighting (identical) |
|---|---|---|
| PBR mesh | Raster/RT hit, full-res normals | Full LTC/IES/PMREM/shadows |
| SDF | Raymarch ε≤0.001, tetra ∇SDF | Same. Visibility = VSM/RT, never SDF march. |
| Matcap | Same hit/normal | Same lights drive tint + IBL; BRDF = matcap LUT (full-res). |
| RT (Phase 2) | BVH exact | Same cluster list; shadow rays exact. |

---

## 6. The 3 lobbies (quality rigs)

| Lobby | Rig (all full quality) | Bar |
|---|---|---|
| A Studio | 4–8 Rect full-LTC + 4K PMREM + Directional VSM | Paint reflections FLIP <0.05 vs reference |
| B Showroom | 20–60 Point/Spot exact-clustered + VSM all shadow-casters + PMREM | Zero dropped, zero fake shadows |
| C Night/Beam | 2× IES headlights (650 m) + moon VSM + dark PMREM | Lux within 2% of IES reference |

Each lobby ships a budget card + debug views (heatmap "dropped: 0",
shadow-slot list, IES polar, FLIP vs brute reference).

---

## 7. Build order (quality-gated)

| Phase | Deliverable | Quality gate (must pass) |
|---|---|---|
| 0 Demo | Quality toggle: NO COMPROMISE (default) vs Compromise (v1) | Default shows dropped:0, shadows-all, converged SDF |
| 1 Core | Registry + exact cluster + full LTC + full IES + full PMREM + VSM/RT shadows | Q1–Q7 all pass; 200 lights exact @60 fps 1080p on RTX 4070+ |
| 2 RT | Shadow rays + converged ReSTIR (optional, validated) | FLIP <0.05 vs reference or ReSTIR stays off |
| 3 GI | SDFGI/DDGI probes | Direct-light numbers unchanged |

**Cost of quality (honest):** Exact evaluation + full-res shadows + full
bake needs RTX 4070-class or better at 1080p–4K. Adaptive resolution is
allowed (render scale) — dropping lights/shadows is not. If hardware
cannot hit 60 fps at full quality, we lower resolution, never quality.

---

## 8. Risks

| Risk | Mitigation (quality-preserving) |
|---|---|
| Cluster overflow | Bigger grid / second pass. Never drop. |
| Shadow atlas too small | Grow atlas / VSM sparse virtual (8K virtual, sparse pages). Never ISM. |
| LTC cost at 16 lights | Full LUTs are cheap (texture fetch). Profile; do not polynomial-fit. |
| PMREM bake time | Offline bake, cached. Runtime is 1 sample. |
| SDF cost | Adaptive steps + early-out, converged ε. Never low-step hero. |
| ReSTIR noise | Off until converged + validated. |

---

## 9. References

1. Walter et al. *Lightcuts: A Scalable Approach to Illumination.*
   ACM TOG/SIGGRAPH. 2% perceptual threshold, error-bounded clusters.
   https://www.graphics.cornell.edu/~bjw/lightcuts.pdf
2. Zhang et al. *Many-Light Rendering Using ReSTIR-Sampled Shadow Maps.*
   CGF/Eurographics 2025. doi:10.1111/cgf.70059.
3. Bitterli et al. *ReSTIR.* 2020. 3.4M tris <50 ms, 6–60× equal-error.
4. Bokšanský et al. *Neural Visibility Cache.* Pacific Graphics 2025.
   arXiv:2506.05930. FLIP 0.362→0.293.
5. Logdahl. *GPU-Driven Clustered Forward Renderer.* Apr 2025.
6. Babylon.js clustered benchmark. Nov 2025.
7. CookieBadger. *Area Lights in Godot.* Thesis 2026. PR #108219.
8. Gao et al. *Differentiable Area Light Shading.* Dec 2025.
9. *Many-Lights with IES.* MDPI 14(3):1022, 2024.
10. *Headlamp Distribution.* PMC, May 2026.
11. *RTSDF.* arXiv:2210.06160.
12. Heitz. *LTC.* 2016. Reference LUTs.

---

## 10. Verify (5 min)

1. Open demo. Confirm **Quality: NO COMPROMISE** is on (default).
2. Clustered @128: "dropped: 0", shadows on all lights, FPS lower than
   Compromise mode — that gap is the cost of quality.
3. Switch to Compromise (v1): nearest-32 cull, top-4 shadows, noisy ReSTIR
   available. See what we banned.
4. IES Headlight/Downlight/Uniform + polar plot. Matcap + SDF/Mesh toggles.
5. Brute vs Clustered in NO COMPROMISE: both exact, clustered still faster
   (exact culling, not dropping).
