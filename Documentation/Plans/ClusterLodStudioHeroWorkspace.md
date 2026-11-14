# Cluster LOD Studio: Hero Workspace plan

Status: implemented, browser-tested. Branch `arena/453e172d-frontier-1`.

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

## Done (second pass)

- `Engine/SdfBake.js` and `Engine/SdfWorker.js`: narrow-band distance plus crossing-parity sign. Verified on a UV sphere
  (max error 0.004 against the exact clamped distance, 112 surface voxels disagree out of 110,592).
- `SdfBaker.js`: bake on a worker at 32–64 cubed, Slice and Raymarch previews, iso offset as a percentage of hero size.
  Preview only.
- `HeroWorkspace.js`: outliner tree (Metadata, Source, Geometry, Icon, UV, SDF for closed surfaces, Cluster LOD),
  centre panes, metadata store in localStorage.
- `ClusterPreview.js`: Inspector 3D preview of one LOD level with a level slider. Triangle and cluster counts match the
  hierarchy exactly (checked for LOD 3 of the vase: 7,504 triangles, 64 clusters).

## Remaining

- Real GPU preview for the SDF raymarch and the cluster preview (both are Canvas 2D projections today).
- Metadata is per browser, not in the source file or a server.

## Layout (current)

- **Outliner (right)**: a tree. The hero row (thumbnail = saved icon) is the source; its children are Geometry Editor,
  Cluster LOD Studio, Icon, UV Workspace and SDF Baker (SDF only for closed surfaces). Caret collapses the children.
- **Centre**: Source list (hero row), Geometry Editor preview, SDF pane, or the Cluster LOD viewport.
- **Inspector (left)**: one workspace panel at a time (Metadata, Geometry, Cluster LOD, Icon, UV, SDF). Each has its
  controls, previews and sliders, and an open button that switches the studio.
- Two studios on one page: Geometry Editor (home; transforms and Rebuild hero) and Cluster LOD Studio (viewport, LOD
  slider, cut, hierarchy, build). Both use the same buttons, icons and Inspector components.
- Source replaces the hero on import (one source at a time). Copy-alongside was not chosen.
- Open items: the `draw` name in `ClusterPreview.js`, `base` variable names in `GeometryEditor.js`, and the
  `info` metadata label (user-specified; kept).

## Original plan notes (superseded)


1. **SDF Baker** (`Engine/SdfBake.js` and `SdfWorker.js`): bake a distance grid on a worker from the hero's triangles,
   using a triangle-bin acceleration grid. Preview: slice view with a Z slider, and a CPU raymarch on a small canvas.
   Iso offset slider. Preview only.
2. **Hero Workspace page**: the hero sits in the centre. The Outliner lists Source, Metadata, Icon, UV, Geometry, SDF
   and Cluster LOD. Each item opens its workspace. The Cluster LOD studio becomes one nested workspace.
3. **Metadata**: name, category, tags and info, stored per hero in localStorage.
4. **Inspector 3D cluster preview with a level slider** for the Cluster LOD workspace.
5. README update and a final naming pass (`asset`, `frame`, `stage`) across the studio.
