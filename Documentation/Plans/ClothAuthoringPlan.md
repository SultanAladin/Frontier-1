# Frontier Cloth Authoring — Plan (Pattern → Sew → Drape → Bake)

> **TL;DR:** Build a **Marvelous-Designer-class authoring workspace**, not runtime game cloth:
> **① Pattern sheet** (pieces, internal lines, darts, grainline, seam allowance, notches) → **② uniform quad-grid sampling per piece at 6–10 mm** → **③ seam chains resampled to stitch pitch and paired** → **④ XPBD small-steps drape (600 Hz, 1 iteration/substep) with strain limiting, SDF body collision and PT/EE self-collision** → **⑤ bake + 1:1 DXF-AAMA / SVG export + fabric-consumption nesting**.
> The garment is **authored flat in 2D and sewn in 3D**. It is **never** a 3D surface draped over a mannequin — that single decision is the difference between Marvelous and what failed last time.
> Canonical solver: **XPBD small-steps** (not mass-spring, not global Newton-PCG, not plain PBD). AVBD is reserved as an optional stiff-fabric bake.

Companion: [PhysicsEnginePlan.md](PhysicsEnginePlan.md) §6.4 · [PhysicsFolderStructure.md](PhysicsFolderStructure.md) §2 `ClothSpace/`.

---

## 1. Target

| Requirement | Acceptance |
| :--- | :--- |
| Real garment construction | a 5-piece shirt with sleeves, placket, collar and cuffs, sewn from flat pattern pieces |
| Real fabric behaviour | silk, cotton poplin, denim and leather drape **visibly differently** from measurable parameters |
| Authoring speed | 60 fps viewport at 20k simulated vertices, ≥ 45 fps at 40k |
| Correctness | zero body penetration, seam residual < 0.5 mm, stretch ≤ 105 % at the 95th percentile |
| Real-world handoff | 1:1 DXF/SVG pattern, seam allowance, notches, grainline, size grading, fabric consumption |
| Determinism | replay of a 3,600-tick authoring session reproduces the same position hash |

**Non-goals for this cycle:** animation-driven runtime cloth, hair, offline film cloth (hours/shot), AI garment recovery, retail fit certification (Browzwear-class measurement validation).

---

## 2. Why the previous attempt failed (and the fix for each)

The earlier branch's cloth did not look like fabric. The failure modes below are the ones that matter for a **look**, listed with the concrete countermeasure that this plan bakes in.

| # | Failure mode | Why it looks wrong | Fix in this plan |
| :---: | :--- | :--- | :--- |
| ① | Iteration-count-dependent stiffness (plain PBD / mass-spring) | the same cloth is soft in one frame and stiff in the next; it "breathes" | **XPBD**: compliance `α̃ = α/Δt²` makes stiffness timestep- and iteration-independent |
| ② | Too few substeps with too many iterations | long, sagging, gummy steps; slow convergence of the seam and contact errors | **10 substeps × 1 iteration** at 600 Hz (the "small steps" result), never 1 step × 40 iterations |
| ③ | Stretch-only edge constraints | the sheet stretches like rubber instead of folding | **shear + bend (dihedral) constraints and a hard strain limit** — cloth must be nearly inextensible and easy to bend |
| ④ | No strain limiting | 20–40 % stretch under gravity, visibly non-fabric | `StrainLimitProjector`: per-substep clamp to ≤ 105 % (knit) / 102 % (woven) |
| ⑤ | Collision against body triangles at vertex level | popping, tunnelling, penetration under the arms | **narrow-band signed distance field** (3–4 mm voxels, ±40 mm band) + thickness offset + friction |
| ⑥ | No self-collision | sleeves weld into the torso, layers interpenetrate | **PT/EE self-contact** with spatial hash, thickness 2 mm, deterministic bucket order |
| ⑦ | Unit muddle (cm vs m, mass ×1000) | compliance off by orders of magnitude → jelly or concrete | everything in **metres, kilograms, seconds**; masses derived from **g/m²**, stiffness from **EA measured on a strip** |
| ⑧ | No bending resistance | a shirt hem behaves like paper; sleeves collapse | **flexural rigidity from the Shirley bending length**, applied per dihedral constraint |
| ⑨ | Unsmoothed normals / single-sided shading | faceted, plasticky, unreadable folds | **double-sided sheen shading**, smoothed normals from the simulation, stitch lines and seam shadows |
| ⑩ | Draping a pre-made 3D surface | no seams, no pieces, no possibility of cut-and-sew | **2D pattern → grid → sewn assembly** (§4) |

