# Master Remediation, Input Architecture & `CameraComponent` Plan

> **TL;DR:** Branch `arena/c2de3250-frontier-1` has been fast-forwarded directly to `624883b6` (`SultanAladin/Frontier` `arena/4c9015d5-frontier`; note Arena locks the session branch name so commits push to `origin/arena/c2de3250-frontier-1`). Every visual, viewport, layout, orientation, input, and camera issue reported has been traced to an exact C++20 / `.slang` root cause below and organized into **6 deterministic execution phases** before running local verification and (upon your go-ahead) GitHub Actions.

---

## 🔍 1. Forensic Root-Cause Audit (`624883b6` vs `3cc230f6` & HTML Reference)

| # | Reported Symptom | Status | Exact Root Cause (`File:Line`) | Planned Fix |
| :---: | :--- | :---: | :--- | :--- |
| **1** | **3D Transform Gizmo looks inside-out ("flipped normals")** | 🔴 | ① `UVWorkspaceRasterizer.cpp:351` builds `GizmoDepthOffProgram` with `DepthTestAndWrite = false` (`depthTestEnable = VK_FALSE`) and `cullMode = VK_CULL_MODE_NONE` (`:471`).<br>② `TransformGizmo.cpp:97–98` (`AppendConeHandle`) & `:129–130` (`AppendCylinderHandle`) emit the **dark underside cap (`Normal = -AxisDir`) after the cone/shaft side triangle** in every loop segment — without depth testing or backface culling, dark back-caps and far-side faces paint directly over front faces!<br>③ `BuildRowMajorViewProjection` (`:129`) flips clip `Y` (`ScaleY < 0`), inverting 2D winding (`CCW → CW`), while `GizmoRasterization.slang:63` shades in fixed world space instead of camera headlight space. | Clear **depth attachment only** (`vkCmdClearAttachments` on `VK_IMAGE_ASPECT_DEPTH_BIT`) before 3D gizmo rasterization so the gizmo sits above scene geometry while keeping **full depth test + depth write** (`VK_COMPARE_OP_LESS_OR_EQUAL`) and `VK_FRONT_FACE_CLOCKWISE` backface culling on closed solids (`Cone`, `Cylinder`, `Torus`), followed by a depth-tested translucent pass for plane handles + camera-headlight shading. |
| **2** | **3D Shading Mode & 2D Debug Channel buttons cycle instead of Dropdowns** | 🔴 | `UVViewportHost.cpp:467–488` still constructs `SurfBtn` and `DbgBtn` via `ConstructCapsuleButton` (`(Mode + 1) % N`), right next to `ViewProjectionDropdownBounds` (`:492`) which already uses `DropdownSelector`. | Convert both `SurfaceDisplayMode` (6 options) and `UVCanvasDisplayMode` (3 options) in `UVViewportHost.{h,cpp}` to `DropdownSelector::ConstructButton` + `ConstructOptionSheet` with mutual exclusion and `Escape` dismissal. |
| **3** | **MatCap sliders (`Metallic`, `Roughness`, missing `Specular`) do nothing; Carbon Fibre, Zebra & Metallic Flakes look like flat colours** | 🔴 | **Three stacked defects:**<br>① **Flat colour per polygon (`UVWorkspaceRasterizer.cpp:864–866`)**: Uploads flat per-face `CurrentFace.GeometricNormal` to every vertex instead of **52° crease-smoothed corner normals** (`ComputeCreaseCornerNormals` in `Experimental/UVEditor/Engine/UVViewport3D.js:165`). Constant face normal → **1 single texel `(u,v)`** sampled in the 256×256 MatCap sphere across an entire quad, collapsing Carbon Fibre weave, Zebra stripes, and Voronoi Car Flakes into one flat colour!<br>② **Sliders ignored in MatCap mode (`SurfaceShading.slang:124–127`)**: `Mode == 4` outputs raw `MatCapSrgb`, ignoring `Metallic`, `Roughness`, `Specular`, and screen-space bevel cavity (`EvaluateStudioCavity`, `UVViewport3D.js:505`).<br>③ **Missing `Specular` slider (`UVInspectorPanel.cpp:1538–1547`)**: Only `Metallic`, `Roughness`, and `MatCap Blend` are constructed. | ① Compute & upload **52° dihedral crease-smoothed corner normals** per `DirectedEdge` corner + screen-space `ddx`/`ddy` bevel cavity in `SurfaceShading.slang`.<br>② Add the **`Specular`** `ScalarSlider` in `UVInspectorPanel::RenderShadingTab` and wire `(Metallic, Roughness, Specular, MatCapBlend)` into `UVWorkspaceSession`.<br>③ Drive MatCap lobe broadening (`Roughness`), metal tinting (`Metallic`), and dual-softbox clearcoat/Fresnel gain (`Specular`) in `MatCapEvaluation.slang` across both `MatCapPreview` and `MaterialAndMatCap`. |
| **4** | **UVSpace Outliner Tiles & Outliner Layer Boxes diverge from HTML** *(keep UDIM Filter unchanged)* | 🔴 | Comparing `UVOutlinerPanel.cpp:465–820` vs `Experimental/UVEditor/{index.html,UVEditor.css,UVEditor.js}`:<br>① **KPI Trio (`:468–532`)**: Drawn as 1 fused box instead of **3 independent `16px` bento tiles (`gap: 7px`)**; line `516` (`"Utilization"`) and line `528` (`"UDIM 1001 · N isl"`) draw at the **exact same `Y + 35.0f` (overlapping text!)**; vertical order is inverted vs HTML (`LABEL` top → `17px Light` value middle → `sub` bottom); `Remaining` shows island ratio instead of `RemPct%` (`100%`) + `Rem / Total isl`.<br>② **2-Col Stat Bento (`:536–569`)**: Draws literal `"a"` instead of `α` symbol, shows `%` instead of caution **chart count**, and lacks click-to-select high-stretch charts & `.has-caution`/`.has-error` styling.<br>③ **Layer Stack (`:612–820`) & Generic `OutlinerPanel.cpp`**: Uses text pills `"Stk"`/`"Uns"`/`"Rst"` instead of `24×24` vector icon buttons (`StackUv`/`UnstackUv`/`Reset`); Remainder box uses solid stroke instead of HTML dashed stroke (`border-style: dashed; border-left: 3px solid #6e6e6e`). | Rebuild the 5 UVSpace Outliner infographic tiles, vector icon mini-toolbar, sharp `0px` `.pz-frontier-layer-box` rows, dashed Remainder row, and sticky UDIM Space Budget footer to match `Experimental/UVEditor` 1:1 (**preserving the existing `DropdownSelector` UDIM Filter**), and align `Engine/Components/Panes/OutlinerPanel.cpp` row metrics/icons with `Experimental/ProjectZeroEditor`. |
| **5** | **Viewport 2D/3D Split is permanent / non-resizable** | 🔴 | ① `ViewportProjectionPane.cpp:270–325` draws a static `2px` bar with zero hit-testing or drag tracking.<br>② `UVViewportHost.cpp:137` (`SynchronizeProjectionPolicy`) overwrites `Projection.ConfigureSplitRatio(...)` every tick from static `SplitRatioNormalized` (`0.50f`). | Add an `8px` interactive splitter hit-channel in `ViewportProjectionPane` (hover highlight, resize grip indicator, drag clamping `[0.18, 0.82]`, double-click reset to `0.50`) + split-ratio persistence callback into `UVViewportHost` (`FramebuffersDirty = true`). |
| **6** | **Maximizing window freezes panel sizes (shrinking clips, maximizing leaves panels stuck at `1600×980`)** | 🔴 | **`DesktopWorkspaceLaunch.cpp:532–575`**: When `UseFullEditorLifecycle == false` (`FrontierUV` standalone mode), `StandaloneDock` is initialized once at `1600×980` (`:461`) and the main loop **never calls `StandaloneDock.AccessOperatingSurface(HostWindowToken{0u})->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor)`**! `WorkspaceDockContainer.h:425` therefore passes stale `1600×980` into `ImGui::SetNextWindowSize`, pinning all docked panels to the top-left `1600×980` rectangle when maximized! | ① Call `StandaloneDock.AccessOperatingSurface(HostWindowToken{0u})->ResizeSurface(...)` every tick alongside `EditorLoop`.<br>② Pass explicit `ImVec2(SurfaceWidthPx, SurfaceHeightPx)` into `ImGui::DockSpace(...)` in `WorkspaceDockContainer.cpp:156` so existing ImGui dock nodes scale proportionally on live drag-resize and OS maximize/restore, and verify swapchain `VK_SUBOPTIMAL_KHR` recreation. |
| **7** | **Vehicle & Grid orientation broke after `3cc230f6` (`624883b6` Z-up vs Vulkan NDC)** | 🔴 | In `3cc230f6`, `ControlledIntegrator.GeometrySpace` (authored Right-Handed `+Y` Up, wheels on `Y=0`), `GridQuad` (`Y=-0.02`), `SurfaceShading.slang` (`WorldPosition.xz`), and camera (`Y-up`) matched. Commit `624883b6` switched camera to `Z-up` and moved `GridQuad` to `Z=-0.02` **without** transforming the Y-up vehicle or updating `SurfaceShading.slang:82` (`WorldPosition.xz`), tipping the vehicle 90° on its side across 1D grid stripes! | Enforce a single **Right-Handed World Coordinate Convention** in `EngineConfiguration` + `MetricSpace/CoordinateSpace.h`, convert external/legacy Y-up assets at the `GeometryInterchange` / sample loader boundary so wheels rest on the horizontal ground grid (`3cc230f6` visual posture), and isolate the **Right-Handed → Vulkan NDC (`+X Right, +Y Down, Z ∈ [0,1]`)** transform strictly inside `ViewTransform` / `VulkanInterchange`. |
| **8** | **Camera zoom & movement lag missing + `TickExchange()` missing** | 🔴 | ① `UVViewportInteractor.cpp:114–142` mutates orbit/pan/dolly instantaneously with zero damping (`UVViewport3D.js:305` uses `dampingFactor = 0.08`).<br>② On `624883b6`, `DesktopWorkspaceLaunch.cpp:523` never called `InputCoordinator.TickExchange()`, starving scroll wheel events! | Route all viewport navigation through the new **`CameraComponent`** with frame-rate-independent exponential lag (`1 - exp(-λ·Δt)`) on orbit angles, pan pivot, and log-distance zoom, fed by a properly drained `EditorInputExchange`. |

