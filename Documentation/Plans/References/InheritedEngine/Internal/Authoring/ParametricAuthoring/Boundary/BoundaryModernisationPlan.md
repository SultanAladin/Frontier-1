# Boundary B-rep — Modernisation Plan (2020–2026 literature)

🧩 The plan that takes `Boundary/` from a Weiler-1986 transcription with tolerance-based arithmetic to a
kernel usable for **2D and 3D boolean operations**, driven by 2020–2026 research rather than the 1990s
paper the original suggestion came from.

Scope selected: **B** — repair + exact predicates + both booleans, entirely in-repo, no new dependency.

---

## 1. What the research actually changed

The radial-edge *combinatorics* are **not** the obsolete part. Lévy 2024 computes exactly this structure
(he names it "the Weiler model") and still radial-sorts facets around every non-manifold edge. What is
1990s here is the **arithmetic**: a fixed `WeldTolerance = 1e-4`, a disagreeing `DistanceEpsilon = 1e-6`,
and a boundary-inclusive `PointInTriangle` over raw doubles are precisely the floating-point fragilities
the whole 2020–2026 line was written to eliminate.

### 1.1 Source table

| # | Work | Year | What it changes here | Weight |
|---|---|---|---|---|
| 1 | Attene, *Indirect Predicates for Geometric Constructions*, CAD 126:102856 · arXiv:2105.09772 · `github.com/MarcoAttene/Indirect_Predicates` | 2020 | Intersection points held as **unevaluated expressions** (LPI / TPI) of input points; the predicate sign resolves exactly through FP filters. No coordinates ⇒ no tolerance | 🔴 |
| 2 | Cherchi, Livesu, Scateni, Attene, *Fast and Robust Mesh Arrangements using Floating-Point Arithmetic*, TOG 39(6):250 | 2020 | Turns any triangle soup into a well-formed simplicial complex **without exact coordinates** for intersection points | 🔴 |
| 3 | Cherchi, Pellacini, Attene, Livesu, *Interactive and Robust Mesh Booleans*, TOG 41(6):248 · arXiv:2205.14151 · `github.com/gcherchi/InteractiveAndRobustMeshBooleans` | 2022 | First robust-guarantee boolean at **interactive rates** (≤200K tri). Inside/outside by the sign of a tetrahedron volume — an orientation predicate, not a ray parity count | 🔴 |
| 4 | Trettner, Nehring-Wirxel, Kobbelt, *EMBER: Exact Mesh Booleans via Efficient & Robust Local Arrangements*, TOG 41(4) | 2022 | Plane-based representation, **homogeneous integer** coordinates; generalized winding numbers; adaptive box subdivision instead of a global acceleration structure | 🚩 |
| 5 | Lévy, *Exact predicates, exact constructions and combinatorics for mesh CSG*, arXiv:2405.12949 | 2024/25 | **Directly validates radial-edge.** co-refine → build the Weiler model by radial sort → classify by the boolean expression. Retriangulates with **CDT + symbolic perturbation** *because CDT is uniquely defined*, so coplanar duplicates remesh identically and dedupe for free | 🔴 |
| 6 | Guo & Fu, *Exact and Efficient Intersection Resolution for Mesh Arrangements*, TOG 43(6) | 2024 | ~1 order of magnitude faster than #2 / #3 at the same guarantees | 🚩 |
| 7 | Diazzi, Panozzo, Vaxman, Attene, *Constrained Delaunay Tetrahedrization*, TOG 42(6) · arXiv:2309.09805 | 2023 | States the 2D baseline: non-intersecting, non-degenerate segments **always** admit a valid CDT (3D does not) — so the planar-face path is the safe one to build first | ✔️ |
| 8 | `elalish/manifold` (now the OpenSCAD kernel) | live | Production counter-example to a fixed tolerance: **adaptive, scale-tracked epsilon**; the boolean is *expected* to emit degenerates the triangulator must survive | 🔴 |
| 9 | `elalish/manifold` discussion #799 | live | Why exact **constructions** beat exact predicates alone: CDT with robust predicates only guarantees output when there are no duplicate vertices and no crossing constraints | 🚩 |

### 1.2 Verdict against the current six files

| Current code | Modern verdict | Source |
|---|---|---|
| `WeldTolerance = 1e-4` fixed, mm-absolute | Tolerance must be **scale-adaptive and tracked**, never a constant | #8 |
| `DistanceEpsilon = 1e-6` < the weld radius | Two disagreeing epsilons is the classic inconsistency; one gate instead | #1 #8 |
| `PointInTriangle` boundary-inclusive, `Cross2` on raw doubles | The sign of a 2×2 determinant in raw FP is the exact failure mode indirect predicates exist for | #1 |
| hand-rolled `ClipEars` + `BridgeHole` doubled seam | **CDT / a proven triangulator**, not ear-clipping with hole bridging | #5 #7 |
| No boolean; `Operations/Boolean` is Clipper2 on 2D sketch polygons only | 3D boolean = co-refine → arrangement → radial sort → classify. The radial ring is already the right substrate | #5 #3 |
| Radial-edge structure itself | 🟢 **Keep.** Lévy's exact-CSG output *is* the Weiler model | #5 |
| `ResolveRayFaceHit` as an in/out test | Replace with **orientation-sign / winding**, not ray parity | #3 #4 |

