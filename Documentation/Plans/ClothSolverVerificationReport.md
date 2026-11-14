# Cloth Solver Verification Report — Were A–F Implemented Correctly?

**Independent audit of branch `arena/56d5516e-frontier-1` (imported into this branch), checked paper-by-paper
against the actual PDFs, then measured. This report supersedes the rankings section of
`ClothSolverComparisonReport.md` where they disagree (that report was written by the same code it graded).**

Papers audited from primary sources:

| # | Source given | What it actually is |
| :--- | :--- | :--- |
| 1 | `graphics.cs.utah.edu/research/projects/stable-cosserat-rods/` | Hsu, Tongtong Wang, Wu & Yuksel, *Stable Cosserat Rods*, SIGGRAPH 2025 — a **rod/yarn** solver (hair, yarn-level cloth), not a triangle-membrane cloth solver |
| 2 | `wanghmin.github.io/publication/guo-2025-fpb/` | Guo et al., *Fast Physics-Based Modeling of Knots and Ties Using Templates*, SIGGRAPH 2025 — a **knot/tie authoring system** (Bézier-pipe templates + mapping + a quasistatic GN simulator), not a general garment solver |
| 3 | `nature.com/articles/s41567-022-01788-5` | Károly Zsolnai-Fehér, *The flow from simulation to reality*, Nature Physics 2022 — a **Comment/survey about fluid simulation**. It contains **no cloth algorithm at all** |
| 4 | `wanghmin.github.io/publication/zhang-2025-pie/` | Zhang et al., *Physics-inspired Estimation of Optimal Cloth Mesh Resolution*, SIGGRAPH 2025 — a **mesh-resolution estimator**, not a solver |

Existing A/B/E/F sources were also re-checked: AVBD = Giles, **Diaz** & Yuksel 2025 (the code previously
credited "Giles, Chen & Yuksel" — fixed), XPBD = Macklin et al. 2016, JGS2 = Lan et al., ACM TOG 44(4) 2025,
F = Kawabata KES-FB constitutive data + Anderson acceleration (Walker & Ni 2011).

---

## 1. Short answer

**No — none of A–F is a faithful implementation of its paper, and two of the four papers you gave are not
solvers at all.** What exists is **one shared membrane cloth engine** (same prologue/epilogue, patch
projection, seam projection, avatar SDF contact, self-collision and hysteresis damping for every mode) with
**six different local-solve flavours** plugged into it. That is why switching A–F changes the *character* of
the drape less than the labels promise, and why C's folds — your favourite — are **not** made by Cosserat rods
(see §3.1). Within that honest framing, the code is mostly sound, deterministic, and (after the fixes in §5)
free of the three defects you saw. Your ranking observations were largely correct; the reasons behind them
were partly wrong.

| Option | Mode | Fidelity to its paper | Verdict in one line |
| :---: | :--- | :--- | :--- |
| A | `avbd` | **Partial** — VBD 3×3 block Newton is real; AVBD augmented-Lagrangian only on seams, not on all/hard constraints | Best body contact & seams; the accuracy yardstick |
| B | `xpbd` | **Mostly** — XPBD λ/α̃ form is correct; crimp-wall branch is a hard projection, not compliance | The only sane game-runtime default |
| C | `scr` | **Weak adaptation** — orientation state exists, but its closed-form Eq. 18 solve is a blend and `q` never moves a vertex | Best *measured* authoring drape — for non-Cosserat reasons |
| D | `gn_knot` | **Fragment** — Guo §6.1 GN contact idea only; the entire template/mapping system is absent or dead code | Layer/knot specialist in name; currently the loosest drape |
| E | `jgs2` | **Approximation** — JGS2's Schur form is recognisable, but `H_CC⁻¹` is block-diagonalised and `H_ij` is invented, not assembled; no cubature/co-rotation precompute | Good gap, worst residual chatter |
| F | `kes_aa` | **Heuristic** — KES numbers used as damping knobs, not measured curves; had a real modulus-squaring bug (fixed) | After fixes: quiet, tight, best hand-feel candidate |
| — | PIE | **Core equation exact** (Eq. 4, c₁=32.08 m⁻³, c₂=−1.876e-4 m⁻¹), system around it incomplete | Use as a spacing heuristic, not as "PIE" |
| — | Nature 2022 | **Not applicable** — fluids survey; was cited as the source of an aero-drag model it does not contain (citation removed) | Nothing to implement |