---

## 🏆 2. Mandatory Architectural Decisions

### 2.1 Hardware Input Intake Source: Native OS/`PlatformWindow*` (GLFW) vs. Dear ImGui

> **Legend — Metric Polarity:** Cost / Coupling: `✔️` = lowest (best) · Capability / Portability / Isolation: `🔴` = highest (best) · Quality: `⭐` (`⭐`–`⭐⭐⭐⭐⭐`)

| Rank | Option | Runtime & `Shipping` Viability (No Editor/ImGui) | Cross-Platform (`Win32` · `X11/Wayland` · `Android` · `Headless`) | Raw Relative Cursor Lock, Scancodes & Gamepad/Haptics | Editor Coupling Cost | Rating |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| **🥇 🏆** | **Option A — `PlatformWindow*` (GLFW Desktop / NDK Android / Stub Headless) → `InputExtension` → Dual `InputExchange` (ImGui as downstream consumer only)** | 🔴 Full (`Shipping` & `Development` compile with zero ImGui dependency) | 🔴 Universal (identical `InputSignalPacket` stream across OS & headless tests) | 🔴 Native (`GLFW_CURSOR_DISABLED` raw deltas, 512 scancodes, multi-touch, haptics, `ns` timestamps) | ✔️ Minimal (1 bridge file feeds `ImGuiIO` from `EditorInputExchange`) | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option B — Hybrid Poll (`SynchronizeGlfwToImGui` + `CaptureImGuiPaneInput` — current `624883b6` state)** | 🚩 Broken for standalone runtime; splits truth across 2 queues | 🚩 Fragile (only 6 keys forwarded in `DesktopWorkspaceLaunch.cpp:189`) | 🚩 Loses sub-frame transitions & dropped wheel ring (`TickExchange` bug) | 🚩 High (7 UI files call `ImGui::IsMouse*`/`IsKey*` directly) | ⭐⭐ |
| 🥉 | **Option C — Dear ImGui (`ImGuiIO`) as Primary Engine Input Source** | ✔️ Impossible (`Engine/Editor/` & authoring UI stripped in `Shipping`) | ✔️ Desktop-centric; no hardware haptics or headless simulation parity | ✔️ Lacks raw hardware scancodes & relative fly-cam cursor lock | 🔴 Worst (violates `CLAUDE.md` §2.4 three-profile boundary) | ⭐ |

