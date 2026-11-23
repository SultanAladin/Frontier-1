# Native C++20 & `.slang` UV Workspace Architecture Plan

> **TL;DR:** Implement the UV Workspace natively in **ISO C++20 + GPU `.slang`** across four ordered milestones: **① `EngineConfiguration` (`HostLaunchMode::FeatureSet` + `StandaloneWorkspace`) & `PolygonNetwork` / `GeometryInstance` Structures** → **② GPU `.slang` Viewport Rasterization, Unified 3D/2D `TransformGizmo` (Slate GizmoPRO), GPU Picking & Reusable `ComputeDispatchEncoder` + `TimelineTransferRegister`** → **③ Modular Frontier-Styled UV Editor Panes** → **④ Parallel GPU/`WorkspaceThread` UV Solvers, `.UVSpace` Codec & `215/750 MiB` Parametric Revision**. Uses **`Axis3D` / `Axis2D`** with zero-cost **`Scalar3D` / `Scalar2D`** `using` aliases alongside a dedicated **`UVAxes`** (`{ U, V }`) struct.

---

## 1. Naming & Structural Decisions (Locked Vocabulary)

### 1.1 Polygon Container Selection (Avoiding Nanite `Cluster` Clash & Banned Vocabulary)

*(Ratings — Connectivity Clarity / Zero Nanite Clash / Naming Compliance: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest/barred)*

| Rank | Candidate | Connectivity Clarity | Zero Nanite LOD Clash | Naming Compliance | Decision |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **`PolygonNetwork` (`Vertex` · `DirectedEdge` · `Face`)** | 🔴 | 🔴 *(leaves `*Cluster` free for virtualized micropolygon LOD)* | 🔴 *(matches `ClosureNetwork` & `NetworkingSpace`)* | **Selected** |
| 🥈 | **`FaceNetwork` (`Vertex` · `DirectedEdge` · `Face`)** | 🔴 | 🔴 | 🔴 | Clean runner-up, slightly less explicit about n-gon polygons. |
| 🥉 | **`PolygonCluster`** | 🚩 | ✔️ *(clashes with Nanite-style virtualized triangle/micropolygon clusters & `ExecutionCluster`)* | 🚩 | Rejected due to virtualized LOD cluster collision. |
| — | **`PolygonGraph` / `PolygonAssembly`** | 🔴 | 🔴 | 🔴 *(barred: `Graph` & `Assembly` are in `BannedWordsVocabulary.md`)* | Rejected (banned vocabulary). |

💡 **Locked Rulings:**
- **`PolygonNetwork`** (`Engine/GeometrySpace/Network/PolygonNetwork.{h,cpp}`) owns `Vertices`, `Edges` (`DirectedEdge`), and `Faces`, leaving `*Cluster` (`GeometryCluster`, `ClusterDAG`) completely unencumbered for Frontier's virtualized micropolygon LOD renderer.
- **`HostLaunchMode::FeatureSet`** replaces `BundledSuite` in `Engine/Configuration/EngineConfiguration.h`.
- **`Axis3D` & `Axis2D` + Zero-Cost `Scalar3D` & `Scalar2D` `using` Aliases + Dedicated `UVAxes`:**
  - *(Note: C++20 & `.slang` lexers forbid identifiers starting with a digit like `3DAxis`, so `3D`/`2D` sits at the end: `Axis3D`, `Axis2D`, `Scalar3D`, `Scalar2D`.)*
  - **`Axis3D`** (`{ X, Y, Z }`) & **`Axis2D`** (`{ X, Y, Z }` / `{ X, Y }`) represent spatial coordinates, normals, and Euler axis angles.
  - **`using Scalar3D = Axis3D;`** & **`using Scalar2D = Axis2D;`** alias the exact same structs at zero runtime cost for scale multipliers, dimensions, and linear RGB channels (`Scalar3D Scale`, `Scalar3D AlbedoLinearRGB`).
  - **`UVAxes`** (`{ float U, float V }`) remains its own **separate struct** so texture parameter coordinates always use `.U` / `.V` explicitly.
