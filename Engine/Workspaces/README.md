# 🧩 `Engine/Workspaces/` — Authoring Workspaces & Multi-Document Infrastructure

> **TL;DR**: `Engine/Workspaces/` hosts Frontier's multi-document session infrastructure (`WorkspaceCapabilityRegister`, `WorkspaceDocument`, `WorkspaceExchange`, `WorkspaceExtension`, `WorkspaceRegistry`, `WorkspaceToken`), branching DAG revision (`Revision/`), and 7 native C++20 / `.slang` domain workspace modules (`Frontier` World Editor lives in `Engine/Lifecycle/`).

---

## 🏷️ 1. Registered Host Workspaces (`WorkspaceCapabilityRegister`)

| Target Enum | Executable (`Engine/Lifecycle/`) | Host / Source Directory | Domain & Capabilities | Detailed Docs |
| :--- | :--- | :--- | :--- | :--- |
| `FrontierWorld` | `Frontier` (`FrontierHostLaunch.cpp`) / `FrontierEditor` | `Engine/Lifecycle/EditorLifecycle.*` | World Editor: Default & Blank World Scenes, Direct MatCap & OpenPBR World Rendering, `TransformGizmo`, `ContentBrowser` | [Engine/Lifecycle/README.md](../Lifecycle/README.md) |
| `UVWorkspace` | `FrontierUV` | `Engine/Workspaces/UV/` | Dual 2D UDIM & 3D Surface GPU viewports, LSCM/ABF++ unwrap, GPU relax, concave packing | [UV/README.md](UV/README.md) |
| `PatternWorkspace` | `FrontierPattern` | `Engine/Workspaces/Texturing/` | Seamless 2D vector stack, 8 GPU modifiers, 22 OpenPBR channels | [Texturing/README.md](Texturing/README.md) |
| `StampWorkspace` | `FrontierStamp` | `Engine/Workspaces/Texturing/` | Alpha cutout vector contours, procedural masks, typography, OpenPBR relief | [Texturing/README.md](Texturing/README.md) |
| `BakingWorkspace` | `FrontierBaking` | `Engine/Workspaces/Texturing/` | Pure-GPU GTX BVH4 & RTX RayQuery 17-target PBR surface baker | [Texturing/README.md](Texturing/README.md) |
| `FractureWorkspace` | `FrontierFracture` | `Engine/Workspaces/Fracture/` | 6-solver brittle/ductile partitioning, fractography & JoltPhysics v5.6.0 | [Fracture/README.md](Fracture/README.md) |
| `SketcherWorkspace` | `FrontierSketcher` | `Engine/Workspaces/ParametricSketcher/` | Exact NURBS / B-rep CAD, 2D constraints, Vulkan matcaps, STEP/STL & ISO 2D drawing sheets | [ParametricSketcher/README.md](ParametricSketcher/README.md) |
| `MaterialWorkspace` | `FrontierMaterial` | `Engine/Workspaces/Material/` | Dynamic OpenPBR recipes, procedural & transition layer stack, `ContentBrowser` thumbnails | [Material/README.md](Material/README.md) |
| `ComponentEditor` | Hosted inside `FrontierEditor` / `DesktopWorkspaceLaunch` | `Engine/Workspaces/ComponentEditor/` | Modular actor/vehicle class assembly, constraints, lights, response curves & Vulkan preview | [ComponentEditor/README.md](ComponentEditor/README.md) |

Supporting workspace module: [Revision/README.md](Revision/README.md) (`ContentRegion`, `NonLinearHistory`, `OperationRecorder`, `CursorResolution`, `RevisionTracker`, `RevisionExtension`).

---

## 🧩 2. Shared Root Infrastructure (`Engine/Workspaces/*.{h,cpp}`)