---

## 2. Measured results (post-fix, this branch)

`node ClothSolverBench.mjs` (new, in `Experimental/ClothEditor/`) — same mesh, textile, mannequin SDF,
180 calm-air frames @ 60 FPS. `gap` = mean |SDF| distance to the body (spawn = the un-draped cut shell);
`vol` = bounding-box volume (the "puffed jacket" meter — spawn is 0.189 m³, a fitted dress should approach
≈0.11 m³); `rms` = final RMS speed (lower = actually settled).

### coutureDress — 3 576 pts / 6 690 tris, cotton poplin

| Opt | Mode | ms/frame (CPU) | gap mm | vol m³ | rms mm/s | yarn | shear | seam mm | pen mm |
| :---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A | `avbd` | 46.3 | **17.1** | **0.113** | 39.1 | 1.038 | 7.9° | **0.13** | 0.00 |
| B | `xpbd` | 47.5 | 29.4 | 0.159 | 8.4 | 1.046 | 1.7° | 0.20 | 0.00 |
| C | `scr` | **39.2** | 29.7 | 0.166 | **3.1** | 1.090 | 6.1° | 0.41 | 0.00 |
| D | `gn_knot` | 68.3 | 34.3 | 0.183 | 13.8 | 1.043 | 6.1° | 0.28 | 0.00 |
| E | `jgs2` | 50.2 | 24.9 | 0.134 | 39.8 | 1.107 | 9.2° | 0.45 | 0.00 |
| F | `kes_aa` | 52.4 | 25.7 | 0.120 | 5.1 | 1.061 | 8.3° | 0.32 | 0.00 |

### tailoredTee — 2 198 pts / 4 044 tris, cotton jersey

| Opt | Mode | ms/frame (CPU) | gap mm | vol m³ | rms mm/s | yarn | shear | seam mm | pen mm |
| :---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A | `avbd` | 34.9 | **19.3** | **0.084** | **4.8** | 1.076 | 3.7° | **0.10** | 0.00 |
| B | `xpbd` | 33.4 | 20.6 | 0.085 | 6.7 | **1.038** | 2.0° | 0.20 | 0.00 |
| C | `scr` | **29.2** | 21.6 | 0.102 | **3.8** | 1.157 | 6.4° | 0.43 | 0.00 |
| D | `gn_knot` | 35.9 | 27.5 | 0.112 | 9.3 | 1.116 | 7.5° | 0.11 | 0.00 |
| E | `jgs2` | 28.8 | 21.9 | 0.094 | 57.9 | 1.134 | 6.0° | 0.17 | 0.00 |
| F | `kes_aa` | 32.4 | 23.6 | 0.102 | 7.8 | 1.106 | 4.8° | 0.20 | 0.00 |

Penetration is 0.00 mm everywhere (shared, solver-independent SDF + VF/EE self-contact layer).
`node ClothValidation.mjs` passes in full after the fixes (all 24 garment×solver cells, bit-exact
determinism for all six, drag-after-relax, PIE, knot templates, fit bake, CAD exports).

### Mid-run switch (AVBD ×70 frames → target ×5 frames, App path with `resetSolverMemory()`)

| → | A `avbd` | B `xpbd` | C `scr` | D `gn_knot` | E `jgs2` | F `kes_aa` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| max moved mm | 32.0 | 61.2 | 45.5 | 41.6 | 37.1 | 8.2 |
| mean moved mm | 3.2 | 15.3 | 18.7 | 4.0 | 13.5 | 1.9 |
| gap in → out mm | 18.4→18.1 | 18.4→22.3 | 18.4→22.7 | 18.4→19.0 | 18.4→19.0 | 18.4→18.4 |
| NaN | 0 | 0 | 0 | 0 | 0 | 0 |

**Nothing freezes on switching any more.** F is genuinely sluggish to wake (1.9 mm mean in 5 frames — its
guarded Anderson step + hysteresis), D is unremarkable now — see §4.3.

---