- **`Face` (`Faces`)** and **`Vertex` (`Vertices`)** are used everywhere (`Facet`, `Complex`, `Topology`, `Assembly`, `Mesh`, `Graph`, `Adjacency`, `LeavingEdge`, and kinship terms are barred).

### 1.2 Pointer vs. Slot-Index Links Inside `PolygonNetwork`

*(Ratings — `std::vector` Growth Safety / `.Space` Serialization / Traversal Speed: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest)*

| Rank | Link Storage Strategy | `std::vector` Growth Safety | Undo / `.Space` Serialization | Traversal Speed | Summary |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — `uint32_t` Slot Indices + Typed Accessors (`ResolveOrigin`, `ResolveOpposite`)** | 🔴 | 🔴 (`64 B` memcpy-ready) | 🔴 (`12 B` vs `24 B` links) | Appending `Vertices`/`Edges`/`Faces` during seam splits never dangles pointers; `InvalidIndex = 0xFFFFFFFFu`. |
| 🥈 | **Option B — Pointers Pinned in Non-Moving `RegionAllocator` Blocks** | 🚩 | ✔️ (requires pointer rebasing) | 🔴 | Direct pointer syntax (`DirectedEdge* OutgoingEdge`), requires rebase table on serialize/clone. |

---

## 2. Why HTML Layer Selection Felt Slow — & How Native C++ + GPU Makes It `< 0.05 ms`

### 2.1 Root-Cause Diagnosis of `Experimental/UVEditor` HTML Layer Click Latency

| Bottleneck in HTML Prototype | What Actually Happened on Every Layer Click | Cost | Native C++20 + `.slang` Elimination |
| :--- | :--- | :---: | :--- |
| **① `ComputeInspectorSelectionInfo()`** | Iterated **every selected face** on CPU computing UV signed area, 3D triangle stretch, and allocating JS `Set`s synchronously inside the click callback. | `4–18 ms` | **Eliminated:** Stretch/area totals are pre-reduced on GPU via `UVDistortionInspection.slang` + cached per `FacePartitionSpan` (`UVRevision`-gated). |
| **② Full DOM `RebuildInspector()`** | Destroyed and recreated the entire right-hand Inspector widget tree on every click. | `6–22 ms` | **Eliminated:** ImGui `UVInspectorPanel` reads bound `PriorityAllocationLayer` scalars directly in `< 0.02 ms` with zero allocation. |
| **③ Full RGBA Selection Texture Rewrite** | Zeroed and re-uploaded the entire `W × H × 4` RGBA texture + rebuilt edge/vertex mask arrays across *all* triangles over WebGL. | `3–12 ms` | **Eliminated:** Each `Face` carries `PriorityLayerOrdinal` in VRAM; clicking a layer either pushes a **4-byte `ActiveLayerOrdinal` push-constant** or dispatches `SelectionMaskCompute.slang` (`1 bit/face` `PackedBits` in VRAM, **zero CPU face loop!**). |
| **④ CPU 2D `<canvas>` Redraw** | Re-stroked thousands of 2D polygons on CPU `CanvasRenderingContext2D`. | `5–20 ms` | **Eliminated:** 2D `UVTileCanvas` is **100% GPU `.slang` (`UVTileRasterization.slang`)** sharing the exact same VRAM buffers as the 3D viewport. |

### 2.2 Reusable Modular GPU Compute & Async Readback System (`Engine/GeometricRaster/Compute/`)

Shared across **all** Frontier workspaces (`UV`, `Geometry`, `Texture`, `Particle`, `Fluid`, `Fracture`, `ForceField`) — never hardcoded to UV alone:

