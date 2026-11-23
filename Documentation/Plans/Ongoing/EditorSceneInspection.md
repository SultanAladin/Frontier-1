# Deferred scene inspection and render controls

Status: **partially implemented (2026-10-28)**. Steps 1–4 are implemented for the environment slice
(Sun, atmosphere, clouds, fog, wind): editor-profile record descriptions, HTML outliner groups/counts,
inspector host with pinned CSS metrics, session-owned records in `FrontierMain`. The retained scene
frame is a host CPU render, not the viewport (step 6), and no ReSTIR/reflection controls (step 5),
transforms/gizmos (step 7), ImGui runtime capture or Windows run exist yet. Vehicle dynamics and Jolt
remain deferred. Evidence: `Exhibits/EnvironmentScene/README.md`. This plan does not establish
renderer or UI parity.

## Verified starting point

| Area            | Present status                                             |
| --------------- | ---------------------------------------------------------- |
| SDK editor      | Outliner and Log Stream registered; viewport empty         |
| Scene directory | Editor/development registration; shipping excluded         |
| Outliner        | Search, tabs, counts, selection, nesting and visibility    |
| Scene creation  | Placeholder records and spawn requests                     |
| Workspace UI    | Separate editor route; viewport unconnected                |
| Renderer        | Bounded software-BVH replay; no editor linkage             |

Inspected paths: `Frontier/Applications/Editor/FrontierMain.cpp`, `Frontier/Engine/Scene/RecordEntry.cpp`,
`SceneDirectory.cpp`, `SceneRecord.h`, `Frontier/Engine/Interface/SceneDirectoryPanel*`,
`UI/Editor/EditorWorkspace.cpp`, root `CMakeLists.txt` and `Frontier/Engine/Rendering/PathReplayVulkan.*`.

Confirm which application and build target the user runs before joining the two UI routes. Reuse the existing
panels and docking/log stream; do not assume that an Outliner registered in one executable is registered in another.
`FRONTIER_EDITOR`, used by the root UI build, is not automatically equivalent to `FRONTIER_EDITOR_PROFILE`.

## Reference behaviour to reproduce

Authoritative HTML source: `SultanAladin/Frontier`, commit `6afb6f00b5f25c45f7cda92c7da65b64845768f3`,
under `Experimental/VehicleHmiPrototype`. [Pinned commit](https://github.com/SultanAladin/Frontier/commit/6afb6f0).
Inspected `Source/EditorShell.js` (`3aa43a2`), `InspectorPages.js` (`8cdb58c`), `EditorControls.js` (`501d60e`),
`EditorTransforms.js` (`7f5dc20`), `ResamplingSequence.js` (`c92b9ee`) and `DisplayProjection.js` (`deddf61`).
The complete local Slate exhibit is an older, different pin; use it for comparison, not as proof of exact parity.

| Area              | Reference behaviour / controls                                                               |
| ----------------- | -------------------------------------------------------------------------------------------- |
| Outliner          | Render, Environment, Forcefield, Objects, Vehicle, Effects, Destruction and Level groups     |
| Rows              | Collapsible groups, counts, search by name/type, matching children, indentation and eye      |
| Selection         | Selection rebuilds the inspector; preserve valid selection and handle removal safely        |
| Object inspection | Per-record cards/actions, live metadata, state, optional transforms and viewport gizmo       |
| GI architecture   | Path bounces 1–4, initial candidates 1–4, visibility, NEE/RIS, roulette and outlier controls |
| ReSTIR reuse      | Temporal/spatial, visibility validation, radiance refresh, count 0–8, radius 2–48 px        |
| GI adjustment     | Temporal M limit 1–40 and indirect intensity 0–3                                             |
| Reflection        | Enable, tracing mode, bounces 1–4, intensity 0–3, roughness cutoff 0.15–1                  |
| Quality / image   | Quality grade, full/half/quarter trace resolution, scale, exposure, shadows, denoiser, TAA   |
| Display           | Final, direct, diffuse GI, denoised/raw irradiance, reflections, structures, cards, normals  |
| Other diagnostics | Albedo, cluster views, BVH/SDF views, readout and slice inset where real outputs exist       |
| Viewport controls | Display selector, camera, readout and clean view where native functionality exists           |

The demo labels its compute-BVH path “RTX” and its alternate path “GTX”; those labels do not establish hardware
ray tracing or a genuinely non-ray-traced GI algorithm. In native UI distinguish software triangle traversal,
actual hardware ray queries/RT, distance-field ray marching and any true non-ray-traced GI mode. Never represent an
unimplemented capability as enabled or claim that distance-field ray marching is non-ray-traced.

## Deferred implementation sequence

1. Inventory the executable, panel registration, record types, renderer capabilities and profile macros. Reconcile
   the workspace UI with the SDK Outliner without duplicating the scene directory or removing existing log/viewport UI.
2. Adapt the existing Outliner to the reference's groups, search, enabled/disabled counts, metadata and selection. Keep
   stable record tokens and lifetimes; synchronize visibility with the actual scene, not just a painted eye icon.
3. Add an inspector host and a typed, editor-only per-record description/construction path. Each supported record
   registers its own menu/editor metadata during editor-conditional construction and builds its own controls when
   selected. Keep runtime scene data independent of a particular widget toolkit and handle deletion/reselection.
4. Gate per-record editor metadata and inspector construction with the established profile configuration using
   `#if defined(FRONTIER_EDITOR_PROFILE)` or an equivalent explicit editor-only condition. Preserve the existing
   development-only directory behaviour where needed; do not silently include editor metadata or UI libraries in
   shipping. Ensure every translation unit sees a consistent profile and binary layout.
5. Bind ReSTIR, reflection and display controls to the real renderer configuration and outputs. Document units,
   bounds, defaults, unsupported controls, reset/invalidation policy and whether a change reallocates or reloads.
   Edits must change rendered results, not merely modify an inspector copy; persist settings through existing TOML
   conventions only after defining ownership and compatibility.
6. Connect the renderer's actual image to the existing viewport when its submission, synchronization, resize and
   presentation contracts are ready. Maintain a clear placeholder until then; no UI screenshot is rendering proof.
7. Add object transforms and gizmos only for record types with authoritative transform and scene-update paths.
   Vehicle/Jolt integration stays outside this phase unless separately requested.

## Acceptance and evidence

- Build and exercise the chosen editor application and the existing directory tests. Verify filtering, grouping,
  selection, expansion, visibility, deletion, live counts, record construction and inspector lifetime.
- Verify editor/development/shipping profiles separately, including absence of inspector symbols/metadata and UI
  dependencies in shipping. Audit profile consistency across static libraries; do not rely on one header macro alone.
- For each available control, prove that the actual renderer receives the new configuration and that history resets
  or survives only as intended; cover unsupported modes and invalid values. Exercise display changes and resizing.
- Capture the native application and interaction evidence with device/build details. Compare against the pinned
  demo's source and rendered reference; disclose missing assets/features and any nonmatching groups or controls.
- Test Windows when the required toolchain and UI environment are available. CPU Vulkan / headless fixtures are
  useful regression evidence, not physical-GPU, Windows, editor-interaction or final visual-fidelity evidence.