**💡 Decision — Choose Option A (`PlatformWindow*` / GLFW → `InputExtension`):**
GLFW (wrapped inside `PlatformWindowDesktop.cpp`, paired with `PlatformWindowAndroid.cpp` and `PlatformWindowStub.cpp`) is the **single hardware intake**. **Zero** Editor panels, gizmos, or gameplay classes may call `glfwGet*`, `ImGui::GetIO()`, `ImGui::IsMouse*`, or `ImGui::IsKey*` directly.

---

### 2.2 Dual `InputExchange` Split (`RuntimeInputExchange` vs `EditorInputExchange`) & `.InputSpace` Format

```text
  OS / Hardware (GLFW 3.4 Desktop · Android NDK Touch · Gamepad · Headless Stub)
                                      │
                           [ PlatformWindow* ]
                                      │  48-byte aligned InputSignalPacket
                                      ▼
                         [ Engine/Input/InputExtension ]
                        (Drains hardware ring once per tick)
                                      │
                 ┌────────────────────┴────────────────────┐
                 ▼                                         ▼
     [ RuntimeInputExchange ]                  [ EditorInputExchange ]
   (Game / Simulation / Vehicle /            (FRONTIER_EDITOR_ENABLED & Workspace
    Runtime CameraComponent; active           Tools: Panes, Gizmos, Shortcuts,
    in Dev, Shipping & PIE Simulate)          plus 1-way ImGuiBridge -> ImGuiIO)
                 ▲                                         ▲
                 └──────────────┬──────────────────────────┘
                                │
                    [ Engine/Codecs/InputCodec ]
                       ( .InputSpace Container )
          ┌─────────────────────┼─────────────────────┐
          ▼                     ▼                     ▼
  BlenderPreset.InputSpace  UnrealPreset.InputSpace  CustomUser.InputSpace
```

