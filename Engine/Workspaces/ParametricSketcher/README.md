# 🧩 `Engine/Workspaces/ParametricSketcher/` — Parametric B-Rep & NURBS CAD (`FrontierSketcher`)

> **TL;DR**: `ParametricSketcher` (`Sketcher`) is Frontier's native B-rep/NURBS CAD studio combining `Engine/GeometrySpace/Parametric/` (exact analytic + rational NURBS, General Fuse Booleans, rolling-ball fillets/chamfers, SolveSpace-style 2D constraints, incremental feature replay, STEP AP214/AP203/AP242 & STL exchange, ISO 2D drawing sheets) with a Vulkan `.slang` MatCap viewport (`SketcherRasterization`) and 5 dockable ImGui panes.

---

## 🧩 1. Module Architecture

| Subdirectory | Files | Responsibilities |
| :--- | :--- | :--- |
| **`Document/`** | `SketcherDocument.{h,cpp}`, `SketcherDocumentCodec.{h,cpp}`, `SketcherScene.{h,cpp}`, `SketcherSpecimen.{h,cpp}`, `SketcherCurveImport.{h,cpp}`, `SketcherDrawingSheet.{h,cpp}`, `SketcherExchange.{h,cpp}`, `SketcherImagePlane.{h,cpp}`, `SketcherImagePlaneToml.h`, `SketcherKnife.{h,cpp}`, `SketcherMatcap.{h,cpp}`, `SketcherMeasurement.{h,cpp}` | Feature tree document model, `.SketcherSpace` codec, specimen catalog, SVG/curve import, ISO 2D drawing sheets, STEP/STL import/export, reference image planes, KitOps-style Knife cutters, MatCap library, 3D measurement |
| **`Interface/`** | `SketcherWorkspaceHost.{h,cpp}`, `SketcherInterface.{h,cpp}`, `SketcherOutliner.{h,cpp}`, `SketcherNavigation.{h,cpp}`, `SketcherOptions.{h,cpp}`, `SketcherIcons.{h,cpp}`, `SketcherFileDialog.{h,cpp}`, `SketcherScript.{h,cpp}`, `SketcherStyleSheet.{h,cpp}`, `SketcherSymbols.inc`, `SketcherUiKit.h` | Dock host (5 panes), Outliner, 4-tab Inspector, Construct drawer, Drawing Sheet viewer, Blender/Plasticity hotkeys, UI automation scripting |
| **`Viewport/`** | `SketcherRasterization.{h,cpp}`, `SketcherCamera.{h,cpp}`, `SketcherOrbitCamera.{h,cpp}`, `SketcherGizmoSpace.{h,cpp}`, `SketcherSnapping.{h,cpp}`, `SketcherSurfaceTracing.{h,cpp}`, `SketcherPortableImage.{h,cpp}` | Vulkan `.slang` B-rep & MatCap rendering, orbit/ortho camera, 3D gizmos, curve/surface snapping, portable PNG capture |

---

## 🏷️ 2. UI Controls

| Panel / Region | Controls & Tabs | Function |
| :--- | :--- | :--- |
| **Dockable Panes (`SketcherWorkspaceHost`)** | `Outliner` (`300 px`) · `Viewport` · `Inspector` (`330 px`) · `Construct` · `Drawing Sheet` | Trapezoidal dockable multi-pane layout |
| **`SketcherOutliner`** | Feature tree steps, Bodies, Sketches, Curves, Image Planes, Eye (visibility) & Lock toggles, rename/reorder | Manage parametric history, visibility, and pick locking |
| **`Inspector` 4 Tabs** | **`Geometry`** · **`Transform`** · **`Topology`** · **`History`** | Inspect selection summary, bounds (`WORLD · m`), ordered selection, bevel history, step variables, edge weights, modifier stack, MatCap library, constraints, and placed dimensions |
| **`Construct` Panel** | Primitive solids (`Block`, `Cylinder`, `Sphere`, `Cone`, `Torus`, `Prism`), 2D curves (`Line`, `Curve` [Hermite/Bezier/NURBS/CubicSpline], `Rectangle`, `Circle`, `Polygon`), `Section`, `Joint`, `ImagePlane` | Construct primitives, sketch curves, assemblies, and backdrop reference planes |
| **`Drawing Sheet` Panel** | ISO `A4–A0` layout, First/Third-angle projection, scale, center marks, section views, SVG / DXF R12 export | Author & export manufacturing 2D orthographic drawings |
| **`MatCap` Picker (`M`)** | 16 built-in studio MatCaps + `Add picture (.png/.jpg)` + `Remove (Alt+M)` | Assign per-body or per-face Vulkan MatCap materials |
| **`SplitView`** | Single / Multi-pane viewport layouts (`Ctrl+Alt+Q`) with independent `OrbitCamera` per pane | Multi-angle simultaneous CAD inspection |

