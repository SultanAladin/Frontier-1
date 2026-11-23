# PLAN — GPU Editing Overlays + Full Modeling / UV Toolset

🏷️ *Saved into `EngineDocs/` on explicit user instruction (2026-07-29), overriding the
"no plans in EngineDocs" rule in `CLAUDE.md`.*

**TL;DR** — The CPU stall was the **per-frame overlay redraw**, not the editing. Move overlay
drawing + picking + drag-preview to the GPU (O(pixels), poly-count independent); keep topology
mutation on the CPU (O(selection), runs once per gesture). The operation kernels already exist;
the missing keystone is the half-edge `TopologyStructure`.

---

# 1. Diagnosis — what actually bogged the CPU down

| Work | Cost model | Frequency | Verdict |
|---|---|---|---|
| Vertex / edge / face overlay as CPU line list | O(edges) | **every frame** | 🔴 the bottleneck |
| Ray pick / hit test | O(triangles) | per click | 🟢 acceptable |
| Extrude / bevel / bridge mutation | O(selection) | per gesture | 🟢 acceptable |

A 500k-triangle `PolygonComplex` carries ~750k loop edges. Rebuilding and pushing that line list at
60 fps is ~45M line-vertices/second **even when the model is untouched**. The extrude itself touches
tens of faces, once. The overlay was the cost; the editing was never the cost.

💡 **Partition rule:** the GPU owns everything that redraws **every frame**. The CPU owns everything
that happens **once per user action**. GPU extrude would be a research project for no gain; CPU
wireframe is unsalvageable at any polygon count.

