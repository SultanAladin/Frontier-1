# 🧩 `Engine/Shaders/Sketcher/`

> **TL;DR**: Shared CAD stroke expansion: homogeneous edge clipping, the six-corner pixel-width quad and its coverage ramp. (8 source/header/shader files under `Engine/Shaders/Sketcher/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SketcherCadStroke.slang` | Shared CAD stroke expansion: homogeneous edge clipping, the six-corner pixel-width quad and its coverage ramp. |
| `SketcherEdge.slang` | B-rep edges as antialiased pixel-width strokes (six corners per instanced segment); the colour programs blend the |
| `SketcherGizmo.slang` | Transform gizmo handles (Slate GizmoPRO): CPU-built world triangles, lit cones / cylinders, flat arcs, quads and ring; |
| `SketcherGrid.slang` | Analytic grid on the active drawing plane (adaptive decades, world-axis colours), depth-tested behind the model |
| `SketcherImagePlane.slang` | Image planes: a blueprint crop on a world parallelogram, depth-tested behind the model, line art drawn as light ink |
| `SketcherMatCapLookup.slang` | Shared MatCap lookup: a camera-space normal → lit-sphere texture coordinates (Frontier MatCapSurface, Sketcher surfaces). |
| `SketcherProjection.slang` | Shared push-constant ABI, selection-state bits and palette of the Sketcher viewport programs |
| `SketcherSurface.slang` | B-rep face shading (analytic Studio or a matcap layer per face) with selection tints and the R32 identity output |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `SketcherCadStroke.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherEdge.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherGizmo.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherGrid.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherImagePlane.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherMatCapLookup.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherProjection.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SketcherSurface.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
