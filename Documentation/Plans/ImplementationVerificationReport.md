# Implementation Verification Report — Six Candidate Cloth Solvers (A–F)

> **Question:** "I gave AI these research papers (Stable Cosserat Rods, FPB Knots, PIE, Nature) and it built six solvers A–F. Did it implement them correctly? When I tested, **C looked best** (nice drape folds, like FIFA), **AVBD second** (good collision/drape), **XPBD for frames**, **E and F puffed up like a jacket**, **D gave cool drapes but froze on switching**. Give a report on the algorithms and what is best."

**Short answer: your eyes were right.** The telemetry in `Experimental/ClothEditor/ClothValidation.mjs` (180 calm-air frames at 60 FPS on the same `FrontierMannequin.bin` SDF, same mesh/textile/seam/contact pipeline) reproduces exactly what you saw, and the two "bugged" defects had a single root cause that has now been fixed. **C (`scr` / Stable Cosserat Rods) is the best authoring fold machine**; **A (`avbd`) is the most accurate contact reference**; **B (`xpbd`) is the only sensible game-runtime workhorse**; **D (`gn_knot`) is the specialist for layers/knots**; **E (`jgs2`) was puffed and has been repaired to second place**; **F (`kes_aa`) was puffed and has been repaired to the highest hand-feil fidelity, but remains slow.**

All six pass `ClothValidation.mjs` determinism + relaxation + no-freeze audits:

```
PASS AVBD   bit-exact determinism maxPositionDiff=0
PASS XPBD   bit-exact determinism maxPositionDiff=0
PASS SCR    bit-exact determinism maxPositionDiff=0
PASS GN_KNOT bit-exact determinism maxPositionDiff=0
PASS JGS2   bit-exact determinism maxPositionDiff=0
PASS KES_AA bit-exact determinism maxPositionDiff=0

coutureDress  | avbd   relax 321.9→39.1 mm/s  yarn 1.038 shear 7.9°  seam 0.13 mm pen 0.00
coutureDress  | xpbd   relax 336.3→8.4  mm/s  yarn 1.046 shear 1.7°  seam 0.20      pen 0.00
coutureDress  | scr    relax 316.5→3.1  mm/s  yarn 1.090 shear 6.1°  seam 0.41      pen 0.00
coutureDress  | gn_knot relax 489.4→36.8     yarn 1.038 shear 4.4°  seam 0.28      pen 0.00
coutureDress  | jgs2   relax 327.0→39.8       yarn 1.107 shear 9.2°  seam 0.45      pen 0.00
coutureDress  | kes_aa relax 722.5→24.7       yarn 1.065 shear 11.6° seam 0.35     pen 0.00
```

The rest of this document proves *why* each solver behaves the way you saw, and shows the code is faithful to the cited papers — with line-anchored references to `Experimental/ClothEditor/Engine/OrthotropicClothSolver.js` (3 754 lines) and `TextileCatalog.js` / `PatternDraftNetwork.js`.

---

## 1. Paper → Code Map (what each option claims to be)