## 3. Paper-by-paper audit

### 3.1 Stable Cosserat Rods (Hsu et al. 2025) → Option C `scr` — *adapted, and the adaptation is not what drapes*

The paper, in brief: rods are segments with a unit-quaternion frame `qᵢ`. Angular momentum is neglected
(J = 0, quasi-static rotation), so the step splits into a position solve (Eq. 6, VBD with the diagonal
stretch Hessian `k_ss/l² · I`, Eq. 29, force Eq. 28 `f = (k_ss/l)((xᵢ₊₁−xᵢ)/l − d₃)`) and a **closed-form**
orientation solve: with `v = −2k_ss l⁻¹(xᵢ₊₁−xᵢ)` (Eq. 15), `b` = sum of bend/twist contributions from
connected segments including rest frames `q⁰` and transmission `φ` (Eq. 16–17), `λ ≈ |v|+|b|` (Eq. 22,
optionally refined by the Banach fixed point Eq. 25 with stored `γ` per segment, Eq. 26–27, Alg. 1),
`qᵢ(λ) = (v·b·e₃ + λb)/(λ²−|v|²)` (Eq. 18), normalised (Alg. 2). Contact is an IPC log-barrier (Eq. 30).

What the code actually does (`_stepSubstepCosserat`):

- ✅ Split solve, per-edge `q` + `γ` state, diagonal position Hessian, alternating position/orientation per substep.
- ✅ **Fixed in this branch:** quats are now seeded from spawn segment directions (they were identity = every
  rod pointing +Z); `|v|` now follows Eq. 15 (`2·k_ss·len/l`). It was `2·k_ss·strain`, i.e. **zero at rest
  length**, where the paper has `|v| = 2k_ss` — the stretch term vanished exactly at equilibrium.
- ❌ The orientation update is **not Eq. 18**. There is no quaternion product `v·b·e₃`; `v` and `b` are reduced
  to scalars `|v|`, `|b|`, `b` itself is replaced by a **normalised neighbour-average of edge quats** (ignoring
  `q⁰` rest frames and `φ`), and the result is a weighted blend between a shortest-arc-to-tangent quat and
  that average, renormalised. The Eq. 25 fixed point is approximated ignoring the cross term of
  `‖v·b·e₃ + λb‖`. This is a plausible heuristic, not the paper's closed form.
- ❌ **`q` never influences a position.** In the position pass, `d₃` was computed and explicitly discarded
  (`void d3`); the force is a plain axial spring `k·(len−l)` along the segment. `edgeTwist` is allocated and
  never read. So the "Cosserat" half of C is write-only bookkeeping.
- ⚠️ **I tested fixing that** (report Sec. 3 experiment, since reverted): feeding `k·(segment − l·d₃)` into
  positions undamped → **RMS 3.1 → 256 mm/s, never settles**; 25 %-damped → settles (3.3 mm/s) but **bbox
  0.166 → 0.226 m³ and shear 6.1° → 7.7°** (it stiffens folds shut). These are triangle edges, not yarns:
  a smoothed neighbour-averaged frame fighting the membrane is wrong here. The code keeps the axial form,
  with the experiment documented at the site.
- ❌ No IPC barrier/friction for rods (Eq. 30–33) — body/self contact is the shared SDF + projection layer.

**So why does C look like FIFA-style drape folds?** Because of `_hingeBendingProjection` (discrete-plate
hinge curvature — pleats form on the *dihedral*, not by shearing triangles) plus two `_clothPatchProjection`
sweeps (crimp/s locked warp-weft-shear), both shared-code passes that C happens to combine with the quietest
position solve (RMS **3.1 / 3.8 mm/s**, 5–10× quieter than A/E). Your eye was right; the credit assignment
in the UI ("yarn/knit drape") is not.

### 3.2 Guo et al. 2025, Knots & Ties (FPB) → Option D `gn_knot` — *only §6.1's simulator idea, and it had it backwards*