```text
EditorThread / WorkspaceThread                 RenderThread (ComputeDispatchEncoder)             GPU (.slang Compute & Raster)
──────────────────────────────                 ─────────────────────────────────────             ─────────────────────────────
EnqueueComputeOperation(Descriptor) ─────────► Encode .slang Dispatch + Storage Buffers ───────► [1] SelectionMaskCompute.slang
  • Target shader & threadgroupXYZ             Signal Vulkan Timeline Semaphore (Ticket N)       [2] ViewportPickReduction.slang
  • PushConstants (64 B)                       Copy compact result -> TimelineTransferRegister[N % 3]   [3] UVDistortionInspection.slang
  • Optional ReadbackByteCount                                                                   [4] StretchRelaxCompute.slang
                                                                                                 [5] ConcaveSkylineCompute.slang
PollCompletedReadback(Ticket N) ◄────────────── Zero CPU stalls (NO vkWaitForFences!) ◄────────── Host-cached 64B-aligned ring slot
  • Updates KPI tiles / Revision baseline        ReadbackTicket completes asynchronously           ready on next completed timeline value
```

| Modular GPU Compute Unit | Workspace Consumers | GPU Workload & Compact Readback Payload |
| :--- | :--- | :--- |
| **`ComputeDispatchEncoder.{h,cpp}`** | **All Workspaces** | Generic `.slang` compute encoder binding storage buffer slots, `64 B` push-constants, memory barriers, and indirect dispatch counts. |
| **`TimelineTransferRegister.{h,cpp}`** | **All Workspaces** | Triple-buffered `HOST_VISIBLE | HOST_CACHED` ring tracked by **Vulkan Timeline Semaphores (`ReadbackTicket`)** — zero render stalls, zero blocking waits. |
| **`SelectionMaskCompute.slang`** | `UV`, `Geometry`, `Fracture`, `Paint` | Evaluates layer click (`FaceLayerOrdinal == LayerId`), box frustum, brush sphere, coplanar/similar tolerances **entirely in VRAM**; writes `FaceSelectionBits` (`1 bit/face`) and reads back `16 B` (`SelectedFaceCount`, `SelectedIslandCount`, `Bounds`). |
| **`UVDistortionInspection.slang`** | `UV`, `Texture` | Parallel per-face angle/area stretch, texel density `[px/m]`, flipped winding, and UDIM spatial-hash overlap count; writes GPU heatmap buffer + `64 B` KPI summary via `TimelineTransferRegister`. |
| **`StretchRelaxCompute.slang`** | `UV` | Parallel Jacobi / Red-Black area-preserving UV spring relaxation in VRAM during interactive brush/Tier 3 slack; reads back `ParameterUV` stream only when recording a revision step in `OperationRecorder`. |
| **`ConcaveSkylineCompute.slang`** | `UV` | Evaluates rotated island upper/lower raster horizons across `0°/90°/45°/15°` candidates in parallel on GPU for `IslandLayoutPacker`. |

---

## 3. Unified 3D & 2D GPU Picking (`Engine/GeometricRaster/Picking/`)

Works identically across `ProjectionViewport` (3D) and `UVTileCanvas` / `VectorCanvas` (2D) for single-click, hover highlight, marquee box, and paint-brush selection:

| Picking Component | Mechanism |
| :--- | :--- |
| **`ViewportPickRasterization.{h,cpp}` + `.slang`** | Rasterizes `R32G32B32A32_UINT` visibility IDs (`R = InstanceToken`, `G = FaceIndex`, `B = DirectedEdgeIndex`, `A = GizmoHandleOrdinal | ComponentTag`) with hardware depth occlusion in 3D and UDIM coordinate projection in 2D. |
| **Hover & Single Click (`1×1` GPU Copy)** | Copies the `16 B` texel under cursor `(CursorX, CursorY)` into `TimelineTransferRegister` (`< 0.02 ms`) — immediately resolves hovered `TransformGizmo` handle, `Face`, `DirectedEdge` seam, or `Vertex` without any CPU ray-triangle loop. |
| **Marquee Box & Paint Brush (`ViewportPickReduction.slang`)** | `8×8` compute workgroups scan the screen-space box or brush disc inside the `R32G32B32A32_UINT` ID target, atomically setting `FaceSelectionBits` / `EdgeSelectionBits` in VRAM so the selection highlight renders **on the very same frame** prior to CPU readback. |

---

## 4. Modular 3D & 2D `TransformGizmo` (`Slate/References/Gizmo.html` — Shared Across All Workspaces)

