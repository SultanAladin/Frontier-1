# Camera, SDF AO & XeGTAO Remediation Report

Baseline: rebuilt from `c1bbbfd` (the commit named safest). Later commits `6936e8c`
(Project Browser) and `70fc526` (ThoughtLog) were cherry-picked and audited, not trusted blindly.

## 1. Why you could not use the gizmo (root cause)

Two camera models ran at the same time in `EditorLifecycle::ProcessShowcaseViewportInteraction`:

1. **RMB model (correct Unreal model):** RMB mouse-look + WASD/QE fly.
2. **LMB model (the bug):** LMB drag performed "yaw + horizontal walk", and `FlyKeysPossible`
   included `LeftHeld`, so WASD also flew while LMB was held.

Gizmo dragging is also LMB. Hover came **only** from the GPU pick patch
(`QueryLastGpuPickAtPixel`); whenever that returned `None` (stale patch, CPU viewport, headless),
`BeginDragInteraction` never ran, `QueryDragging()` stayed false — and the LMB-walk branch then
moved the **camera** instead of the object. Every gizmo drag became a camera walk.

### Fix (single model, `b65d2e1`)

| Input | Behaviour now |
|---|---|
| RMB held | Mouse-look + WASD/QE, Shift ×3 sprint, Ctrl ×0.3 slow, cursor locked |
| LMB | Gizmo drag & selection **only** — never camera, never fly keys |
| MMB / LMB+RMB | Truck/pedestal pan |
| Alt+LMB / Alt+RMB / Alt+MMB | Orbit / dolly / pan |
| Wheel | Fly-speed ×0.10–×16 (never dolly-zoom) |
| Arrows + PgUp/PgDn | Keyboard flight (no mouse button) |
| W/E/R/Q (not flying) | Gizmo Translate/Rotate/Scale/Universal |

Plus `TransformGizmo::EvaluateRayHover` — analytic CPU ray hover over the exact handle geometry
`BuildTriangleStream` emits (axes, scale band, plane quads, rotation arcs, centre ring). Editor
uses GPU pick first, CPU hover as fallback, so the gizmo works on every presentation path.

Validation added: LMB drag must produce zero camera drift; CPU hover must resolve the +X
translate handle and miss cleanly (`ProjectBrowserValidation` §g).

## 2. Project Browser (audited from `6936e8c`, completed)

- Library seeds **Default Project only**; Cornell-Studio removed from code and Content
  (verified: zero `Cornell` matches in `Engine/`/`Exhibits/` code after the pick).
- New Project writes a real blank `MainWorld.WorldSpace` (single ground plane + clear-sky
  camera) and opens it in Frontier; details pane now offers **Frontier only** (remaining
  UV-workspace advertising text/list removed in this pass).

## 3. Thought Log (audited from `70fc526`, extended)

RAM-only lock-free ring (1M records) + background aggregator thread; **no disk I/O while the
app runs**; single flush to `Logs/FrontierThoughtLog.txt` at application close (explicit
`Shutdown()` + `atexit` fallback). Added `Editor.ViewportInteraction` scope in this pass.
Per-scope output: calls, total, avg, min, max, P50/P95, share of frame — sorted for
optimisation targeting.

## 4. Camera lag (new, reusable)

`Engine/Components/Camera/CameraLagComponent` — owns no editor state, reusable by any camera
(follow/cinematic/gameplay): location lag on/off + speed (1/s), rotation lag on/off + speed,
maximum lag distance cap, `SnapTo` priming. Built on the existing
`Kinematics::EvaluateExponentialBlendFactor` (step-rate independent, no new math). Editor wiring:
input moves the *desired* pose, the component damps the rendered pose toward it each tick
(including settle after release); snapped on scene populate and Frame Selection so it never
sweeps across the scene. Controls listed in `UIControllableItems.md` §9 for your prototype.
Covered by `CameraComponentValidation` §7 (trail, settle, cap, disabled=snap).