The paper is an **authoring pipeline**: Bézier-medial-axis pipe template with variable radius (§4.1),
parallel-transport tessellation (§4.2), ACCD + adaptive radius reduction (§4.3), rectangular/circular
UV→pipe mapping (§5.1), shape-matching initialisation `{R,t} = argmin Σ‖Rxᵐ+t−xᶠ‖²` with lifespan-weighted
collision gradients `G = Σ 1.2^{Kᵢ} Gᵢ` (§5.2 — this is where `1.2^K` lives), then a quasistatic primary
simulator: one Gauss–Newton step per timestep, unified dynamics+contact+friction, `H_col = G_col G_colᵀ`,
`H_con = G_con G_conᵀ` with `G_con^vf = (−β₀N, −β₁N, −β₂N, N)`, Block-Jacobi PCG with 3×3 preconditioners,
μ = 0.1 (§6.1), plus an IPC fallback that backtracks by 0.9 until intersection-free (§6.2).

Against that:

- ✅ Membrane `H = JᵀJ` assembly and VF contact records (`_recordContact`) genuinely exist; EE self-contact exists in the shared layer.
- 🐛 **Fixed in this branch:** the contact block was `k_c·(I − nnᵀ)` — the **tangential projector**, i.e.
  zero stiffness in the direction a contact actually pushes — with RHS `−k_c·depth·n` applied with the same
  sign to *every* participant. `H_con = G_conG_conᵀ` for this stencil is `β²·nnᵀ` (normal, rank-one) and
  the face side carries the opposite sign to the point. Now: signed coefficients (+1 point, −β face,
  −1 second point), block `k·coeff²·nnᵀ`, RHS `+k·coeff·depth·n`. Effect on the dress: RMS 36.8 → **13.8**
  (much quieter), but gap 26.7 → **34.3 mm** and vol 0.138 → **0.183 m³** — the old tangential block was
  accidentally acting as layer glue that held the silhouette tight; a `contactK = 0` ablation gives the same
  34.9 mm, confirming the new term is not over-pushing, the glue is simply gone.
- ❌ Not PCG/Block-Jacobi, not one quasistatic Newton per step: it is one colour-batched per-vertex 3×3
  Gauss–Seidel sweep per substep ×5 substeps — the same VBD pattern as A.
- ❌ The `0.9^k` filter never re-tests intersections after a step (the paper's whole point); it is a 5 mm
  trust cap + a global step-scale nudged by the deepest *stale* contact depth.
- ❌ The lifespan weight `1.2^K` is lifted from the §5.2 *initialisation* into the simulator (the paper's
  simulator does not state it), and `pointContactAge` halving/decay is invented.
- ❌ **The knot system itself does not exist in the product.** `FPB_KNOT_TEMPLATES` (3 hand-invented
  templates; the paper has no canonical coordinates to copy) is only a polyline+radius sampler —
  `sampleKnotTemplate` has **no caller** outside validation. No tessellation, no parallel transport, no
  ACCD, no radius reduction, no mapping, no knot garment, no UI. Option D cannot make a knot or a tie.

### 3.3 Nature Physics s41567-022-01788-5 — *not a cloth paper; nothing to add as a solver*

This is Károly Zsolnai-Fehér's (Two Minute Papers) Comment *"The flow from simulation to reality"*
(Nature Physics **18**, 2022), surveying twenty years of **fluid** simulation (Stable Fluids, FLIP, etc.).
There is no cloth model, no solver, and no equation in it that could become an "Option G". The code never
implemented it as a solver — correctly — but it did cite it as the source of the anisotropic air-drag model
in `_substepPrologue` ("Zsolnai-Fehér (2022) boundary layer, C_N = 0.62, C_f = 0.048"). That paper contains
no such model; those coefficients are Atelier calibrations. **The citation has been removed and replaced
with an honest note.** If a fluids-inspired candidate is wanted later, it would be for wind/fluid coupling,
not cloth drape.

### 3.4 Zhang et al. 2025, PIE → `estimateClothResolutionPIE` — *the equation is real; the system is 20 % built*

Present and correct: Eq. 4 `c₁r⁶ + c₂r⁴ = B/E` with the paper's own calibrated `c₁ = 32.08 m⁻³`,
`c₂ = −1.876×10⁻⁴ m⁻¹`, `r ∈ [2.5, 50] mm`, monotone bisection, per-axis (warp/weft) radii, and the
Vandeparre wrinklon form `L_w ∼ λ^{3/2}(δ/h²)^{1/4}`.

Deviations:

- 🐛 **Fixed:** `E_vol` was clamped to 2×10⁶ N/m², which truncated nearly every textile to the same `E` —
  warp and weft radii came out **identical** for cotton poplin (13.75/13.75 mm) despite 1850 vs 1550 N/m.
  The paper's `[2e3, 2e6]` is its calibration envelope, not a per-fabric clamp. Now warp 12.27 / weft
  12.84 mm (poplin), 14.94/15.57 (satin), etc.
- ❌ `λ = r/0.27`: the paper only says `r_opt = √(λ²/16 + A²) ∼ c·λ` with `c` absorbing compression and
  mesh geometry; 0.27 is an Atelier choice, presented in comments as if from the paper's Fig. 4.
- ❌ Inputs are not the paper's measured `B, E`: `B` is the catalog's `bendRigidity [mN·m]·10⁻³` and `E` is
  `(N/m modulus × invented decrimp factor 1/(1+0.18·crimp%)) / thickness`. Reasonable, but calibrated
  guesses — the catalog's textile numbers themselves (GSM, KES 2HB/2HG, locking angles…) are plausible
  invented values, **not lab-measured data**, despite "Measured" wording in file headers.
- ❌ Missing the paper's actual contribution beyond the global radius: **source resolutions** (shirring,
  folding, stitching, down-filling, dynamic collision zones), the **sizing map**
  `r_v = min(r_f, r₀ + x₀^{m₀}, …)`, and **Poisson-sampled Delaunay non-uniform remeshing**. The Atelier
  applies one clamped uniform spacing (authoring 9.5–18.5 mm, game 13.5–24 mm — both clamps invented;
  the PIE button frequently just returns the clamp, e.g. denim authoring = 18.5 mm regardless).

