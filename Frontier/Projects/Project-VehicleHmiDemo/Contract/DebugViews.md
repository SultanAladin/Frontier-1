# Debug & inspection views — inventory and handling

Extracted in Phase 0 (`ShaderContract.tsv`, `UiLabels.tsv`); handling decided
2026-10-22 after the owner's MatCap/CAD/debug-view question.

## What the demo has

**Debug entrypoints (WGSL):**

| Entrypoint | Shader | Shows |
|---|---|---|
| `DebugVisibilityIds` | MaterialResolve | stable hash colour per cluster-triangle (glass front cyan / back magenta) |
| `DebugGlassLayers` | MaterialResolve | front green / back red / overlap yellow |
| `DebugCopyResolved` | MaterialResolve | the resolved frame verbatim |
| `DebugHiZView` | MaterialResolve | reversed-Z HiZ pyramid |
| `DebugLodLevels` | MaterialResolve | cluster DAG cut level per pixel (warm→cool) |
| `DebugUnavailable` / `DebugFailed` / `DebugNoGeometry` | ClusterDebugFallback | cluster system failure states |
| `MarchInspection` | InspectionMarch | marched inspection volume |

**Display inspection list (JS labels):** the display chain is itself inspectable
step-by-step (Display resolve → antialias → exposure → present), plus per-system
views (card capture/depth/lighting/irradiance/final radiance, cabin optical
transmission, solar source visibility, cached optical flare, idle plume…).
Every label is a row in `UiLabels.tsv` and ends the port as a named view or an
approved removal, like every other UI item.

**Upstream preview modes (not demo):** TextureWorkspace's renderer owns
Surface / Edge (CAD) / MatCap / PreviewPbr shader sets
(`Engine/Render/SurfaceRasterization.h` @ `c177821`). Those are *shading
modes* of the preview surface, not extra passes.

## How the port handles them

1. **Debug views are units, not modes.** `Engine/Rendering/InspectionUnits`
   declares one unit per view (`DeclareInspectionUnit`) reading its source
   target and writing one shared transient **Inspection** target, appended to
   the same frame declaration — ordering, aliasing and barriers derive exactly
   as for production units (validated in `Exhibits/RenderInfrastructure`).
   The atlas can alias Inspection over dead targets, so debug views cost no
   steady memory.
2. **Editor/Development only.** `InspectionViewsEnabled()` is
   `BuildConfiguration::EditorMachineryEnabled`; Shipping declarations contain
   no inspection units, targets or shaders (plan §5).
3. **One active view** renders into Inspection; the viewport panel displays it
   *instead of* the swapchain chain when selected (the demo presents inspection
   output un-tonemapped — same rule).
4. **MatCap / CAD** ride `ViewportShadingMode` (ViewportUnits.h): the mode
   selects the surface shader set for the existing SurfaceRaster unit at the
   SDK phase, reusing upstream's MatCap/Edge sets. No new passes, no new
   barriers.