## 5. Why SDF AO looked terrible — and the fix

Old `EvaluateGlobalDistanceAmbientOcclusion` was **not** cone AO. It marched 4 samples in a
straight line along the normal and compared SDF distance against height above the surface:

- Occluders **sideways** (walls, nearby objects) contributed nothing — leaking.
- Occluders directly overhead darkened even mid-room points — smearing.
- `×1.85` geometric steps + `exp(-StepT·0.55)` absolute-metre falloff → visible banding that
  changes with scene scale; 64³ trilinear field added blotch. You remembered cleaner AO because
  proper distance-field AO (UE DFAO / voxel cone AO) traces a **hemisphere of cones**.

Fix: 5 cones (1 axial + 4 at 60°, cosine-weighted, radius clamped 0.40–2.75 m, voxel-aware lift),
reusing the existing `TraceGlobalDistanceConeVisibility`. Same call site, no GI change.

### New SDF debug views (Debug dropdown 12–16)

| # | View | Shows |
|---|---|---|
| 12 | SDF Distance | Signed-distance heat at the hit (red inside / green surface / blue far) |
| 13 | SDF with Materials | Material albedo lit by the SDF gradient normal |
| 14 | SDF Normal | GDF gradient normal (central differences) |
| 15 | SDF AO Raw | Cone AO **before** spatial filtering — compare with filtered ch 9 |
| 16 | SDF Cascade | Fine (green) / coarse (orange) / outside (red) cascade coverage |

Use 15 vs 9 to judge whether dirt comes from the AO itself or the à-trous filter; 12/16 to judge
field quality/coverage. CPU Tiered path implements 13/15; 12/14/16 are GPU-path views.

## 6. XeGTAO — yes, added (toggleable, PBR-only, no GI)

Decision: **SDF cone AO stays** (world-space, works off-screen, feeds GI engines), **XeGTAO is
added** for direct PBR ambient detail the coarse 64³ field cannot resolve.
`EvaluateGroundTruthAmbientOcclusion`: 4 slices × 2 sides × 4 steps horizon search over the
G-buffer, cosine-weighted visibility integral per slice (XeGTAO formulation). In the composite
it only tightens the ambient/indirect/specular-AO terms (`min(SDF_AO, XeGTAO)`) — direct sun is
untouched and **zero GI is added**. Toggle in Viewport Settings sheet ("XeGTAO (PBR AO)"),
strength 0–2 plumbed through `WorldRenderConfiguration` → spare
`GlobalDistanceBakeControl.w` uniform bits (no uniform-layout change).

## 7. Verification status (executed in this session)

Full Editor-profile build (cmake + Slang + SwiftShader Vulkan) completed with zero errors.
All four validation binaries **PASSED**:

| Validation | Result |
|---|---|
| `CameraComponentValidation` | PASS — incl. new CameraLag §7 (trail / settle / cap / disabled=snap) |
| `ProjectBrowserValidation` | PASS — Default-Project-only browser, Unreal RMB camera (§f, lag-aware), LMB-zero-drift + CPU gizmo hover (§g), GPU compute render |
| `GtxSdfGiShadowValidation` | PASS — HDR RMSE 0.070–0.072 GTX / 0.052 HighEnd (limits 0.134) after the AO change |
| `TieredWorldRenderValidation` | PASS — 24 passed, 0 failed, incl. debug-view proofs |

Thought Log evidence (from the validation run, flushed only at close):
`Render.WorldViewportRgba8 — 3 calls, 2123 ms total, P50 744.8 ms` in
`build/Logs/FrontierThoughtLog.txt`.

Known limits: XeGTAO search radius fixed at 0.85 m (expose in prototype if wanted); SDF debug
views show glass as black like pre-existing channels 1–7; CPU Tiered path implements 13/15
only (12/14/16 are GPU-path views); XeGTAO visual quality still wants an eyeball pass in the
real editor (SwiftShader proofs here are numeric, not aesthetic).
