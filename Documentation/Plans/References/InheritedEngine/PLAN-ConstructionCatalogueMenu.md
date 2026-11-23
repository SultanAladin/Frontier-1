# PLAN — Construction Catalogue Menu (SpawnContextMenu → C++)

Port of `Documentation\Prototypes\SpawnContextMenu.html` into a reusable, **document-state-driven**
construction menu under `Interface/Components/Menus/`, driven by a declarative catalogue with
predicate-gated availability (workplane + topological dimension + profile closure).

Sibling to `PLAN-TopologyActionMenu.md`. That plan answers *"what can I do to this selection?"*; this
one answers *"what can I bring into existence here?"* — the two consume the **same** `Menus/` chrome
seam but different payload subfolders.

Target executable: `Binaries\Validation\ConstructionCatalogueValidation.exe` — buttons that author a
fake document state, **no viewport**.

**Backend: Win32 + Direct3D 11**, linking only `EngineContext.lib` + vendored ImGui. Glyphs are
`ImDrawList` unit-square paths through the existing `GlyphInscription` unit — the constraint recorded
in `PLAN-TopologyActionMenu.md` §2 already forced that, and it is reused here unchanged.

---

## 1. Research base — 17 surveyed applications

Two exhaustive sweeps: creation vocabulary and modification vocabulary. Every tool's *literal* menu
structure was read, then deduplicated.

| Register | Applications surveyed |
|---|---|
| Parametric B-rep | FreeCAD (Part · PartDesign · Sketcher · Draft · Surface · BIM · Curves), Dune3D |
| Constraint solver | SolveSpace, Dune3D |
| CSG / script | OpenSCAD, BRL-CAD, CadQuery, build123d |
| Kernel | OpenCASCADE (OCCT) `BRepPrimAPI` · `BRepAlgoAPI` · `BRepOffsetAPI` · `GC_`/`gce_` |
| 2D drafting | LibreCAD, QCAD |
| Subdivision | Blender, Wings3D |

**Access note for future sessions:** `wiki.freecad.org` sits behind Anubis proof-of-work and refuses
fetchers; `brlcad.org/wiki` is offline; `docs.blender.org` returns 403. Working mirrors —
`raw.githubusercontent.com/FreeCAD/FreeCAD-documentation/main/wiki/<Page>.md` (official raw markdown)
and `reqrefusion.github.io/FreeCAD-Documentation-html/wiki/<Page>.html`. BRL-CAD:
`brlcad.org/docs/cad/manuals/mged/mged_cmd_index.html`.

---

## 2. Naming (per SKILL-Naming §0 — mechanism → verb+substrate)

Mechanism: *"filters a catalogue of geometry-construction operations against the document's active
workplane, the topological dimension of what is selected, and whether that profile closes — then lists
the survivors."*

`Create`/`Spawn`/`Add`/`Tools`/`Primitive`-as-a-noun are all rejected: activity gerunds and category
words, not mechanisms (§0 gate 5). `Entity`, `Element`, `Kind`, `Item` are blacklisted outright (§1).

| Prototype thing | Frontier name | Why |
|---|---|---|
| spawn context menu | **`ConstructionCatalogueMenu`** | `ContextMenu` is the approved popover noun; scoped to construction |
| the left rail | **`CatalogueRail`** | the prototype's own structural word; a rail of bands |
| one rail row | **`ConstructionBandCaption`** | reuses the `Bands/` idiom of the sibling plan |
| the right tile grid | **`ConstructionTileField`** | `…Field` is a sanctioned container suffix (§5) |
| one grid tile | **`ConstructionDescriptor`** | approved `…Descriptor` suffix (§5) |
| the master list | **`ConstructionCatalogue`** | domain register, not a `Registry` of UI |
| availability test | **`ConstructionPredicate`** → `EvaluateConstructionPredicate` | Math register, verb `Evaluate` (§6) |
| why a tile is greyed | **`ConstructionCondition`** | §8 — no `is`/`has` |
| what the document offers | **`DocumentGeometryProfile`** | mirrors `SelectionProfile`; flat POD of readable facts |
| topological dimension | **`GeometryDimension`** | OCCT's own register (§3) — not `Type`/`Kind` |
| immediate-spawn row | **`DirectConstruction`** | the prototype's `shot:true` |
| parameter dialog affordance | **`ParameterDisclosure`** | Wings3D's split-button (§6) |
| the validation app | **`ConstructionCatalogueValidation`** | `harness` → `…Validation` (§2) |
| the payload subfolder | **`GeometryConstruction/`** | parallel to `PolygonMutation/`; `Construct` is an approved verb (§6) |

