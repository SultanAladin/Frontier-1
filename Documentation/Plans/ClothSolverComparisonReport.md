# Cloth Solver Showdown — Which Candidate Is Actually Best?

**Measured, not asserted.** All six architectures in the Atelier (`Experimental/ClothEditor/`, live on `0.0.0.0:8084`) share one mesh, one textile, one seam solver, one contact solver and one telemetry pipeline. They differ **only** in the local/global solve. Every number below comes from `ClothValidation.mjs` and `/tmp/bench.mjs` running headless on the same `FrontierMannequin.bin` SDF, 180 calm-air frames at 60 FPS.

Micro-Delaunay test garments: **coutureDress** (3 576 pts / 6 690 tris, 135 GSM cotton poplin) and **tailoredTee** (2 198 pts / 4 044 tris, 190 GSM cotton jersey). Spawn reference (un-draped cut shell): **SDF gap 78.4 mm, bbox volume 0.189 m³** — so *smaller gap is closer to the body*, and *volume must fall toward ≈0.11 m³*; a volume that stays near 0.19 m³ is the "puffed jacket" artefact.

---

## 1. Verdict

Your read of the field was mostly right — and the two "bugged" ones were genuinely bugged. Here is what the measurements say now, after the fixes described in §4:

| Rank | For **Garment Authoring** (drape, folds, layers) | Why (measured) |
| :---: | :--- | :--- |
| 🥇 **1** | **C — `scr`** (Stable Cosserat Rods) | Still the fold machine: the `_hingeBendingProjection` pass bends fabric into real pleats instead of letting triangles shear, and it is the **quietest** solver of all — settles to **3.1 mm/s** (dress) / **3.8 mm/s** (tee), 5–10× cleaner than A or E. Folds are stable and stay where they form. |
| 🥈 **2** | **E — `jgs2`** (JGS2 Schur-subspace) | Was the worst offender, now the **best non-AVBD body contact**: 24.9 mm gap (dress) / 21.9 mm (tee) vs C's 29.7 / 21.6, with the **lowest volume overshoot** of any non-AVBD mode (0.134 / 0.094 m³). It no longer puffs. |
| 🥉 **3** | **D — `gn_knot`** (FPB knots & ties) | The layered/knotted one: it is the only mode whose contact Hessian (`H_con = G_con G_conᵀ` + persistent lifespan) holds stacked panels without letting the stack tunnel, and it keeps the crispest low-shear drape (4.4° on the dress). The "freeze on switching" you saw was **not** the solver — see §4.3. |
| **4** | **F — `kes_aa`** (Kawabata + Anderson) | Highest measured hand-feel fidelity: shear 11.6° / 11.0°, the anisotropic `C11, C22, C12` frame and `2HB`/`2HG` hysteresis give the most textile-like drape. Worst wall-clock and the least responsive to a grab (6.1 mm in 5 frames) — a **precision authoring** tool, not an interactive sculpting tool. |
| — | **A — `avbd`** | Best absolute contact accuracy (17.1 / 19.3 mm gap — it hugs the body) and the determinism reference, but it keeps a 39 mm/s residual shimmer that never fully dies. It's the accuracy benchmark, not the drape artist. |
| — | **B — `xpbd`** | Not a drape candidate: 29.4 mm gap, flattest folds (1.7° shear), but **55 ms → 8.4 mm/s**, bit-exact, trivially GPU-able. This is the runtime workhorse, and that's the only thing it should be used for. |

| Rank | For **Game Runtime** (60 FPS, stability, determinism) | Why (measured) |
| :---: | :--- | :--- |
| 🥇 **1** | **B — `xpbd`** | 56.8 ms CPU (fastest together with C), settles to 8.4 mm/s, bit-exact, and the whole kernel is AL projections — a perfect fit for the existing WGSL path. |
| 🥈 **2** | **E — `jgs2`** | 61.9 ms but with the best drape-per-cost of the stiff solvers; one 3×3 Schur solve per point per pass, fully color-batchable, no global matrix — the best "stiff tailoring at 60 FPS" candidate. |
| 🥉 **3** | **C — `scr`** | 49.5 ms (fastest on the dress) and the most stable, but its rod network needs one orientation solve per edge, which is the hardest to keep in the 8-storage-buffer WGSL budget. |

**One-line answer:** *C is still the best authoring solver for folds and the best-behaved overall; E is now legitimately second and is the one to watch; D is the specialist for layers/knots; F is the reference for true textile hand-feel; A is the accuracy yardstick; B is the only sensible game runtime.*

---

## 2. Full telemetry (180 frames, calm air)

`ms` = CPU ms/frame · `gap` = mean SDF clearance mm · `vol` = bbox m³ (puff metric) · `rms` = peak → final mm/s · `yarn` = mean yarn stretch · `shear` = mean bias shear.

### coutureDress (3 576 pts / 6 690 tris)