Implemented **once** in `Engine/Editor/Viewports/TransformGizmo.{h,cpp}` + `Engine/GeometricRaster/Rasterization/GizmoRasterization.{h,cpp}` + `Engine/Shaders/Raster/GizmoRasterization.slang` and reused across **Geometry, UV (3D & 2D), Vector, Texture, Simulation, Photometric, ForceField, Particle, and Vehicle** workspaces:

```text
3D Mode (ProjectionViewport)                         2D Mode (UVTileCanvas / VectorCanvas / Sketch)
────────────────────────────                         ──────────────────────────────────────────────
        +Y (#12D40A)                                         +V (#12D40A)
           ▲  Cone Tip (0.95 L)                                 ▲  Translate Cone Tip (0.95 L)
           █  Scale Cylinder (0.67 L, r=0.06)                   █  Coaxial Scale Cylinder (0.67 L)
           │    ╭─── 31° Flat Arc Bar (Rot Z)                   │    ╭─── 31° Blue Flat Arc Bar (#1560E0, Rot UV)
           │  ┏━┓ XY Plane Quad (#E0CD12, 0.28α                 │  ┏━┓ UV Plane Quad (#E0CD12, 0.28α + 2 outer edges)
          (○)─┗━┛─█──► +X (#E01414)                            (○)─┗━┛─█──► +U (#E01414)
         ╱   White Camera-Billboarded Torus (r=0.16)             White Center Ring (r=0.16, Pivot / Free Drag)
        ▼ +Z (#1560E0) + YZ (#1FC7C7) & XZ (#C81EC8) Quads
```

| GizmoPRO Element (`Slate/References/Gizmo.html`) | Exact Geometry, Colours & Interaction Math |
| :--- | :--- |
| **Axis Colours & Plane Accents** | `+X / +U` = `#E01414` (Red) · `+Y / +V` = `#12D40A` (Green) · `+Z / 2D Rot` = `#1560E0` (Blue) · `YZ` = `#1FC7C7` (Cyan) · `XZ` = `#C81EC8` (Magenta) · `XY / UV` = `#E0CD12` (Yellow) · Center Ring = `#FFFFFF`. |
| **Shaftless Coaxial Axis Handles** | **Translate Cone Tip** (`Radius = 0.06`, `Height = 0.18` at `0.95 L`) + **Inboard Scale Cylinder** (`Radius = 0.06`, `Height = 0.14` at `0.67 L`) — no cluttered full-length axis lines; perspective-normalized in `.slang` so handle scale never distorts with object `Scale` or camera distance. |
| **Dual-Edge Plane-Translate Quads** | Translucent `0.16 × 0.16` quad (`α = 0.28` idle, `α = 0.55` hovered) at `(u + v) * (0.95 L - 0.08)` framed on its **two outer edges** by crisp opaque `L`-lines. |
| **Flat Annular-Sector Rotation Arcs** | Flat curved ribbon (`Radius = 0.589 L ± 0.038`, `Sweep = 31°`) centered on the `45°` bisector between each pair of orthogonal axes (3 arcs in 3D; 1 in-plane arc in 2D). |
| **Camera-Billboarded Center Torus & Snapping** | Thin white torus (`Radius = 0.16`, `Tube = 0.008`) always facing the active camera; skew-line closest-point drag for axes, ray-plane intersection for planes/angles, **`Ctrl` snapping** (`0.25 m` / `0.05 uv` move, `0.1x` scale, `5°` angle), and live delta readout (`X move`, `scale x`, `rotate °`). |
| **Zero-Cost GPU Hover & Pick** | Writes `GizmoHandleOrdinal` (`1..13` in 3D, `1..7` in 2D) directly into `ViewportPickRasterization` channel `A` so hover emissive boost + drag capture require zero CPU mesh-ray intersections. |

---

## 5. Core Geometry & Instance Structures (`Engine/Mathematics/` & `Engine/GeometrySpace/`)

