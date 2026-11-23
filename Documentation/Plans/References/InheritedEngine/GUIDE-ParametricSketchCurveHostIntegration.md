# GUIDE — Driving the Parametric-Sketch Curve Sequence from a Host

How a Vulkan + ImGui host wires `ParametricSketchCurveSequence` (the GPU thick-line OUTLINE
rasterizer) into its frame loop, alongside the already-ported `ParametricSketchSolidSequence`
(matcap faces). Both are peers: same five-call lifecycle, same layered-ImGui composite. This guide is
the contract Phase 5 implements in `SketchModelViewport` and every editor workspace reuses unchanged.

> Guard: the whole curve chain is compiled only under **`FRONTIER_PARAMETRIC_SKETCH`**. A host that
> does not define it links a define-less build with no outline overlay (the game-clean proof).

---

## The one-frame-late layered composite

Each GPU consumer renders into its OWN offscreen `ParametricSketchViewTarget` (colour + depth) and
publishes an `ImTextureID` through a reverse bridge in `ParametricSketchShapeStore`. The active view
draws those images back-to-front with `ImGui::AddImage`, in this fixed z-order:

```
dark canvas fill  →  matcap SOLID image  →  grid  →  OUTLINE image (on top)
                     (SolidSequence)              (CurveSequence)
```

The publish is **one frame late by construction** — the pre-pass for frame N runs after the paint of
frame N, so the image the view samples is frame N−1's. Invisible as the camera eases; do not try to
"fix" it with a stall.

---

## The five calls, and where each belongs in the loop

| Call | When | Render-pass scope |
| --- | --- | --- |
| `InitializeParametricSketchCurveSequence` | once, at bring-up (after the ImGui Vulkan backend is live) | — |
| `SynchronizeParametricSketchCurveSequence` | once per frame, **OUTSIDE any render pass** (device-idle-safe) | none |
| `RecordParametricSketchCurveSequenceInto` | once per frame, on an open primary cmd buffer, **BEFORE the swapchain pass** | begins + ends its OWN target render pass internally |
| `PublishParametricSketchStrokeImage` | once per frame, after RecordInto, before the ImGui paint | — |
| `FinalizeParametricSketchCurveSequence` | once, at teardown, **while the ImGui Vulkan backend is still up** (the colour descriptor is an ImGui texture) | — |

`RecordInto` opens and closes its target's `VkRenderPass` itself (transparent clear, leaves the colour
image `SHADER_READ_ONLY`). The host's job is only to hand it an **already-begun, still-open primary
command buffer** — it must NOT already be inside another render pass.

---

## Bring-up (mirror the solid sequence)

```cpp
#include "Graphics/Render/Surface/ParametricSketchSolidSequence.h"
#include "Graphics/Render/Surface/ParametricSketchCurveSequence.h"

ParametricSketchSolidSequence SolidSequence;
ParametricSketchCurveSequence CurveSequence;

// The ImGui Vulkan backend MUST be live first — both targets register their colour image with it.
InitializeParametricSketchSolidSequence(SolidSequence, Host,
    "Assets/Matcap/Chrome.png",                 // asset-root-relative
    "Shaders/ParametricSketchSurface.vert.spv",
    "Shaders/ParametricSketchSurface.frag.spv",
    /*InitialExtent*/ 1024);

InitializeParametricSketchCurveSequence(CurveSequence, Host,
    "Shaders/ParametricSketchCurve.vert.spv",   // beside the exe (ShaderPlan.ps1 stages them)
    "Shaders/ParametricSketchCurve.frag.spv",
    /*InitialExtent*/ 1024);

// Optional: pick the on-screen line weight (half-width in px). Default 1.5 (≈3 px stroke).
CurveSequence.HalfWidthPixels = 1.5f;
```

Any failure inside either Initialize leaves `Enabled = false` and every later call no-ops — the host
falls back to the pure-2D sketch. **Never gate the host's own bring-up on these returning true.**

---

## Per-frame loop

The sequences slot into the SAME pre-ImGui one-shot-buffer + fence pattern the grid already uses in
`SketchModelViewportHost.cpp` (record → submit → wait fence → the images are `SHADER_READ_ONLY` before
ImGui samples them this frame). Order within the frame:

```cpp
// 1) SYNC — outside any render pass. Sizes each target to the canvas rect, refreshes the shared
//    camera from the published ParametricSketchSceneView, re-stages any body whose Revision advanced.
//    A genuine resize drains the device internally; a revision re-stage drains before free/reupload
//    (the DEVICE_LOST guard). Idle frames cost nothing (the revision gate short-circuits).
const ParametricSketchSceneView& SceneView = RetrieveParametricSketchSceneView();
SynchronizeParametricSketchSolidSequence(SolidSequence, SceneView);
SynchronizeParametricSketchCurveSequence(CurveSequence, SceneView);

// 2) RECORD — on an open primary command buffer, BEFORE the swapchain pass. Each call begins + ends
//    its own target render pass. SOLID FIRST, CURVE SECOND (matches the composite z-order; the images
//    are independent targets, so order here is only for readability, not correctness).
vkBeginCommandBuffer(PrePassCmd, &OneTimeBegin);
RecordParametricSketchSolidSequenceInto(SolidSequence, PrePassCmd);
RecordParametricSketchCurveSequenceInto(CurveSequence, PrePassCmd);
vkEndCommandBuffer(PrePassCmd);
// submit PrePassCmd on its own fence and WAIT it (validation host) — both target images now readable.

// 3) PUBLISH — hand each target's descriptor to its reverse bridge for the paint to composite.
PublishParametricSketchSolidImage(SolidSequence);
PublishParametricSketchStrokeImage(CurveSequence);

// 4) PAINT — the view reads the images back and layers them (fill → solid → grid → outline).
ImGui::NewFrame();
uint32_t SolidW, SolidH, StrokeW, StrokeH;
ImTextureID SolidImage  = RetrieveParametricSketchSolidImage(SolidW, SolidH);
ImTextureID StrokeImage = RetrieveParametricSketchStrokeImage(StrokeW, StrokeH);
// ... AddImage(SolidImage) under the grid, AddImage(StrokeImage) on top ...
ImGui::Render();
SubmitAndPresentImguiFrame(Interface, Host, ImGui::GetDrawData());
```

A published image is `(ImTextureID)0` with extent `0×0` when the sequence is disabled or nothing is
resident — the view then simply draws no overlay for that layer.

---

## What the sequence reads, and who feeds it

The curve sequence is a pure consumer of two published bridges — the host does not push geometry into
it directly. The active parametricSketching view must publish these each frame BEFORE `Synchronize`:

| Bridge | Publisher (view) | Reader (sequence) |
| --- | --- | --- |
| `RegisterParametricSketchSceneView` — the settled `ViewProjection` / `ViewMatrix` + canvas rect | the view's paint | `Synchronize…` camera + target sizing |
| `RegisterParametricSketchStrokeBodies` — the flattened outlines (world-mm polylines + colour + linetype + revision) | `AssembleParametricSketchStrokeBodies(Store, …)` then Register | `Synchronize…` re-stage walk |

`AssembleParametricSketchStrokeBodies` is a pure adapter over the existing flatten
(`RetrieveCachedOutline`) — no new tessellation — so the GPU stroke and the CPU pick/length outlines
never drift. Every `ConstructParametricSketchShape` edit bumps the shape's revision, which is what
makes the "updates when the CPU changes" requirement automatic: the sequence re-uploads only the
bodies whose `Revision` advanced.

---

## Teardown (reverse of bring-up)

```cpp
vkDeviceWaitIdle(Host.Device);                       // drain first
FinalizeParametricSketchCurveSequence(CurveSequence); // while the ImGui backend is STILL up
FinalizeParametricSketchSolidSequence(SolidSequence);
// ... then ImGui_ImplVulkan_Shutdown(), etc.
```

Both Finalize calls destroy an ImGui backend texture (the target's colour descriptor), so they MUST
run before `ImGui_ImplVulkan_Shutdown()`. Both are safe on a never-initialized value.

---

## Gotchas (each already cost a debugging session somewhere)

- **Record needs an OPEN buffer, not an open pass.** Hand `RecordInto` a begun primary command buffer
  that is NOT inside a render pass — it begins its own. Calling it inside the swapchain pass is a
  validation error.
- **Publish is one frame late.** Do not add a stall to "sync" the image to the current frame; the
  layered composite tolerates the lag and a stall only hurts.
- **Finalize before the ImGui backend shuts down.** The colour descriptor is an ImGui texture.
- **`Enabled == false` is normal.** A missing `.spv` / any Vulkan failure disables the sequence; the
  host must keep running the 2D sketch. Never abort bring-up on it.
- **Revision drives re-upload, not a per-frame rebuild.** If outlines look stale, the bug is a missing
  revision bump upstream, not in the sequence — it re-stages exactly when `Revision` changes.

---

## Related

- Sequence contract: `Internal/Graphics/Render/Surface/ParametricSketchCurveSequence.h`
- Pipeline unit: `Internal/Graphics/Render/Surface/ParametricSketchCurveRasterization.h`
- Bridges: `Internal/Authoring/ParametricAuthoring/ParametricSketchShapeStore.h`
  (`RegisterParametricSketchStrokeBodies` / `…StrokeImage`, `RetrieveParametricSketch…`)
- Solid peer this mirrors: `Internal/Graphics/Render/Surface/ParametricSketchSolidSequence.h`
- Reference index: `Documentation/REFERENCE-CadAnalyticPrimitives.md`