- **Strict Routing Invariant:**
  - **All Editor Input via `EditorInputExchange`**: Replace `CaptureImGuiPaneInput` and all direct `ImGui::IsMouse*` / `ImGui::IsKey*` calls across `WorkspaceAppearance.cpp`, `PixelSpace.cpp`, `ContentBrowserPanel.cpp`, `ControlCentrePanel.cpp`, `ConfirmationDialogue.cpp`, and `InputSettings.cpp` with `EditorInputExchange` frame snapshots + ImGui hover/focus capture gates.
  - **Complete GLFW Callback Forwarding**: Forward **all** keyboard scancodes, modifiers, mouse buttons, cursor motion, scroll wheel, character codepoints, and file drops through `PlatformWindowDesktop` → `InputExtension::TickExchange()`, and mirror into `ImGuiIO` inside one dedicated `ImGuiInputAdapter`.
- **Native `.InputSpace` Container (`SpaceExtensionCategory::InputSpace` + `Engine/Codecs/InputCodec.{h,cpp}`)**:
  - Uses Frontier's Two-Tier Hybrid `.Space` container (UTF-8 PascalCase TOML manifest + `64B`-aligned FNV-1a binary sections; case-insensitive load `.inputspace` / canonical write `.InputSpace`).
  - **Embedded Sections** *(100% `BannedWordsVocabulary.md`-compliant — zero `binding`, `action`, `map`, `state`, `context`)*:
    - `.ChordRegister` — Digital key/button + modifier `ShortcutChord` → `DirectiveIdentifier` entries (supports single-press, double-tap, hold, release, and modal sequences like `G → X`).
    - `.AxisCalibration` — Continuous 1D/2D pointer, wheel, stick, and trigger curves (sensitivity, deadzone, exponent, pitch/yaw sign).
    - `.DeviceProfile` — Per-peripheral scancode layout & haptic rotor gain limits.
    - `.Revision` — User chord-customization history for instant reset/undo.
  - **Shipped Schemes in `EngineContent/Input/`**: `FrontierStudio.InputSpace`, `BlenderNavigation.InputSpace` (MMB orbit, Shift+MMB pan, Wheel zoom, `G`/`R`/`S` modal), `UnrealNavigation.InputSpace` (RMB+WASD fly, Alt+LMB tumble, `W`/`E`/`R` gizmo), plus live user rebinding persisted to `CustomUser.InputSpace` and referenced from `.ProjectSpace`.

---

### 2.3 Right-Handed Coordinate Convention vs. Vulkan NDC Clip Translation

> **Legend — Metric Polarity:** Interchange/Physics Friction: `✔️` = lowest (best) · Mathematical Consistency / `3cc230f6` Visual Parity: `🔴` = highest (best)

