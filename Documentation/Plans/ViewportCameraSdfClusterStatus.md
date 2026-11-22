# Viewport camera, distance field, and cluster status

This is an investigation and partial repair, not a completion report. The imported history at
`d332837` contains both `c1bbbfd` and `arena/20fc22da-frontier` as ancestors. It already includes
`arena/25b070ae-frontier`; importing that tip retains the prior merge history rather than performing
another merge.

## Camera and gizmo

- The interaction code previously used the lagged rendered pose as the starting point for each
  navigation update, overwriting the desired pose every frame. This attenuates sustained movement
  and mouse-look as well as introducing the intended smoothing. Navigation now accumulates in the
  desired pose; picking still uses the rendered pose. Focus, navigation capsule zoom and projection
  presets synchronize the desired pose with the rendered camera.
- Gizmo mode shortcuts are now `1` translate, `2` rotate, `3` scale and `4` universal, so flight
  keys do not also select gizmo modes. This does not establish that every hover/pick issue is fixed.
- The editor camera has finite far clips: the vehicle scene uses 240 m and another scene uses
  120 m. `ProjectBoundingSphereToScreen` rejects clusters beyond the active far plane. This can
  cause distant geometry to disappear, but without a reproduction it is not proven to be the
  glitch in the screenshot. The lagged target point can also create unstable view directions when
  position and target are smoothed independently; that requires separate investigation.
- Mouse capture, Shift-modified navigation, gizmo interaction, far-distance movement and projection
  presets still need interactive validation in the desktop editor. There is no claim that the
  screenshot defects are all resolved.

## SDF and AO: not fixed

`VulkanWorldRenderSequence::DispatchGpuScenePrecompute` bakes a two-cascade global distance field
from scene geometry when the field is not ready or geometry fields are requested for rebuilding.
During fast manipulation, resource synchronization can call it with `RebuildGeometryFields=false`,
leaving the old field in place; a later full synchronization can rebuild it. This is not the
requested per-object local distance field with incremental global composition. Rebuilding the full
field more frequently would not satisfy the request. A real repair needs reusable object-local
fields, transforms and localized invalidation/composition for both old and new object bounds, with
correct removal and overlapping objects, plus visual and timing tests during continuous dragging.
The current AO and the independently toggleable XeGTAO path have not been visually validated here.

## Clusters: existing code, not yet proven for the requested behaviour

- The bake recipe has a `CoarsestAllowedLevel`, with `0` meaning finest only. The showcased glass
  setting is hard-coded and the glass path is excluded from the cluster BVH; this is not an
  editor-configurable per-asset glass proof.
- A cluster may contain multiple source faces of the *same* material; the current implementation
  groups by material and permits up to 128 triangles, not one cluster per face. Two different
  materials are intended to be kept in separate clusters, and triangle entries carry material slots.
  Finest-level entries retain a `SourceFaceIndex`, but a stable original face ID through simplification
  and the complete viewport picking/debug path is not established. Do not infer face-ID correctness
  solely from coloured images.
- Existing debug channels include LOD-level colours and hashed cluster colours. LOD level is not a
  true triangle-density view. The offscreen validation rendered the vehicle through the Vulkan
  sequence on SwiftShader and refreshed `Exhibits/Images/NaniteViewport/VehicleNearClusters.png` and
  `VehicleFarLodLevels.png`. This is not proof of interactive editor presentation or moving objects.
- The validation reports 10 material slots at L0 and 5 in the L1/L2 level summaries. Singleton
  clusters can remain at finer levels, so those summaries alone do not prove material loss. The
  strengthened far-cut test now checks that all finest-level material slots remain in the cut when
  backface culling is disabled, and passed. This still does not prove preservation of every face;
  protected glass, motion, source face IDs and actual editor presentation need additional tests.

## Validation run

Configured an Editor-profile build with pinned dependencies, a headless GLFW build and SwiftShader.
`CameraComponentValidation`, `ClusterLodBakeValidation` and `NaniteViewportVehicleValidation` all
passed. The latter was rerun after strengthening the far-cut material-slot assertion. The camera
suite tests the reusable component, not interactive `EditorLifecycle` mouse navigation; the vehicle
suite renders offscreen and does not exercise live gizmo drags. No hardware GPU or desktop window
was available for interactive acceptance testing.

## Other inherited work

The imported branch contains implementations for F/F1/F3, sampled telemetry, render menus, a
RAM-buffered ThoughtLog, progressive path tracing, XeGTAO and project-browser changes. They have not
all been independently verified in this session. `Documentation/UIControllableItems.md` is the
inherited inventory, including backend-only options, and should not be read as proof that every item
is currently wired to a UI control.