```cpp
namespace Frontier::Mathematics
{

struct Axis3D
{
    float X = 0.0f; // [m | rad] - Primary X-axis coordinate or angle
    float Y = 0.0f; // [m | rad] - Primary Y-axis coordinate or angle
    float Z = 0.0f; // [m | rad] - Primary Z-axis coordinate or angle
};

struct Axis2D
{
    float X = 0.0f; // [m | px]  - Primary horizontal planar coordinate
    float Y = 0.0f; // [m | px]  - Primary vertical planar coordinate
};

// 📝 Zero-overhead C++20 type aliases sharing all SIMD/scalar operators while expressing non-positional magnitudes
using Scalar3D = Axis3D; // [-] - 3-component scale factor, extent, or linear channel triplet
using Scalar2D = Axis2D; // [-] - 2-component scale factor or planar extent

// 📝 Dedicated separate UV struct preserving explicit .U and .V members
struct UVAxes
{
    float U = 0.0f; // [uv] - Horizontal parameter coordinate (UDIM tile U + local [0,1])
    float V = 0.0f; // [uv] - Vertical parameter coordinate   (UDIM tile V + local [0,1])
};

struct SpatialTransform
{
    Axis3D   Position { 0.0f, 0.0f, 0.0f }; // [m]   - Translation along X, Y, Z axes
    Axis3D   Rotation { 0.0f, 0.0f, 0.0f }; // [rad] - Euler orientation around X, Y, Z axes
    Scalar3D Scale    { 1.0f, 1.0f, 1.0f }; // [-]   - Scale multiplier along X, Y, Z axes
};

} // namespace Frontier::Mathematics

namespace Frontier::GeometrySpace
{

enum class EdgeAttributeMask : uint8_t
{
    None              = 0u,      // [-] - Smooth interior edge
    EnableSeamCut     = 1u << 0, // [-] - UV seam split across OppositeEdge
    EnableSharpCrease = 1u << 1, // [-] - Hard shading normal crease
    EnablePinLock     = 1u << 2, // [-] - Origin UV corner locked during ConformalFlattening / StretchRelaxer
};

struct FaceShadingAssignment
{
    Mathematics::Scalar3D AlbedoLinearRGB { 0.30f, 0.49f, 0.79f }; // [0-1] - Per-face linear RGB reflectance
    float                 Metallic        = 0.60f;                 // [0-1] - Metallic response
    float                 Roughness       = 0.24f;                 // [0-1] - Microfacet roughness
    float                 Specular        = 0.68f;                 // [0-1] - Dielectric F0 specular weight
    float                 MatCapBlend     = 0.62f;                 // [0-1] - Blend weight in Lit+MatCap mode
    uint8_t               MatCapSlot      = 5u;                    // [0-15]- Procedural engineering MatCap slot
};

struct Vertex
{
    Mathematics::Axis3D Axes              {};          // [m]   - Exact 3D vertex coordinates
    Mathematics::Axis3D Normal            {};          // [-]   - Smooth or area-weighted vertex normal
    uint32_t            OutgoingEdgeIndex = 0xFFFFFFFFu; // [idx] - First DirectedEdge originating at this Vertex
};

struct DirectedEdge
{
    uint32_t            OriginVertexIndex  = 0xFFFFFFFFu; // [idx]  - Starting Vertex index
    uint32_t            OppositeEdgeIndex  = 0xFFFFFFFFu; // [idx]  - Paired reverse DirectedEdge across shared edge
    uint32_t            NextEdgeIndex      = 0xFFFFFFFFu; // [idx]  - Next DirectedEdge around EnclosingFace loop
    uint32_t            PreviousEdgeIndex  = 0xFFFFFFFFu; // [idx]  - Previous DirectedEdge around EnclosingFace loop
    uint32_t            EnclosingFaceIndex = 0xFFFFFFFFu; // [idx]  - Incident Face bounded by this loop
    Mathematics::UVAxes ParameterUV        {};            // [uv]   - Per-corner UV coordinate at OriginVertex
    EdgeAttributeMask   AttributeMask      = EdgeAttributeMask::None; // [mask] - Seam, sharp-crease & UV-pin directives
};

struct Face
{
    uint32_t              BoundaryLoopEdgeIndex = 0xFFFFFFFFu; // [idx] - Entry DirectedEdge of closed face loop
    Mathematics::Axis3D   GeometricNormal       {};            // [-]   - Unit face normal
    FaceShadingAssignment Shading               {};            // [-]   - Per-face Surface Material & MatCap assignment
    uint16_t              CornerCount           = 0u;          // [cnt] - Loop valence (3 = triangle, 4 = quad, N = n-gon)
    uint16_t              PriorityLayerOrdinal  = 0u;          // [idx] - 0 = Unassigned Remainder; 1..N = PriorityAllocationLayer
};

class PolygonNetwork
{
public:
    std::vector<Vertex>       Vertices        {};   // [-]     - Contiguous 3D vertices
    std::vector<DirectedEdge> Edges           {};   // [-]     - Contiguous directed half-edges
    std::vector<Face>         Faces           {};   // [-]     - Contiguous polygonal faces
    uint64_t                  NetworkRevision = 1ull; // [count] - Bumped on vertex/edge/face connectivity edits
    uint64_t                  UVRevision      = 1ull; // [count] - Bumped on ParameterUV / seam / pack edits
    uint64_t                  ShadingRevision = 1ull; // [count] - Bumped on per-face Material / MatCap edits
};

} // namespace Frontier::GeometrySpace
```