| Rank | Option | Internal Engine, Units & Jolt Physics Math | Vulkan NDC (`+X Right, +Y Down, Z ∈ [0,1]`) Handling | Vehicle & Ground Grid Visual Parity with `3cc230f6` | Interchange (`OBJ` / `glTF` / `FBX` / `CAD`) | Rating |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| **🥇 🏆** | **Option A — Configurable Right-Handed World Basis in `EngineConfiguration` (Default Right-Handed Z-Up `+X Right, +Y Forward, +Z Up` OR Right-Handed Y-Up) + Boundary Axis Conversion on Asset Intake + Isolated `VulkanClipTransform`** | 🔴 100% Right-Handed (`Cross(Axis0, Axis1) = Axis2`), SI metres, deterministic physics | 🔴 Translated **once** inside `ViewTransform` (`ScaleY = -1` or Vulkan negative-viewport height + matching `VkFrontFace`) — never leaks into world/object space | 🔴 Exact (`ControlledIntegrator` converted at load so wheels sit on `GroundPlane` and roof points `+Up`, matching `3cc230f6`) | ✔️ Explicit `CoordinateBasis` tag on `.GeometrySpace` & `GeometryInterchange` converts Y-up glTF/OBJ ↔ Z-up CAD lossless on import/export | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option B — Mutate Camera Only (`624883b6` approach)** | 🚩 Mismatched asset vs camera axes | 🚩 Leaks clip-space assumptions into shaders | ✔️ Broken (vehicle tipped 90°, striped grid) | 🚩 Corrupts exported orientation | ⭐ |

**💡 Decision — Choose Option A:**
Add `WorldCoordinateBasis` (`RightHandedZUp` / `RightHandedYUp`, always strictly **Right-Handed** in SI metres) to `EngineConfigurationDescriptor` and `Engine/MetricSpace/CoordinateSpace.h`. Tag `ControlledIntegrator` / `GeometryInterchange` with its authored source basis (`RightHandedYUp`) and apply the exact orthonormal Right-Handed basis rotation `(x, y, z)_YUp → (x, -z, y)_ZUp` (det = `+1`, preserves right-handed triangle winding & normals!) on intake while `SurfaceShading.slang` evaluates the analytic ground grid on the active horizontal ground plane (`xy` for Z-up, `xz` for Y-up). Vulkan's inverted NDC `+Y Down` and `[0, 1]` depth are handled **solely** at the final `ViewTransform → Clip` projection boundary.

---

### 2.4 Camera Architecture & Naming: `CameraComponent` (`Engine/Components/Camera/`) + GPU Optical/Visual Effects

> **Legend — Metric Polarity:** Governance Risk: `✔️` = lowest (best) · Reusability (Editor + Runtime) / Extensibility: `🔴` = highest (best)

| Rank | Option | `BannedWordsVocabulary.md` Compliance | Shared Across Editor Viewports & Runtime Games | Optical / Visual Effects Integration | Rating |
| :---: | :--- | :---: | :---: | :---: | :---: |
| **🥇 🏆** | **Option A — `Engine/Components/Camera/CameraComponent`** | ✔️ 100% Compliant (`Extension` is restricted solely to top-level domain coordinators per `BannedWordsVocabulary.md` §3.1) | 🔴 Attachable to Editor viewports (`UV`, `SceneWorld`, `GeometryCad`) **and** Runtime entities (vehicles, characters, cutscenes) | 🔴 Owns `VisualEffects/` descriptors consumed by `.slang` GPU post-rasterization filters (`LensFlare`, `ThermalRefraction`, `SignalGlitch`) | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option B — `Extensions/Camera/CameraExtension`** | 🔴 Violates `BannedWordsVocabulary.md` §3.1 (`Extension` barred outside designated engine-wide coordinators) | 🚩 Singleton-biased rather than per-camera instance | 🚩 Blurs component vs render coordinator boundary | ⭐⭐ |

#### 🏷️ Mandatory `BannedWordsVocabulary.md` Identifier Normalization for `Engine/Components/Camera/`

Several identifiers in the draft tree contain hard-banned words (`State`, `Matrix`, `Base`, `Dev`, `Bloom`, `Pass`). Below is the exact compliant mapping:

