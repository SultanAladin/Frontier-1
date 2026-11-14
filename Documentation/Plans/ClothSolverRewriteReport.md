# Cloth Solver Rewrite — Report (from-scratch A–F implementation)

**What happened.** The previous branch's solver (`OrthotropicClothSolver.js`, ~5,200 lines) and its
WGSL GPU variant were **deleted, not patched**. `Documentation/Plans` items that described the old
implementation (comparison report, verification report, architecture plan) were removed with it,
and the old validation/bench harnesses were deleted and replaced. Written from scratch:

| File | Contents |
|---|---|
| `Experimental/ClothEditor/Engine/ClothSolver.js` | The six solvers A–F behind one `ClothSolver` class (~1,370 lines) |
| `Experimental/ClothEditor/Engine/KnotTemplates.js` | Guo et al. knot/tie templates (§4–5): Bézier pipes, parallel-transport frames, radius reduction, cloth mapping |
| `Experimental/ClothEditor/Engine/TextileCatalog.js` | Rewritten `SOLVER_CATALOG` + Zhang PIE estimator; textile data unchanged |
| `Experimental/ClothEditor/ClothValidation.mjs` | New headless harness (fixed physical thresholds, written before tuning) |
| `Experimental/ClothEditor/Engine/ClothStudioApp.js` | Rewired to the new solver; GPU backend removed (CPU only, honestly labelled in UI) |
| `Experimental/ClothEditor/RenderClothExhibits.mjs` | Import updated; renders the drape gallery used as visual evidence |

Kept untouched (per scope): garments, pattern network, mannequin/SDF, viewport, canvas, exports.

## How each paper is implemented — and where it departs

- **A · AVBD** (`avbd`) — true vertex block descent: every vertex solve recomputes forces from
  *current* neighbour positions (incident-face CSR), with a per-vertex 3×3 block (inertia +
  isotropic membrane + edge rank-1 terms + seam/contact terms) and AVBD seam augmentation
  (dual estimate + penalty ×4–8 growth capped at 64). Contact is hard SDF projection between
  sweeps. A globally-assembled-gradient version was tried first and was measurably unstable
  (finite-difference check −1.000 correlation exposed a gradient sign flip; after fixing that,
  Jacobi-style stale gradients still limit-cycled) — BCD is what the paper does and what works.
- **B · XPBD** (`xpbd`) — substepped constraint projection, physical compliance
  `α̃ = 1/(k h²)`, directional edge stiffness from warp/weft, hinge bending, zip-ramped seams.
- **C · Stable Cosserat Rods** (`scr`) — graph form: edges are rods with quaternions; split
  position/orientation iterations; diagonal position Hessian (their Eq. 29); closed-form
  quaternion update `q ∝ v⊗(b⊗e3) + λb` with `v = −2k^ss l⁻¹(x_{i+1}−x_i)` (Eq. 15),
  `λ = √(|v|²+|b|²)`, γ stored; rod-stretch coupling `(k/2l)|seg − l·d3|²` back into positions.
  Stated departures: `b` is a frame-misalignment torque (not their orientation-Hessian solve),
  plus hinge bending and a seam stitch pass (without them seams stay 3–7 mm open and garments
  slide off — measured).
- **D · Gauss–Newton knots & layers** (`gn_knot`) — quasistatic Newton: contact inside the
  system (`H_con = G_con G_conᵀ`, signed gradients), block-Jacobi PCG with an exact matvec,
  IPC-style 0.9ᵏ step filter on the barrier-inclusive objective. Knot pipes from
  `KnotTemplates.js` act as capsule colliders. Two-tier topological template–simulation
  parallelism from the paper is not implemented (single garment).
- **E · JGS2** (`jgs2`) — Schur-form local solves `(H_ii − H_iC H_CC⁻¹H_Ci) dx = …` from the
  1-ring, hybrid Jacobi→Gauss-Seidel rounds. Stated approximations: `H_CC` block-diagonal, no
  cubature precompute. Plus a seam stitch pass.
- **F · KES-AA** (`kes_aa`) — Kawabata-model shell: coupled stresses with `C12 > 0`, compressive
  buckling cap (×0.06), trellis shear locking, hysteresis as step damping; Anderson(m=2) mixing
  of fixed-point iterates accepted only if displacement is small **and** total energy
  (elastic + gravity + contact) does not rise. Plus a seam stitch pass.
- **Zhang PIE** — a mesh-resolution *estimator* (as in the paper): solves
  `32.08 r⁶ − 1.876e-4 r⁴ = B/E` per axis. r\*: cotton 12.6 mm, silk 9.0, denim 17.8, wool 16.0,
  jersey 17.6, satin 15.3 mm; warp/weft differ for anisotropic textiles.
