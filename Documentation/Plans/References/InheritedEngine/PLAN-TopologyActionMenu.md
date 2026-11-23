# PLAN — Topology Action Menu (EntityContextMenu → C++)

Port of `Documentation\Prototypes\EntityContextMenu.html` into a reusable, **selection-driven** action
menu component under `Interface/Components/Menus/`, driven by a declarative operation catalogue with
predicate-gated availability (stratum + count + connectivity).

Target executable: `Binaries\Validation\PolygonActionValidation.exe` — buttons that author a fake
selection, **no viewport**.

**Status: built and verified (2026-07-29).** This document records the plan as executed, including the
two places the delivered shape diverged from the plan (§9). Section 6's file layout is the final one.

**Backend: Win32 + Direct3D 11**, linking only `EngineContext.lib` + vendored ImGui. No Graphics
pillar, no Vulkan, no RootSystem. That constraint is the whole reason glyphs are `ImDrawList` vector
paths — see §2.

---

## 1. Naming (per SKILL-Naming §0 — mechanism → verb+substrate)

`Entity` is banned (§1, 2026-07-25). Mechanism: *"filters a catalogue of topology operations against
the current selection's stratum, count and connectivity, and lists the survivors."*

| Prototype thing | Frontier name | Why |
|---|---|---|
| entity context menu | **`TopologyActionMenu`** | `ContextMenu` is the approved noun for a popover; scoped to topology ops |
| one menu row | **`TopologyOperationDescriptor`** | approved `…Descriptor` suffix (§5) |
| the master list | **`OperationCatalogue`** | domain register, not a `Registry` of UI |
| grouping caption | **`OperationBandCaption`** | reuses the existing `Bands/` idiom |
| availability test | **`SelectionPredicate`** → `EvaluateSelectionPredicate` | Math register (§0), verb `Evaluate` (§6) |
| why a row is greyed | **`AvailabilityCondition`** | §8 — no `is`/`has` |
| submenu | **`OperationBranch`** | topology register |
| selection kind | **`TopologyStratum`** | vertex/edge/face are *strata* of a complex — not `Kind`/`Type` |
| the validation app | **`PolygonActionValidation`** | `harness` → `…Validation` (§2) |
| polygon-specific subfolder | **`PolygonMutation/`** | `TopologyMutation` is sanctioned vocabulary (§4). Rejected: `…Tools` (= UI controls, §10), `…Modelling`/`…Authoring` (activity gerunds, not mechanisms) |

---

## 2. Constraint discovered — icons

`SvgIconRegistry` is **Vulkan-only** (`vulkan.h`, `VkImage`, `ImGui_ImplVulkan_AddTexture`), but every
validation host is D3D11 + `EngineContext.lib` only. The prototype's 35 SVG glyphs cannot go through it.

| Option | Works on D3D11 | Fidelity | Cost |
|---|---|---|---|
| **`ImDrawList` vector glyphs** ⭐ | ✔️ | ⭐⭐⭐ | one unit, hand-authored paths |
| Text/unicode placeholder glyphs | ✔️ | ⭐⭐ | loses the prototype's look |
| Reach through `SvgIconRegistry` | ❌ | — | would drag Vulkan into a D3D11 host |

**Chosen:** vector glyphs drawn from a `GlyphStrokeSet` of unit-square paths, tinted `currentColor`
style. Backend-agnostic, and satisfies §9 (one unit serves every glyph).

---

## 3. The core mechanism — three-state availability

🔴 **The design centre.** Research flagged Blender's *Grid Fill* as the cautionary tale: it reports the
*wrong* precondition — it says "select two edge loops" when the real fault is odd boundary parity. So
each row resolves to one of three states, rendered differently:

| `AvailabilityCondition` | Cause | Rendering |
|---|---|---|
| `Available` | all predicates pass | normal row |
| `StratumMismatch` | wrong selection stratum | 🔴 **omitted entirely** |
| `CountShortfall` | right stratum, wrong count | greyed + reason chip *"needs 2 loops"* |
| `TopologyShortfall` | right stratum + count, wrong connectivity | greyed + reason chip *"requires a quad ring"* |

The hide-vs-grey split is the point: a wrong-stratum row is *noise* and vanishes, while a
right-stratum-wrong-shape row is *actionable* and stays visible with the **true** reason.

`SelectionProfile` carries what the predicates read:

```cpp
TopologyStratum ActiveStratum;          // V/E/F/L/R/B/O
int             SelectedCount;
int             DistinctRegionCount;    // → Bridge Span needs exactly 2
int             BoundaryEdgeCount;      // → Fill Boundary; parity for Grid Fill
int             ShellCount;             // → Separate Shell needs ≥2
int             MaximumFaceSideCount;   // → Make Faces Planar needs ≥4 (tris are always planar)
bool            QuadRingCondition;      // → Insert Edge Loop
bool            ClosedLoopCondition;    // → Circularize: circle vs arc
bool            BoundaryCondition;
bool            ManifoldCondition;
bool            SharedComponentCondition;
bool            TwoFaceEdgeCondition;   // → Spin Edge Diagonal
bool            QuadPairCondition;
bool            TrianglePairCondition;  // → Quadrangulate
bool            OpenSurfaceCondition;   // → Solidify
bool            SymmetryAxisCondition;
```

Border is a **first-class stratum** (per 3ds Max), not a flavour of edge — several gates key off it.

---

## 4. Operation catalogue

Distilled from research across Blender · Wings3D · Modo · Maya · 3ds Max · Silo · Hexagon · Houdini ·
MeshLab. **94 operations across 11 bands.** Strata: `V`=Vertex `E`=Edge `F`=Face `L`=EdgeLoop
`R`=EdgeRing `B`=Border `O`=Object.

| Band | Ops | Gate exemplars |
|---|---|---|
| Transform | 9 | `FlattenToPlane` ≥3 · `CircularizeLoop` closed→circle, open→arc |
| Extrude / Build | 16 | 🔴 `BridgeSpan` **exactly 2 regions** · 🔴 `GridFillBoundary` **even parity** |
| Cut / Split | 12 | 🔴 `InsertEdgeLoop` **walkable quad ring** · `ConnectVertexPath` ≥2 same face |
| Merge / Weld | 7 | 🔴 `TargetWeld` **exactly 2, no 3+-face edge** |
| Smooth / Refine | 7 | `DecimateByQuadricCollapse` · `RemeshIsotropic` |
| Normals / Shading | 6 | `MarkSharpEdge` · normal recompute/flip |
| Attributes / Creasing | 5 | `AssignEdgeCrease` · `MarkUvSeam` · `AssignSurfaceMaterial` |
| Topology repair | 9 | 🔴 `MakeFacesPlanar` **≥4-gon** · 🔴 `SpinEdgeDiagonal` **edge between exactly 2 faces** |
| Duplicate / Symmetry | 8 | `SeparateShell` ≥2 shells · `SymmetrizeAcrossAxis` |
| Removal | 11 | `DeleteWithDependents` (leaves a hole) vs `DissolveIntoNeighbours` (merges survivors) |
| Selection conversion | 4 | menu reloads in place — Wings3D's trick |

The full-catalogue choice was deliberate over a ~40-op Tier-1+2 subset: **only the full set exercises
every gate class.** The parity and ring-walkability gates — the ones the whole three-state model exists
for — live on `GridFillBoundary` and `InsertEdgeLoop`, both of which a "common ops" subset would cut.

Glyphs are **shared across related ops** (one bevel glyph serves vertex/edge/face bevel), exactly as
the prototype already does: 74 glyph identities cover 94 operations.

---

## 5. Layering — why the polygon payload is a subfolder

Dependency direction is strictly **`PolygonMutation/` → `Menus/`**. The menu component knows nothing
about extrude or bevel; it consumes `ResolvedOperationEntry` arrays. A future `CurveMutation/` or
`SolidShellMutation/` catalogue reuses the same chrome — that is §9 (one unit serves many).

The glyph unit splits along the same seam: the **drawing mechanism** is generic and lives in `Menus/`;
the **74 polygon path definitions** are domain content and live in `PolygonMutation/`.

---

## 6. Files

```
Internal/EngineContext/Interface/Components/Menus/
├── TopologyActionMenu.{h,cpp}         generic chrome — bands, rows, branches, placement, reason chips
├── GlyphInscription.{h,cpp}           generic ImDrawList glyph unit (draws handed path data)
└── PolygonMutation/
    ├── SelectionPredicate.{h,cpp}     TopologyStratum · SelectionProfile · gate evaluation
    ├── OperationCatalogue.{h,cpp}     the 94-op declarative table
    ├── PolygonGlyphIdentity.h         the PolygonGlyph enum ALONE — no ImGui (see §9)
    └── PolygonGlyphTable.{h,cpp}      the 74 extrude/bevel/weld path definitions

Executables/Validation/PolygonActionValidation/
├── Build.bat                          cloned from ControlsGallery
├── PolygonActionValidationHost.cpp    Win32 + D3D11 + ImGui
└── PolygonActionPanel.{h,cpp}         stratum buttons + count spinners + live menu
```

### Existing files modified: exactly one

| File | Change | Why |
|---|---|---|
| `EngineDocs/FolderStructure.md` | additive | CLAUDE.md mandates it when adding a folder/subsystem |

Both build-registration risks were checked and resolved to **no edit**:

- `Internal/EngineContext/Build.bat:71` — `for /R "%EXTDIR%" %%F in (*.cpp)` recurses the whole pillar
  ("recursive, no manual list"). New `Components/Menus/**/*.cpp` is picked up automatically.
- `Automation/BuildPlan.ps1` — an incremental staleness planner invoked *by* each target's own
  `Build.bat`, not a target registry. Nothing to register.

🟢 Zero risk to existing code: no shared header signature changes, no touched components, no renderer
involvement.

---

## 7. Validation app layout — no viewport, buttons only

```
┌─ Frontier — Polygon Action Menu ───────────────────────────────┐
│ STRATUM   [Vertex][Edge][Face][Loop][Ring][Border][Object]     │
│ COUNTS    Selected ◄ 5 ►   Regions ◄ 2 ►   Boundary ◄ 8 ►      │
│ CONNECT   ☑ Quad ring walkable  ☑ Closed loop  ☑ Manifold      │
│           ☑ Shared components   Max face sides ◄ 4 ►           │
│ PRESETS   [border, 7 edges (odd)]  → Grid Fill greyed, parity  │
│ FILTER    ☑ Reveal gated rows                                  │
│                                                                │
│ AVAILABILITY   catalogue 94   available 58   gated 0  hidden 36 │
│ LAST ACTIVATION  Bridge Span                                   │
└────────────────────────────────────────────────────────────────┘
```

Flipping a toggle re-filters live — **that is the actual thing under test.**

**Eight presets**, each driving one gate class to a *known* outcome and labelled with the expectation,
so a reviewer confirms the interesting behaviour without hand-dialling ten fields:

| Preset | Expected |
|---|---|
| `2 face regions` / `1 face region` | Bridge Span live / → needs 2 faces |
| `border, 8 edges (even)` / `border, 7 edges (odd)` | Grid Fill live / → requires an even boundary |
| `edge ring, quad walk` / `edge ring, tri blocked` | Insert Edge Loop live / → requires a quad ring |
| `single vertex` | face/normal rows **absent, not greyed** |
| `object, 2 shells` | Separate Shell + Partition live |

The panel also shows a live invariant — every catalogue row lands in exactly one bucket
(`available + gated + hidden == 94`), displayed rather than asserted so a reviewer watches it hold
while clicking.

---

## 8. Sequencing (as executed)

① `SelectionPredicate` (pure logic, no UI) → ② `OperationCatalogue` → ③ glyph unit →
④ `TopologyActionMenu` → ⑤ validation host + panel → ⑥ `Build.bat`, compile, run →
⑦ `FolderStructure.md`.

---

## 9. Divergences from the plan, and why

| Planned | Delivered | Reason |
|---|---|---|
| `OperationGlyphInscription.{h,cpp}` | `GlyphInscription.{h,cpp}` + `PolygonGlyphTable.{h,cpp}` | The unit name carried `Operation`, but the generic half knows nothing about operations — it draws handed paths. Dropping the prefix made the §5 seam honest. |
| glyph enum inside `PolygonGlyphTable.h` | extracted to **`PolygonGlyphIdentity.h`** | `OperationCatalogue.cpp` *names* glyphs 96 times but never draws one. The combined header pulled ImGui in transitively, which would have made the pure-logic gate layer unlinkable without a renderer. Verified by a probe: the catalogue now links with **no ImGui at all**. |
| "~95 operations" | **94** | The natural result of the 11 approved bands; nothing was silently dropped. |

Two compile-forced choices worth recording, both the same root cause: `ImCos`/`ImSin`/`ImFormatString`
live in **`imgui_internal.h`**, not the public header. Replaced with `<cmath>`/`<cstdio>` rather than
coupling a shared component to ImGui's private surface for five calls.

Rows are drawn with `ImGui::InvisibleButton` + `ImDrawList`, not `Selectable`: a `Selectable` cannot
express glyph + two-tone label + gate-dependent reason chip without fighting its own padding.

Dismissal is **reported, not acted on** — a component that closed itself could not be reused by a
caller that keeps it pinned.

---

## 10. Verification

| Probe | Result |
|---|---|
| Gate assertions | 9/9 pass |
| Glyph coverage | 74/74 resolve; 0 empty, 0 malformed, all within the unit square |
| Capacity guards | overflow / degenerate / null runs all refused |
| Availability tally | consistent across all 7 strata (27 rows admit `Object`; tally reports 27, cross-checked by grep) |
| Build | clean, **zero warnings** |
| Deliverable | `PolygonActionValidation.exe` links, launches, creates its D3D11 device, stays alive |

Per-stratum availability (available / gated / hidden):

| Stratum | V | E | F | L | R | B | O |
|---|---|---|---|---|---|---|---|
| available | 36 | 46 | 58 | 50 | 48 | 50 | 27 |
| hidden | 58 | 48 | 36 | 44 | 46 | 44 | 67 |