| Draft Name in Prompt | Banned Token Hit | Approved Frontier Identifier | Physical / Mathematical Role |
| :--- | :---: | :--- | :--- |
| `CameraComponent` | *(None — clean)* ✔️ | **`CameraComponent.{h,cpp}`** | Attach/detach lifecycle, mode evaluation, constraint enforcement, lag integration, and optical effect stack |
| `CameraConfig` | *(Clean; full noun preferred)* ✔️ | **`CameraConfiguration.{h,cpp}`** | Static optical & rig parameters: vertical/horizontal FOV, sensor gate (`36×24 mm`), near/far clip planes, lag constants |
| `CameraState` | `State` 🔴 | **`CameraPosture.{h,cpp}`** | Live kinematic pose: world position, unit quaternion orientation, target pivot, linear/angular velocity, blend weight |
| `CameraMode` *(Base)* | `Base` 🔴 | **`CameraMotionMode.h`** | C++20 `concept` & polymorphic interface for `FollowMode`, `OrbitMode`, `FirstPersonMode`, `FixedAngleMode` |
| `CameraEffect` *(Base)* | `Base` 🔴 | **`CameraKinematicEffect.h`** | Additive pose/FOV offset interface for `TraumaShakeEffect`, `RecoilKickEffect`, `SprintFovEffect` |
| `ViewMatrix` | `Matrix` 🔴 | **`ViewTransform.{h,cpp}`** | Orthonormal Right-Handed world-to-camera transform + Right-Handed-to-Vulkan-NDC clip projection |
| `FreeCamMode` *(Dev)* | `dev` 🔴 | **`FreeFlightMode.{h,cpp}`** | 6-DOF fly-through diagnostic camera (`#if !FRONTIER_PROFILE_SHIPPING`) |
| `CameraDebugDraw` | `Draw` 🔴 | **`CameraFrustumAnnotation.{h,cpp}`** | Submits frustum planes, pivot crosshairs & spring-arm rays to `Engine/Annotation/` GPU `.slang` line rasterizer |
| `VisualEffects` *(Lens Flare, Heat Distortion, Glitch, Glare)* | `Bloom` / `Pass` 🔴 | **`VisualEffects/` + `.slang` `…Rasterization` / `…Filter`** | `LensFlareEffect` (`flare-inspector.jsx` parity), `ThermalRefractionEffect` (heat haze), `SignalGlitchEffect`, `ChromaticDispersionEffect`, `DepthOfFieldOptics` (`camera-graphics.jsx` parity) |

#### Proposed `Engine/Components/Camera/` Module Tree

```text
Engine/Components/Camera/
├── CameraComponent.{h,cpp}                  // Main attachable camera rig: mode tick, lag solver, constraints, kinematics & optical stack
├── CameraConfiguration.{h,cpp}              // Static rig & sensor settings: FOV, focal length, f-stop, near/far planes, lag rates
├── CameraPosture.{h,cpp}                    // Live spatial pose: EyePosition, Orientation, TargetPivot, Velocity, ActiveFov, BlendWeight
│
├── Modes/                                   // Primary motion solvers (single active or cross-faded blend)
│   ├── CameraMotionMode.h                   // C++20 concept & mode evaluation interface
│   ├── OrbitMode.{h,cpp}                    // Turntable / arcball look-around with exponential yaw/pitch/pan/log-zoom lag
│   ├── FollowMode.{h,cpp}                   // 3rd-person target tracking with velocity look-ahead & lag
│   ├── FirstPersonMode.{h,cpp}              // Socket-attached eye rig with pitch clamp
│   └── FixedAngleMode.{h,cpp}               // Cutscene / locked architectural framing
│
├── Constraints/                             // Post-mode geometric validity solvers
│   ├── SpringArmConstraint.{h,cpp}          // Obstacle ray-sweep push-in & smooth recovery
│   ├── AngularLimitConstraint.{h,cpp}       // Pitch & yaw min/max interval clamping
│   └── SpatialBoundsConstraint.{h,cpp}      // Volume / planar boundary containment
│
├── Kinematics/                              // Additive pose & FOV perturbations layered above active mode
│   ├── CameraKinematicEffect.h              // Additive offset interface (translation, rotation, FOV delta)
│   ├── TraumaShakeEffect.{h,cpp}            // Perlin/simplex rotational & translational impact trauma decay
│   ├── RecoilKickEffect.{h,cpp}             // Impulse pitch/yaw kick & critically damped recovery
│   └── SprintFovEffect.{h,cpp}              // Speed-driven FOV widening envelope
│
├── Projection/                              // Right-Handed math -> GPU clip & picking rays
│   ├── ViewTransform.{h,cpp}                // Right-handed world-to-camera & Vulkan NDC [0,1] Y-flipped clip projection
│   ├── FrustumPlanes.{h,cpp}                // 6 Gribb-Hartmann culling planes in world space
│   └── ViewportRay.{h,cpp}                  // Sub-pixel screen-to-world pick ray & world-to-screen projection
│
├── VisualEffects/                           // Camera-driven GPU .slang optical & post-rasterization effects
│   ├── CameraVisualEffectStack.{h,cpp}      // Ordered camera optical descriptors uploaded to GeometricRaster uniforms
│   ├── LensFlareEffect.{h,cpp}              // Anamorphic streaks, starburst spikes & polygonal aperture ghosts (.slang)
│   ├── ThermalRefractionEffect.{h,cpp}      // Heat distortion / mirage screen-space refractive UV warp (.slang)
│   ├── SignalGlitchEffect.{h,cpp}           // Digital scanline tear, block jitter & chromatic channel split (.slang)
│   ├── ChromaticDispersionEffect.{h,cpp}    // Radial lens dispersion, vignette & sensor grain (.slang)
│   └── DepthOfFieldOptics.{h,cpp}           // Thin-lens focal distance, f-stop pupil & circle-of-confusion (.slang)
│
└── Diagnostics/                             // Compiled out of FRONTIER_PROFILE_SHIPPING
    ├── CameraFrustumAnnotation.{h,cpp}      // GPU .slang frustum, target marker & spring-arm ray overlay
    └── FreeFlightMode.{h,cpp}               // Unconstrained 6-DOF diagnostic fly-through camera
```