---

## 📝 3. Hotkeys & Mouse Controls (`SketcherInterface` & `SketcherNavigation`)

### 3.1 Pick Modes & Viewport Overlays

| Shortcut | Action |
| :--- | :--- |
| `1` / `2` / `3` / `4` / `5` (or `Tab`) / `6` | Pick mode: `ControlPoint` / `Edge` / `Face` / `Solid` / `Any` / `Curve` |
| `Space` (or Numpad `.`) | Frame selection in camera |
| `Alt+Z` / `Alt+Shift+Z` | Toggle X-Ray / Toggle grid overlays |
| `Ctrl+Alt+Q` | Toggle Single vs Multi-pane viewport layout |
| `H` / `Shift+H` / `Alt+H` / `Ctrl+H` | Hide selected / Hide unselected / Unhide all / Invert hidden |
| `Ctrl+I` | Toggle viewport Isolate |
| `M` / `Alt+M` | Open MatCap material picker / Remove MatCap |
| `Alt+S` / `Alt+D` / `Alt+J` / `Alt+N` | Toggle Section / Toggle Drawing Sheet / Open Joint (`R`/`V`/`S`/`C`/`P`/`L`/`B`, `F` flip) / Flip normals |

### 3.2 Document, Editing & Construct Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` | New / Open / Save / Save As |
| `Ctrl+Shift+I` / `Ctrl+Shift+E` | Import (STEP/SVG) / Export (STEP/STL) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / Redo |
| `Ctrl+A` / `Alt+A` | Select All / Clear Selection |
| `Ctrl+C` / `Ctrl+V` / `Ctrl+D` / `Shift+D` | Copy / Paste / Duplicate + Move / Subdivide spline (`Ctrl+Shift+D`) or Duplicate (`Shift+D`) |
| `X` / `Del` / `Backspace` | Delete picked geometry or dimension |
| `Shift+C` / `Shift+V` / `Shift+X` / `Shift+Z` | Construct Corner Box / Centre Box / Cylinder / Sphere |
| `Shift+A` / `Shift+S` / `Shift+Q` / `Shift+W` | Draw `Line` / `Curve` (`1–4` spline form) / `Rectangle` (`S` square, tap `Alt` centre) / `Circle` (`V` diameter/centre) |
| `Shift+R` | Repeat last command |

### 3.3 Modeling, Sweep & Transform Sessions

| Shortcut | Action | Modal Sub-Keys |
| :--- | :--- | :--- |
| `G` / `R` / `S` / `Y` | `Move` / `Rotate` / `Scale` / Cycle Gizmo Modes | `X`/`Y`/`Z` axis (`Shift+X/Y/Z` plane), `G`/`R`/`S` switch, digits, `Enter`/`Esc` |
| `E` / `Ctrl+E` | Extrude (`LastExtrudeMode`) / Extrude menu | `Q` Union · `W` Diff · `E` Intersect · `R` New Body · `A` Auto |
| `B` / `Ctrl+B` / `Ctrl+Shift+B` | Bevel Fillet / Fillet / Chamfer | `D` Fillet · `C` Chamfer · `V` Variable · `F` Flip anchor |
| `Q` | Boolean | `Q` Union · `W` Diff · `E` Intersect · `L` Live Boolean |
| `C` | Chamfer (if edges/Edge mode) or Cut | — |
| `I` / `O` / `P` / `L` / `J` | `Inset` (on faces, else Measure) / `Offset` (else Inset) / `Pipe` / `Loft` / `Join` | Pipe/Offset: `Q`/`W`/`E`/`R` combine; Revolve (`Alt+R`): `X`/`Y` axis |
| `Alt+E` or `Shift+P` | Evolution sweep | — |
| `T` / `V` / `F` | `Trim` / Point on curve / Toggle sketch Fill | `Enter` / `Esc` / re-press ends |
| `Alt+X` | Mirror panels (`X = 0`) | — |
| `Shift+K` | Knife cutter (`Sketch`/`Insert`/`Object`) | `Tab` mode · `Q`/`W`/`E`/`R` combine · `T` through · `K` keep · `L` live · `[`/`]` turn · `D`/`C` blend |
| `D` | Dimension tool | `X`/`Y`/`Z` slide · `Shift+X/Y/Z` span · `A` free · `R` `Ø/R` · `Enter`/`Esc` |
| `K` | Constraint tool (SolveSpace verbs) | `D` Dim · `N` Angle · `H` Horiz · `V` Vert · `O` On · `Q` Equal · `M` Mid · `Y` Sym · `L` Parallel/Tan · `[` Perp · `]` Lock · `C` G2 Curvature · `Shift+D` Ref · `E` Toggle Ref |