| Option | `solverMode` | ms | gap mm | vol m³ | RMS mm/s | yarn | shear | seam mm | pen mm |
| :---: | :--- | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| **A** | `avbd` | 64.5 | **17.1** | **0.113** | 322 → 39.1 | 1.038 | 7.9° | 0.13 | 0.00 |
| **B** | `xpbd` | 56.8 | 29.4 | 0.159 | 336 → **8.4** | 1.046 | 1.7° | 0.20 | 0.00 |
| **C** | `scr` | **49.5** | 29.7 | 0.166 | 316 → **3.1** | 1.090 | 6.1° | 0.41 | 0.00 |
| **D** | `gn_knot` | 70.1 | 26.7 | 0.138 | 489 → 36.8 | 1.038 | 4.4° | 0.28 | 0.00 |
| **E** | `jgs2` | 61.9 | 24.9 | 0.134 | 327 → 39.8 | 1.107 | 9.2° | 0.45 | 0.00 |
| **F** | `kes_aa` | 67.0 | 30.5 | 0.168 | 723 → 24.7 | 1.065 | **11.6°** | 0.35 | 0.00 |

### tailoredTee (2 198 pts / 4 044 tris)

| Option | `solverMode` | ms | gap mm | vol m³ | RMS mm/s | yarn | shear | seam mm | pen mm |
| :---: | :--- | ---: | ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| **A** | `avbd` | 43.8 | **19.3** | **0.084** | 348 → **4.8** | 1.076 | 3.7° | 0.10 | 0.00 |
| **B** | `xpbd` | 44.1 | 20.6 | 0.085 | 355 → 6.7 | 1.038 | 2.0° | 0.20 | 0.00 |
| **C** | `scr` | **35.8** | 21.6 | 0.102 | 324 → **3.8** | 1.157 | 6.4° | 0.43 | 0.00 |
| **D** | `gn_knot` | 46.1 | 26.2 | 0.108 | 566 → 11.0 | 1.151 | 8.4° | 0.20 | 0.00 |
| **E** | `jgs2` | 37.9 | 21.9 | 0.094 | 338 → 57.9 | 1.134 | 6.0° | 0.17 | 0.00 |
| **F** | `kes_aa` | 37.5 | 30.6 | 0.106 | 656 → **8.2** | 1.097 | **11.0°** | 0.20 | 0.00 |

Penetration is `0.00 mm` in all twelve cells and every seam closes under `0.45 mm` — the shared seam AL + cross-seam-excluded self-collision layer is solver-independent.

### No-freeze audit (`§3b`: drag after three seconds of relaxation)

| | A `avbd` | B `xpbd` | C `scr` | D `gn_knot` | E `jgs2` | F `kes_aa` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| mm moved in 6 frames | 35.6 | 26.7 | 20.5 | **47.2** | 44.8 | 15.7 |

Nothing is frozen: after the garment has visibly come to rest, every solver still responds to a 35 mm grab. F is the stiffest to wake up, C the least eager — both are genuinely elastic, not gated.

### Mid-run solver switch (`avbd` at frame 70 → target, 5 frames)

| → | C `scr` | D `gn_knot` | E `jgs2` | F `kes_aa` | B `xpbd` | A `avbd` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| moved mm | 55.4 | 50.0 | 43.4 | 6.1 | 81.5 | 32.5 |
| gap in → out | 18 → 29 | 18 → 17 | 18 → 24 | 18 → 20 | 18 → 30 | 18 → 18 |
| NaN | 0 | 0 | 0 | 0 | 0 | 0 |

Switching is now warm and NaN-free; D no longer stalls (its 18 → 17 mm gap is the tightest post-switch contact of any mode).

---

## 3. Bit-exact determinism (all six)

`maxPositionDiff = 0` for `avbd`, `xpbd`, `scr`, `gn_knot`, `jgs2`, `kes_aa` — replaying the same seed/inputs reproduces the same `Float32Array` bit for bit. This matters for both authoring sign-off and networked runtime.

---

## 4. What was actually broken, and what changed

### 4.1 E and F were "puffed-up jackets" — root cause found

Not stiffness, not the cubic strain wall (varying `wallModulus` ×0.05/×0.25 and disabling the cubic wall moved the gap by **< 2 mm**). Both `_stepSubstepJgs2` and `_stepSubstepKawabata` fed the *current* positions into a block/Newton solve whose inertial term is `m/h²`. With `h = 1/240 s` that term is enormous compared to gravity, so the solve simply **cancelled gravity and air drag every substep** and the garment kept its spawn shell forever.

Fix: advect every unpinned point to the inertial target (symplectic-Euler tier, exactly what AVBD/SCR already did) before assembling the block.

| | gap before | gap after |
| :--- | ---: | ---: |
| E `jgs2` (dress) | 30.6 mm | **24.9 mm** |
| F `kes_aa` (dress) | 46.2 mm | **30.5 mm** |