💡 The single most important finding: **the structure is right and the arithmetic is wrong.** This is a
bounded repair, not a rewrite.

---

## 2. Defect list carried into Phase 0

| # | Sev | File | Defect |
|---|---|---|---|
| 1 | 🔴 | `BoundarySweep.cpp` | `EnforceSweepDistance` re-derives the base ring circularly — symmetric drag creeps (NewD−OldD)/2 per frame |
| 2 | 🔴 | `BoundarySweep.cpp` | A negative `Distance` inverts the solid; the extrude modal produces negative depths on a downward drag |
| 3 | 🔴 | `BoundarySweep.cpp` | `Start/EndRingVertices` are outer-ring only, so holes tear off on re-drag |
| 4 | 🔴 | `BoundarySweep.cpp` | `DistanceEpsilon` 1e-6 < `WeldTolerance` 1e-4 — a 1e-5 sweep welds the end ring into the start ring |
| 5 | 🚩 | `BoundarySweep.cpp` | `ResolveRingMiterNormals` wraps `% Count`, so an open profile gets garbage endpoint miters when draft ≠ 0 |
| 6 | 🚩 | `BoundarySweep.h` | Self-contradiction: line 113 promises full rollback, line 48 says `ValidationFault` leaves the body for inspection |
| 7 | 🚩 | `BoundaryTopology.cpp` | `<cstdarg>` missing though `va_list` / `va_start` / `va_end` are used |
| 8 | 🚩 | `BoundaryTessellation.cpp` | `BridgeHole` doubles seam vertices and `PointInTriangle` is boundary-inclusive, so a holed cap mis-triangulates |
| 9 | ✔️ | `BoundarySweep.cpp` | Dead first `Base` loop — `Along - (Along - StartOffset)` IS `StartOffset` |
| 10 | ✔️ | `BoundarySweep.cpp` | `ResolvePlaneFrame`'s Right / Up computed then `(void)`-discarded at three sites |
| 11 | 🚩 | `BoundaryTopology.cpp` | `AttachFace` ignores `EnforceFacePlane`'s return, so a zero-normal face survives |
| 12 | 🚩 | `BoundarySweep.cpp` | `EnforceSweepDistance` never checks `Outcome.Category`, returns true on total failure, and silently requires an identical re-passed `Specification` |
| 13 | 🚩 | `BoundarySweep.cpp` | `Outcome.Audit` audits the whole body, so an older fault misreports this sweep |
| 14 | ✔️ | `BoundarySweep.cpp` | `OpenProfileCapped` never assigned to `Category`, only to `Notice` |
| 15 | ✔️ | `BoundarySweep.cpp` | A closed profile with both caps off builds an open tube inside an `Outer` envelope — should be `Sheet` |
| 16 | ✔️ | `BoundarySweep.cpp` | An author-to-user note shipped in source after the namespace close |
| 17 | ✔️ | `BoundaryTessellation.cpp` | Inconsistent triangle winding across faces (latent — the sketch passes cull `NONE`) |
| 18 | ✔️ | both | Naming violations: `TRAVERSAL` section headers (banned word), `auto Position2` |

---

## 3. The phases

### Phase 0 — de-contradict
Fix defects 1–18 with no new file and no new dependency. Key structural change: replace the two loose
vertex runs in `SweepOutcome` with a per-ring record that stores the profile-plane **base positions**, so
a re-drag never re-derives them from the current geometry.

```cpp
struct SweepRingRecord
{
    std::vector<BoundaryVector> BasePositions;   // [mm] - profile-plane source, never re-derived
    std::vector<VertexToken>    StartRing;
    std::vector<VertexToken>    EndRing;
    bool                        HoleRing = false;
};
```

Sign normalisation at build time, and one tolerance gate keyed off the weld radius:

```cpp
BoundaryVector Axis     = NormalizeBoundaryVector(Specification.Direction, AxisResolved);
double         Distance = Specification.Distance;
if (Distance < 0.0) { Axis = ScaleBoundaryVector(Axis, -1.0); Distance = -Distance; }
if (Distance <= Body.WeldTolerance * 2.0) { /* ZeroDistance */ }
```