---

## 3. 🔴 The core mechanism — OCCT's dimension-raising law as the gating engine

**The design centre, and the single best idea in the research.** Every surveyed application hand-codes
per-command gates and gets them wrong. OCCT states one law that generates almost all of them:

```
Vertex ──→ Edge ──→ Face ──→ Solid
                │
Edge   ──→ Face │   Wire ──→ Shell      Shell ──→ CompSolid
```

> `BRepPrimAPI_MakePrism` / `MakeRevol` raise topological dimension by **exactly one**.

Three consequences that every downstream tool documents as a *limitation* fall out as **theorems**:

| Observed "limitation" | Actually a consequence of the law |
|---|---|
| "Extruding an open wire gives a shell, not a solid" | Wire → Shell. There is no path to Solid. |
| FreeCAD's `Solid` checkbox is meaningless on an open profile | Capping is an *extra* step, not part of the sweep |
| A face input needs no `Solid` flag | Face → Solid directly |
| `MakeRevol` rejects solids outright | Solid has no successor in the chain |

🔴 **Therefore availability is computed, not tabulated.** `GeometryDimension` + a closure bit derives
the result type and the gate for every feature-from-profile operation — one rule, uniformly correct.

### Four-state availability

Extends the sibling plan's three states with `WorkplaneAbsent`, because the research found the
workplane gate to be the *most common* create-time failure and the one tools handle worst:

| `ConstructionCondition` | Cause | Rendering |
|---|---|---|
| `Available` | all predicates pass | normal tile |
| `DimensionMismatch` | selection is the wrong topological dimension | 🔴 **omitted entirely** |
| `WorkplaneAbsent` | operation needs an active workplane; none is set | greyed + **offer-the-fix chip** *"set a workplane"* |
| `ProfileShortfall` | right dimension, wrong count or wrong closure | greyed + reason chip *"needs 2 profiles"* · *"profile must close"* |
| `ClosureShortfall` | profile exists and closes, but the result would be a shell not a solid | tile **stays live**, badge reads *"→ shell"* |

`ClosureShortfall` is not a failure — it is a **truthful result-type prediction**, the direct payoff of
§3's law. No surveyed tool does this; FreeCAD ships a checkbox that silently does nothing instead.

### `DocumentGeometryProfile` — what the predicates read

```cpp
GeometryDimension ActiveDimension;        // Nothing/Vertex/Edge/Wire/Face/Shell/Solid
int   SelectedCount;                      // → Loft needs >= 2, Fill needs 2-4
int   ProfileCount;                       // Distinct unconsumed profiles
int   BoundaryEdgeCount;                  // → Fill boundary curves: exactly 2, 3 or 4
int   ExistingCircleCount;                // → tangency construction (LibreCAD needs 3)
int   SolidCount;                         // → Boolean needs >= 2; Cut exactly 2
bool  WorkplaneActivation;                // 🔴 the most-failed gate in every tool
bool  ClosedProfileCondition;             // → Solid vs Shell prediction
bool  PlanarProfileCondition;             // → MakeRevolution: curve coplanar with the axis
bool  AxisAvailability;                   // → Revolve: axis is MANDATORY
bool  PathAvailability;                   // → Sweep: profile + path
bool  UniformClosureCondition;            // 🔴 Sweep: profiles must be ALL open or ALL closed
bool  PendingGeometryCondition;           // CadQuery's un-extruded-wires stateful gate
bool  SupportMaterialCondition;           // → "To last"/"To first" error without material
bool  TangentEndpointCondition;           // → SolveSpace tangent needs a shared endpoint
```