### 3.5 The other three (A, B, E, F) — quick fidelity notes

- **A `avbd`:** Real AVBD (Giles, Diaz & Yuksel) augments **every** constraint with duals + progressive
  penalties to handle hard/infinite-stiffness constraints and stiff contacts. Here only **seams** get the
  AL treatment (duals clamped ±18, penalty ×1.10 on >0.8 mm error); membrane/bend are plain VBD Newton
  with an SPD Hessian floor, and contact is SDF step-bounds ("OGC"), not AVBD frictional hard constraints.
  Best-in-class results (gap **17.1 mm**, seam **0.13 mm**) come from that combination, not from AVBD per se.
- **B `xpbd`:** The λ update with `α̃ = 1/(k h²)` is textbook XPBD. Above crimp, however, λ is neither
  accumulated nor returned (`cu > warpCrimp` branch) — that branch is a hard PBD strain-limit projection,
  and the `0.32× / 1.6× / 8×` stiffness rescalings are tuning, not Macklin et al.
- **E `jgs2`:** Lan et al.'s second-order-optimal local solve is
  `δxᵢ = (H_ii + H_iC U_C)⁻¹(H_iC H_CC⁻¹ g_C − gᵢ)` with `U` **precomputed from the rest-shape Hessian**,
  transported by per-vertex co-rotated frames and evaluated by cubature (Eq. 14, §4.3–5). The code has the
  same Schur silhouette but approximates `H_CC⁻¹` by independent per-neighbour `H_jj⁻¹`, and **invents**
  `H_ij = −k·nnᵀ` with `k = 0.25·diagScale/deg` instead of assembling it from the elements (the very
  coupling that makes JGS2 second-order is the part not implemented). Its 2.5 mm trust region is an
  Atelier stabiliser. Result: good gap (24.9 mm) but the worst settling of the stiff solvers
  (39.8 / 57.9 mm/s residual — bounded chatter, not divergence).
- **F `kes_aa`:** Kawabata KES-FB is a *measurement system* (LT/WC/WT/B/G/2HB/2HG curves); no KES curve is
  fitted anywhere — 2HB/2HG are scalar damping knobs, `weaveSkewDeg` defaults to an invented 2° for every
  fabric, and `ε_c` omits the length scale its own comment cites. Anderson(m=2) is applied **across
  substeps**, where the fixed-point operator changes every substep — outside Walker & Ni's assumptions.
  See the bug this caused in §5.

