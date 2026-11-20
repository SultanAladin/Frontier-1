# 🧩 `Engine/Workspaces/Texturing/` — Pattern, Stamp & Baking Studios

> **TL;DR**: `Engine/Workspaces/Texturing/` hosts three workspaces: **`PatternWorkspace`** (`FrontierPattern`), **`StampWorkspace`** (`FrontierStamp`) sharing the ThorVG/Vulkan 2D vector stack + 8 GPU modifiers + 22 OpenPBR channels, and **`BakingWorkspace`** (`FrontierBaking`), a pure-GPU GTX BVH4 & RTX RayQuery 18-target PBR & 3D Volume SDF baker.

---

## 🧩 1. Module Architecture

| Workspace | Files | Core Capabilities |
| :--- | :--- | :--- |
| **`PatternWorkspace` & `StampWorkspace`** | `PatternStampWorkspaceHost.{h,cpp}`, `PatternWorkspaceHost.h`, `StampWorkspaceHost.h`, `PatternStampStackSession.{h,cpp}`, `PatternStampToolHeaderPane.{h,cpp}`, `PatternStampLayerStackPane.{h,cpp}`, `PatternStampInspectorPane.{h,cpp}`, `VectorViewportInteractor.{h,cpp}` | Seamless 2D vector authoring (`Pattern`), alpha-cutout decal/stencil authoring (`Stamp`), SVG clipboard import, 8 GPU modifiers, 22 OpenPBR output channels, 2×2 seamless tiling preview |
| **`BakingWorkspace`** | `BakingWorkspaceHost.{h,cpp}`, `BakingWorkspaceSession.{h,cpp}`, `BakingTargetsPane.{h,cpp}`, `BakingViewportPane.{h,cpp}`, `BakingInspectorPane.{h,cpp}` | High-to-low cage raytracing (`GTX BVH4` / `RTX RayQuery`), 18 PBR baked targets + Float32 `VolumeSdf`, 2D/3D/High-Wire/Cage viewport inspection, edge dilation padding |
| **Shared Appearance** | `TexturingPaneAppearance.h` | Shared dark/light theme tokens, header/footer helpers |

---

## 🏷️ 2. UI Controls

### 2.1 `PatternWorkspace` & `StampWorkspace` Controls

| Pane | Controls & Sections | Function |
| :--- | :--- | :--- |
| **`PatternStampToolHeaderPane`** | **Band 1**: Editable Document Title, `Presets` dropdown pill, Resolution combo | Rename document, load starter presets, set texture resolution |
| **`PatternStampToolHeaderPane`** | **Band 2 + Tool Rail**: 15 vector tools (`Select`, `Curve`, `Pen`, `EditPoints`, `PointGradient`, `Square`, `Circle`, `Diamond`, `Triangle`, `Star`, `Hexagon`, `Stripe`, `TextBlock`, `HandPan`), `Snap` sheet, `View` sheet, `Save` sheet, `More Tools` sheet | Author shapes/contours/text/gradients, toggle snapping & 2×2 seamless preview |
| **`PatternStampLayerStackPane`** | Layer hierarchy tree, visibility/lock toggles, blend mode, reorder, add/delete/duplicate | Manage vector motif layers, cutouts, and groups |
| **`PatternStampInspectorPane`** | Fill/Stroke OpenPBR properties, 8 GPU modifier cards, Roughness / Height / Relief / Channel routing (22 OpenPBR channels) | Configure procedural & material channel outputs |

### 2.2 `BakingWorkspace` Controls

