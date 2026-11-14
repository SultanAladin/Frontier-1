# Cluster LOD Studio: Hero Workspace plan

Status: in progress. Branch `arena/453e172d-frontier-1`.

## Decisions (from the user)

- The page is called **Hero Workspace**. The word "asset" is banned in repo names.
- SDF Baker output is **preview only**: bake on a worker, show slice and raymarch preview, iso offset. It does not feed
  the renderer.
- Geometry Editor: the UI covers all import formats (OBJ, glTF, GLB, STL, PLY, FBX). No import code is written yet.

## Done

- `Engine/GeometryTransform.js`: scale, centre (origin or floor), mirror, rotate (XYZ degrees), flip normals. Mirror
  reverses winding, and flip toggles it again.
- `Engine/HierarchyWorker.js`: applies the transform before the hierarchy build, and returns an untransformed `base`
  copy for the editor preview.
- `GeometryEditor.js` and overlay: transform controls, depth-sorted preview, Apply rebuilds the hero, Escape closes.
- UV Workspace: planar unwrap with tiny-island merge, Shift/Ctrl multi-select, group move, rotate and mirror.

## Remaining

1. **SDF Baker** (`Engine/SdfBake.js` and `SdfWorker.js`): bake a distance grid on a worker from the hero's triangles,
   using a triangle-bin acceleration grid. Preview: slice view with a Z slider, and a CPU raymarch on a small canvas.
   Iso offset slider. Preview only.
2. **Hero Workspace page**: the hero sits in the centre. The Outliner lists Source, Metadata, Icon, UV, Geometry, SDF
   and Cluster LOD. Each item opens its workspace. The Cluster LOD studio becomes one nested workspace.
3. **Metadata**: name, category, tags and info, stored per hero in localStorage.
4. **Inspector 3D cluster preview with a level slider** for the Cluster LOD workspace.
5. README update and a final naming pass (`asset`, `frame`, `stage`) across the studio.