---

## 6. Host Launch Modes (`HostLaunchMode::FeatureSet` vs. `StandaloneWorkspace`)

| Launch Mode (`EngineConfiguration`) | `FRONTIER_PROFILE` | CMake Target | Active Runtime Loop | Behaviour |
| :--- | :---: | :--- | :--- | :--- |
| **`HostLaunchMode::FeatureSet`** | `Editor` | `FrontierEditor` | `EditorLifecycle` + `WorkspaceExtension` | Full multi-workspace Frontier suite (`Geometry`, `UV`, `Texture`, `Material`, `Vector`, `Simulation`) docked in `WorkspaceDockContainer` with live `WorkspaceExchange`. |
| **`HostLaunchMode::StandaloneWorkspace`** | `Editor` | `FrontierUV` (`FRONTIER_BUILD_STANDALONE_UV=ON`) | `EditorLifecycle` (`WorkspaceDomainPreset::UVParameterization`) | Standalone UV executable booting straight into the 3-pane UV Workspace from the exact same `FrontierEngine` library, `.slang` shaders & `TransformGizmo`. |
| **`HostLaunchMode::HeadlessBatch`** | `Development` | `FrontierUVBatch` / `Exhibits/*` | `WorkspaceLifecycle` + `WorkspaceExtension` | Headless CLI / CTest execution running `WorkspaceThread` + `ExecutionCluster` + software/offscreen `.slang` verification. |
| **Runtime Consumer** | `Shipping` | Game Binary | `RuntimeLifecycle` (`FRONTIER_EDITOR_ENABLED == 0`) | Strips `Engine/Editor/*` and authoring threads; decodes `64B`-aligned `.GeometrySpace` & `.UVSpace` streams. |

---

## 7. Ordered 4-Phase Implementation Roadmap

### Phase 1 — `EngineConfiguration` (`FeatureSet` / `Standalone`) & `PolygonNetwork` Structures
1. `Engine/Configuration/{EngineConfiguration,WorkspaceConfiguration}.{h,cpp}` (`HostLaunchMode::FeatureSet`, `StandaloneWorkspace`, `HeadlessBatch`; CMake `FRONTIER_BUILD_STANDALONE_UV` option).
2. `Engine/Mathematics/AxisCoordinates.h` (`Axis3D`, `Axis2D`, `using Scalar3D = Axis3D`, `using Scalar2D = Axis2D`, separate `UVAxes`, `SpatialTransform`, `alignas(16)` GPU parity `static_assert`s).
3. `Engine/GeometrySpace/Network/{PolygonNetwork,GeometryInstance,FacePartitionSpan}.{h,cpp}` (`Vertex`, `DirectedEdge`, `Face`, `32 B` `GeometryInstance`, `PriorityAllocationLayer`, `FacePartitionSpan` with `O(1)` GPU layer ordinal + sorted `FaceIndices`).
4. `Engine/GeometrySpace/Primitives/AnalyticPrimitives.{h,cpp}` (5 procedural sample `PolygonNetwork` assets with clean initial UVs & per-part MatCaps).
5. `Exhibits/GeometryNetworkValidation.cpp` → CTest + `.png` visual proof in `Exhibits/Images/Geometry/`.