🔴 The most important of these is ⑩. Without a pattern there is no cloth authoring product, only a blanket.

---

## 3. Algorithm decision

| Candidate | Stiffness accuracy | Stability | 20k-vertex speed | Self-collision friendliness | Implementation risk | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| Mass-spring + PBD | 🚩 iteration-dependent | ✔️ | 🔴 | ✔️ | ✔️ | ❌ superseded everywhere (Vellum, Chaos, Blender all left it) |
| **XPBD small-steps** | 🔴 compliance-exact | 🔴 unconditional at 600 Hz | 🔴 | 🔴 | ✔️ | 🏆 **authoring core** |
| Implicit Newton / PCG FEM | 🔴 highest | 🚩 needs line search | 🚩 2,000 dispatches/frame | 🚩 expensive Jacobians | 🚩 | ❌ offline bake reference only |
| Projective dynamics | 🟠 | 🔴 | 🔴 | 🚩 | 🚩 | ❌ stiff-only, weak friction/contact |
| VBD / **AVBD** (SIGGRAPH 2024/2025) | 🔴 | 🔴 hard constraints | 🔴 | 🟠 | 🚩 very new | 🥈 **optional final bake** for denim/leather |
| Jolt soft body | 🟠 edge/dihedral/volume | 🔴 | 🟠 ≤ 3k verts | 🚩 no EE | ✔️ | 🥉 runtime cheap cloth only |

**Chosen:** XPBD small-steps for authoring; Jolt soft body or a proxy lattice for the runtime game tier; AVBD only if the XPBD bake cannot hold denim stiffness (> 800 N/m at 0.6 % strain) convincingly.

---

## 4. Authoring pipeline (the five stages)

### 4.1 Stage ① — Pattern sheet (2D)

```cpp
//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PatternPiece
{
    uint32_t               PieceIndex      = 0;        // [idx]  - Stable identity inside the garment
    std::string            PieceName;                  // [-]    "Front", "Back", "Sleeve L"
    std::span<const Vec2>  BoundaryLoop;               // [m]    - Outer closed loop, CCW
    std::span<const Loop>  HoleLoops;                  // [m]    - Armholes, neckline cuts inside the piece
    std::span<const Vec2>  InternalLines;              // [m]    - Darts, pleat folds, pocket placement
    float                  GrainAngle      = 0.0f;     // [rad]  - Warp direction in pattern space
    float                  SeamAllowance   = 0.010f;   // [m]    - 10 mm default, per-edge overridable
    uint16_t               FabricIndex     = 0;        // [idx]  - Into the FabricRepository
    uint8_t                LayerDepth      = 0;        // [idx]  - 0 = inner, 1 = outer
};

struct SeamChainLink
{
    uint32_t FirstPieceIndex = 0;   // [idx] - Piece owning the first boundary chain
    uint32_t FirstChainIndex = 0;   // [idx] - Boundary chain inside that piece
    uint32_t SecondPieceIndex = 0;  // [idx]
    uint32_t SecondChainIndex = 0;  // [idx]
    float    StitchPitch      = 0.003f; // [m] - 3 mm resample pitch
    float    FoldDepth        = 0.0f;   // [m] - Non-zero for pleats/gathers
};
```

Editor operations: draw polygon (line/arc/spline), add hole, add internal line, assign seam to boundary segments, set grainline, set seam allowance, mirror piece, duplicate, rename, fabric assignment, measurement overlay (piece edge length vs. body landmark).

### 4.2 Stage ② — Uniform quad-grid sampling (the crucial trick)

Each piece is sampled with a **regular grid in pattern space**, clipped to the boundary, with boundary vertices snapped to the loop:

- target edge length `ℓ = 6–10 mm` (garment detail dependent; 6 mm for collars/cuffs, 10 mm for skirts),
- boundary rows/columns inserted so that the **outer ring of grid points lies exactly on the boundary loop**, spaced at ≤ `ℓ`,
- interior triangles from the grid cells (2 triangles per cell, alternating diagonal to avoid directional bias),
- every vertex carries `(patternU, patternV, pieceIndex, isBoundary, grainReliefAngle)`.

Why a grid and not a Delaunay triangulation: XPBD convergence, bending stencil quality, and seam matching all depend on **uniform edge lengths and a regular connectivity**. Delaunay gives irregular valence, uneven stiffness response and expensive dihedral bookkeeping. CLO and Marvelous both sample panels on regular grids.