- **Nature `s41567-022-01788-5`** is a *Nature Physics* Comment about fluid simulation. There is
  no cloth method in it to implement; nothing in the new code cites it.

## Measured results (headless, 200 frames @60 fps, 16 mm mesh, 3,815-point dress)

`node Experimental/ClothEditor/ClothValidation.mjs` → **122 pass / 9 fail**. Failures are mode
limitations, listed below, not crashes: every mode is finite, penetration ≤ 1.1 mm everywhere,
and all six pass the mode-switch test.

| Mode | Dress seam mm | Dress pen | Dress stretch mean | Skirt stays on? | Tunic stays on? | Cost (Node) |
|---|---|---|---|---|---|---|
| A AVBD | **0.04** | 0.05 | 1.020 | yes (105 mm settle) | yes | ~290 ms/f |
| B XPBD | 0.60 | 1.09 | **1.006** | yes | yes | **~57 ms/f** |
| C SCR | 0.40 | 0.02 | 1.019 | yes | yes | ~246 ms/f |
| D GN | 0.16 | **0.00** | 1.040 | yes | yes | ~164 ms/f |
| E JGS2 | 0.04 | 0.00 | 1.064 ✗ | **no (396 mm)** | yes | ~326 ms/f |
| F KES-AA | 0.02 | 0.00 | 1.009 | **no (238 mm)** | yes | ~142 ms/f |

(Penetration floor ≈1 mm is the mannequin SDF's trilinear voxel quantisation, 52×96×40 grid.)

## Your test questions, answered from the measurements

- **Does C give good folds?** It settles cleanly and drapes *smoothly* — on the bias silk tunic it
  shows the least bias-shear of the implicit modes (2.1° vs E 17.9°, F 13.2°; B 0.7° is the stiff
  reference). Folds are stable and coarse rather than rich; use C for knit/rod-like drape
  behaviour, not for crisp fold detail. Its real defect was seam openness, fixed with the stitch
  pass (dress seam 4.3 → 0.40 mm; retention −14 mm).
- **AVBD collisions/drape?** Best in the set: penetration 0.01–0.05 mm, seams 0.01–0.07 mm,
  retention everywhere. Drape quality is excellent; price is the highest iteration cost and a few
  single-vertex jitter spots (max-speed outliers to ~5 m/s on 1–2 vertices; median speed 1–46 mm/s).
- **XPBD for games?** Yes — the only mode that passes every gate on every garment, and the
  cheapest by 3–6×. This is the game-runtime pick. (All modes here are CPU JS; at 3.8k points B
  is ~57 ms/frame in Node — a game would run it at lower resolution/substeps, which the PIE game
  spacing supports.)
- **E/F on the puffed jacket?** There is **no puffed-jacket preset in this repo** (four garments:
  dress, gathered skirt, tee, silk tunic). The nearest stress test — the heavy gathered satin
  skirt — is exactly where E and F fail: both gradually slide it off the body (E −396 mm,
  F −238 mm top-line drop) while passing dress and tunic cleanly. Treat E/F as unsuitable for
  heavy layered garments as shipped; on fitted garments F's textile hand-feel model is the most
  faithful.
- **D freezes when switching?** Reproduced the mechanism during development: a step filter that
  rejects any penetration increase damps ~92% of every gravity-loaded step once cloth rests on
  the body. Fixed by filtering on the barrier-inclusive objective instead, and switching modes
  calls `resetSolverMemory()` (already wired in the App). Switch tests: all modes keep simulating
  after a mid-run switch; D moves 19.5 mm post-switch with seams continuing to close.

## Recommendations

- **Game runtime: B (XPBD).** Predictable cost, passes everything, PIE game spacing available.
  A is the hero-garment option if the budget allows (best seams/collisions) — as shipped it runs
  5 BCD sweeps, which is authoring-priced; fewer sweeps trade seam quality for speed.
- **Garment authoring: D first** (deterministic quasistatic fit, best penetration/retention
  balance, knot/tie pipes for ties and layered pieces), **A for seam-critical checks** (tightest
  seams), **F when the textile's Kawabata character matters** on dresses/tunics.
- **C** is a specialty mode (knit-like, fold-stable drape); **E** is not recommended yet — it is
  systematically the stretchiest (Schur coupling under-weights the membrane) and fails the heavy
  skirt.

## Known limitations (stated, not hidden)

- E's stretch and E/F heavy-skirt retention, above. A's single-vertex jitter outliers.
- No GPU compute path (the old WGSL solver was deleted with the old solver; UI says CPU).
- Knot *templates* are authored examples (the paper publishes none); the §4–5 machinery
  (frames, radius reduction, mapping) is real and unit-validated, knot authoring UI is not built.
- SCR is a graph-form adaptation, F's hysteresis is a damping realisation, D skips the paper's
  template–simulation parallelism — each is stated in `SOLVER_CATALOG` and in code comments.
