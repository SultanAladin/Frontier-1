# Viewer Redesign Brief — Standing Instructions

🔴 Read this before any work on the Component Viewer, Asset Viewer, Geometry Compiler or their cards.
It records the owner's standing instructions so they are never asked for twice. It does not replace `CLAUDE.md`
or the four mandatory skills; it adds the rules for this redesign track.

## Standing instructions (owner, 2026-11-16)

| #  | Rule                                                                                                          |
|----|---------------------------------------------------------------------------------------------------------------|
| 1  | 🔴 Frontier is **not Unreal Engine**. Do not copy UI wording, layout signatures or concepts from any engine.  |
| 2  | 🔴 All names must be unique and inventive, built with `SKILL-Naming.md` §0. **The owner approves every name** before it ships. |
| 3  | 🔴 Every prototype is built **separately and standalone** first. Only after owner approval is it integrated into the Component Viewer or Asset Viewer. |
| 4  | Cards should be **interactive infographic UI** (recommended), sliders, or other inventive controls besides sliders. |
| 5  | UI inspiration comes from `SourceImages/General UI/` (also on Frontier-OLD, table below).                     |
| 6  | Always render the HTML and **show actual screenshots** of the work; keep them in `Exhibits/ViewerRedesign/`.   |
| 7  | The UI needs cleaning up, adjustment and redesign, one approved step at a time.                              |
| 8  | Keep this brief and the repository registry current so the owner does not repeat instructions.               |

## Repository registry

| Repository                                | Branch / path                                     | Holds                                                         |
|-------------------------------------------|---------------------------------------------------|---------------------------------------------------------------|
| `eosclient0001-rgb/Frontier`              | working repository                                | This checkout; target for all redesign work                   |
| `SultanAladin/Frontier`                   | `arena/19265b6e-frontier`, commit `1fef98c8`      | Source of the Component Viewer + Asset Viewer prototypes      |
| `streamlinkinbox/Frontier-OLD`            | `main`, `SourceImages/General UI/`                | UI inspiration images (identical copy in `SourceImages/General UI/`) |
| `SultanAladin/Frontier-1`                 | `arena/453e172d-frontier-1`, `Experimental/ClusterLodStudio` | Origin of the cluster tier, SDF and UV engine files  |
| `streamlinkinbox/Frontier-OLD`            | `arena/d96da802-frontier`, `Experimental Remastered/AssetInspector` | Origin of Vehicle Lab                        |

Clone the prototype source again with:

```bash
git clone --branch arena/19265b6e-frontier https://github.com/SultanAladin/Frontier.git /tmp/sultan
```

## What was imported (2026-11-16)

All 110 files of commit `1fef98c8` were copied unchanged into `Experimental/`:
`ComponentViewer/`, `GeometryCompiler/`, `Shared/`, `FrontierWorldMirror/`, `FractureEditor/`, `StudioIconSet/`.

## Running

| Server                     | Command                                                     | Opens                                         |
|----------------------------|-------------------------------------------------------------|-----------------------------------------------|
| Component Viewer           | `python3 Experimental/ComponentViewer/Serve.py 8095`        | `/Experimental/ComponentViewer/`              |
| Asset Viewer               | `python3 Experimental/ComponentViewer/ServeAssetViewer.py 8096` | `/Experimental/ComponentViewer/AssetViewer.html` |
| Prototype gallery          | either server above                                         | `/Experimental/ComponentViewer/Prototypes/`   |

Both servers bind `0.0.0.0` and serve the repository root, so every prototype is reachable from either port.

## Prototype workflow

```text
① propose names (table, two options each)  →  owner approves
② build standalone prototype in Experimental/ComponentViewer/Prototypes/<Name>/
③ capture screenshots → Exhibits/ViewerRedesign/Images/
④ owner approves the prototype
⑤ integrate into Component Viewer / Asset Viewer, rerun smoke tests, capture again
```

Prototype status lives in `Experimental/ComponentViewer/Prototypes/README.md`.

## Verification commands

```bash
node Experimental/ComponentViewer/Scripts/CheckComponentViewer.mjs
npm install --prefix /tmp/smoke jsdom@25
JSDOM_DIR=/tmp/smoke node Experimental/ComponentViewer/Scripts/SmokeComponentViewer.mjs
JSDOM_DIR=/tmp/smoke node Experimental/ComponentViewer/Scripts/SmokeAssetViewer.mjs
JSDOM_DIR=/tmp/smoke node Experimental/GeometryCompiler/Scripts/SmokeGeometryCompile.mjs
```

Screenshot capture setup is in `Exhibits/ViewerRedesign/README.md`.

## Engine-borrowed vocabulary to replace (pending owner approval)

| #  | Current term                          | Option A                      | Option B                    |
|----|---------------------------------------|-------------------------------|-----------------------------|
| 1  | Component Viewer (app)                | Assembly Bench                | Part Lattice                |
| 2  | Asset Viewer (app)                    | Specimen Bench                | Survey Bench                |
| 3  | Blueprint class · `BP_` prefix        | Schematic · `SC_`             | Archetype · `AT_`           |
| 4  | Parent class Pawn / Actor             | Pilotable / Placeable         | Steerable / Anchored        |
| 5  | Components panel                      | Assembly Directory            | Part Directory              |
| 6  | Component                             | Part                          | Member                      |
| 7  | Static Mesh component                 | Rigid Surface                 | Polygon Surface             |
| 8  | Construction Script                   | Assembly Sequence             | Build Sequence              |
| 9  | Details / Inspector panel             | Specification                 | Property Sheet              |
| 10 | Content tab / Asset Browser           | Content Directory             | Part Shelf                  |
| 11 | Simulate                              | Motion Preview                | Live Drive                  |
| 12 | Compile (class)                       | Finalize                      | Seal                        |
| 13 | Geometry Compile (import page)        | Geometry Intake               | Intake Sequence             |
| 14 | Cluster LOD / "Nanite-style"          | Cluster Tiers                 | Detail Tiers                |
| 15 | Socket                                | Joint                         | Mount                       |
| 16 | Instance (empty placeholder)          | Vacancy                       | Empty Mount                 |
| 17 | Actor components (section)            | Behaviour Parts               | Logic Parts                 |
| 18 | Vehicle Movement / Rotating Movement  | Drivetrain Model / Spin Drive | Traction Solver / Rotor Drive |

📝 Content Directory (row 10, option A) is already mandated by `CLAUDE.md`. Origin, Viewport and Pivot are kept.
Record the owner's choices here, then rename the UI text first and identifiers second.