Rest-state data produced here: rest lengths, rest dihedral angles, rest areas, per-edge grain angle, vertex masses (`m = gsm × cellArea / verticesPerCell`).

### 4.3 Stage ③ — Sewing

1. `SeamChainLinker` walks both boundary chains, resamples them to `StitchPitch` by arc length, and pairs vertex `i` with vertex `i` of the partner chain (shortest-chain termination allowed for eased seams).
2. Each pair becomes a **seam constraint**: distance rest length `0` plus the thickness offset, compliance from the fabric's shear stiffness (a seam is stiff but not rigid).
3. Optionally a **tack** (fully rigid) for plackets, button stands, and waistbands.
4. Gathers/pleats: one chain is resampled with a *shorter* effective length, so pairing distributes the excess into folds — exactly how a gathered skirt is built.

Sewing is run as a **dedicated solve phase**: 40 iterations with collision disabled, then the seam constraints persist for the whole simulation so a garment can never split open.

### 4.4 Stage ④ — Drape

| Parameter | Value | Rationale |
| :--- | :--- | :--- |
| Substep Δt | **1/600 s** (10 per 1/60 tick) | small-steps regime; stable with 1 iteration |
| Iterations / substep | **1** (stretch, shear, seam) · **1** (bend) | iteration count must not affect stiffness |
| Substeps per present | 10 (authoring), 4 (playback/preview) | document property, never adaptive |
| Constraint order | coloured batches, fixed order | deterministic; no atomics on GPU |
| Damping | velocity damping 0.05–0.15 /substep + XPBD damping on bend | folds must settle, not jitter |
| Gravity | `-9.80665 m/s²` (`StandardGravity`) | physical |
| Air drag | optional `½ρC_dA v²` per vertex, clamped | visible flutter without instability |
| Strain limit | woven 102 %, knit 105 % | kills the rubber look at zero cost |
| Solve budget | 10 ms/frame @ 20k verts (authoring) | interactive |

**Constraint set per substep**, all in the same coloured-batch pass:

1. **Distance (stretch)** — per grid edge, compliance `α = ℓ / EA` where `EA` comes from the strip test.
2. **Shear** — per cell diagonal, softer compliance (`≈ 8–20 ×` stretch compliance); this is what makes the fabric resist skewing.
3. **Bend** — per interior edge, a dihedral-angle constraint with compliance derived from flexural rigidity `B = w·c³/12` (`w` = areal weight N/m², `c` = Shirley bending length).
4. **Long-range attachments (LRA)** — each vertex tethered to a coarse anchor set with a strain-limited rest length; this is what keeps a long seam from stretching without raising local stiffness.
5. **Seam** and **tack** constraints (§4.3).
6. **Pin / attachment** constraints (shoulders, avatar pins, hanger points) — zero compliance, with release thresholds for "unpin and fall".
7. **Collision** projections (body SDF, self-contact) after the constraint pass, before the velocity update.

**XPBD projection** (per constraint, `w_i` = inverse mass, `α̃ = α/Δt²`):

```
Δλ  = (−C − α̃·λ) / (Σ w_i + α̃)
Δx_i =  w_i · Δλ · ∇C_i
λ   += Δλ
```

### 4.5 Stage ⑤ — Bake and export

| Output | Format | Notes |
| :--- | :--- | :--- |
| Garment document | `.ClothSpace` | pattern + seams + fabrics + avatar binding + bake cache pointer |
| Bake cache | 16-bit quantised positions @ 30 Hz | 20k verts × 3 × 2 B = 120 KiB/tick → delta-coded |
| Pattern for real sewing | **DXF-AAMA / ASTM D6673** + **1:1 SVG** (mm units, no scaling) | seam allowance, notches, grainline, piece labels, size |
| Garment geometry | OBJ / glTF | for DCC handoff |
| Animation | Alembic (`.abc`) | drape cache for the engine |
| Tech pack | Markdown + CSV | piece list, fabric consumption, measurement table |
| Fabric consumption | nesting onto 1.40/1.50 m fabric width | `FabricConsumptionNester`, 3 mm gap, grain-aligned |

---

## 5. Fabric model — from a real fabric to solver parameters

The point of an authoring tool is that the parameters are **measurable on a real fabric**, not invented sliders.