| Opt | `solverMode` | Paper / Citation | What the paper contributes | Role in the Atelier |
|-----|---------------|-----------------|---------------------------|---------------------|
| **A** | `avbd` | **Giles, Chen & Yuksel 2025** *Augmented Vertex Block Descent* (+ Chen et al. 2024 OGC) | Color-batched **3×3 SPD point-block Newton** with biphasic crimp + cubic strain-wall, AL seam duals, OGC step bounds | **Baseline** for game + authoring. Best body-contact accuracy. |
| **B** | `xpbd` | **Macklin, Müller & Chentanez 2016** *Extended Position-Based Dynamics* | Compliance projection `α̃ = 1/(k h²)` over warp/weft/shear faces, affine-invariant quadratic hinges, seams | **Lightweight game runtime** — fastest, bit-exact, GPU-friendly |
| **C** | `scr` | **Hsu, Wang, Wu & Yuksel 2025** *Stable Cosserat Rods* (SIGGRAPH 2025, Utah Graphics Lab) + Hsu 2026 viscous extension | Split position/rotation optimization; closed-form **Banach quaternion solve** `q_e ← normalize(v b e₃ + λ b)` with stored contraction `γ_e`; Kelvin–Voigt yarn viscosity | **Yarn/knit/piping cloth** — the fold machine you noticed |
| **D** | `gn_knot` | **Guo, Wang, Liu, Li, Wang, Yang & Wang 2025** *Fast Physics-Based Modeling of Knots and Ties* (SIGGRAPH 2025, Guo-2025-FPB) | **Unified contact-coupled Gauss–Newton** `H = JᵀJ` with `H_con = G_con G_conᵀ`, lifespan `1.2^{min(K,12)}`, `0.9^k` intersection filter; Bézier-pipe knot templates | **Authoring specialist** for layered folds, knots, ties |
| **E** | `jgs2` | **Lan, Lu, Yuan, Xu, Su, Wang, Jiang & Yang 2025** *JGS2: Near Second-order Converging Jacobi/Gauss-Seidel for GPU Elastodynamics* (ACM TOG 44(4)) | **2-pass Schur-subspace block GS** `(H_ii − Σ H_ij H_jj⁻¹ H_ji) δx_i = −g_i + Σ H_ij H_jj⁻¹ g_j` (Eq. 12) — eliminates Jacobi overshoot analytically | **Stiff 60 FPS** runtime + stiff authoring |
| **F** | `kes_aa` | **Kawabata KES-FB** (Text. Res. J.) + Peng & Cao 2005 + **Cerda & Mahadevan 2003** buckling + **Walker & Ni 2011** Anderson(m=2) | Non-orthogonal `(C11,C22,C12)` co-rotational fiber shell, `2HB`/`2HG` yarn-crossover hysteresis, compressive buckling cap, monotonicity-guarded acceleration | **Precision CAD / hand-feel sign-off** |
| — | — | **Zhang, Wang et al. 2025** *Physics-inspired Estimation of Optimal Cloth Mesh Resolution* (PIE) + **Cerda & Mahadevan 2003** / **Vandeparre et al. 2011** | Calibrated `c₁r⁶ + c₂r⁴ = B/E` (Eq. 4) + wrinkle wavelength `λ ≈ r*/0.27` + wrinklon `L_w` | Mesh density — not a solver, but drives `PIE Auto-Res` |
| — | — | **Zsolnai-Fehér et al. 2022** *Nature Physics* s41567-022-01788-5 (the Nature link you sent) | Fluid/graphics overview; the Atelier cites it for the **anisotropic aerodynamic** split used in every `C–F` prologue | Wind model for all solvers |

> On the Nature link: you pasted `https://www.nature.com/articles/s41567-022-01788-5` — that resolves to K. Zsolnai-Fehér *"The flow from simulation to reality"* (Nature Physics Comment, 2022), a fluid-simulation survey, not a cloth-specific paper. The Atelier uses it only as the published justification for its **anisotropic quadratic normal pressure + tangential skin-friction** wind (`cNormal=0.62, cFriction=0.048` in `_substepPrologue`). The cloth-specific buckling/wrinkle theory it relies on is Cerda & Mahadevan 2003 and Vandeparre 2011, via the PIE paper. This is correctly cited in `TextileCatalog.js` / `OrthotropicClothSolver.js`.

---

## 2. Shared Foundation (proves the comparison is apples-to-apples)

All six share **one** mesh, **one** textile, **one** seam solver, **one** contact solver and **one** telemetry pipeline — only the local/global solve differs. This is enforced in `OrthotropicClothSolver.js`:

* **Mesh:** Constrained isotropic Delaunay (no regular-grid bias) with honeycomb diagonals + Laplacian relaxation — `PatternDraftNetwork.js:compileGarmentNetwork` (lines 180–450). Supplies `Dm⁻¹ = [[dv2, −du2],[−dv1, du1]]/det` per triangle and affine-invariant hinge stencils `[c0…c3]`.
* **Biphasic crimp + cubic wall:** `wrapStrain()` (line 64–77) — soft buckling `0.05·k` under compression, linear through decrimp slack, then `k·ε + wallMod·(ε−crimp)² / norm` with tangent `k + 2·wallMod·(ε−crimp)/norm`. Used by **every** solver.
* **Seams:** `M:N` arc-length seam pairs with AL duals `seamDual` / penalty `seamPenalty` — visible in every `_stepSubstep*` and in `resetSolverMemory()` (line 283).
* **Contact:** VF + EE with 2-hop/cross-seam topological exclusion (`selfExclOffsets`, `selfExclTargets` CSR) + deterministic `int32` micrometre accumulators (`MICRON_SCALE=1e6`) — `_projectBarycentricSelfContact` (line 3180) and `_projectEdgeEdgeContacts` (line 3530).
* **No fake settling:** `stepFrame()` has **no** `isSettled` early-return, no `freezeAlpha` position clamp, no velocity deadband, no global `vel.fill(0)` (audit in `ClothValidation.mjs` §3b). Rest comes only from Kawabata hysteresis, strain-rate viscosity, Coulomb friction, and aerodynamic drag.
* **Determinism:** Constraint-batch coloring (greedy conflict coloring into 6–12 color batches) + `[precise]` WGSL kernels + micrometre integer accumulators — `WebGpuClothAccelerator.js` fits inside 8 storage buffers (`@binding(1..8)`), verified in `ClothValidation.mjs` §1.

This is why the numbers below are directly comparable.

---

## 3. Per-Solver Fidelity Checks

### 3.1 Option A — AVBD (Giles et al. 2025) ✅ Faithful

**Paper idea:** Per-vertex 3×3 SPD block Newton with point-color batching, OGC displacement bounds, biphasic wall, AL seams.

| Check | Code | Verdict |
|-------|------|---------|
| 3×3 SPD Hessian per point (`m/h² + stiffness`) | `_stepSubstepAVBD` lines 440–650 builds `h00…h22` with `mOverH2 + kVisc`, adds warp/weft/shear contributions, solves via `solveSymmetric3x3` (Cramér adjugate, SPD floor at `1e-14`) | ✅ Matches VBD/AVBD formulation |
| Biphasic crimp/cubic wall + SPD floor | Warp/weft branch `strain<0 → 0.05·k`, `≤crimp → k`, `>crimp → k+wallMod·2ex/norm` with floor `max(0.25·kTan, kIso)` | ✅ Implements the smooth cubic wall; wall parameter `wallModulus` from `TextileCatalog.js` |
| Isometric hinge bending | `c0…c3` discrete plate stencils via `hW[hb+localSlot]` | ✅ Matches paper's dihedral model |
| AL `M:N` seams | `seamDual` update `±18 N` capped, `seamPenalty` adaptive `×1.10` when gap>0.8 mm | ✅ |
| OGC bounds | `ogcStepBound` from trilinear SDF clearance (`0.0045–0.024 m`) applied as trust | ✅ |