| Pane | Controls & Sections | Function |
| :--- | :--- | :--- |
| **`BakingTargetsPane`** | 4 quick-select preset chips + 2-column categorized 18-target swatch grid (`VolumeSdf`, `TangentNormal`, `WorldNormal`, `ModelSpaceNormal`, `BentNormal`, `BevelNormal`, `AmbientOcclusion`, `SecondaryAo`, `Curvature`, `Convexity`, `Concavity`, `Thickness`, `HeightDisplacement`, `TopDownDust`, `UvIslandId`, `PartMaterialId`, `WorldPosition`, plus readiness state `SDF OK`/`SDF`) | Select & inspect individual bake targets (`GTX BVH4 / RTX RayQuery`, `High -> Low Cage`) |
| **`BakingViewportPane`** | `Bake Selected Maps` · `Send to Paint` · `High-Wire` · `Cage` toggles · Target preview mode (3D Specimen / 2D Map / Split) | Trigger GPU bake, inspect high-poly wireframe & cage offset envelope |
| **`BakingInspectorPane`** | **Surface Section**: `AO / Bent Samples` (`4–36 ray`), `Front Cage Offset` (`0.02–0.80 m`), `Rear Cage Offset` (`0.02–0.80 m`), `AO Max Ray Distance` (`0.10–2.00 m`), `UV Edge Padding` (`0–14 px`) | Configure raytracing & UV dilation |
| **`BakingInspectorPane`** | **Volume Section**: `Bounds Padding` (`0.05–0.45`), `Slice Depth` (`0.00–1.00`) | Configure 3D Float32 SDF bounds & interactive slice plane |
| **`BakingInspectorPane`** | **Export Section**: Format/output path & dilation export (`Edge Dilation Padding Included`) | Export baked maps & `.ImageSpace`/`.TextureSpace` assets |

---

## 📝 3. Hotkeys & Mouse Controls

### 3.1 `PatternWorkspace` & `StampWorkspace` (`VectorViewportInteractor`)

| Shortcut | Action | Notes |
| :--- | :--- | :--- |
| `V` | `Select` tool | Single-key tool switch |
| `U` / `P` / `A` | `Curve` / `Pen` / `EditPoints` tool | Path & control-point authoring |
| `F` | `PointGradient` tool | Interactive gradient handles |
| `R` / `O` / `D` / `T` / `S` / `G` | `Square` / `Circle` / `Diamond` / `Triangle` / `Star` / `Hexagon` | Parametric shape creation |
| `L` / `X` / `H` | `Stripe` / `TextBlock` / `HandPan` | Pattern stripes, typography, canvas pan |
| `M` or `Shift+G` | Modal Move (`Grab`) | `X`/`Y` axis lock, numeric entry, `Enter` commit, `Esc`/`RMB` cancel |
| `Shift+R` / `Shift+S` | Modal Rotate / Modal Scale | Numeric entry, `Shift`/`Alt` constraints |
| `1` / `2` / `3` | Channel preview: `VectorSVG` / `Roughness` / `Seamless2x2` | Instant channel switch |
| `Z` / `Ctrl+0` | Reset zoom / Fit canvas (`100%`) | 2D viewport |
| `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` | Undo / Redo | History |
| `Ctrl+D` / `Ctrl+V` | Duplicate selection / Paste SVG from clipboard | Clipboard & cloning |
| `Del` / `Backspace` | Delete selected vector layer or point | — |
| `Enter` / `Esc` | Commit active path or modal transform / Cancel or clear selection | — |
| `Space+LMB` or `MMB` Drag / `Wheel` | Pan 2D canvas / Cursor-centered zoom | Mouse navigation |

### 3.2 `BakingWorkspace` (`BakingViewportPane`)

| Input | Action |
| :--- | :--- |
| `R` | Reset 3D preview camera |
| `Q` | Toggle viewport inspection mode |
| `LMB` Drag | Orbit 3D specimen |
| `Shift+LMB` or `MMB` Drag | Pan 3D camera |
| `Mouse Wheel` | Zoom / dolly camera |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `25` files under `Engine/Workspaces/Texturing/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | 3 | `BakingWorkspaceSession.cpp:92,204,638` (`ToStudy<K-suffix>`) | `ToStudyCategory` / `ToBakerTarget` |
| **`Base` / `Get*`/`Set*` / `Is*`/`Has*`** | 123 | `Base` (`51`), `Get/Set` (`50`), `Is/Has` (`22`) across `PatternStampStackSession.*`, `BakingWorkspaceSession.*` | `Primary`, `Query*`/`Assign*`, direct boolean names |
| **`Mesh` / `Map` / `Flag` / `Frame`** | 58 | `Mesh` (`17`), `Map` (`16`), `Flag` (`13`), `Frame` (`12`) | `PolygonNetwork`, `Register`/`Lookup`, `DirectiveMask`, `Step`/`Extent` |
| **`Record` / `Badge` / `Data` / `Context` / `Menu` / `Stage`** | 26 | `Record` (`7`), `Badge` (`7`), `Data` (`4`), `Context` (`4`), `Menu` (`3`), `Stage` (`1`) | `RecordEntry`, `Indicator`, `Payload`, `Session`, `DirectiveSheet`, `Sequence` |
