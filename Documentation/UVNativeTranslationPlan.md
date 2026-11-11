# Frontier Native UV Studio Translation Plan

## Decision

| Approach | Native integration | Long-term cost | Visual parity | Rank |
| -------- | ------------------ | -------------- | ------------- | ---- |
| **Reimplement behavior in C++20 and Slang** | 🔴 Highest | ✔️ Lowest | 🔴 Highest | 🥇 |
| Embed a JavaScript runtime | 🚩 Bridging required | 🔴 Highest | 🚩 Browser-only | 🥉 |
| Keep browser studio beside native editor | ✔️ No bridge | 🚩 Duplicate documents | 🚩 Split workflow | 🥈 |

Translate the reference studio's behavior, not its JavaScript syntax. The native document is authoritative; both UV and perspective views read that document. Browser files remain reference-only and are not copied into `Engine/`.

## Prerequisites

| Prerequisite | Purpose | Current status |
| ------------ | ------- | -------------- |
| CMake 3.21+ and C++20 compiler | Editor, Development, Shipping targets | GCC 12 available; CMake installed in an ignored local build environment |
| Python 3.9+ | Reproducible dependency preparation | Available |
| `python3 Automation/PrepareDependencies.py` | Pinned ImGui, GLFW, Vulkan, volk, stb, toml++ | Required before full configure |
| `python3 Automation/PrepareShaderToolchain.py` | Slang compiler and SwiftShader validation | Slang generation passes; SPIR-V still requires downstream glslang/`spirv-opt` libraries absent from the pinned wheel |
| `Engine/GeometricRaster/` device interface | Portable GPU resource and draw execution | Phase 3 dependency; do not bypass it |
| `Engine/Workspaces/Revision/` | Non-linear undo/redo document history | Required before interactive editing milestone |
| Example OBJ surfaces and matcap images | Import, multi-material and multi-UDIM validation | User-supplied production assets still needed |

## Native Boundaries

| Concern | Destination | Responsibility |
| ------- | ----------- | -------------- |
| Corner topology | `Engine/UVSpace/UVSurface` | Position index plus independent UV index per corner; seams and face-material ranges |
| Surface appearance | `Engine/GeometricRaster/SurfaceShading` | Matcap layers, normalized Blinn–Phong EON lighting, PCF visibility reference |
| Parameterization | `Engine/UVSpace/ChartParameterizer` | Seam charts, pins, sparse LSCM and analytic projections |
| Packing | `Engine/UVSpace/UVChartPacker` | UDIM placement, importance budgets, texel-density normalization |
| UV viewport | `Engine/Editor/Viewports/` | GPU UV lines, selection, seams, UDIM grid through `.slang` rasterization units |
| Perspective viewport | `Engine/Editor/Viewports/` | Shared surface preview, picking, GPU seam and selection overlays |
| Controls | `Engine/Components/` and `Engine/Editor/` | Tool ribbon, face material inspector, numeric transforms, UDIM controls |
| Interchange | `Engine/Interchange/` | OBJ import/export with independent position/UV indices and material identifiers |

## Appearance Contract

Each face owns a range in a packed material-layer array. A range may reference several matcap images, each with weight, view-normal scale/bias and tint. Faces do not share mutable layer arrays accidentally; assignment replaces only the destination face range.

The initial lighting path is intentionally small:

1. Transform the interpolated normal into view space and sample every face matcap layer.
2. Combine weighted matcap radiance with the diffuse directional term.
3. Evaluate normalized Blinn–Phong EON specular using `(n + 8) / (8π) * pow(N·H, n)`.
4. Multiply direct lighting by directional shadow visibility.
5. Add unshadowed ambient/matcap fill and apply display transfer elsewhere in the render sequence.

The first shadow implementation is one directional depth texture with configurable texel bias and 3×3 PCF. It is not presented as area shadows, cascaded shadows or a general many-light renderer.

## Delivery Sequence

| Milestone | Work | Acceptance |
| --------- | ---- | ---------- |
| M1 — Surface foundation | UV document, UDIM, seams, face layers, selection transforms, CPU shading reference | Headless numeric and visual validation |
| M2 — Geometry editing | Selection expansion, weld/split/stitch, seams, pins, transforms, history | Topology and undo/redo regressions |
| M3 — Unwrap | LSCM sparse PCG, projections, flip-guarded relaxation | Area/angle distortion and flipped-corner checks |
| M4 — UDIM packing | Importance budgets, exact chart occupancy, overflow, density normalization | Zero overlaps and repeatable placement |
| M5 — GPU preview | Matcap array descriptors, EON surface shader, directional PCF | CPU/Slang parity and SwiftShader images |
| M6 — Native workspace | Docked controls, GPU 2D/3D views, picking, shortcuts | Shared-selection and visual interaction validation |
| M7 — Interchange and scale | OBJ, SVG, budgets, profiling and cancellation | Round-trip and large-surface performance budgets |

## Immediate Invariants

- No per-face single-material restriction and no fixed matcap-count limit in the document.
- UV coordinates are corner-indexed so one geometric position can cross seams and UDIM boundaries.
- Rendering never changes UV coordinates; UV editing never silently changes geometric positions.
- Viewport overlays use GPU `.slang`; ImGui remains limited to controls and inspector chrome.
- CPU rasterization is restricted to deterministic `Exhibits/` validation images.
- No raw Vulkan execution inside UV editor code.
- No JavaScript runtime or Three.js dependency in native engine targets.