---

## 4. Construction catalogue — 11 bands

Consolidated from the survey. Ordered as the rail presents them: the bands reached for constantly at
the top, data-sourced and annotation last.

| # | Band | Ops | Gate exemplars |
|---|---|---|---|
| 1 | Solid Primitive | 18 | ungated (Blender's law: creation has no preconditions) |
| 2 | Sketch Geometry | 22 | 🔴 all require `WorkplaneActivation` |
| 3 | Profile Sweep | 12 | 🔴 `Revolve` **axis mandatory** · 🔴 `Sweep` **uniform closure** · `Loft` ≥2 |
| 4 | Curve Construction | 14 | `TangentArc` needs a shared endpoint · `Interpolate` ≥2 points |
| 5 | Surface Construction | 11 | 🔴 `FillBoundaryCurves` **exactly 2, 3 or 4 edges** · `BlendCurve` **exactly 2** |
| 6 | Boolean Construction | 9 | 🔴 `Cut` **exactly 2, order significant** · `Union` ≥2 equal dimension |
| 7 | Pattern Construction | 10 | `PathArray` needs a path · `Mirror` needs a plane |
| 8 | Reference Geometry | 9 | ungated — and it *creates* the workplane band 2 demands |
| 9 | Data-Sourced | 8 | `Heightfield` needs an image · `Cluster` needs an import |
| 10 | Illumination & Framing | 7 | ungated |
| 11 | Annotation | 8 | dimension ops need ≥1 measurable component |

**128 operations across 11 bands.** Two structural decisions, both taken from the research:

🔴 **`ConstructionMode` collapses the additive/subtractive split.** FreeCAD spends **16 menu entries on
8 primitives** by mirroring every one into Additive and Subtractive. build123d spends 8 primitives + one
orthogonal `Mode` parameter. We take build123d's axis: `Add` / `Subtract` / `Intersect` / `Reference`,
carried as a menu-level modifier (hold Alt = Subtract), not a parallel band. **Saves ~40 tiles.**

🔴 **The catalogue is deliberately wider than the type system** — BRL-CAD's alias tier. `Cuboid`,
`Box-from-3-points`, `Wedge` and `Prism` are four distinct tiles that all construct one internal
hexahedron; `Cylinder`, `EllipticalCylinder`, `TruncatedCone` all construct one general cone. ~40 mged
create commands cover ~25 stored types, because **the menu should offer the construction method the
caller is thinking in**, not the storage format.

Glyphs are shared across related ops, as the sibling catalogue already does: ~92 glyph identities cover
128 operations.

---

## 5. Interaction model — rail + tile field, from the prototype

The prototype's two-pane shape is kept because it solves a problem the sibling menu does not have:
**128 operations will not fit in a vertical list.** The sibling's flat card tops out around 60 rows.

```
┌─ CatalogueRail ────────┬─ ConstructionTileField ─────────────────┐
│  ⊕ Create              │  Solid Primitive          18 items      │
│  ▸ Solid Primitive  18 │  ┌────┐ ┌────┐ ┌────┐ ┌────┐          │
│  ▸ Sketch Geometry  22 │  │Cube│ │Sphr│ │Cyln│ │Cone│          │
│  ▸ Profile Sweep    12 │  └────┘ └────┘ └────┘ └────┘          │
│  ▸ Curve            14 │  ┌────┐ ┌────┐ ┌────┐ ┌────┐          │
│  ▸ Surface          11 │  │Torus│ │Wedg│ │Prsm│ │Plne│         │
│  ▸ Boolean           9 │  └────┘ └────┘ └────┘ └────┘          │
│  ▸ Pattern          10 │                                         │
│  ▸ Reference          9 │  ── greyed, workplane absent ──         │
│  ──────────────────    │  ┌────┐ ┌────┐                          │
│  ⚡ Light          L   │  │Line│ │Arc │  set a workplane          │
│  ⚡ Camera         C   │  └────┘ └────┘                          │
│  ⚡ Empty          E   │                                         │
└────────────────────────┴─────────────────────────────────────────┘
```

- **Hover a rail row** swaps the tile field; **click a `DirectConstruction` row** (⚡) fires immediately.
- Rail row count badges are **live** — they show *available* count, so an empty band is visible before
  you enter it.
- 🔴 **Never an empty tile field.** Blender filed the blank-submenu case as a *bug* (T55214) and fixed
  it to a disabled explanatory label. A band with zero available ops shows *why*.

### Patterns taken from the research, ranked

| # | Pattern | Source | Why it wins |
|---|---|---|---|
| 1 | **`ParameterDisclosure` split tile** — click the tile = defaults now; click the corner box = parameter dialog first | Wings3D 🥇 | Dominates both Blender's post-hoc panel and FreeCAD's always-modal dialog; the *caller* chooses per-invocation, costing nothing either way |
| 2 | **`ConstructionMode` modifier, not a mirrored band** | build123d 🥈 | 8 + 1 axis instead of 16 entries (§4) |
| 3 | **Dimension-raising law as the gate engine** | OCCT 🥉 | One rule replaces 128 hand-coded gates (§3) |
| 4 | **Hide-not-grey, plus preview before commit** | Dune3D | Inapplicable ops absent; applicable ones previewed |
| 5 | **Offer-the-fix chip** | Dune3D's coincident-constraint repair tool | A greyed tile carries the *remedy*, not just the complaint |
| 6 | **Argument-level filtering** — offer only *valid* candidate profiles | FreeCAD Sweep/Loft | Makes an error class unrepresentable |
| 7 | **Accelerator letters + flat search over the hierarchy** | Blender `Shift+A`/`F3`, Dune3D spacebar | 3-deep menu becomes as fast as a hotkey |
| 8 | **Snapshot the workplane reference at creation** | Dune3D | Kills a whole class of parametric surprise |
| 9 | **Alias tier — menu wider than the type system** | BRL-CAD | Offer the construction method, not the storage type |
| 10 | **Overflow band for the long tail** | Wings3D `MORE →` | Keeps the rail at ~11 rows |

### 🔴 Known misleading gates — the failures this design exists to avoid

The research surfaced eleven documented cases where a tool reports the *wrong* precondition. The four
that shape this plan:

| Failure | Tool | What we do instead |
|---|---|---|
| **Thickness** demands a removed face; selecting the solid says *"Wrong selection"* — reads as a selection-type error when the real constraint is architectural (OCCT shelling needs an opening). Clearing the face list is **silently ignored**. FreeCAD #30460. | FreeCAD | `ProfileShortfall` names the *architectural* reason: *"needs an opening"* |
| **Fillet silent no-op** — an over-large radius produces nothing, no message | FreeCAD | build123d's numeric gate stated up front: *radius < half local width* |
| **Draft `Pull Direction`** accepts a selection and does nothing unless Neutral Plane is set; docs concede *"results can be unpredictable"* | FreeCAD | A dependent input is never presented as independent — progressive disclosure |
| **Hole ignores sketch circle radii** — varied circles silently yield identical holes | FreeCAD | If an input is ignored, it is not offered |

Also recorded, not designed around: `MakeFace`'s multi-wire rule is documented then admitted
**unchecked**; BRL-CAD `area` **ignores holes**; `bev` silently evaluates left-to-right with no
parentheses; `Through All` is a magic 10 m constant; OpenSCAD `minkowski` may treat a compound child as
two inputs.

---

## 6. Layering — why the geometry payload is a subfolder

Dependency direction is strictly **`GeometryConstruction/` → `Menus/`**, identical to the sibling plan
§5. The menu chrome knows nothing about cubes or lofts; it consumes `ResolvedConstructionEntry` arrays.

```
                    Menus/  (generic chrome — knows nothing of geometry)
                      ▲                          ▲
        ┌─────────────┘                          └─────────────┐
   PolygonMutation/                                  GeometryConstruction/
   (94 ops — sibling plan)                           (128 ops — this plan)
```

Both payloads reuse `GlyphInscription` unchanged, and both split the same way: the **drawing mechanism**
is generic and lives in `Menus/`; the **path definitions** are domain content and live in the payload
folder. That is §9 (one unit serves many) holding across two independent catalogues — the real proof the
sibling plan's seam was cut in the right place.

`ConstructionCatalogueMenu` is a **new chrome component**, not a variant of `TopologyActionMenu`: a flat
row card and a rail+grid card share no layout code. What they share is `GlyphInscription`, the theme, and
the resolved-entry pattern.

---

## 7. Files

```
Internal/EngineContext/Interface/Components/Menus/
├── ConstructionCatalogueMenu.{h,cpp}       NEW — rail + tile field chrome, placement, fix chips
└── GeometryConstruction/                   NEW
    ├── ConstructionPredicate.{h,cpp}       GeometryDimension · DocumentGeometryProfile · gate evaluation
    ├── ConstructionCatalogue.{h,cpp}       the 128-op declarative table across 11 bands
    ├── ConstructionGlyphIdentity.h         the ConstructionGlyph enum ALONE — no ImGui (see below)
    └── ConstructionGlyphTable.{h,cpp}      the ~92 primitive/sweep/boolean path definitions

Executables/Validation/ConstructionCatalogueValidation/
├── Build.bat                               cloned from PolygonActionValidation
├── ConstructionCatalogueValidationHost.cpp Win32 + D3D11 + ImGui
└── ConstructionCataloguePanel.{h,cpp}      dimension buttons + workplane toggle + live menu

Reused unchanged: Menus/GlyphInscription.{h,cpp} · Theme/ThemeConfiguration.h
```

🔴 **`ConstructionGlyphIdentity.h` is split out from the start.** The sibling plan discovered this the
hard way (its §9): the catalogue *names* glyphs ~130 times but never draws one, and a combined header
pulls ImGui in transitively — which makes the pure-logic gate layer unlinkable without a renderer. Not
rediscovering that.

### Existing files modified: exactly one

| File | Change | Why |
|---|---|---|
| `EngineDocs/FolderStructure.md` | additive | CLAUDE.md mandates it when adding a folder/subsystem |

Both build-registration risks resolve to **no edit**, same as the sibling plan:

- `Internal/EngineContext/Build.bat:71` — `for /R "%EXTDIR%" %%F in (*.cpp)` recurses the whole pillar.
  New `Components/Menus/GeometryConstruction/*.cpp` is picked up automatically.
- `Automation/BuildPlan.ps1` — an incremental staleness planner invoked *by* each target's own
  `Build.bat`, not a target registry. Nothing to register.

🟢 Zero risk to existing code: no shared header signature changes, no touched components, no renderer
involvement. `PolygonMutation/` is not read or modified.

---

## 8. Validation app layout — no viewport, buttons only

```
┌─ Frontier — Construction Catalogue ────────────────────────────────┐
│ DIMENSION  [None][Vertex][Edge][Wire][Face][Shell][Solid]          │
│ WORKPLANE  ☐ Active                                                │
│ COUNTS     Selected ◄ 2 ►  Profiles ◄ 2 ►  Boundary ◄ 4 ►         │
│            Solids ◄ 2 ►    Circles ◄ 3 ►                           │
│ CLOSURE    ☑ Profile closes   ☑ Uniform closure   ☑ Planar          │
│ INPUTS     ☑ Axis   ☑ Path   ☐ Support material   ☑ Pending         │
│ MODE       (•) Add  ( ) Subtract  ( ) Intersect  ( ) Reference      │
│ PRESETS    [open wire + axis]  → Revolve live, badge "→ shell"      │
│ FILTER     ☑ Reveal gated tiles                                     │
│                                                                     │
│ AVAILABILITY  catalogue 128  available 71  gated 12  hidden 45      │
│ RESULT TYPE   Revolve → Shell   (Wire raises to Shell, not Solid)   │
│ LAST CONSTRUCTION  Loft                                             │
└─────────────────────────────────────────────────────────────────────┘
```

Flipping a toggle re-filters live — **that is the actual thing under test.** The `RESULT TYPE` readout
is the second thing under test: it proves §3's law is driving the answer rather than a lookup table.

**Nine presets**, each driving one gate class to a *known* outcome and labelled with the expectation:

| Preset | Expected |
|---|---|
| `no workplane` / `workplane active` | all 22 sketch tiles greyed with *"set a workplane"* / live |
| `closed wire + axis` / `open wire + axis` | Revolve → **Solid** / Revolve → **Shell** (badge, still live) |
| `face selected` | Extrude → Solid with **no closure flag offered** |
| `solid selected` | 🔴 Revolve **absent, not greyed** — Solid has no successor |
| `1 profile` / `2 profiles` | Loft greyed *"needs 2 profiles"* / live |
| `boundary 3 edges` / `boundary 5 edges` | Fill live / greyed *"needs 2-4 edges"* |
| `mixed closure profiles` | Sweep greyed *"profiles must share closure"* |
| `2 solids` / `1 solid` | Cut live / greyed *"needs exactly 2"* |
| `3 circles present` | tangency construction live |

Plus a live invariant — every catalogue row lands in exactly one bucket
(`available + gated + hidden == 128`), displayed rather than asserted so a reviewer watches it hold
while clicking. Same device as the sibling plan §7, and for the same reason.

---

## 9. Sequencing

① `ConstructionPredicate` (pure logic, no UI — the §3 law lives here) → ② `ConstructionCatalogue` →
③ `ConstructionGlyphIdentity` + `ConstructionGlyphTable` → ④ `ConstructionCatalogueMenu` (rail, then
tile field, then fix chips) → ⑤ validation host + panel → ⑥ `Build.bat`, compile, run →
⑦ `FolderStructure.md`.

⚠️ Step ① must land and be probe-tested **before** ②. The dimension law is the whole design; a
catalogue written against a wrong law encodes 128 wrong gates.

---

## 10. Verification targets

| Probe | Target |
|---|---|
| Dimension-law assertions | every `(dimension × closure)` pair predicts the documented OCCT result type |
| Gate assertions | ≥1 per band, ≥14 total; every 🔴 gate in §4 covered |
| Glyph coverage | all resolve; 0 empty, 0 malformed, all within the unit square |
| Capacity guards | overflow / degenerate / null runs all refused |
| Availability tally | `available + gated + hidden == 128` across all 7 dimensions × workplane on/off |
| Linkage | `ConstructionCatalogue.cpp` links with **no ImGui at all** (the §7 split, probe-verified) |
| Build | clean, **zero warnings** |
| Deliverable | `ConstructionCatalogueValidation.exe` links, launches, creates its D3D11 device, stays alive |

---

## 11. Deferred

Appended to `EngineDocs/Backlog.md`, not built here:

- **Flat search over the catalogue** (Blender `F3` / Dune3D spacebar) — needs a text-input surface the
  validation host does not have.
- **Preview-before-commit** (Dune3D) — needs a viewport; this host has none by design.
- **Accelerator-letter navigation** — needs the keymap layer.
- **Strict vs permissive construction modes** (OCCT's `GC_` vs `gce_` twin APIs) — a real kernel-level
  precedent, but meaningless before a kernel is wired in.
- **`ConstructionMode` beyond `Add`** — the axis is designed and carried in the descriptor, but only
  `Add` has anything to act on until booleans are live.