### Phase 1 — exact arithmetic substrate  ·  `BoundaryPredicate.{h,cpp}` (new)
Shewchuk-style **filtered adaptive** predicates returning an exact sign, plus a scale-adaptive tolerance
derived from body bounds (per #8) that replaces the constant `WeldTolerance`:

| Verb | Returns | Use |
|---|---|---|
| `EvaluateOrientation2` | −1 / 0 / +1 | planar turn direction, ear tests, 2D containment |
| `EvaluateOrientation3` | −1 / 0 / +1 | tetrahedron volume sign — the in/out classifier of #3 |
| `EvaluateInCircle` | −1 / 0 / +1 | the CDT flip test (#5, #7) |
| `ResolveAdaptiveTolerance` | double | tolerance from body extent, not a constant (#8) |

Self-contained; no external dependency and therefore no licence question. The seam is written so
Attene's `Indirect_Predicates` (#1) can be swapped in behind the same four verbs later.

### Phase 2 — triangulation replacement  ·  `BoundaryTessellation.cpp`
Drop `ClipEars` / `BridgeHole`; route through the already-vendored `mapbox::earcut` (the working
`ParametricSketchLoft.cpp` precedent, already on `AuthoringParametric.lib`'s include path). Winding is
normalised per face against the face normal so defect 17 closes at the same time. The call seam is
written so a CDT (#5 / #7) drops in behind it without touching callers.

### Phase 3 — 2D boolean  ·  `BoundaryRegionBoolean.{h,cpp}` (new)
Project a planar face's loops to its dominant plane, run **Clipper2** (already vendored, already linked,
and already exact by integer scaling), lift the result back as outer + inner loops through
`AttachFace` / `AttachInnerLoop`. The low-risk half of "usable for booleans 2d/3d"; needs no new maths.

Verbs: `IntegrateFaceRegion`, `SubtractFaceRegion`, `IntersectFaceRegion`, `ResolveFaceRegionOutcome`.

### Phase 4 — 3D boolean  ·  `BoundaryArrangement.{h,cpp}` + `BoundaryBodyBoolean.{h,cpp}` (new)
Following Lévy (#5) exactly:

```
co-refine faces (exact predicates, implicit intersection points)
      ↓
retriangulate each cut face (CDT, symbolic perturbation)
      ↓
radial-sort coedges around every shared edge      ← RadialFirst / NextRadial already exist
      ↓
classify facets by the boolean expression (orientation sign, per #3)
      ↓
re-attach surviving faces into Outer / Void envelopes
```

Verbs: `ResolveBrepArrangement`, `IntegrateBrepBody`, `SubtractBrepBody`, `IntersectBrepBody`.

### Phase 5 — wiring + documentation
- `SketchModelExtrudeModal` drives a `SweepOutcome` instead of the inert `ExtrudeDepth` scalar.
- A `BoundaryRenderStream` → `ParametricSketchSolidSequence` uploader (the two vertex formats differ).
- `EngineDocs/FolderStructure.md:287–293` corrected: it documents a `Boundary/` that does not exist and
  plans a `Boolean/` that Phases 3–4 supersede.

---

## 4. Scope comparison (the decision this plan records)

**Direction: more 🔴 = more capability for the boolean columns; more 🔴 = worse for Effort / Risk.**

| Scope | Phases | 2D bool | 3D bool | Exactness | New dependency | Effort | Risk | Rank |
|---|---|---|---|---|---|---|---|---|
| A · Repair only | 0 | ✖ | ✖ | ✔️ tolerance | none | ✔️ | ✔️ | 🥉 |
| **B · Repair + exact + both booleans, in-repo** | 0–4 | 🔴 Clipper2 | 🚩 own arrangement | 🚩 filtered predicates | none | 🔴 | 🚩 | 🥇 🏆 |
| C · B + vendor Attene `Indirect_Predicates` | 0–4 | 🔴 | 🔴 guaranteed | 🔴 provably exact | **LGPL** header library | 🔴 | ✔️ | 🥈 |

⭐ **Selected: B.** Delivers the complete class usable for 2D *and* 3D booleans with zero new licence
exposure, and Phase 1's predicate seam makes C a later swap rather than a rewrite.

🔴 Why C is not the default: `Indirect_Predicates` is **LGPL** (the repository sidebar says 2.1, its own
README text says 3-or-later — unresolved upstream) and it mandates compiler flags (`/fp:strict /Oi` on
MSVC, `-frounding-math` on GCC) that would have to propagate through all of `AuthoringParametric.lib`.

---

## 5. Build reality (verified, not assumed)

- `Internal\Authoring\ParametricAuthoring\Build.bat` sweeps with `for /R "%EXTDIR%" %%F in (*.cpp)`, so
  **every new `Boundary\*.cpp` compiles into `AuthoringParametric.lib` with no edit to that file.**
- `%CLIPPER%` and `%EARCUT%` are already include roots there, and the three Clipper2 vendor sources are
  already in `VENDOR_SRCS`. Phases 2 and 3 therefore add no build configuration.
- A `BREPROOT` include root is **not** needed: a quoted `#include` searches the includer's own directory
  first, so the intra-folder includes resolve unconfigured. The proposal in `ParametricNote.md` to add it
  to `SketchModelViewport\Build.bat` is dead configuration.
