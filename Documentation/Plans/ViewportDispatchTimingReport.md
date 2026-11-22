# Viewport Dispatch & Timing Report

> **TL;DR**: Static Local SDF assets are not rebuilt per frame, and the resident cluster BVH is not reassembled for camera motion or LOD changes. The actual per-pixel renderer still traces/shades every dispatched image; the HighEnd GI mode adds several BVH rays per visible pixel. The viewport now defaults to progressive Path Tracer, which accumulates to its 256-sample target and then stops dispatching until the view changes. Idle-frame telemetry no longer repeats the last dispatch's GPU timings.

## What runs and what rebuilds

| Work | Current behavior |
| :--- | :--- |
| Baked Local SDF asset voxels | Packed/uploaded only when the asset signature changes or the GPU buffer is missing. Static viewport redraws do not rebuild the asset SDF. |
| Dynamic Local SDF instance transforms | A small CPU scan/signature check runs during viewport dispatch. The transform buffer uploads only when its signature changes. |
| Resident cluster BVH | Reassembled on initial scene sync or a changed instance-transform signature. Camera motion and Nanite LOD changes update the camera cut/cluster mask, not BVH geometry. |
| GDF clipmaps / radiosity | Incremental dirty slabs on camera movement; full bake only on initialization, reset, or invalidation. They are not rebuilt on every static frame. |
| Viewport image | Every actual dispatch traces and shades pixels. HighEnd GI adds 3 BVH AO rays and 2 BVH GI rays per visible hit; the BVH4 solar-disc path adds 4 shadow rays when selected. |
| Progressive Path Tracer | Traces one progressive sample batch per requested redraw. The default 256 spp target then suspends viewport dispatch until camera, scene, or display settings change. |

## Timing interpretation

The checked-in `FrontierTiming.log` contains HighEnd BVH4 spike records around frames 39290–39305 with roughly 30–34 ms in the primary trace. Path Tracer records show roughly 15 ms primary-trace cost in that captured run. These are observations from the log, not a fresh hardware benchmark. Separate UI-build and presentation spikes also appear in the log.

The old timing writer retained the previous GPU timestamps when the viewport was idle, so periodic entries could imply that a completed render was still running. `EditorLifecycle` now clears GPU/scene-sync measurements on frames with no successful viewport dispatch and marks `WorldDispatched` false. Timing rows are accumulated in memory; `FrontierTiming.log` is opened and written once when `EditorLifecycle::Finalize` runs on orderly editor shutdown. A forced process termination can lose the in-memory session log.

## Change and tradeoff

The default viewport engine changed from GTX GDF GI to **Path Tracer (Real-Time)**. This is a full progressive path-traced renderer, not a shadow-only substitution: it evaluates direct shadows and indirect light in the same path loop. If users explicitly choose HighEnd BVH4 GI, that engine retains its GI/AO rays and remains more expensive in the captured timing data. Path tracing also changes convergence/noise behavior; its real-time target is 256 spp with a one-sample batch by default.

## Verification

- Standalone C++20 telemetry test: verified the log file remains absent after initialization and appended samples; it is created with the buffered header/rows only at finalization.
- `git diff --check`: passed.
- Full build/runtime benchmark: not run in this sandbox; no configured build tree or `ExternalPackages/` dependency directory is present.