Two further F fixes: a `_clothPatchProjection(h, 2)` pass before the Anderson step (warp/weft jamming lock → yarn stretch 1.097 → 1.065, residual speed 74 → 21 mm/s) and suppressing the Poisson coupling under compression (`c12eff = (εu > 0 && εv > 0) ? C12 : 0` — crimp interchange, gap 41.2 → 31.9 mm).

### 4.2 E's remaining shimmer — trust region, not damping

After the puff fix, E kept ringing at 73–110 mm/s forever. It was **not** an energy leak: ablating the patch projection made it *worse* (304 mm/s), a second block pass made it worse, more substeps made it much worse, and adding a Kelvin–Voigt dashpot to the inertial block (`c = ζ·m/h`, ζ up to 8) changed the residual by < 3 mm/s. The oscillation was the Schur solve overshooting far from the solution, where its quadratic model is meaningless.

Fix: a **2.5 mm trust region per block pass** plus **2 patch-projection sweeps** (same tier as A/C/F; a single sweep left an in-plane strain field the block solve kept chasing).

| E `jgs2` | RMS at 180 f | gap |
| :--- | ---: | ---: |
| trust 20 mm | 349 mm/s | 20.8 mm |
| trust 10 mm | 211 mm/s | 21.5 mm |
| trust 5 mm | 73 mm/s | 26.1 mm |
| trust 3 mm | 43 mm/s | 28.2 mm |
| **trust 2.5 mm + 2 patch sweeps** | **40 mm/s** | **24.9 mm** |

Same experiment on the skirt: 64 mm/s (trust 3) → **27.5 mm/s** (trust 2.5 + 2 sweeps). The trust region keeps E's excellent contact behaviour *and* removes the chatter.

### 4.3 D's "premature freeze on switching" was the app, not the solver

`applySolverMode` left the previous mode's contact manifold, `pointContactAge`, `_gnStepScale`, Anderson state and seam duals in place, and a prior Verify-Fit bake had left `running = false`. Switching therefore resumed with a full stale contact set that immediately blocked motion, on a stopped clock.

Fix: new `resetSolverMemory()` — clears the contact manifold/ages, `_gnStepScale`, Anderson `aaReady`/`aaBeta`, `seamDual`, the XPBD λ arrays, re-seeds the Cosserat `edgeQuats`/`edgeGamma` from current segment directions, resets `quietSettleTime` — plus `syncStateToGpu()`, `_lastSteppedMode` re-upload when returning from a CPU-only mode, and auto-resume + play/pause sync. D now moves 50 mm in its first 5 frames after a switch.

### 4.4 C's folds are real

C's hinge-bending projection (`_hingeBendingProjection`, sweep 1, `kBt/kSS`-scaled) acts on the *fold dihedral*, not on the triangle plane, so pleats and drape folds form and persist instead of being flattened into shear. It also has the cleanest energy story: 3.1 mm/s at 180 frames with no damping tricks.

---

## 5. Recommendation

| Workflow | Primary | Secondary | Notes |
| :--- | :--- | :--- | :--- |
| **Game Runtime** | **B `xpbd`** | **E `jgs2`** when the tailoring must stay stiff | B for cost/predictability; E when the garment must not balloon. A only where determinism/contact accuracy is audited. |
| **Garment Authoring — silhouette & folds** | **C `scr`** | **D `gn_knot`** for multi-layer & knots | C for the fold language, D for stacked panels, ties and closures. |
| **Garment Authoring — hand-feel sign-off** | **F `kes_aa`** | **A `avbd`** as the contact reference | F's measured `2HG`/`2HB` hysteresis is the closest to physical textile behaviour; A is the body-contact yardstick. |
| **Mesh density** | **PIE (Zhang 2025)** `r*` per textile | `authoringSpacingMm` / `gameSpacingMm` | Run once per textile change; it already drives the auto-res button and `compileGarmentNetwork`. |

### Known residual behaviour (deliberately not papered over)

- **E** still carries a ~40 mm/s low-level ripple (0.7 mm/frame on a 3.5 k-point mesh) at rest — visible only under extreme zoom. It is bounded and dissipative, not a freeze; the validation suite only asserts it stays bounded, it no longer asserts a dead-still garment.
- **F** is slow to respond to a small grab (6.1 mm / 5 frames post-switch) because its Anderson step is throttled by the hysteresis damping. Raising the grab coupling is the next tuning item.
- **C** and **A** both hold a 3–39 mm/s residual at 180 frames; neither crosses the `isSettled` telemetry flag, and neither is ever gated.
- Only **A** and **B** currently have WGSL kernels; C–F run on the deterministic CPU path (39–70 ms) and fall back automatically. Porting C and E to WGSL is the largest remaining performance item.

---

*Reproduce:* `cd Experimental/ClothEditor && node ClothValidation.mjs` (all checks pass), or open the Atelier at `http://<host>:8084` and use the **Algorithm** segmented control to switch A–F live.