| File | Role |
| :--- | :--- |
| `WorkspaceToken.h` | Stale-detecting generation-checked token identifying open workspace documents |
| `WorkspaceDocument.{h,cpp}` | Base document container binding `RevisionTracker`, viewport projection & dirty state |
| `WorkspaceRegistry.{h,cpp}` | Active document slot table, lookup & cross-tab focus tracking |
| `WorkspaceExchange.{h,cpp}` | Lock-free triple-buffered / queued command exchange between UI & `WorkspaceThread` |
| `WorkspaceExtension.{h,cpp}` | Designated lifecycle participant (`Init` → `Tick` → `Shutdown`) coordinating workspaces |
| `WorkspaceCapabilityRegister.{h,cpp}` | Authoritative list of active host workspace targets, launch verbs & capabilities |

---

## 🔍 3. Repository Naming-Violation Audit Census (Read-Only)

⚠️ **Audit Policy**: All counts below come from a read-only static scan against [Documentation/BannedWordsVocabulary.md](../../Documentation/BannedWordsVocabulary.md) (`*.{h,hpp,c,cpp,cc}`). C++ sources were **not** modified; items flagged 🔴 require scheduled refactoring. Because the `K`-word suffix is strictly prohibited in both code and documentation prose, offending identifiers using that suffix are denoted `<K-suffix>` throughout this audit.

| Module | Files | Prohibited `<K-suffix>` Hits | Top Banned-Vocabulary Violations (Read-Only Counts) | Audit Status |
| :--- | :---: | :---: | :--- | :---: |
| **`Workspaces/` Root** | 11 | 0 | `Record` (`9`, via `RecordEntry`/`RecordToken` composites) | 🟢 |
| **`UV/`** | 29 | 17 | `Frame` (`160`), `Record` (`129`), `Value` (`73`), `Popup` (`54`), `Card` (`50`), `Pill` (`48`), `Palette` (`44`), `Base` (`43`), `Is/Has` (`36`), `Get/Set` (`33`) | 🔴 |
| **`Texturing/`** | 25 | 3 | `Base` (`51`), `Get/Set` (`50`), `Is/Has` (`22`), `Mesh` (`17`), `Map` (`16`), `Flag` (`13`), `Frame` (`12`) | 🔴 |
| **`Fracture/`** | 13 | 11 | `Draw*` (`199`), `Card` (`120`), `Glyph` (`63`), `Is/Has` (`61`), `Item` (`53`), `Frame` (`40`), `Badge` (`34`), `Flag` (`33`), `Pill` (`31`), `Base` (`25`) | 🔴 |
| **`ParametricSketcher/`** | 56 | 1732 | `State` (`7232`), `<K-suffix>` (`1732`), `Frame` (`1239`), `Value` (`573`), `Draw*` (`512`), `Stage` (`449`), `Map` (`374`), `Get/Set` (`320`), `Card` (`265`), `Item` (`215`) | 🔴 |
| **`Material/`** | 8 | 0 | `Badge` (`13`), `Base` (`12`), `Get/Set` (`10`), `Flag` (`5`), `Map` (`4`) | 🚩 |
| **`ComponentEditor/`** | 14 | 41 | `<K-suffix>` (`41`), `Catalog` (`28`), `Draw*` (`13`), `Is/Has` (`11`), `Get/Set` (`10`), `Data` (`10`), `Frame` (`8`) | 🔴 |
| **`Revision/`** | 12 | 0 | `Record` (`162`, mostly `RecordToken`/`RecordEntry`), `Base` (`33`) | 🚩 |
| **`Editor/Explorer/`** | 4 | 0 | `Get/Set` (`17`), `Palette` (`15`), `Data` (`13`), `Item` (`6`), `Frame` (`6`) | 🚩 |
| **`Lifecycle/`** | 23 | 0 | `Frame` (`76`), `Record` (`66`), `Context` (`22`), `Is/Has` (`21`), `Get/Set` (`14`), `Pill` (`14`), `Item` (`11`), `Map` (`11`) | 🚩 |