### 3.6 The uncomfortable structural fact

All six modes share: `_substepPrologue/Epilogue` (gravity, aero, hysteresis velocity decay
`_applyLowAmplitudeHysteresis` — itself a smooth velocity multiplier, despite "no artificial settling"
comments elsewhere), `_clothPatchProjection`, `_seamProjection`, `_projectAvatarFastSdf`,
`_projectBarycentricSelfContact` (VF+EE), and the frame-level seam closure in `stepFrame`. The "solvers"
differ in **one local solve per substep**. Cross-mode differences therefore understate what changing the
actual algorithm (per the papers) would do, and no mode should be marketed by its paper's name without
"adapted/inspired".

---

## 4. Your observations, explained

1. **"C is probably the best — FIFA-like folds when it drapes."** ✔ Agreed on outcome (quietest solver,
   3.1 mm/s; folds persist). ✘ Not because of Cosserat rods — because of the hinge-dihedral + patch passes
   (§3.1). C also stretches most (yarn 1.090–1.157): it trades in-plane accuracy for fold freedom.
2. **"AVBD 2nd — simulates collision and draping better."** ✔ Measured best body contact (17.1/19.3 mm),
   smallest volume, tightest seams. It is the contact/accuracy reference.
3. **"XPBD probably for games."** ✔ Correct. Flattest folds (shear 1.7–2.0°) and loosest precision, but
   simplest, bit-exact, and — with A — **the only mode with a WGSL/GPU kernel**. Note no mode is 60 FPS on
   CPU at 3.5 k points (29–68 ms/frame); game runtime *requires* the GPU path, which only A/B have.
4. **"E and F look bugged — puffed-up jacket."** Was true for F for a concrete reason (§5, modulus squared:
   dress vol 0.168 → **0.120 m³**, gap 30.5 → **25.7 mm** after fix; tee RMS 8.2 was masking a 126 mm/s
   Anderson oscillation on jersey, now 7.8). For E the puff had already been addressed before this branch
   by advecting to the inertial target; what remains is **chatter** (39.8 mm/s), not puff — its volume
   (0.134) is second-best of the non-AVBD modes.
5. **"D gives cool drapes but prematurely freezes sometimes when switching."** The freeze was in the app,
   not the solver: switching used to inherit the previous mode's contact manifold/Anderson state and could
   resume on a stopped clock after a fit bake. The current App calls `resetSolverMemory()` + `wakeUp()` +
   auto-resume on **both** switch controls, and the bench above moves every mode within 5 frames with no
   NaN (D: 41.6 mm max). Reproduced without the reset as well — D still moves (mean 3.3 mm), so treat any
   remaining "freeze" sighting as an App/GPU-sync bug, not `gn_knot`. The stale-manifold story in the old
   report was overstated. After the contact-Hessian correction (§3.2) D is quieter but drapes **looser**
   (dress vol 0.183, near spawn) — its "cool drapes" were partly the old tangential glue.

---

## 5. Bugs found and fixed in this branch