### Phase 2 — Workspace GPU `.slang` Renders, Modular Compute/Readback, GPU Picking & 3D/2D `TransformGizmo`
1. **Modular Compute & Readback (`Engine/GeometricRaster/Compute/`)**: `ComputeDispatchEncoder.{h,cpp}` + `TimelineTransferRegister.{h,cpp}` (Vulkan timeline semaphores, zero stalls) + `SelectionMaskCompute.slang` & `UVDistortionInspection.slang`.
2. **Unified 3D/2D GPU Picking (`Engine/GeometricRaster/Picking/`)**: `ViewportPickRasterization.{h,cpp}` + `ViewportPickQuery.{h,cpp}` + `ViewportPickRasterization.slang` & `ViewportPickReduction.slang` (1×1 hover/click + GPU box marquee & paint brush).
3. **Modular 3D & 2D GizmoPRO (`Engine/Editor/Viewports/TransformGizmo.{h,cpp}` + `GizmoRasterization.{h,cpp,.slang}`)**: Faithful Slate `Gizmo.html` 3D & 2D handles (cones, inboard scale cylinders, dual-edge translucent plane quads, `31°` flat arc bars, white billboarded center ring, `Ctrl` snapping) reusable across all workspaces.
4. **3D & 2D UV Workspace Shaders (`Engine/Shaders/` & `Engine/GeometricRaster/Rasterization/`)**:
   - `DirectionalLitShading.slang` + `MatCapShading.slang` (Surface Material PBR + 16 engineering MatCaps + per-face `Apply Selected` / `Fill All`).
   - `EdgeHighlightRasterization.slang` (AA barycentric wireframe, default-off seams, pins, & instant VRAM `FaceSelectionBits` highlight).
   - `UVTileRasterization.slang` & `GroundPlaneRasterization.slang` (2D UDIM `1001+` grid, checker, priority tint, distortion lenses & 3D studio ground grid).
   - `GeometryVertexStreamUploader` + `GeometryRasterSequence`.
5. `Exhibits/UVWorkspaceRasterValidation.cpp` → CTest + `.png` visual proofs in `Exhibits/Images/UV/` (Surface Material, 16 MatCaps, 3D & 2D GizmoPRO, GPU Pick ID buffer, Wireframe/Selection, and 2D UDIM lenses).

### Phase 3 — Modular UV Workspace UI (`Engine/Editor/Workspaces/UV/`)
1. `UVWorkspaceRegistration.{h,cpp}` (registers in `WorkspaceDockContainer` for both `FeatureSet` and `StandaloneWorkspace`).
2. `UVOutlinerPanel.{h,cpp}` (`UV Space` heading + `+` symbol button; Search + UDIM filter; 5 KPI tiles with amber `Stretch Caution` & red `Overlaps`; sharp `0 px` unrounded colour-coded Frontier boxes; `< 0.05 ms` GPU layer selection; sticky `UDIM Space Budget` footer with colour spectrum only).
3. `UVInspectorPanel.{h,cpp}` (`Pack` with 3 live UDIM selection/fill KPI tiles + `ScalarSlider` `%` + layer rename `TextInputField` + `[VisualToken Name #HEX]` colour selector + Target UDIM `DropdownSelector`; `Unwrap`; `UDIM`; `Shading`; neutral-charcoal `Check`).
4. `UVTileCanvas.{h,cpp}`, `UVOperationToolbar.{h,cpp}`, `UVSelectionInteractors.{h,cpp}` (wired to GPU Picking & 3D/2D `TransformGizmo`).
5. `Exhibits/UVWorkspacePanesValidation.cpp` → CTest + `.png` visual proofs in `Exhibits/Images/UV/`.