```text
┌─ GPU — per-frame, must scale with polygon count ─────────────────┐
│  wireframe / edge / vertex overlay      → fragment shader        │
│  selection + hover tint                 → id-buffer compare      │
│  hover identity                         → 1-pixel copy-back      │
│  drag / tweak preview                   → compute over vertices  │
├─ CPU — per-gesture, scales with SELECTION not polygon count ─────┤
│  extrude bevel inset bridge loop-cut weld dissolve knife         │
│  selection set logic, loops, rings, linked                       │
│  undo / redo, modifier ordering                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

# 2. Existing assets (verified by reading the files)

| Asset | Location | Role in this plan |
|---|---|---|
| R32_UINT id buffer, packed (partition, primitive) | `Internal/Graphics/Visibility/VisibilityImage.h:32` | picking + selection tint, already paid for |
| Empty sentinel `0xFFFFFFFF` | `VisibilityImage.h:36` | background rejection in overlay shaders |
| `TRANSFER_SRC` on the id image | `VisibilityImage.h:6` | 1-pixel hover copy-back, no full readback |
| Shared multi-mesh scope + `PreserveContents` | `VisibilityRasterization.h:108` | append overlay onto the existing depth buffer |
| Indirect draw + survivor remap | `VisibilityRasterization.h:145` | overlay inherits the GPU cull for free |
| Device-local vertex / index buffers, stride 32 | `Internal/Graphics/Render/Resources/BufferAllocation.h:38` | add `STORAGE_BUFFER` usage → compute access |
| `AdjacencyIndex` — loop edges, fan tris, Newell normals, edge→faces | `Internal/Authoring/Geometry/Adjacency/AdjacencyIndex.h` | build the GPU edge-classification buffer **once** |
| `ComponentSelection` — sets, loops, linked, preview | `Internal/Authoring/Geometry/Selection/ComponentSelection.h` | stays CPU; uploaded as a GPU bitset |
| `RayPickIntersection` | `Internal/Authoring/Geometry/Picking/RayPickIntersection.h` | demoted to fallback + exact hit-point math |
| Operation kernels: Extrude Bevel Inset LoopCut Deform TopologyEdit | `Internal/Authoring/PolygonAuthoring/Operations/*` | already ported — extend, don't rewrite |

🔴 **The keystone gap:** `TopologyStructure` (half-edge) is referenced in three header comments
(`AdjacencyIndex.h`, `DisplayPolygonAssembly.h`, `RayPickIntersection.h`) but **is defined nowhere**.
`AdjacencyIndex` is a derived cache with unordered edge→face incidence — it cannot answer the ordered
queries loop/ring walking, knife, bridge, and rip require. Every advanced tool in §5 depends on this.
Build it first.

---

# 3. Device capability gate

Host targets **Vulkan 1.2** (`Internal/Graphics/RenderExtension/Device/VulkanHost.cpp:144`), already
gating optional features cleanly (`shaderInt64`, `VK_EXT_shader_image_atomic_int64`).

| Capability | Needed for | If absent |
|---|---|---|
| `VK_KHR_fragment_shader_barycentric` | zero-cost wireframe | fall back to per-vertex barycentric attribute on an unwelded copy — still O(pixels), costs vertex duplication |
| `STORAGE_BUFFER` on vertex buffer | compute tweak preview | add the usage bit; no extension needed |
| `wideLines` | thick overlay lines | shader-side quad expansion (preferred anyway — portable + consistent) |
| `fillModeNonSolid` | `VK_POLYGON_MODE_LINE` debug | not used by this plan; barycentric path is superior |

⚠️ Header comments reference **Pascal** support. Barycentrics are **not** available on Pascal —
assume the fallback path is the one that ships, and treat the extension as an optimization.

---

# 4. GPU editing work items

## 4.1 `PolygonWireInscription` — overlay without line primitives

Naming per `SKILL-Naming.md` §2: composites onto an existing target → `…Inscription`.

Distance-to-nearest-edge in the fragment shader; no line list, no per-frame CPU work:

```glsl
float EdgeDistance = min(min(Barycentric.x, Barycentric.y), Barycentric.z);
float WireCoverage = 1.0 - smoothstep(0.0, fwidth(EdgeDistance) * 1.5, EdgeDistance);
```

`fwidth` yields constant-width antialiased lines at any zoom. Cost is **O(pixels)** — a 10M-triangle
`PolygonComplex` costs exactly what a 1k-triangle one costs. That single property is the whole reason
this plan exists.

⚠️ This draws *triangle* edges including fan diagonals. `AdjacencyIndex` keeps faces as original
N-gons, so upload a per-triangle 3-bit mask ("which of my edges are real loop edges") built once from
`FaceEdges`, and mask `WireCoverage` against it. Quads and N-gons then wireframe correctly.

## 4.2 Selection + hover tint via id-buffer compare

The id buffer already holds a packed identity per pixel. Upload the selection as a GPU bitset (one
bit per face / edge / vertex ordinal), test the bit in the resolve, tint. Selecting 200k faces is
**one buffer update**, not 200k draws. Hover is the same test against a single hovered ordinal.

## 4.3 `ComponentPickReadback` — O(1) hover identity

Copy **one pixel** of the id image at the cursor into a host-visible buffer; read it the next frame.
One-frame latency is imperceptible for hover. Poly-count independent, replacing the per-hover
raycast. `RayPickIntersection` stays for off-surface gestures and exact hit-point math (knife,
snapping), where the actual intersection position — not just identity — is required.

## 4.4 `VertexTweakEvaluation` — drag preview on the GPU

Add `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` to the vertex buffer. A compute shader applies the live
gizmo transform to selected vertices each frame; drag feedback never touches the CPU and never
re-uploads geometry. **On release**, the CPU applies the same transform authoritatively to the
`PolygonCluster` so the source of truth stays canonical and undo works on real data.

⚠️ GPU preview and CPU truth must use one shared transform formulation, or the model will visibly
snap on mouse-release. Derive both from the same parameter block.

---

# 5. Full modeling toolset

Reference algorithms drawn from Blender (`bmesh`), OpenSubdiv, and standard half-edge literature.
`Status`: ✔️ exists · 🚧 partial · 🔴 absent.

## 5.1 🔴 Keystone — `TopologyStructure` (half-edge)

Blender's `BMesh` shape: per half-edge `Next` / `Previous` / `Twin` / `IncidentFace` / `OriginVertex`,
plus per-face and per-vertex cycle entry links. Requirements:

- **Ordered** radial cycle per edge (`AdjacencyIndex` is unordered — insufficient for ring walks).
- Non-manifold tolerance: an edge with >2 incident faces must not corrupt the walk.
- Stable ordinals across mutation, so selection sets and undo survive an edit.
- `ManifoldCondition` / `BoundaryCondition` predicates (booleans per `SKILL-Naming.md` §8).

Suggested home: `Internal/Authoring/Geometry/Topology/TopologyStructure.{h,cpp}`.

## 5.2 Selection operations

| Tool | Algorithm | Status |
|---|---|---|
| Click / box / lasso / circle select | id-buffer compare over a screen region | 🚧 sets exist; GPU path new |
| Edge loop | valence-4 forward walk; straightness fallback at poles | ✔️ `ComponentSelection` |
| Edge ring | cross-quad walk via opposite half-edge | 🔴 needs radial cycle |
| Face loop / ring | ring walk, faces between paired edges | 🔴 |
| Select linked | connected component, flood over shared vertices | ✔️ |
| Shortest path | Dijkstra over the edge network | 🔴 |
| Select similar | per-component predicate (area, normal, valence, length) | 🔴 |
| Grow / shrink | one-ring dilate / erode | 🔴 |
| Select all by trait | boundary / interior / non-manifold / loose / ngon | 🔴 |
| Checker deselect | ordinal parity along a loop | 🔴 |

## 5.3 Vertex operations

| Tool | Algorithm | Status |
|---|---|---|
| Move / rotate / scale | gizmo transform; GPU preview per §4.4 | 🚧 |
| Merge by distance | spatial-grid cluster, then weld | 🔴 |
| Merge at centre / first / last | centroid weld | 🔴 |
| Rip / rip-fill | split radial cycle at the vertex, fill the gap | 🔴 |
| Vertex slide | constrain to an incident edge, parameter clamp | 🔴 |
| Smooth (Laplacian) | one-ring centroid, weighted, N iterations | 🚧 `PolygonDeform` |
| Vertex bevel | corner→face expansion | 🚧 `PolygonBevel` |
| Connect vertex path | insert edges along a selected vertex chain | 🔴 |
| Randomize / snap | per-vertex jitter / grid quantize | 🔴 |

## 5.4 Edge operations

| Tool | Algorithm | Status |
|---|---|---|
| Extrude edges | boundary sweep + quad generation | ✔️ `PolygonExtrude` |
| Bevel / chamfer (n-segment) | offset planes, miter at corners, profile blend | 🚧 `PolygonBevel` |
| Loop cut + slide | ring walk, midpoint split, parametric slide | 🚧 `PolygonLoopCut` |
| Subdivide edge (n-cut, smooth) | midpoint split, optional Catmull smoothing | 🚧 |
| Bridge edge loops | pair by proximity / winding, generate quads | 🚧 `BridgeBoundaryLoops` |
| Edge slide | constrain to adjacent ring, clamp | 🔴 |
| Rotate edge CW / CCW | flip the shared diagonal of two triangles | 🔴 |
| Dissolve edges | remove edge, fuse the two faces | 🚧 `ResolveDissolveSelection` |
| Mark seam / sharp / crease | per-edge attribute write | 🔴 |
| Knife / bisect | screen-ray plane cut, split faces at crossings | 🔴 |
| Offset edge loop | parallel loop either side | 🔴 |

## 5.5 Face operations

| Tool | Algorithm | Status |
|---|---|---|
| Extrude region / individual / along normal | boundary detection, sweep, cap | ✔️ `PolygonExtrude` |
| Inset (region / individual) | edge offset inside the loop, cap ring | 🚧 `PolygonInset` |
| Poke | fan-triangulate to the centroid | 🔴 |
| Triangulate (ear-clip / beauty) | ear clipping; Delaunay-flip beautify | 🚧 `FaceTriangulation` |
| Tris → quads | greedy pair merge by shape metric | 🔴 |
| Solidify / shell | offset along normals, generate rim | 🔴 |
| Grid fill | boundary loop → structured quad grid | 🔴 |
| Bridge faces | face-pair tunnel | 🚧 |
| Flip / recalculate normals | winding flip; BFS orientation propagation | 🚧 `ReverseAppendedFaceWinding` |
| Dissolve / delete faces | remove, optional boundary fuse | 🚧 |
| Shade smooth / flat | per-face attribute + normal split | 🔴 |
| Fill / beauty fill | boundary loop triangulation | 🚧 `ConstructBoundaryFill` |

## 5.6 LoopTools equivalents (Blender add-on parity)

| Tool | Algorithm | Status |
|---|---|---|
| Circle | fit best plane, project loop to a common radius | 🔴 |
| Relax | iterative smoothing constrained to the original surface | 🔴 |
| Space | redistribute vertices at equal arc length | 🔴 |
| Flatten | project loop onto a fitted plane | 🔴 |
| Curve | fit a spline through the loop, re-project | 🔴 |
| Bridge / loft | loop-to-loop quad generation with twist minimization | 🚧 |
| GStretch | fit a loop to a drawn stroke | 🔴 |

## 5.7 Modifier chain

`ModifierStack` exists (`Internal/Authoring/PolygonAuthoring/Modifier/ModifierStack.h`) with
`SubdivideCatmullClark` + `SubdivideSimple`. Still absent: Mirror, Array, Solidify, Bevel-as-modifier,
Boolean, Shrinkwrap, Lattice, Weighted Normal, Decimate, Remesh, Triangulate.

⚠️ `Commit` is banned (§1) — the chain's apply verb is `Finalize` / `Resolve`.

---

# 6. UV editor toolset

`ComponentUVSurface` (`Internal/Authoring/Geometry/UV/ComponentUVSurface.h`) + `UVWorkspace` +
`UVPropertyPanel` exist. The unwrap solvers are the gap.

| Tool | Algorithm | Status |
|---|---|---|
| Angle-based unwrap (ABF++) | angle optimization + linear solve | 🔴 |
| Conformal unwrap (LSCM) | least-squares conformal map, sparse solve, 2 pinned | 🔴 |
| Smart project | normal-clustered face grouping, per-group project, pack | 🔴 |
| Follow active quad | propagate a quad's UV across a face loop | 🔴 |
| Cube / cylinder / sphere project | analytic parameterization | 🚧 |
| Seam marking + island split | per-edge seam attribute → island partition | 🔴 |
| Island packing | bin-pack / irregular pack with margin | 🔴 |
| Stitch / weld UVs | merge coincident island boundaries | 🔴 |
| Average island scale | per-island texel-density normalization | 🔴 |
| Minimize stretch | relaxation against a distortion metric | 🔴 |
| Pin / unpin | constraint set fed to the solver | 🔴 |
| Live unwrap | re-solve on seam change | 🔴 |
| Stretch overlay (angle / area) | per-triangle distortion → colour ramp | 🔴 |
| Straighten / align / rectify | project selection to a fitted line/rect | 🔴 |
| Snap to pixel / grid | quantize to texel centres | 🔴 |

💡 LSCM before ABF++: same sparse-solver dependency, far simpler, and good enough for most authoring.
ABF++ becomes worthwhile only once distortion complaints are measured, not assumed.

🔴 Both need a **sparse linear solver** — the first genuinely new numerical dependency in this plan.
Decide build-vs-vendor before starting §6.

---

# 7. Ordering

| Phase | Work | Unblocks |
|---|---|---|
| **P1** | `TopologyStructure` half-edge + ordered radial cycles | every ring/loop/knife/rip tool |
| **P2** | `PolygonWireInscription` + per-triangle real-edge mask | kills the original stall |
| **P3** | Selection bitset tint + `ComponentPickReadback` | overlay-free picking at any poly count |
| **P4** | Ring / face-loop / shortest-path / grow-shrink selection | the §5.2 remainder |
| **P5** | `VertexTweakEvaluation` compute preview | interactive drag on dense models |
| **P6** | Edge ops: slide, rotate, knife, bisect, offset loop | §5.4 completion |
| **P7** | Face ops: poke, tris↔quads, solidify, grid fill | §5.5 completion |
| **P8** | LoopTools set (circle, relax, space, flatten, curve) | §5.6 |
| **P9** | Sparse solver → LSCM → seams → packing | §6 |

**P1 → P2 → P3 is the critical path** for the problem that prompted this plan. P2 alone removes the
frame-rate collapse; P1 is required first only because the real-edge mask reads ordered topology.

---

# 8. Verification

| Phase | Gate |
|---|---|
| P1 | Euler characteristic preserved; radial cycles ordered; non-manifold edge survives a walk |
| P2 | Frame time flat from 1k → 1M triangles with overlay enabled |
| P3 | Picked ordinal matches the CPU raycast on the same pixel, across zoom levels |
| P5 | GPU preview and CPU-applied result agree to float tolerance — **no snap on release** |
| P9 | Round-trip distortion metric within tolerance on a known-good model |

New validation targets belong under `Executables/Validation/` alongside the existing
`RenderExtensionValidation` / `ClipmapFieldValidation` pattern — per `SKILL-Naming.md`, a test
scaffold is a `…Validation`, never a `harness` / `Trial`.

---

# 9. Status caveat

Status marks come from **file inventory and header signatures**, not from reading every
implementation body. Files marked 🚧 exist and expose plausible signatures; their internal
completeness is unverified. Confirm the specific kernel before scheduling any phase that extends it.