### 3.4 Mouse & Blender-Style Numpad Navigation (`SketcherNavigation`)

| Input | Action |
| :--- | :--- |
| `MMB` Drag / `RMB` Drag / `Wheel` | Orbit camera / Pan camera / Dolly zoom |
| `Numpad 1` / `3` / `7` (`+Ctrl`) | Front/Back · Right/Left · Top/Bottom |
| `Numpad 0` / `9` / `5` | Isometric / 180° opposite flip / Toggle Ortho ↔ Perspective |
| `Numpad 2` / `4` / `6` / `8` (`+Ctrl`) | 15° step orbit (or `Ctrl` + Numpad pan) |
| `Numpad +` / `Numpad -` / `Numpad .` | Zoom in / Zoom out / Frame selection |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `56` files under `Engine/Workspaces/ParametricSketcher/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **`State`** | 7232 | `SketcherWorkspaceState`, `State` parameters throughout `SketcherInterface.cpp` | `SketcherSession` / concrete state holder (`ViewportConfiguration`, `SessionDocument`) |
| **Prohibited `<K-suffix>`** | 1732 | `SketcherDocument.cpp` (`FeatureStep<K-suffix>`, `Reference<K-suffix>`, `SketchConstraint<K-suffix>`, `SketcherConstruct<K-suffix>`, `SketcherSelection<K-suffix>`, `Step<K-suffix>`) | `*Category` / `*Variant` / `*Mode` |
| **`Frame` / `Value` / `Draw*` / `Stage` / `Map`** | 2927 | `Frame` (`1239`), `Value` (`573`), `Draw*` (`512`), `Stage` (`449`), `Map` (`374`) | `Extent`/`CameraProjection`, `ScalarReading`, `Construct*`, `Step`, `Register` |
| **`Get*`/`Set*` / `Card` / `Item` / `Glyph` / `Is*/Has*` / `Pipeline`** | 1277 | `Get/Set` (`320`), `Card` (`265`), `Item` (`215`), `Glyph` (`187`), `Is/Has` (`185`), `Pipeline` (`185`) | `Query*`/`Assign*`, `SectionBlock`, `Entry`, `Icon`/`Symbol`, `RenderSequence` |
| **`Base` / `Pill` / `Flag` / `Catalog` / `Popup` / `Menu` / `Info` / `Pass` / `Badge` / `Record` / `Context` / `Palette` / `Data` / `Make*` / `Node`** | 932 | `Base` (`149`), `Pill` (`144`), `Flag` (`119`), `Catalog` (`110`), `Popup` (`100`), `Menu` (`82`), `Info` (`76`), `Pass` (`58`), `Badge` (`48`), `Record` (`47`), `Context` (`37`), `Palette` (`23`), `Data` (`21`), `Make*` (`21`), `Node` (`17`) | `Primary`, `CapsuleButton`, `DirectiveMask`, `Repository`, `Sheet`, `DirectiveSheet`, `Metrics`, `Rasterization`, `Indicator`, `RecordEntry`, `Session`, `ColorCodex`, `Payload`, `Construct*`, `Slot` |