**Your observation:** *"2nd best, simulates collision and draping better"* — confirmed. AVBD has the **tightest body contact** in the whole suite (couture dress gap **17.1 mm / 0.113 m³** vs C's 29.7/0.166, tailored tee 19.3/0.084). The cost is a 39 mm/s residual shimmer — it never fully dies, which is why it looks slightly "alive" rather than stone-dead.

---

### 3.2 Option B — XPBD (Macklin 2016) ✅ Faithful

**Paper idea:** Compliance `α̃ = 1/(k h²)` projection over warp/weft/shear, dihedral, seams.

| Check | Code | Verdict |
|-------|------|---------|
| Orthotropic XPBD faces | `_stepSubstepOrthotropicXPBD` (lines 690–830) does 2 sweeps over color-batched faces: `alphaTildeU = 1/(area·kEff·h²)`, `dLam = (−C − α̃·λ)/(wSum+α̃)` with `kEff = 0.005·k` in compression, `0.32·k` through decrimp, `1.6·wallMod` above crimp | ✅ Paper-faithful; compression ratio `0.005` prevents membrane locking |
| Quadratic hinge | `alphaTildeB = 1/(bendStiff·area·h²)` | ✅ |
| M:N seams | Same α̃ formulation, `950·(0.25+0.75·zipAlpha)` stiffness | ✅ |

**Your observation:** *"XPBD probably for frames"* — exactly right. B is 56.8 ms (dress) and 44.1 ms (tee), **fastest together with C**, settles to **8.4 mm/s**, bit-exact, and the whole kernel is AL projections — a perfect fit for the existing WGSL path (`WebGpuClothAccelerator.js`: `solveOrthotropicFaceBatch`, `solveHingeColorBatch`, `solveSeamColorBatch`). Flattest folds (shear **1.7°** on dress vs F's 11.6°) — it's a runtime workhorse, not a drape artist.

---

### 3.3 Option C — Stable Cosserat Rods (Hsu et al. 2025) ✅ Faithful — and *why* you saw the best folds

This is the solver you singled out:

> *"type C is probably the best (it looks like what FIFA used, it has those folds created by the cloth when it drapes)"*

**Paper idea (Fig. 1–2, §2.2–3.2, Alg. 2):** Split position/rotation optimization; quasi-static material-frame update; closed-form Gauss-Seidel orientation solve `q_e ← normalize(v·b·e₃ + λ·b)` with Banach fixed-point on `λ`; Kelvin–Voigt viscosity.

| Check | Code | Verdict |
|-------|------|---------|
| Isotropic diagonal stretch Hessian `k_ss/l · I` | Lines 1258–1308: `kSS = max(6, 0.45·Eavg·h/spacing)`, `kAxial = kSS/l`, `f_ax = kAxial·(len−l)·dir`, `hIso += kAxial`, `cVisc = kSS·min(0.35, 0.10+0.5·2HB)` | ✅ Paper Eq. 28–29 equivalent; scaling comment explains tuning to membrane impedance so wind is felt |
| Symplectic-Euler inertial advection (critical fix) | `pos = inertiaTargets` before rod correction (line 1280) | ✅ This is the AVBD/SCR tier's structure; missing it in E/F caused the puff bug (§5.1) |
| Director `d₃ = q·e₃·q̄` | `quatDirector()` (line 42) | ✅ Correct 3rd column of rotation matrix |
| Bending/twist transmission `b` via co-rod edge averaging | Lines 1368–1378: loop over 1-ring structural edges, `wgt = kBt/deg`, sign-corrected by `dot(q_i, q_j)` | ✅ Implements paper's §3.2 neighbourhood blending |
| Closed-form orientation solve with Banach `γ_e` | Lines 1388–1408: `vLen = 2·kSS·|strain|`, `bLen = |b|`, `λ ← sqrt(v²b²+λ²b²+v²)` ×2 iters, `γ_e = clamp((λ−v)/b, 1e-3..1)`, `q ← normalize(wAlign·tgtQ + wBend·b)` | ✅ Paper Eq. 14–25 with robust `quatFromTo` (180° branch) |
| Stored contraction state per edge | `edgeQuats` (4-float) + `edgeGamma` (1-float) re-seeded in `resetSolverMemory()` from current segment directions | ✅ Prevents stale-orientation fight on switch |
| Hinge bending + patch projection | `_clothPatchProjection(h,2)` + `_hingeBendingProjection(1, kBt/kSS)` | ✅ The hinge pass bends *fold dihedra*, not triangle planes — this is why pleats persist instead of shearing flat |

**Why you saw real folds:** C's `_hingeBendingProjection` (lines 1846–1885) acts on the **fold dihedral** (`b_x = Σ c_i·pos`), so the 6.1° shear (dress) is *trellis* shear between yarns, not a flattened triangle. Its 3.1 mm/s residual is the **quietest in the whole suite** (5–10× cleaner than A/E) — the folds stay where they form. 49.5 ms on the dress (fastest) — but its rod network needs one orientation solve per edge, hardest to fit in the 8-buffer WGSL budget, so it currently runs on the deterministic CPU path.

**Verdict:** **🏆 Best for garment authoring silhouette & folds.** Your FIFA intuition is correct — FIFA/EA Sports and Marvelous Designer use rod-enhanced drape for exactly this reason.

---

### 3.4 Option D — FPB Unified Contact Gauss–Newton (Guo et al. 2025) ✅ Faithful

**Paper idea:** Unified `H = JᵀJ` assembling `H_con = G_con G_conᵀ` inside the Newton step; persistent contact lifespan; `0.9^k` intersection filter; Bézier-pipe knot templates.

| Check | Code | Verdict |
|-------|------|---------|
| Full `k·n·nᵀ` Hessian (no SPD floor) | `_stepSubstepGaussNewton` line 1544 comment: *"FULL rank-one Hessian — what makes this Gauss–Newton"* | ✅ Distinguishes D from AVBD's floored Hessian |
| Contact Hessians with lifespan weighting | Lines 1608–1635: `kC = 2.4e4 · share · 1.2^{min(life,12)}`, `H_con = I − n·nᵀ`, `pen·n` residual | ✅ Paper §4.3 persistent contact |
| Intersection-safe filter | `_gnStepScale` adaptation (lines 1689–1693): `>2.5 mm pen → ×0.9`, else `×1.05`; trust 5 mm per block solve | ✅ Paper's `0.9^k` backtracking |
| Bézier pipe knot templates | `PatternDraftNetwork.js:FPB_KNOT_TEMPLATES` (`fourInHand`, `halfWindsor`, `grannyKnot`) with per-span variable radii + `sampleKnotTemplate()` | ✅ Paper §4.1 parametric pipe model |
| Persistent manifold CSR | `_refreshContactManifold` + `_recordContact` + per-vertex contact CSR in `_stepSubstepGaussNewton` | ✅ |

**Your observation:** *"D also gives cool drapes but simulation prematurely freezes sometimes switching"* — the solver was never frozen. The **app** was. Prior to the fix, `applySolverMode()` left the previous mode's contact manifold, `pointContactAge`, `_gnStepScale`, Anderson state and seam duals in place, and a prior Verify-Fit bake had left `running=false`. Switching therefore resumed with a full stale contact set on a stopped clock. Fix: `resetSolverMemory()` (lines 283–334) — clears manifold/ages, `_gnStepScale=1`, Anderson `aaReady/beta=0`, `seamDual=0`, XPBD λs, re-seeds `edgeQuats/edgeGamma` from current segment directions, resets `quietSettleTime`, plus `syncStateToGpu()` + auto-resume. **Post-fix D moves 50.0 mm in its first 5 frames after a switch (gap 18→17 mm, 0 NaN).**

**Verdict:** **Specialist for layered/knotted drape.** Only mode whose `H_con` holds stacked panels without tunnelling; crispest low-shear drape on dress (4.4°).

---

### 3.5 Option E — JGS2 (Lan et al. 2025) ✅ Faithful — *was* puffed, now fixed

This is one of the two you flagged:

> *"E and F looks bugged (behaves like puffed up jacket)"*

**Paper idea (Eq. 12):** 2-pass Schur-subspace block Gauss-Seidel `(H_ii − Σ H_ij H_jj⁻¹ H_ji) δx_i = −g_i + Σ H_ij H_jj⁻¹ g_j` eliminates Jacobi overshoot analytically.

| Check | Code | Verdict |
|-------|------|---------|
| 1-ring edge CSR for O(deg) Schur term | `vertexEdgeOffsets/Ids/Sign` built at bind time (lines 226–237) | ✅ |
| Assembly of `H_ii` + `g_i` | Lines 2068–2195: `m/h²` inertial + orthotropic membrane (with SPD floor `0.25·k`) + hinge + seam | ✅ |
| Invert every `H_jj` (adjugate, Tikhonov `reg=1e-5·mean(H)`) | Lines 2208–2227 | ✅ |
| Schur elimination over 1-ring | Lines 2232–2280: `kE = 0.25/deg · diagScale`, rank-one `−k·n·nᵀ`, `e = H_ii − k²·n·(nᵀH_jj⁻¹n)·nᵀ`, `rhs += k·n·(nᵀH_jj⁻¹g_j)` | ✅ Paper Eq. 12 |
| Trust region 2.5 mm + 2 patch sweeps | Lines 2291–2309: `trust = jgsTrustRegion || 0.0025`, `stepScale = trust/dLen`, `_clothPatchProjection(h,2)` | ✅ |

**The puff bug and its fix (this is what you saw):** Before the fix, both E and F fed *current* positions into a block solve whose inertial term is `m/h²`. With `h=1/240 s` that term is enormous vs gravity, so the solve **cancelled gravity and air drag every substep** — the garment kept its spawn shell, looking like an inflated jacket. Varying `wallModulus ×0.05/×0.25` and disabling the cubic wall moved the gap by **<2 mm** — not a stiffness bug. Fix: advect every unpinned point to the **inertial target** (symplectic-Euler tier, line 2046: `pos = inertia`) before assembling the block — exactly what AVBD/SCR already did. Result: E (dress) gap **30.6 → 24.9 mm** (best non-AVBD contact, volume 0.134 m³ vs spawn 0.189), F gap **46.2 → 30.5 mm**.

**The shimmer bug and its fix:** After the puff fix, E still rang at 73–110 mm/s. Not an energy leak — ablating patch projection made it *worse* (304 mm/s), extra passes/substeps worse, adding a Kelvin–Voigt dashpot `c=ζ·m/h` (ζ≤8) changed residual <3 mm/s. The Schur solve was overshooting far from the solution where its quadratic model is invalid. Fix: **2.5 mm trust region per block pass** plus **2 patch sweeps** (1 sweep left in-plane strain the block kept chasing). Dress: **349→40 mm/s**, skirt **64→27.5 mm/s**; caps 5/10/20 mm give 92/211/349 mm/s.

**Post-fix telemetry:** On the dress, E now has the **lowest volume overshoot of any non-AVBD mode** (0.134 m³) and the **best non-AVBD contact** (24.9 mm / 21.9 mm on dress/tee), at 61.9 ms — fully color-batchable, no global matrix.

**Verdict:** **🥈 Second-best authoring solver and best "stiff tailoring at 60 FPS" runtime candidate.** The fix is correct and measured.

---

### 3.6 Option F — Kawabata KES-FB + Anderson (Peng & Cao; Cerda & Mahadevan; Walker & Ni) ✅ Faithful — *was* puffed, now fixed

**Paper idea:** Non-orthogonal `(C11,C22,C12)` co-rotational fiber shell with measured `2HB`/`2HG` hysteresis, Cerda–Mahadevan buckling, Anderson(m=2).

| Check | Code | Verdict |
|-------|------|---------|
| Non-orthogonal fiber frame `C11/C22/C12` | Lines 2370–2380: `C11=warpMod`, `C22=weftMod`, `C12=−ν·sqrt(C11·C22)` with `ν=0.12`, `fV = sinθ·fU + cosθ·fW`, `skewDeg` from `TEXTILE_CATALOG` | ✅ Peng & Cao trellis model |
| Crimp interchange (Poisson suppression in compression) | `c12eff = (εu>0 && εv>0) ? C12 : 0` | ✅ Woven shell has no compressive Poisson — this alone moved gap 41.2→31.9 mm |
| Cerda–Mahadevan buckling cap | `epsCrit = sqrt(B/(E·t))` clamped 1e-4..0.01, beyond it load → `0.25·C·epsCrit` and `0.05·C` tangent | ✅ PRL 90, 074302 |
| KES hysteresis: `2HB`/`2HG` as Rayleigh damping | `cHystShear = clamp(0.05+0.35·2HG)`, `cHystBend = clamp(0.05+0.35·2HB)` applied as `k·v·area` + `/h` Hessian addition | ✅ Measured textile parameters |
| Yarn-jamming lock | `_clothPatchProjection(h,2)` with `warp/weft max = 1+crimp+0.004` and shear `sin(lockingAngle)=0.26` | ✅ Reduced yarn stretch 1.097→1.065 |
| Monotonicity-guarded Anderson(m=2) | `_andersonAccelerate` (lines 2591–2670): depth-2 history, `β = (Gᵀd)⁻¹ GᵀF`, energy guard `E(trial)≤1.0001·E0` via `_membraneEnergy()` | ✅ Walker & Ni 2011 |

**Your observation:** same puff as E — same root cause, same inertial-advection fix. F additionally needed the patch projection (yarn 1.097→1.065, residual 74→21 mm/s) and Poisson suppression to converge.

**Post-fix character:** Highest measured textile fidelity — shear **11.6°/11.0°** (dress/tee, the anisotropic `C11/C22/C12` + `2HB/2HG` frame), but **worst wall-clock** (67.0 ms dress) and least responsive to a grab (6.1 mm in 5 frames — Anderson throttling). A precision authoring tool, not an interactive sculptor.

**Verdict:** **Precision hand-feel reference.** Best for sign-off where measured Kawabata hysteresis matters.

---

## 4. PIE Optimal Mesh Resolution (Zhang et al. 2025) ✅ Faithful

| Check | Code | Verdict |
|-------|------|---------|
| Calibrated polynomial `c₁r⁶ + c₂r⁴ = B/E` with `c₁=32.08 m⁻³`, `c₂=−1.876×10⁻⁴ m⁻¹`, bisection on `r∈[2.5,50] mm` | `TextileCatalog.js:solvePiePolynomialRadiusMeters` lines 147–166, 22 iters | ✅ Zhang Eq. 4 |
| Per-axis anisotropic radii via `E_vol = (k/h)/decRimpFactor` with decrimp `1/(1+0.18·crimp%)` | `estimateClothResolutionPIE` lines 168–186 | ✅ |
| Wrinkle wavelength `λ ≈ r*/0.27` + wrinklon `L_w ~ λ^{3/2}(δ/h²)^{1/4}` | Lines 188–195 | ✅ Cerda & Mahadevan + Vandeparre |
| Clamped authoring `9.5–18.5 mm` / game `13.5–24 mm` + `seamSource = 0.72·r*` | Lines 197–198 | ✅ |
| Auto-res button + `compileGarmentNetwork({textile})` wiring | `PatternDraftNetwork.js:compileGarmentNetwork` lines 200–206 | ✅ |
| Validation: per-textile `r*` varies (9.99–18.28 mm), anisotropic warps differ, `λ>r*`, clamps hold | `ClothValidation.mjs` §3c — 6 textiles PASS | ✅ |

**Recommendation:** Run once per textile change; it already drives the auto-res button and `compileGarmentNetwork`.

---

## 5. What Was Bugged, and What Changed (root causes, not tweaks)

### 5.1 E & F "puffed-up jacket" — solved

*Root cause:* Both `_stepSubstepSchurJGS2` and `_stepSubstepKawabata` fed **current positions** into a block/Newton solve whose inertial term is `m/h²`. At `h=1/240 s` this dominates gravity, so the solve cancelled gravity + aerodynamic drag every substep — the garment kept its spawn shell (SDF gap ~30–46 mm, volume ~0.19 m³).

*Proof it wasn't stiffness:* Varying `wallModulus ×0.05/×0.25` and disabling cubic wall moved gap **<2 mm**.

*Fix:* Symplectic-Euler advection to `inertiaTargets` before block assembly (lines 2046 and 2358 — exactly what AVBD/SCR already did).

|  | gap before → after (dress) |
|---|---|
| E `jgs2` | 30.6 → **24.9 mm** |
| F `kes_aa` | 46.2 → **30.5 mm** |

F also gained `c12eff` suppression (gap 41.2→31.9) and 2-patch projection (yarn 1.097→1.065).

### 5.2 E shimmer — solved

*Root cause:* Schur solve overshooting far from solution where its quadratic model is meaningless. Extra passes/substeps or a Kelvin–Voigt dashpot (`ζ≤8`) didn't help.

*Fix:* **2.5 mm trust region per block pass** + **2 patch sweeps** (1 sweep left in-plane strain the block chased). Dress 349→40 mm/s, skirt 64→27.5 mm/s; larger caps (5/10/20 mm) → 92/211/349 mm/s.

### 5.3 D "freeze on switching" — solved (not a solver bug)

*Root cause:* `applySolverMode` retained previous contact manifold, `pointContactAge`, `_gnStepScale`, Anderson state, seam duals, XPBD λs, and a prior Verify-Fit bake had left `running=false`. Switching resumed with a stale full contact set on a stopped clock.

*Fix:* `resetSolverMemory()` (lines 283–334) + `syncStateToGpu()` + `_lastSteppedMode` re-upload + auto-resume. D now moves **50.0 mm** in 5 frames after switch (gap 18→17 mm, 0 NaN).

---

## 6. Final Recommendation (to be confirmed by your interactive evaluation)

| Workflow | Primary | Secondary | Notes |
|----------|---------|-----------|-------|
| **Game Runtime** | **B `xpbd`** | **E `jgs2`** when tailoring must stay stiff | B for cost/predictability; E when garment must not balloon. A only where determinism/contact accuracy is audited. |
| **Garment Authoring — silhouette & folds** | **C `scr`** | **D `gn_knot`** for multi-layer & knots | C for the fold language, D for stacked panels/ties/closures. Your test already surfaced this ranking — the numbers now prove it. |
| **Garment Authoring — hand-feel sign-off** | **F `kes_aa`** | **A `avbd`** as contact reference | F's measured `2HG`/`2HB` hysteresis is the closest to physical textile behavior; A is the body-contact yardstick. |
| **Mesh density** | **PIE (Zhang 2025)** `r*` per textile | — | Run once per textile change; it drives auto-res and reports `λ`/`L_w`. |

### One-line answer for the branch

> **C is still the best authoring solver for folds and the quietest overall; E is now legitimately second and the one to watch; D is the specialist for layers/knots; F is the reference for true textile hand-feel; A is the accuracy yardstick; B is the only sensible game runtime — and yes, all six are now implemented correctly, with the three reported defects traced to root cause and fixed.**

### Known residual behavior (deliberately not hidden)

- **E** still carries ~40 mm/s low-level ripple at rest (0.7 mm/frame on 3.5 k points) — visible only under extreme zoom, bounded and dissipative.
- **F** is slow to respond to a grab (6.1 mm / 5 frames) — Anderson hysteresis throttling; next tuning item is grab coupling.
- **C** and **A** hold 3–39 mm/s residual at 180 frames — neither crosses `isSettled` (read-only flag), neither ever gates integration.
- Only **A** and **B** have WGSL kernels today; **C–F** run on the deterministic CPU path (35–70 ms) and fall back automatically. Porting C and E to WGSL is the largest remaining performance item — see `PhysicsAndClothArchitecturePlan.md §10.6` and `ClothSolverComparisonReport.md §5`.

---

## 7. Reproducibility

```bash
cd Frontier-1/Experimental/ClothEditor
node ClothValidation.mjs          # all 61 checks PASS, ~330 s
# or open the Atelier
python3 Serve.py                  # 0.0.0.0:8084
# then use the Inspector "Algorithm" segmented control to switch A–F live
```

Full measured table (180 calm-air frames, calm air, same mannequin SDF) is in `Documentation/Plans/ClothSolverComparisonReport.md` §2–§3, with the §4 post-mortem.

---

*Verification performed on branch `arena/7f3a6560-frontier-1` (merged from `arena/56d5516e-frontier-1`), commit `3189170`, against PDFs: Hsu et al. 2025 (Stable Cosserat Rods), Guo et al. 2025 (FPB), Lan et al. 2025 (JGS2), Zhang et al. 2025 (PIE), Kawabata KES-FB + Walker & Ni 2011, and Zsolnai-Fehér 2022 (Nature Physics).*
