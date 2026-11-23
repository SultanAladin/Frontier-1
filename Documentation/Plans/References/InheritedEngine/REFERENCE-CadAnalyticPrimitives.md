# REFERENCE — CAD Analytic Primitives: Source Locations + Completion List

🧩 A lookup table for reconstructing the CAD workspace. Every row names the ONE location (path + line)
for a capability, so a rehook can jump straight to it. **Blank cells mean the capability does not
exist yet** — those are the build-new rows.

Status: reference only (2026-07-29). Complements `EngineDocs/REFERENCE-ParametricSketchSolidLoop.md`
(the GPU solid loop) — this covers the analytic primitive layer and what is missing from it.

---

## 0. Paths — working tree vs port source

🔴 **Working tree** (where we build; all §1–§10 paths are relative to it):

```
C:\Users\OS\Documents\Projects\Frontier\Internal\
```

📝 **Port source** (read-only origin; consult when a capability is missing here but present there):

```
C:\Users\OS\Documents\Frontier\RetiredProject\Engine\Internal\
```

The two use different prefixes for the same code. Mapping:

| Working tree (`Projects\…\Internal\`) | Port source (`RetiredProject\…\Internal\`) |
|---|---|
| `ParametricSketch…` prefix | `Draught…` prefix |
| `Authoring\ParametricAuthoring\` | `Authoring\Modeling\Draughting\` |
| `Graphics\Render\Surface\` | `Graphics\Renderer\Surface\` + `Graphics\Renderer\Viewport\` |
| `…SolidSequence` | `…SolidScene` |
| `…SurfaceInscription` | `…SurfacePass` (`Pass` is banned — the rename is correct) |

⚠️ A **stale duplicate** also sits in the working tree at
`Internal\Authoring\Modeling\Draughting\` (`Draught…` prefix, not included by anything —
verified: 655 refs, all internal to its own 12 files). Do not build on it; do not delete it without
the user saying so.

🔴 **Not yet ported** — these exist only in the port source and have no working-tree counterpart:
the entire CPU stroke/interaction layer (`Interaction\Interface\Components\WorkspaceHost\`,
incl. `DraughtingView.cpp` at 388 KB) and `EngineInfrastructure\Platform\Application\CadMain.cpp`.
The working tree's `EngineContext\Interface\Workspaces\ParametricSketcher\ParametricSketcherWorkspace.h`
is a 2.5 KB stub with only `InitializeParametricSketcherWorkspace`.

| Layer | Working tree folder |
|---|---|
| Geometry (analytic model + ops) | `Authoring\ParametricAuthoring\` |
| Render (GPU solid chain) | `Graphics\Render\Surface\` |
| Drawing (CPU stroke / ImGui) | *(not ported — see §10)* |

---

## 1. Analytic model — types

All in `Authoring\ParametricAuthoring\ParametricSketchShapeStore.h` (abbreviated `…Store.h`).

| Concern | Type | Location |
|---|---|---|
| Primitive family tag | `ParametricSketchShapeCategory` | `…Store.h:32` |
| One analytic shape | `ParametricSketchShape` | `…Store.h:153` |
| Corner fillet/chamfer record | `ParametricSketchCornerFillet` | `…Store.h:140` |
| Snap target tag | `ParametricSketchSnapCategory` | `…Store.h:53` |
| Constraint tag | `ParametricSketchConstraintCategory` | `…Store.h:67` |
| Dimension tag | `ParametricSketchDimensionCategory` | `…Store.h:82` |
| Datum axis tag | `ParametricSketchDatumAxis` | `…Store.h:95` |
| Point reference | `ParametricSketchPointHandle` | `…Store.h:326` |
| Constraint record | `ParametricSketchConstraint` | `…Store.h:336` |
| Dimension record | `ParametricSketchDimension` | `…Store.h:350` |
| Text annotation | `ParametricSketchInscription` | `…Store.h:250` |
| Reference datum | `ParametricSketchDatum` | `…Store.h:267` |
| Outliner folder | `ParametricSketchFolder` | `…Store.h:365` |
| Undo snapshot | `ParametricSketchRevision` | `…Store.h:377` |
| History row | `ParametricSketchHistoryEntry` | `…Store.h:403` |
| Per-view store | `ParametricSketchShapeStore` | `…Store.h:418` |
| Store registry | `ParametricSketchShapeRegistry` | `…Store.h:500` |
| Snap hit | `ParametricSketchSnapCandidate` | `…Store.h:506` |
| Camera/canvas bridge payload | `ParametricSketchSceneView` | `…Store.h:538` |
| Tessellated body → GPU | `ParametricSketchShapeBody` | `…Store.h:573` |
| Loft recipe | `LoftSpecification` | `…Store.h:286` |
| Loft body | `ParametricSketchLoftBody` | `…Store.h:307` |

---

## 2. Primitive construction — per category

All construction funnels through **one factory**: `ConstructParametricSketchShape(Category,
DefiningPoints, SideCountOverride, RhoOverride, DegreeOverride)` — `…Store.h:599`. It copies the
defining clicks and solves the analytic scalars. The enum value is the switch key.

| Primitive | Enum value | Construction | Flatten (analytic → polyline) |
|---|---|---|---|
| Line | `ParametricSketchShapeCategory::Line` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Polyline | `…::Polyline` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Arc (3-point) | `…::Arc` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Bezier | `…::Bezier` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| BSpline | `…::BSpline` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Nurbs | `…::Nurbs` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Spline (Catmull-Rom) | `…::Spline` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Conic (rho-weighted) | `…::Conic` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Rectangle (2-corner) | `…::Rectangle` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Circle (centre-radius) | `…::Circle` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Ellipse | `…::Ellipse` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Polygon (regular N-gon) | `…::Polygon` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Slot (stadium) | `…::Slot` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |
| Profile (boolean result) | `…::Profile` `…Store.h:32` | `…Store.h:599` | `EvaluateShapePolyline` `…Store.h:608` |

Supporting:

| Concern | Function | Location |
|---|---|---|
| Clicks a category needs | `ResolveParametricSketchDefiningCount` | `…Store.h:593` |
| Memoised flatten (hot path) | `RetrieveCachedOutline` | `…Store.h:614` |
| Zoom-adaptive sample budget | `ResolveAdaptiveSampleBudget` | `…Store.h:632` |
| One defining edge as polyline | `ResolveAnalyticEdgeSpan` | `…Store.h:621` |
| Append constructed shape to store | `AppendParametricSketchShape` | `…Store.h:638` |

---

## 3. Primitives to BUILD — nothing exists in either tree

🔴 Blank source column = new work. Grouped by the family they extend, so each lands beside an
existing constructor.

| Primitive | Family it extends | Source location |
|---|---|---|
| Point (as an entity) | — | |
| Infinite / construction line | Line | |
| Ray | Line | |
| Centerline | Line | |
| Rectangle — centre | Rectangle | |
| Rectangle — 3-point (rotated) | Rectangle | |
| Rectangle — rounded | Rectangle | |
| Parallelogram | Rectangle | |
| Polygon — circumscribed | Polygon | |
| Polygon — by edge | Polygon | |
| Polygon — star | Polygon | |
| Circle — 3-point | Circle | |
| Circle — 2-point (diameter) | Circle | |
| Circle — tangent-tangent-radius | Circle | |
| Arc — centre-start-end | Arc | |
| Arc — tangent (continuation) | Arc | |
| Arc — start-end-radius | Arc | |
| Elliptical arc (partial ellipse) | Ellipse | |
| Parabola (typed) | Conic | |
| Hyperbola (typed) | Conic | |
| Fit-point / interpolated spline | Spline | |
| Periodic (closed) spline | BSpline | |
| Slot — overall length | Slot | |
| Slot — centre-point | Slot | |
| Slot — 3-point arc | Slot | |
| Slot — arc (centre-point) | Slot | |
| Helix / spiral | — | |
| Text → outline profile | Profile | |
| Hatch / region | Profile | |
| Projected curve | — | |
| Intersection curve | — | |
| Silhouette / isocline | — | |
| Equation-driven curve | — | |

---

## 4. Curve operations

Folder: `Authoring\ParametricAuthoring\Operations\`.

| Operation | Function | Location |
|---|---|---|
| Fillet corner | `FilletShapeCorner` | `Operations\Fillet\ParametricSketchFillet.h:77` |
| Chamfer corner | `ChamferShapeCorner` | `…\ParametricSketchFillet.h:82` |
| Corner radius safe limit | `ResolveCornerSafeLimit` | `…\ParametricSketchFillet.h:71` |
| Fillet arc snap points | `EvaluateArcParametricSnaps` | `…\ParametricSketchFillet.h:87` |
| Trim (remove a span) | `PartitionShapeSegment` | `…Store.h:849` |
| Trim hover preview | `ResolveTrimPreviewSpan` | `…Store.h:856` |
| Cut / split at point | `CutShapeAtPoint` | `…Store.h:869` |
| Join open runs | `AssembleOpenShapes` | `…Store.h:878` |
| Offset a region (outer + holes) | `SolveRegionOffset` | `Operations\Transform\ParametricSketchTransform.h:68` |
| Mirror (live/parametric) | `ReflowMirrorChildren` | `…\ParametricSketchTransform.h:80` |
| Array (live/parametric) | `ReflowArrayChildren` | `…\ParametricSketchTransform.h:94` |
| Boolean — two loops | `SolveLoopBoolean` | `Operations\Boolean\ParametricSketchBoolean.h:45` |
| Boolean — region | `SolveRegionBoolean` | `…\ParametricSketchBoolean.h:51` |
| Boolean — region with holes | `SolveRegionBooleanWithHoles` | `…\ParametricSketchBoolean.h:59` |
| Boolean loop normalize | `NormalizeBooleanLoops` | `…\ParametricSketchBoolean.h:64` |
| Loft sections → body | `ReflowLoftBodies` | `Operations\Loft\ParametricSketchLoft.h:52` |
| Detach loft body | `DetachLoftBody` | `…\ParametricSketchLoft.h:59` |
| Duplicate shape | `DuplicateParametricSketchShape` | `…Store.h:832` |
| Detach shape | `DetachParametricSketchShape` | `…Store.h:840` |
| Extend | | |
| Trim/extend to a boundary curve | | |
| Convert to B-spline | | |
| Knot insert / degree elevate | | |

---

## 5. Constraints + dimensions

| Constraint | Enum value | Solver |
|---|---|---|
| Coincident | `ParametricSketchConstraintCategory::Coincident` `…Store.h:67` | `SolveParametricSketchSketch` `ParametricSketchConstraintSolver.h:28` |
| Horizontal | `…::Horizontal` `…Store.h:67` | `…Solver.h:28` |
| Vertical | `…::Vertical` `…Store.h:67` | `…Solver.h:28` |
| Parallel | `…::Parallel` `…Store.h:67` | `…Solver.h:28` |
| Perpendicular | `…::Perpendicular` `…Store.h:67` | `…Solver.h:28` |
| Tangent | `…::Tangent` `…Store.h:67` | `…Solver.h:28` |
| Equal | `…::Equal` `…Store.h:67` | `…Solver.h:28` |
| Fixed | `…::Fixed` `…Store.h:67` | `…Solver.h:28` |
| Point-on-object | | |
| Collinear | | |
| Concentric (explicit) | | |
| Midpoint | | |
| Symmetric | | |
| Curvature / G2 | | |
| Internal alignment (conic foci, spline hull) | | |

| Dimension | Enum value | Record |
|---|---|---|
| Horizontal | `ParametricSketchDimensionCategory::Horizontal` `…Store.h:82` | `…Store.h:350` |
| Vertical | `…::Vertical` `…Store.h:82` | `…Store.h:350` |
| Aligned | `…::Aligned` `…Store.h:82` | `…Store.h:350` |
| Radius | `…::Radius` `…Store.h:82` | `…Store.h:350` |
| Diameter | `…::Diameter` `…Store.h:82` | `…Store.h:350` |
| Angular | `…::Angular` `…Store.h:82` | `…Store.h:350` |
| Arc length | | |
| Reference / driven (non-driving) | | |
| Constraint enable/disable | | |
| Expression-valued dimension | | |

Solver support (`Authoring\ParametricAuthoring\ParametricSketchConstraintSolver.h`):

| Concern | Function | Location |
|---|---|---|
| Run the solve | `SolveParametricSketchSketch` | `…Solver.h:28` |
| Remaining DOF | `ResolveDegreesOfFreedom` | `…Solver.h:34` |
| Is a point pinned | `ResolvePointFixed` | `…Solver.h:38` |
| Over-constrained / redundancy report | | |
| Non-convergence vs inconsistency split | | |

---

## 6. Parameter editing (Properties panel)

| Concern | Function | Location |
|---|---|---|
| Radius (Circle/Arc/Polygon/Slot) | `EnforceShapeRadius` | `…Store.h:752` |
| Ellipse axes | `EnforceEllipseAxes` | `…Store.h:755` |
| Read rectangle W/H | `ResolveRectangleDimensions` | `…Store.h:758` |
| Set rectangle W/H | `EnforceRectangleDimensions` | `…Store.h:761` |
| Rotation | `EnforceShapeRotation` | `…Store.h:764` |
| Polygon side count | `EnforcePolygonSideCount` | `…Store.h:767` |
| Conic rho | `EnforceConicRho` | `…Store.h:770` |
| Curve degree | `EnforceCurveDegree` | `…Store.h:773` |
| Move one defining point | `EnforceShapePoint` | `…Store.h:777` |
| Primary dimension read | `ResolvePrimaryDimension` | `…Store.h:701` |
| Primary dimension write | `EnforcePrimaryDimension` | `…Store.h:707` |
| Outline length | `EvaluateParametricSketchShapeLength` | `…Store.h:696` |

---

## 7. Picking, snapping, winding

| Concern | Function | Location |
|---|---|---|
| Distance to a shape | `EvaluateDistanceToShape` | `…Store.h:687` |
| Pick nearest shape | `PickParametricSketchShape` | `…Store.h:692` |
| Pick a datum | `PickDatum` | `…Store.h:678` |
| Resolve a snap target | `ResolveSnapCandidate` | `…Store.h:739` |
| Signed area (shoelace) | `EvaluateSignedArea` | `…Store.h:717` |
| Enforce winding order | `EnforceWindingOrder` | `…Store.h:721` |
| Closed polygon for fill | `EvaluateFilledPolygon` | `…Store.h:726` |
| Shape centroid | `ResolveParametricSketchShapeCentroid` | `…Store.h:836` |
| Boundary of all shapes (Fit) | `ResolveShapesBoundary` | `…Store.h:882` |
| Snap: endpoint/midpoint/centre/along-curve | `ParametricSketchSnapCategory` | `…Store.h:53` |
| Snap: intersection | | (named at `…Store.h:53`, never returned) |

---

## 8. Store, history, annotation

| Concern | Function | Location |
|---|---|---|
| Resolve/create per-view store | `ResolveParametricSketchShapeStore` | `…Store.h:518` |
| Release store | `ReleaseParametricSketchShapeStore` | `…Store.h:521` |
| Publish store for the frame | `RegisterParametricSketchShapeSource` | `…Store.h:527` |
| Read published store | `RetrieveParametricSketchShapeSource` | `…Store.h:531` |
| Append edit + snapshot | `AppendParametricSketchEdit` | `…Store.h:787` |
| Append edit with detail | `AppendParametricSketchEditDetailed` | `…Store.h:791` |
| Capture snapshot | `CaptureParametricSketchRevision` | `…Store.h:807` |
| Restore to step | `RestoreParametricSketchRevisionAt` | `…Store.h:812` |
| Undo | `UndoParametricSketchEdit` | `…Store.h:815` |
| Redo | `RedoParametricSketchEdit` | `…Store.h:818` |
| Attach folder | `AttachParametricSketchFolder` | `…Store.h:822` |
| Append inscription | `AppendInscription` | `…Store.h:651` |
| Resolve inscription | `ResolveInscription` | `…Store.h:654` |
| Move inscription | `EnforceInscriptionAnchor` | `…Store.h:658` |
| Detach inscription | `DetachInscription` | `…Store.h:662` |
| Append datum | `AppendDatum` | `…Store.h:666` |
| Resolve datum | `ResolveDatum` | `…Store.h:669` |
| Move datum | `EnforceDatumAnchor` | `…Store.h:673` |
| Detach datum | `DetachDatum` | `…Store.h:682` |

---

## 9. Rendering — GPU solid chain

Folder: `Graphics\Render\Surface\`.

⚠️ This chain renders **filled faces + lofted solids only** (matcap). There is no GPU curve/line
rasterizer — see §11.

| Concern | Function | Location |
|---|---|---|
| Bring up the whole chain | `InitializeParametricSketchSolidSequence` | `ParametricSketchSolidSequence.h:68` |
| Per-frame sync + re-stage | `SynchronizeParametricSketchSolidSequence` | `…SolidSequence.h:79` |
| **Record the draw** | `RecordParametricSketchSolidSequenceInto` | `…SolidSequence.h:84` |
| Publish image to the view | `PublishParametricSketchSolidImage` | `…SolidSequence.h:89` |
| Teardown | `FinalizeParametricSketchSolidSequence` | `…SolidSequence.h:93` |
| Resident body row | `ResidentParametricSketchBody` | `…SolidSequence.h:37` |
| Sequence struct | `ParametricSketchSolidSequence` | `…SolidSequence.h:48` |
| Matcap pipeline init | `InitializeParametricSketchSurfaceInscription` | `ParametricSketchSurfaceInscription.h:76` |
| Matcap camera refresh | `RefreshParametricSketchSurfaceCamera` | `…SurfaceInscription.h:85` |
| Matcap record | `RecordParametricSketchSurfaceInto` | `…SurfaceInscription.h:91` |
| Matcap pipeline teardown | `FinalizeParametricSketchSurfaceInscription` | `…SurfaceInscription.h:99` |
| Camera block | `ParametricSketchSurfaceCameraBlock` | `…SurfaceInscription.h:33` |
| Matcap texture init | `InitializeParametricSketchMatcapTexture` | `ParametricSketchMatcapTexture.h:47` |
| Matcap texture teardown | `FinalizeParametricSketchMatcapTexture` | `…MatcapTexture.h:50` |
| Offscreen target init | `InitializeParametricSketchViewTarget` | `ParametricSketchViewTarget.h:61` |
| Offscreen target resize | `ReconfigureParametricSketchViewTarget` | `…ViewTarget.h:66` |
| Offscreen target teardown | `FinalizeParametricSketchViewTarget` | `…ViewTarget.h:70` |
| Vertex shader | — | `Graphics\Render\Surface\Shaders\ParametricSketchMatcap.vert` |
| Fragment shader | — | `…\Shaders\ParametricSketchMatcap.frag` |
| App-level pre-pass wiring | | *(not ported — `CadMain.cpp::RecordCadSolidPrePass` in port source)* |

### Bridges (the geometry ↔ render seam — the rehook surface)

| Direction | Function | Location |
|---|---|---|
| Store → render | `RegisterParametricSketchShapeSource` | `…Store.h:527` |
| Camera + canvas → render | `RegisterParametricSketchSceneView` | `…Store.h:551` |
| Render reads camera | `RetrieveParametricSketchSceneView` | `…Store.h:555` |
| Tessellated bodies → render | `RegisterParametricSketchShapeBodies` | `…Store.h:585` |
| Render reads bodies | `RetrieveParametricSketchShapeBodies` | `…Store.h:589` |
| Render → view (image back) | `RegisterParametricSketchSolidImage` | `…Store.h:562` |
| View reads image | `RetrieveParametricSketchSolidImage` | `…Store.h:566` |

Data path:

```
ConstructParametricSketchShape (…Store.h:599)
  → RetrieveCachedOutline (…Store.h:614) / EvaluateFilledPolygon (…Store.h:726)
  → ParametricSketchShapeBody {Positions, Normals, Indices, Revision}   (…Store.h:573)
  → RegisterParametricSketchShapeBodies (…Store.h:585)
  → SynchronizeParametricSketchSolidSequence (…SolidSequence.h:79)   re-stage on Revision bump
  → RecordParametricSketchSolidSequenceInto  (…SolidSequence.h:84)   offscreen matcap draw
  → RegisterParametricSketchSolidImage (…Store.h:562)                view composites into canvas
```

---

## 10. Drawing — CPU stroke (ImGui drawlist)

🔴 **Not ported.** The working tree has only a stub
(`EngineContext\Interface\Workspaces\ParametricSketcher\ParametricSketcherWorkspace.h:40` —
`InitializeParametricSketcherWorkspace`) plus `SketchPropertyPanel.h` and
`EngineContext\Interface\WorkspaceHost\SketchOutliner\SketchOutlinerPanel.h:161`.

Port-source locations (under `RetiredProject\Engine\Internal\Interaction\Interface\Components\WorkspaceHost\`):

| Concern | Function | Working tree | Port source |
|---|---|---|---|
| **Canvas paint entry point** | `ConstructDraughtingViewInterior` | | `DraughtingView.cpp:2406` (decl `.h:285`) |
| **Stroke one shape outline** | `PaintShapeOutline` | | `DraughtingView.cpp:876` |
| Constraint glyph | `PaintConstraintMark` | | `DraughtingView.cpp:1964` |
| Linear dimension + leader | `PaintLinearDimension` | | `DraughtingView.cpp:1990` |
| Radius/diameter dimension | `PaintRoundDimension` | | `DraughtingView.cpp:2047` |
| Ground grid | `ConstructGroundGrid` | | called `DraughtingView.cpp:3969` |
| Live draw preview | factory preview call | | `DraughtingView.cpp:4257` |
| Planar gizmo | `ConstructPlanarGizmo` | | called `DraughtingView.cpp:4534` |
| Viewport gizmo | `ConstructViewportGizmo` | | called `DraughtingView.cpp:4547` |
| Settings menu | `ConstructDraughtingSettingsMenu` | | `DraughtingView.cpp:5649` (decl `.h:308`) |
| Release view state | `ReleaseDraughtingViewState` | | `DraughtingView.h:275` |
| Toolbar | — | | `DraughtingToolbar.h` / `.cpp` |
| Tool icons | — | | `DraughtToolIcons.h` |
| Outliner panel | — | `SketchOutlinerPanel.h:161` | `WorkspaceDraughtOutliner.h` / `.cpp` |
| Properties panel | — | `SketchPropertyPanel.h` | `WorkspaceDraughtProperties.h` / `.cpp` |
| Fillet modal | — | | `DraughtFilletModal.h` / `.cpp` |
| Inset modal | — | | `DraughtInsetModal.h` / `.cpp` |
| Modal transform (G/R/S) | — | | `DraughtModalTransform.h` / `.cpp` |
| Numeric entry field | — | | `DraughtNumericField.h` / `.cpp` |
| Overlay capture | — | | `DraughtingOverlayCapture.cpp` |

---

## 11. Display capabilities to BUILD — nothing exists in either tree

| Capability | Source location |
|---|---|
| GPU thick-line / curve rasterizer | |
| Screen-constant-width strokes | |
| Dashed / patterned lines | |
| Point sprites (vertex display) | |
| Construction-geometry linetype | |
| Curvature comb | |
| Control polygon + knot display | |
| Hidden-line / silhouette output | |

---

## 12. Reconstruction order (dependency-first)

| # | Step | Anchor | State |
|---|---|---|---|
| 1 | Store + shape type | `…Store.h:153`, `:418` | ✔️ ported |
| 2 | Factory + flatten | `…Store.h:599`, `:608`, `:614` | ✔️ ported |
| 3 | Picking + snapping | `…Store.h:687`, `:739` | ✔️ ported |
| 4 | History / undo | `…Store.h:787`, `:812` | ✔️ ported |
| 5 | Constraint solver | `…Solver.h:28` | ✔️ ported |
| 6 | Curve operations | `…Fillet.h:77`, `…Store.h:849` | ✔️ ported |
| 7 | Booleans + offset | `…Boolean.h:45`, `…Transform.h:58` | ✔️ ported |
| 8 | GPU solid chain + bridges | `…SolidSequence.h:68`, `…Store.h:585` | ✔️ ported |
| 9 | Loft | `…Loft.h:52` | ✔️ ported |
| 10 | **CPU stroke paint + toolbar + modals** | §10 | 🔴 port next |
| 11 | **App pre-pass wiring** | §9 last row | 🔴 port next |
| 12 | New primitives | §3 | 🚧 new work |
| 13 | New constraints/dimensions | §5 | 🚧 new work |
| 14 | GPU curve rasterizer | §11 | 🚧 new work |

---

## 13. Naming note

🔴 The port source uses banned words; the working tree has already fixed some. See
`AgenticInstuctions/SKILL-Naming.md` before porting §10.

| Port-source term | Working-tree state |
|---|---|
| `…Pass` (`DraughtSurfacePass`) | ✔️ renamed → `…Inscription` / `…Sequence` |
| `Draught…` prefix | ✔️ renamed → `ParametricSketch…` |
| `Handle` (`DraughtPointHandle`) | 🔴 still `ParametricSketchPointHandle` — banned, → `…Token` |
| `Kind` | ✔️ already `.Category` |