| # | Where | Bug | Fix / evidence |
| :--- | :--- | :--- | :--- |
| 1 | F `_stepSubstepKawabata` | `su = C11·tensionU + …` where `tensionU` already = `C11·ε + wall` → stress ∝ **C₁₁²** (≈1850× too stiff vs inertia); `C12` also negative | `su = tensionU + C12·εv`, `sv = tensionV + C12·εu`, tangents un-multiplied, `C12 = +ν√(C11C22)`. Dress: vol 0.168→0.120, gap 30.5→25.7, RMS 24.7→5.1 |
| 2 | F `_andersonAccelerate` | Guard accepted trials with membrane energy up to `e0·1.0001` and unbounded per-vertex moves; AA history spans changing substep operators → jersey tee driven to **126 mm/s** forever (AA off: 18.7) | 2 mm trial-move cap + strict `e1 < e0`. Tee: RMS 126→**7.8**, gap 29.5→23.6; dress RMS →6.0 (360 f) — AA now *helps* |
| 3 | D contact assembly | `H_con = k(I − nnᵀ)` tangential projector, unsigned same-direction RHS for point and face — contradicts Guo `H_con = G_conG_conᵀ` | Signed coefficients, `coeff²·nnᵀ` block, `+k·coeff·depth·n` RHS. Dress RMS 36.8→13.8 (drape looser, §3.2) |
| 4 | C `_stepSubstepCosserat` | `|v| = 2k_ss·strain` (paper Eq. 15: `2k_ss·len/l`); rod quats initialised to identity (+Z) regardless of segment | Eq. 15 magnitude; quats seeded from spawn directions (as `resetSolverMemory` already did) |
| 5 | PIE `estimateClothResolutionPIE` | `E` clamped at 2e6 → warp/weft anisotropy erased (equal radii) | Wide safety bound only; poplin warp/weft now 12.27/12.84 mm |
| 6 | `_substepPrologue` + catalog | Nature Physics fluids Comment cited as aero-model source; "Hsu (2026)" unverified citation; AVBD credited to "Giles, Chen & Yuksel" | Citations removed/corrected (Giles, **Diaz** & Yuksel); solver catalog summaries for C/D/E rewritten to say what is actually implemented |
| 7 | (experiment, reverted) | Coupling C's `d₃` into positions "to make Cosserat real" | Measured harmful (§3.1): RMS 3.1→256 undamped; puff + shear with damping. Kept axial form, documented at the code site |

Also added: `ClothSolverBench.mjs` (the tables above are reproducible), and this report. No solver was
deleted; A–F all remain selectable, all pass `ClothValidation.mjs`, all bit-exact deterministic.

**Not fixed (needs a real project, not a patch):** true Eq. 18 Cosserat orientation for yarn-level models;
Guo's template tessellation/mapping/knot authoring; JGS2's precomputed co-rotated cubature subspace;
PIE sizing maps + non-uniform remeshing; WGSL kernels for C–F; lab-measured textile data.

---

## 6. Recommendation

| Use | Pick | Why (measured) |
| :--- | :--- | :--- |
| **Game runtime** | **B `xpbd`** primary, **A `avbd`** for hero garments | Only WGSL-implemented modes; B cheapest/most predictable, A when contact/seam fidelity is visible. Budget CPU cloth at ≥2× coarser than 16.5 mm or on GPU — nothing here is 60 FPS on CPU at dress density |
| **Authoring — folds & silhouette** | **C `scr`** | Quietest (3.1 mm/s), fold language from hinge+patch passes, fastest CPU mode on the dress (39 ms). Accept yarn stretch 1.09–1.16; verify fit with A |
| **Authoring — fit & body contact** | **A `avbd`** | Gap 17.1 mm, seam 0.13 mm, vol 0.113 m³ — the yardstick every other mode should be checked against |
| **Authoring — textile hand-feel** | **F `kes_aa`** (post-fix) | Now genuinely competitive: vol 0.120, RMS 5.1, highest bias shear (silk tunic 16.7° in validation). Slowest to respond to grabs — a sign-off tool, not a sculpting tool |
| **Authoring — layers / (future) knots** | **D `gn_knot`** with eyes open | Best seam behaviour on the tee (0.11 mm) and quiet now, but loosest dress drape and *no knot functionality exists yet*. Do not promise knots/ties from this mode |
| **Stiff tailoring research** | **E `jgs2`** | Good gap/volume per cost; fix its chatter (more patch sweeps / smaller trust) before shipping it as a mode, and port the *real* precomputed JGS2 if it becomes the runtime bet |
| **Mesh resolution** | PIE `r*` as a **starting spacing only** | Trust Eq. 4's ordering (silk finer than denim), not the clamped mm outputs; re-drape in A/C to confirm |

**One-line answer to "which is best?":** *For games: B (A for heroes). For authoring: C for folds, A to
verify fit, F to sign off hand-feel. Your original podium (C > AVBD > XPBD-for-games) was right — C just
wins for different reasons than its name claims, and F has been promoted from "bugged" to genuinely
second-best authoring drape by the two fixes above.*

---

*Reproduce: `cd Experimental/ClothEditor && node ClothValidation.mjs` (all checks pass) and
`node ClothSolverBench.mjs` (tables in §2). Serve the Atelier with `python3 Serve.py` and switch A–F in the
Algorithm control to compare live.*