| Fabric property | Measurement | Solver parameter |
| :--- | :--- | :--- |
| Areal density `gsm` [g/m²] | cut 100 × 100 mm, weigh | vertex mass `m = gsm·A/3` |
| Extensibility `ε@50N` | 50 × 200 mm strip, 50 N load | `EA = 50/ε` → per-edge compliance `α = ℓ/EA` |
| Shear rigidity | bias strip, 5° shear | shear compliance (separate warp/weft/bias factors) |
| Bending length `c` [mm] | Shirley stiffness tester | flexural rigidity `B = w·c³/12` → dihedral compliance |
| Drape coefficient | Cusick drape meter | **validation target**, not a solver input |
| Thickness [mm] | caliper | collision offset + seam thickness |
| Friction | inclined-plane | tangential contact projection factor |

Anisotropy (`FabricAnisotropyRegister`): compliance is scaled per edge by the angle between the edge and the **grainline** — warp and weft are stiff, bias (45°) is soft. Without this, a garment drapes isotropically and reads as plastic.

Presets shipped in `EngineContent/Fabrics/`: cotton poplin 110 g/m², silk habotai 60, wool flannel 260, denim 380, leather 700, jersey knit 180 (with a 105 % strain limit), organza 40 (stiff bend, near-zero stretch).

---

## 6. Collision

| Contact | Mechanism | Cost control |
| :--- | :--- | :--- |
| Cloth ↔ avatar | narrow-band SDF, 3–4 mm voxels, ±40 mm band, trilinear + gradient; thickness offset 2 mm; Coulomb friction | SDF built once per avatar pose; ~10–14 MB |
| Cloth ↔ rigid props | SDF for hero props, else BVH sphere-cast per vertex at 30 Hz for far vertices | vertex LOD by distance |
| Cloth ↔ cloth (self) | PT and EE contacts via `SpatialGridHash` (cell = 2 × max edge length + thickness); thickness 2 mm; layer offset for multi-layer garments | swept AABB culling, contact cap 200k, 30 Hz refresh, 60 Hz resolve |
| Cloth ↔ cloth (seams) | excluded through seam adjacency masks | baked bitmask |
| Cloth ↔ cloth (layers) | inter-layer `LayerDepth` bias so an outer shell never resolves inside an inner one | per-pair bias |

Friction: after positional projection, tangential displacement is limited to `μ · |Δnormal|` (Coulomb-style), which is what stops a sleeve from sliding off a shoulder.

---

## 7. Editor workspace (same UI language as Frontier Editor / UV Workspace)

**Layout:** `Outliner | 2D Pattern Sheet + 3D Drape (split, resizable) | Inspector | Content Browser`, dockable, identical chrome and theme tokens to the UV Workspace.

### 7.1 Panes

| Pane | Contents |
| :--- | :--- |
| `ClothOutlinerPanel` | Garment → Pieces → Seams → Fabrics, visibility/lock, per-piece vertex and mass readout |
| Pattern sheet (2D) | 1:1 scale, ruler, grid, piece fill by fabric, seam edges highlighted, grainline arrows, notch marks, measurement tool |
| 3D viewport | mannequin + drape, wireframe/heat overlays, penetration markers, seam preview |
| `ClothInspectorPanel` | **Pattern** (piece geometry, seam allowance, grainline) · **Seam** (pairing, pitch, fold depth) · **Fabric** (preset, gsm, stretch, bend, friction) · **Fit** (ease map, tape measure, pressure/contact heat) · **Collision** (SDF resolution, thickness, self-contact toggles) · **Simulate** (substeps, damping, wind, gravity, reset) · **Bake** (range, rate, export) |
| `FabricPresetBrowser` | swatch + measurable data + drape coefficient preview |

### 7.2 Tools (modal, Blender-style G/R/S inside the sheet)

Select · Move/rotate/scale piece · Add/remove point · Cut piece · Add hole · Draw internal line · Dart · Pleat/gather · Seam paint (drag across two boundary chains) · Notch · Grainline · Seam allowance · Measure · Pin/attachment · Fabric paint (regional) · Smooth/relax · Simulate/pause/reset · Pin-and-drag on the 3D drape.

### 7.3 2D ↔ 3D sync rules

- Selecting a piece in 2D isolates it in 3D (ghosted context), and vice versa.
- Edge length selected in 2D highlights the corresponding stretched edge in 3D with its current strain.
- Any pattern edit marks the drape **stale** and offers "re-sample and re-sew"; the bake cache is invalidated with a visible banner (never silently kept).
- The 3D drape can be **pinned and pulled** for art direction; those pins are document data, not runtime state.