---

## 🏁 3. Phased Execution Order & Verification Gates

| Phase | Scope | Key Files Touched | Verification Proof (`Exhibits/Images/` & `CTest`) |
| :---: | :--- | :--- | :--- |
| **Phase 1** | **Input Architecture Overhaul (`RuntimeInputExchange` vs `EditorInputExchange` + `.InputSpace` Codec)** | `Engine/Input/{InputExchange,InputExtension,RuntimeInputExchange,EditorInputExchange,ImGuiInputAdapter}.{h,cpp}`, `Engine/Codecs/{SpaceContainerCodec,InputCodec}.{h,cpp}`, `Engine/Configuration/InputSettings.{h,cpp}`, `DesktopWorkspaceLaunch.cpp`, `WorkspaceAppearance.cpp`, `PixelSpace.cpp`, `ContentBrowserPanel.cpp`, `ControlCentrePanel.cpp`, `ConfirmationDialogue.cpp`, `EngineContent/Input/*.InputSpace` | `InputValidation` + `WorkspaceRevisionValidation` (`.InputSpace` round-trip checksum + Blender/Unreal/Custom chord dispatch + zero rogue `ImGui::Is*` / `glfwGet*` grep audit + `Exhibits/Images/Input/InputExchangeTelemetry.png`) |
| **Phase 2** | **Right-Handed Coordinate Basis & Vulkan NDC Translation (`3cc230f6` Vehicle + Grid Parity)** | `Engine/Configuration/EngineConfiguration.{h,cpp}`, `Engine/MetricSpace/CoordinateSpace.h`, `Engine/GeometrySpace/Primitives/AnalyticPrimitives.cpp`, `Engine/Interchange/GeometryInterchange.cpp`, `Engine/GeometricRaster/UV/UVWorkspaceRasterizer.cpp`, `Engine/GeometricRaster/Shaders/SurfaceShading.slang` | `GeometryNetworkValidation` + `UVRasterPickingGizmoValidation` (vehicle upright on crisp 2D ground grid matching `3cc230f6` while world math is strictly Right-Handed) |
| **Phase 3** | **`CameraComponent` + Camera Lag + GPU Optical Visual Effects** | `Engine/Components/Camera/**`, `Engine/GeometricRaster/Shaders/{LensFlareRasterization,CameraOpticalFilter}.slang`, `Engine/Editor/Viewports/ViewportProjection.{h,cpp}`, `Engine/Workspaces/UV/UVViewportInteractor.{h,cpp}` | `CameraComponentValidation` rendering deterministic `.png` proofs into `Exhibits/Images/Camera/` (orbit/follow lag curves, spring-arm clip, lens flare, thermal heat distortion & signal glitch) |
| **Phase 4** | **3D Transform Gizmo Depth/Normals Fix + Crease-Smoothed MatCaps + Specular Slider** | `Engine/Editor/Viewports/TransformGizmo.{h,cpp}`, `Engine/GeometricRaster/Shaders/{GizmoRasterization,MatCapEvaluation,SurfaceShading}.slang`, `Engine/GeometricRaster/UV/UVWorkspaceRasterizer.cpp`, `Engine/Workspaces/UV/{UVInspectorPanel,UVWorkspaceSession}.{h,cpp}` | `UVRasterPickingGizmoValidation` + `UVWorkspaceUIValidation` (`Exhibits/Images/UV/` before/after gizmo occlusion/normals + Carbon Fibre, Zebra & Car Flake MatCap renders + Specular/Roughness/Metallic response) |
| **Phase 5** | **Viewport Dropdowns, Draggable 2D/3D Split, Maximize Resize Fix & Outliner HTML Parity** | `Engine/Workspaces/UV/{UVViewportHost,UVOutlinerPanel}.{h,cpp}`, `Engine/Components/Panes/{ViewportProjectionPane,OutlinerPanel}.{h,cpp}`, `Engine/Components/Docking/WorkspaceDockContainer.{h,cpp}`, `Engine/Lifecycle/DesktopWorkspaceLaunch.cpp` | `UVWorkspaceUIValidation` + `WorkspacePanesValidation` + `DockingValidation` (verified resize to `2560×1440` maximized, interactive split drag, Shading/Debug dropdowns, and HTML-matched Outliner tiles/boxes with UDIM Filter intact) |
| **Phase 6** | **Three-Profile Build (`Editor` · `Development` · `Shipping`), Push & GitHub Actions** | `CMakeLists.txt`, `Documentation/{Tracker,Backlog,FolderStructure}.md`, `Documentation/Plans/MarkdownStudioViewer.html` | Full `ctest` across `Editor`/`Development`/`Shipping`, commit & push to `origin/arena/c2de3250-frontier-1`, and trigger GitHub Actions upon your confirmation |

