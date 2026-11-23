# 🧩 `Engine/GeometrySpace/`

> **TL;DR**: Subsystem umbrella grouping 6 child modules under `Engine/GeometrySpace/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Analysis/`](Analysis/README.md) | Discrete Meyer cotangent mean & angle-deficit Gaussian curvature plus Mikkelsen/Duff continuous corner tangent bases |
| [`Fracture/`](Fracture/README.md) | Exact 3D convex polyhedron half-space clipping, PolygonNetwork bridges, 6-solver fracture dispatch & shared-cut bonds |
| [`Network/`](Network/README.md) | PriorityAllocationLayer & UVRevision-stamped FacePartitionSpan enabling O(1) GPU layer selection and sub-0.05ms CPU spans |
| [`Parametric/`](Parametric/README.md) | 175 source/shader files |
| [`Primitives/`](Primitives/README.md) | Parametric solid builders and 5 multi-part studio sample PolygonNetwork constructors with clean UDIM UVs and FacePartitionRegister |
| [`Selection/`](Selection/README.md) | Packed bitmask selection state across PolygonNetwork Vertices, DirectedEdges, and Faces with grow/shrink/invert operations |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