### 7.4 Avatar

Mannequin from `Experimental/Shared/Mannequin/` (10,764 vertices / 21,524 triangles, 5 body presets, preserved from the previous attempt). Added here: landmark set (bust, waist, hip, shoulder, inseam), A-pose/T-pose switch, **size grading** (S–XXL via per-landmark grade increments applied to pattern pieces, not to the avatar), and body morph sliders that re-derive the SDF.

---

## 8. Validation gates

| Metric | Target | Measured by |
| :--- | :--- | :--- |
| Stretch ratio, 95th percentile | ≤ 105 % (knit) · ≤ 102 % (woven) | `ClothDrapeValidation` |
| Area error per cell | < 2 % | same |
| Body penetration | **0** vertices inside SDF − thickness | SDF query sweep |
| Self-intersection pairs | < 0.02 % of PT/EE candidates unresolved | `SelfContactSolver` telemetry |
| Seam residual | < 0.5 mm after 300 ticks | seam constraint error |
| Dihedral smoothness | mean |Δθ| < 5° between neighbours | fold quality proxy |
| Settle time (shirt on mannequin) | < 2 s at 60 fps | first tick where kinetic energy < 1 % of peak |
| Energy behaviour | monotone non-increasing without external force | energy ledger |
| Drape coefficient vs. Cusick | within 10 % | offline comparison in the exhibit |
| Determinism | identical hash over 3,600 ticks | `SimulationHashRegister` |
| Authoring frame rate | 60 fps @ 20k verts, 45 fps @ 40k | profiler |

Every gate emits a `.png` proof into `Exhibits/Images/Cloth/` (silhouette overlay, strain heat map, contact map, seam residual plot, drape-coefficient chart).

---

## 9. Phases

| Phase | Deliverable | Exit criteria |
| :--- | :--- | :--- |
| **C0 · Pattern sheet skeleton** | `Experimental/ClothStudio/` — 2D sheet, pieces, mannequin viewer, fabric assignment (no simulation) | a 5-piece shirt pattern can be drawn and saved as JSON; 2D/3D sync works |
| **C1 · Grid + single-piece drape** | grid sampler, XPBD substep solver, strain limit, body SDF collision | one 10k-vertex panel drapes over a shoulder without penetration; 60 fps |
| **C2 · Sewing + multi-piece garment** | seam chains, sewing phase, LRA, bend | shirt closes from flat pieces onto the mannequin; seam residual < 0.5 mm |
| **C3 · Self-collision + layers** | PT/EE hash, layer bias, friction | two-layer garment (shirt under jacket) with zero interpenetration |
| **C4 · Bake, export, fabrics** | bake cache, DXF/SVG 1:1, nesting, grading, fabric presets | exported DXF sews on a real machine at 1:1; consumption within 5 % of hand layout |
| **N1–N3 · Native port** | `ClothSpace` + `.slang` + `Workspaces/Cloth/` in C++20 | parity with the studio within visual tolerance; `.png` proofs |

**Demo D1 (the immediate next step):** a live `Experimental/ClothStudio/` page — mannequin, two pattern pieces, sew, drape, with substep/fabric sliders, a strain heat overlay and a determinism hash readout. WebGPU compute primary (colour-batched constraints, ping-pong position buffers), WebGL2 fallback via vertex-texture ping-pong. Target: 8k vertices × 8 substeps at 60 fps on a discrete GPU, 3k on integrated.

---

## 10. Risks

| Risk | Mitigation |
| :--- | :--- |
| Drape still looks wrong after C2 | C0/C1 demo is explicitly the *look* checkpoint: if the single panel is not convincing, do not proceed to seams |
| Self-collision swallows the frame budget | 30 Hz refresh, swept culling, contact cap, layer-aware pruning |
| Sewing pulls pieces through the body | run the sewing phase **before** collision is enabled, then re-enable with a penetration recovery pass |
| Stiff fabrics (denim/leather) look rubbery | raise substeps to 16, tighten the strain limit to 101 %, then AVBD bake if still insufficient |
| Real-fabric parameters untested | ship an intake form + a Cusick/Shirley measurement guide in `EngineDocs/`; validate against the fabric presets |
| Scope creep into a CAD pattern tool | the pattern sheet covers drawing, seams, allowance, grading and export — **not** industrial CAD drafting |