### Phase 4 — Remaining UV Solvers, `WorkspaceThread` Execution & `215/750 MiB` Revision
1. `GeometrySpace/Selection/{ComponentSelectionMask,GeometrySelection,SelectionTraversal,IslandFloodSelection}.{h,cpp}` (Edge Loops, Rings, Geodesic Shortest Path, Sharp/Boundary, Face Loops, Flat/Coplanar, Similar, Checker & Random).
2. `GeometrySpace/UV/{UDIMTileAddressing,TileLayout,TexelLayout,TileSegmentation,SeamRouter,ConformalFlattening,StretchRelaxer,IslandLayoutPacker}.{h,cpp}` + `StretchRelaxCompute.slang` & `ConcaveSkylineCompute.slang` (LSCM/ABF++, Projections, `FollowActiveQuads`, Importance `%` Concave Packing, `StackSimilarUVTiles` / `UnstackOverlappingUVTiles`, Auto-UDIM Spillover).
3. `Workspaces/UV/{UVWorkspaceDocument,UVWorkspaceLifecycle}.{h,cpp}` & `Operations/*` (`WorkspaceThread` + `ExecutionCluster`, `OperationRecorder` `215/750 MiB` `RedoPurge → PrefixFold`, `.UVSpace` `UVCodec`, `.OBJ` `GeometryInterchange`, `WorkspaceExchange`).
4. `Exhibits/UVParameterizationValidation.cpp` & `Exhibits/UVInteractiveAuthoringValidation.cpp` → CTest + `.png` visual proofs in `Exhibits/Images/UV/`.

---

## 8. 6-Pillar Completion Ledger (🟢 All Pillars Complete & Verified)

| Pillar | Status | Implemented Modules & Targets |
| :--- | :---: | :--- |
| **1. Runnable Desktop Executables & Vulkan Swapchain** | 🟢 Complete | `FrontierUV`, `FrontierEditor`, `Engine/Lifecycle/{DesktopWorkspaceLaunch,FrontierUVLaunch,FrontierEditorLaunch}`, `ImGuiOverlayRasterization`, `ImGuiOverlayRasterization.slang`, `EditorLifecycle::AttachUVWorkspaceHost` |
| **2. Interactive 2D & 3D Viewport Navigation, GPU Picking & Gizmo Drag** | 🟢 Complete | `UVViewportInteractor`, `ViewportProjection`, `ViewportProjectionPane` (orbit/pan/cursor-zoom/frame, `R32G32B32A32_UINT` hover/click/marquee/brush, 2D/3D `TransformGizmo` drag, modal `G`/`R`/`S` + numeric input) |
| **3. Direct Seam, Pin, Stitch, Split, Selection Traversal & Rectify** | 🟢 Complete | `ComponentSelectionMask`, `GeometrySelection`, `UVAuthoringOperations`, `UVTileRasterization.slang` (GPU pin beacons & coral seams), pin-locked `ConformalUnwrapSolver` & `ProgressiveStretchRelaxer` |
| **4. Full Unwrap, Projection & Island Layout Suite** | 🟢 Complete | `UVProjectionSolvers` (ABF++, Smart UV, Box, Cylindrical, Spherical, CameraView, Follow Active Quads) & `UVIslandLayoutOperations` (Stack Similar, Unstack Overlaps, Fill Empty Pockets) |
| **5. External `.obj` & `.gltf` Interchange & Drag-Drop Intake** | 🟢 Complete | `Engine/Interchange/GeometryInterchange.{h,cpp}` (Wavefront `.obj` & Khronos glTF 2.0 `.gltf` multi-UDIM import/export), `UVWorkspaceHost::IntakeExternalFileDrop` |
| **6. Complete Outliner & 5-Tab Inspector Parity** | 🟢 Complete | `UVOutlinerPanel` (Face/Edge/Island/Layer modes, search, create/rename/duplicate/delete layers, `+ UDIM`) & `UVInspectorPanel` (`Pack`, `Unwrap`, `UDIM` resolution/backdrops/IO, `Shade`, `Check`) |