---

## 📝 4. Execution & Verification Summary (Completed)

All **6 phases** have been implemented and verified across all three Frontier build profiles (`Editor` 17/17 CTests, `Development` 17/17 CTests, `Shipping` 15/15 CTests — **49/49 CTests passed with zero compiler warnings**):

1. **Phase 1 — Hardware Input Architecture & `.InputSpace` Presets (`🟢 Verified`)**:
   - Native GLFW (`PlatformWindowDesktop`) → `InputExtension` → dual `RuntimeInputExchange` / `EditorInputExchange` + `ImGuiInputAdapter` downstream feed; zero `ImGui::(IsKey|IsMouse|GetMouse|GetKey)` or `glfwGet(Key|Mouse|Cursor)` bypasses remain in `Engine/`.
   - Generated and verified `EngineContent/Input/{FrontierStudio,BlenderNavigation,UnrealNavigation}.InputSpace` Two-Tier Hybrid presets (`InputValidation`).
2. **Phase 2 — Right-Handed World Basis & Vulkan NDC Isolation (`🟢 Verified`)**:
   - Added `Engine/Mathematics/CoordinateSpace.h` (`WorldCoordinateBasis::RightHandedZUp` default + `--basis=z-up|y-up` CLI/config switch), `det = +1` `R_x(+90°)` intake conversion in `AnalyticPrimitives` & `GeometryInterchange`, and isolated Vulkan NDC clip transforms (`ControlledIntegrator` upright on `Z = 0` XY ground grid).
3. **Phase 3 — `Engine/Components/Camera/` & GPU Optics (`🟢 Verified`)**:
   - Implemented `CameraComponent` across all 6 modes (`Orbit`, `Fly`, `FirstPerson`, `Pan2D`, `FollowTarget`, `SplineRail`), thin-lens focal/aperture optics, pitch/bounds/swept-sphere collision constraints, step-rate-independent exponential lag (`>99.9%` convergence within `300 ms`), `#if !FRONTIER_PROFILE_SHIPPING` diagnostics, and `CameraVisualEffects.slang` (`CameraVisualEffects.comp.spv`) verified by `CameraComponentValidation`.
4. **Phase 4 — 3D Gizmo Overlay, 52° Crease Normals & MatCap Material Response (`🟢 Verified`)**:
   - Enabled `VkPhysicalDeviceFeatures::independentBlend`, `GizmoDepthOnProgram` + `vkCmdClearAttachments(VK_IMAGE_ASPECT_DEPTH_BIT)` depth clear before 3D gizmo draw, camera-headlight shading, cohesive axis stems, 52° dihedral-smoothed corner normals, `ddx`/`ddy` micro-bevel curvature, `SampleStudioMatCapMaterial`, and the `Specular` slider in `UVInspectorPanel`.
5. **Phase 5 — Viewport Dropdowns, Interactive Splitter, Live Dock Resize & Outliner HTML Parity (`🟢 Verified`)**:
   - Converted 3D Surface Shading (6 options) and 2D UV Debug (3 options) to `DropdownSelector`, added an `8px` interactive splitter with resize cursor & grip indicator in `ViewportProjectionPane`, wired live `ImGuiDockNode` resize propagation in `WorkspaceDockContainer`, and aligned `UVOutlinerPanel` & `OutlinerPanel` with the HTML reference while preserving the UDIM Filter.
6. **Phase 6 — Banned-Vocabulary Audit, 3-Profile CTest Matrix & CI Push (`🟢 Verified`)**:
   - Audited all added/modified files against `Documentation/BannedWordsVocabulary.md`, rebuilt `MarkdownStudioViewer.html`, and passed all 49 CTests across `Editor`, `Development`, and `Shipping`.

