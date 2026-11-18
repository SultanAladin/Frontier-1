# Frontier Component Editor Architecture Plan (Blueprint Class Workspace, Physics Constraints, PBR Direct Rendering & `BP_ControlVehicle`)

**Status:** Approved Design Specification (Pre-Implementation Blueprint)  
**HTML Prototype Reference:** `Experimental/ComponentViewer/` (20/20 checks passing via `CheckComponentViewer.mjs`)  
**Visual Reference Captures:** `Exhibits/Images/ComponentEditor/HtmlReference/*.png` (5 Chromium WebGL captures verified)  
**Target Modules:** `Engine/BlueprintSpace/`, `Engine/SimulationSpace/PhysicsSpace/`, `Engine/Interchange/`, `Engine/GeometricRaster/`, `Engine/Shaders/ComponentEditor/`, `Engine/Components/`, `Engine/Workspaces/ComponentEditor/`, `Engine/Editor/`, `Exhibits/`  
**Hard Invariants:**
- **1:1 UI Parity with `Experimental/ComponentViewer/`** — every layout region, metric tile, segmented switcher, 8-tile constraint picker, curve drawer, Construction Script view, and Simulation HUD matches the verified HTML captures.
- **PBR Rendering Without GI** — unified `WorldRenderEngine::OpenPbrDirect` (`TieredWorldRenderSequence` + `VulkanWorldRenderSequence` + `WorldViewportRadiometricCompute.slang`) with zero GI bounces, plus `.slang` shaders for all 7 inspection channels and volumetric/X-ray gizmo visuals (zero CPU `ImDrawList` viewport overlays).
- **Physics Constraints Replace Sockets** — every HTML `Socket` maps to a real JoltPhysics v5.6.0 `ConstraintInstance` (`Fixed`, `Hinge`, `Slider`, `Ball`, `Spring`, `Spin`, `Tether`, `Dock`) with solver-driven forces, limits, motors, and break loads.
- **Typed Outliner Instances & World Placement** — lean contiguous instance arrays (`GeometryInstance`, `ConstraintInstance`, `ParticleInstance`, `FluidInstance`, `LightInstance`, `CameraInstance`, `CollisionVolumeInstance`, `VehicleDynamicsInstance`) inside a `.BlueprintSpace` class that instantiates directly into the World Outliner (`OutlinerPanel`) with per-instance overrides.
- **Strict Repository Governance** — zero `And` conjunctions across all filenames/identifiers, zero banned vocabulary (`Mesh`, `Node`, `Graph`, `Frame`, `Pass`, `Stage`, `Badge`, `Card`, `Pill`, `Menu`, `Popup`, `Palette`, `Table`, `Data`, `Info`, `Object`, `Parent`, `Child`, `Family`, `Make`, `Draw`, `Commit`, `Compose`, `State`, `Value`, `Flag`), `Editor` / `Development` / `Shipping` 3-profile isolation.

---

## 1. Executive Architecture Overview

Frontier's **Component Editor** is the native C++20 + Vulkan `.slang` equivalent of Unreal Engine's Blueprint Class Editor. It unites five engine layers into one self-contained authoring workspace (`ComponentWorkspaceHost`) whose compiled output (`.BlueprintSpace` class definitions) can be spawned and overridden inside the World Editor (`EditorLifecycle` + `ShowcaseGalleryScene` + `OutlinerPanel`):

| Architectural Pillar | HTML Prototype (`Experimental/ComponentViewer/`) | Native C++20 / `.slang` Target |
| :--- | :--- | :--- |
| **Blueprint Class Model** | `Blueprint.js` (`createBlueprint`, `walkScene`, `validateBlueprint`) | `Engine/BlueprintSpace/`: `BlueprintClassDefinition`, `BlueprintInstanceStorage`, `BlueprintValidator`, `.BlueprintSpace` Two-Tier Hybrid Codec |
| **Typed Outliner Instances** | Scene + Actor component rows under `Origin [ROOT]` | Contiguous category arrays: `OriginRoot`, `SceneAnchor`, `GeometryInstance`, `ConstraintInstance`, `ParticleInstance`, `FluidInstance`, `LightInstance`, `CameraInstance`, `CollisionVolumeInstance`, `DirectionArrowInstance`, `VehicleDynamicsInstance`, `RotationalMotionInstance` |
| **Physics-Backed Constraints** | `Sockets.js` (`fixed`, `hinge`, `slider`, `ball`, `spring`, `spin`, `tether`, `dock`) | JoltPhysics v5.6.0 `ConstraintSolver` + `PhysicsExtension` + `VehicleDynamicsSolver` replacing kinematic socket stubs with real two-body rigid constraints |
| **PBR Direct + Debug Shaders** | `Render.js`, `Visuals.js`, `SocketVisuals.js` (Three.js PBR + 7 channels + X-ray) | `WorldRenderEngine::OpenPbrDirect` (Zero GI) + `Engine/Shaders/ComponentEditor/*.slang` (7 inspection channels, Fresnel field shells, light beams, dot floor, Layer-1 X-ray constraint visuals) |
| **1:1 UI Workspace** | `index.html`, `ComponentViewer.js`, `ComponentViewer.css` | `Engine/Workspaces/ComponentEditor/` + reusable widgets in `Engine/Components/` (`ConstraintTilePicker`, `Vector3CapsuleInput`, `BlueprintScriptViewport`, `TelemetryDrawerPanel`) |
| **`BP_ControlVehicle` Sample** | `ControlVehicleBody.facet`, `ControlVehicleTyre.facet`, `ControlVehicleRim.facet` + `VEHICLE_SHEET` | `GeometryInterchange::ImportFacetStream` (FCT1 reader) + `ControlVehicleSampleCatalog` building the full 25+ component vehicle + optional `ParticleInstance` / `FluidInstance` showcase attachments |

---

## 2. Canonical C++20 & `.slang` Folder Structure

All new directories and files strictly obey `Documentation/FolderStructure.md`, `CodingStandards.md`, `BannedWordsVocabulary.md`, and the zero-`And` mandate:

```text
Engine/
├── BlueprintSpace/                                // Lean Blueprint class definitions, typed instances, validation & .BlueprintSpace codec
│   ├── BlueprintToken.h                           // Generation-checked BlueprintClassToken & ComponentInstanceToken
│   ├── BlueprintClassDefinition.{h,cpp}           // Class metadata (Title, SuperClassToken, RevisionNumber, CompiledDirty, BakedIcon)
│   ├── BlueprintInstanceCategories.h              // Enumerations & POD descriptors for Geometry, Constraint, Particle, Fluid, Light, Camera, Collision, Arrow, Vehicle, Rotation
│   ├── BlueprintInstanceStorage.{h,cpp}           // Contiguous category storage + EnclosingIndex/FirstNestedIndex/NextPeerIndex hierarchy links
│   ├── BlueprintValidator.{h,cpp}                 // Pre-compile structural diagnostics (missing assets, wheel bindings, torque curve monotonicity, dock key checks)
│   ├── BlueprintWorldPlacement.{h,cpp}            // World instance binding, per-instance property overrides & JoltPhysics runtime spawning
│   └── ControlVehicleBlueprintBuilder.{h,cpp}     // Deterministic BP_ControlVehicle class assembly from FCT1 assets + VEHICLE_SHEET physics parameters
│
├── Codecs/
│   └── BlueprintSpaceCodec.{h,cpp}                // Two-Tier Hybrid .BlueprintSpace encoder/decoder (UTF-8 TOML manifest + 64B-aligned binary streams + .Revision)
│
├── Interchange/
│   ├── GeometryInterchange.{h,cpp}                // Extend with FCT1 (.facet), binary/ASCII .stl, .ply, .glb alongside existing .obj & .gltf
│   └── FacetStreamInterchange.{h,cpp}             // Dedicated Frontier FCT1 binary facet stream reader (positions, triangle indices, CourseMaterial slots 6..15)
│
├── SimulationSpace/
│   └── PhysicsSpace/
│       ├── ConstraintSolver.{h,cpp}               // Extend ConstraintCategory to 8 kinds: Fixed, Hinge, Slider, Ball, Spring, Spin, Tether, Dock + motor/drive params
│       ├── PhysicsExtension.{h,cpp}               // Jolt v5.6.0 implementation of SwingTwist, motorized Hinge/Spin, Spring Slider, unilateral Tether, dynamic breakable Dock
│       └── VehicleDynamicsSolver.{h,cpp}          // Chassis dynamics, 18-point TorqueCurve spline, 6-speed gearbox, differential, aerodynamic drag/downforce, MF6.1 tyre slip
│
├── GeometricRaster/
│   └── ComponentEditor/
│       ├── ComponentViewportRenderer.{h,cpp}      // Vulkan OpenPbrDirect (Zero GI) + 7 inspection channel dispatch + Layer-1 X-ray constraint pass
│       ├── ComponentSpecimenBaker.{h,cpp}         // Offscreen 128x128 transparent ACES-sRGB Blueprint class icon baker (reusing GeometrySpecimenRasterizer)
│       └── ComponentGizmoGeometry.{h,cpp}         // GPU vertex/index generation for shaded solid constraint hardware, beams, arrows, anchors & field shells
│
├── Shaders/
│   └── ComponentEditor/
│       ├── ComponentChannelInspection.slang       // Lit (OpenPbrDirect), Unlit, Density heat ramp, Corner Normals, UV Checker (A1-H8), Cluster LOD colours, Collision ghost
│       ├── ComponentFieldShell.slang              // Fresnel rim, soft box edge gradient, rising world-Y scan band, SDF voxel cell dots
│       ├── ComponentLightVolume.slang             // Additive spotlight cone & camera frustum volumetric beam shader
│       ├── ComponentDotFloor.slang                // Infinite metric & quarter-meter ground dot grid fading by rig focus & radius
│       ├── ComponentConstraintVisual.slang        // Angular limit range fan, Spin comet arcs, Dock pulsing iris & X-ray glow sprites
│       └── ComponentSelectionHalo.slang           // Silhouette amber tint applied directly in selected surface materials
│
├── Components/
│   ├── Controls/
│   │   ├── ConstraintTilePicker.{h,cpp}           // 4x2 radiogroup tile selector (Fixed, Hinge, Slider, Ball, Spring, Spin, Tether, Dock) with VectorIcons & accent borders
│   │   └── Vector3CapsuleInput.{h,cpp}            // Colour-coded X/Y/Z numeric capsules with unit suffix & aspect-lock toggle for Scale
│   └── Panes/
│       └── BlueprintScriptPane.{h,cpp}            // Interactive Construction Script visual block viewport (zoom, pan, drag blocks, Bezier exec/object wires)
│
├── Workspaces/
│   └── ComponentEditor/                           // Headless workspace engine + UI composition (compiled under Editor profile)
│       ├── ComponentWorkspaceDocument.{h,cpp}     // Multi-tab Blueprint session, selection token, active channel, simulation state, RevisionTracker hooks
│       ├── ComponentWorkspaceHost.{h,cpp}         // Top-level dock layout orchestrator (Left Components/Content, Centre Viewport/Script/Drawer, Right Details)
│       ├── ComponentTreePanel.{h,cpp}             // Left Components hierarchy (Origin root, nested instances, Actor divider, + Add menu, drag-to-reparent)
│       ├── ComponentContentTray.{h,cpp}           // Left Content compiled-asset tray (Import object..., format chips, + Add / Blueprint / Compile actions, Samples)
│       ├── ComponentViewportHost.{h,cpp}          // Centre viewport controls (7-channel strip, Simulate, Frame, Grid, Telemetry readout, Legend, Camera orb, SimHud)
│       ├── ComponentDetailsPanel.{h,cpp}          // Right Details inspector (Class Defaults metrics, Compiler Results, Transform, Constraint editor, Typed instance sections)
│       ├── ComponentDrawerPanel.{h,cpp}           // Bottom collapsible drawer (5-card Compiled Data summary + interactive Engine Torque ScalarCurveEditor)
│       └── ComponentEditorRuntimeBridge.{h,cpp}   // Connects Blueprint simulation stepping (PhysicsExtension + VehicleDynamicsSolver + Particle + Fluid) at 60 Hz
│
├── Editor/
│   └── Explorer/
│       └── ContentBrowser.* / OutlinerPanel.*     // Register .BlueprintSpace assets in ContentBrowser & expandable BlueprintWorldPlacement rows in OutlinerPanel
│
└── Exhibits/
    ├── ComponentBlueprintPhysicsValidation.cpp    // Headless validation: FCT1 import, BP_ControlVehicle compile, 8 Jolt constraint kinds, VehicleDynamicsSolver, World placement
    ├── ComponentEditorUIValidation.cpp            // ImGui + Vulkan/SoftwareCanvasRasterizer visual capture comparing every panel 1:1 against HtmlReference/*.png
    └── Images/ComponentEditor/
        ├── HtmlReference/                         // 5 verified HTML WebGL reference captures (already captured)
        └── CppParity/                             // Rendered C++ screenshots verified side-by-side against HtmlReference
```

### Build Profile Isolation (`Editor` / `Development` / `Shipping`)
- **`Editor` profile**: Compiles `Engine/Workspaces/ComponentEditor/`, `Engine/Components/Panes/BlueprintScriptPane.*`, `Engine/GeometricRaster/ComponentEditor/`, and `ComponentEditor` authoring panes in `Engine/Editor/`.
- **`Development` & `Shipping` profiles**: Completely exclude all `Engine/Workspaces/ComponentEditor/` and `Engine/Editor/` UI code; retain `Engine/BlueprintSpace/` runtime loading (`BlueprintClassDefinition`, `BlueprintWorldPlacement`), `Engine/SimulationSpace/PhysicsSpace/` constraint/vehicle solvers, and `OpenPbrDirect` runtime spawning so Blueprint assets authored in the Editor run identically in shipped builds with zero editor overhead.

---

## 3. Blueprint Class & Typed Outliner Instance Architecture

Following `Documentation/Plans/DocumentFormatsInstancePlan.md` (§4), Frontier avoids polymorphic `UObject` bloat and kinship terminology (`Parent`/`Child`). A `BlueprintClassDefinition` stores components in **cache-contiguous typed arrays** linked by non-kinship index handles (`EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex`, `DepthLevel`).

### 3.1 Lean Component Instance Taxonomy

Every component slot in `BlueprintInstanceStorage` carries a 32-byte header (`ComponentInstanceHeader`: `ComponentInstanceToken`, `ComponentCategory`, `Name`, `EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex`, `DepthLevel`, `TransformSlotIndex`, `VisibilityEnabled`, `HiddenInWorld`, `TagList`) plus a typed index into its category array:

| Category | Transform | Category Payload (`BlueprintInstanceCategories.h`) | World / Simulation Coupling |
| :--- | :---: | :--- | :--- |
| **`OriginRoot`** | Mandatory Root | Blueprint root anchor (`BlueprintRootMetrics`) | Root rigid body / world transform anchor |
| **`SceneAnchor`** | Yes | Pure transform pivot | Kinematic or massless pivot transform |
| **`GeometryInstance`** | Yes | `GeometryAssetToken`, `ForcedLodLevel` (`-1` Auto), `SlotMaterialOverrides[16]`, `CastShadow`, `CollisionProfile` (`BlockAll`/`OverlapAll`/`NoCollision`) | Uploads compiled geometry/LOD/collision to `OpenPbrDirect` & `RigidBodyDescriptor` |
| **`ConstraintInstance`** | Yes | `PhysicsConstraintKind` (8 kinds), `AxisIndex` (`X`/`Y`/`Z`), limits, spring frequency/damping, motor target/drive, break force/torque, `DockKey`, `SnapRadiusMeters`, `InitialPoseNormalized` | Spawns real JoltPhysics `TwoBodyConstraint` connecting `EnclosingBody` -> nested child rigid body (`MountOffset`) |
| **`ParticleInstance`** | Yes | `ParticleAssetToken` / preset ID (from `ParticleSpace` 93-preset library), `EmissionRateScale`, `VelocityScale`, `AutoActivate`, `LocalSpaceSimulation` | Registers `ParticleSystemInstance` attached to component transform in `ParticleExtension` |
| **`FluidInstance`** | Yes | `FluidVolumeToken` (`VolumetricSpace` / `SimulationSpace`), `BoundsExtentMeters`, `BuoyancyScale`, `FuelInjectionRate`, `EmissionMode` (`Smoke`/`Fire`/`Steam`), `AutoActivate` | Registers Eulerian source/emitter box attached to component transform in `FluidExtension` |
| **`LightInstance`** | Yes | `LightKind` (`Point`, `Spot`, `Area`), `LinearRgbColour`, `IntensityCandela`, `AttenuationRangeMeters`, `OuterConeAngleDeg`, `PenumbraSoftness`, `CastShadow`, `IesProfileToken` | Evaluated directly by `WorldViewportRadiometricCompute.slang` (`OpenPbrDirect`) + volumetric beam preview |
| **`CameraInstance`** | Yes | `VerticalFovDeg`, `NearClipMeters`, `FarClipMeters`, `AspectRatio` (`16:9`), `PilotActive` | Supports viewport `"Look through"` (hotkey `C`) & frustum beam gizmo |
| **`CollisionVolumeInstance`** | Yes | `CollisionShapeKind` (`Box`, `Sphere`, `Capsule`), `HalfExtentsMeters`, `RadiusMeters`, `HalfHeightMeters`, `TriggerOverlapOnly` | Feeds JoltPhysics compound/body shape or overlap sensor in `PhysicsExtension` |
| **`DirectionArrowInstance`** | Yes | `ArrowLengthMeters`, `ArrowColourRgb` | Shaded solid arrow indicator (+X forward) |
| **`VehicleDynamicsInstance`** | No (Logic) | `ChassisMassKg`, `WheelbaseMeters`, `TrackWidthMeters`, `WheelRadiusMeters`, `WheelInstanceTokens[4..8]`, `TorqueCurvePoints[18]`, `CurveInterpolation`, `IdleRpm`, `RedlineRpm`, `GearRatios[6]`, `FinalDriveRatio`, `DifferentialBias`, `DragCoefficient`, `FrontalAreaM2`, `AeroLiftAreaM2` | Drives `VehicleDynamicsSolver` + applies real wheel torques, steering hinge targets & aero downforce to Jolt constraints |
| **`RotationalMotionInstance`** | No (Logic) | `AngularRateDegPerSec`, `TargetInstanceToken` | Drives continuous rotation or `Spin` constraint velocity target |

### 3.2 World Placement & Per-Instance Overrides (`BlueprintWorldPlacement`)

When a user drags a `.BlueprintSpace` class from `ContentBrowserPanel` into `ViewportPanel` or `OutlinerPanel` (extending the drag-drop pipeline established in `EditorLifecycle::ConstructShowcaseModelInOutliner`):
1. **World Outliner Row Expansion**: `OutlinerPanel` creates a top-level `BlueprintWorldPlacement` entry (`WorldPlacementToken`, `BlueprintClassToken`, `WorldTransform`) with expandable nested rows displaying its typed instances (`Body`, `SuspensionFL`, `HeadlightL`, `ExhaustPlume`, `ExhaustSmoke`, etc.).
2. **Non-Destructive Property Overrides**: Selecting a nested instance inside the World Outliner edits a sparse `InstanceOverrideEntry` delta (Transform offset, Light intensity/colour, Material slot override, Particle rate, Constraint stiffness) keyed by `ComponentInstanceToken` — leaving the source `.BlueprintSpace` asset untouched unless the user explicitly chooses *"Apply Overrides to Blueprint"*.
3. **Live Runtime Compilation**: Pressing `Simulate` in either the Component Editor or World Editor resolves every `BlueprintWorldPlacement` into contiguous Jolt rigid bodies, constraints, `OpenPbrDirect` renderables, `ParticleSpace` emitters, and `FluidSpace` emitters.

---

## 4. Physics-Based Constraint System (Replacing Sockets)

In `Experimental/ComponentViewer/Sockets.js`, joints were simulated kinematically in JavaScript (`stepSocket`). In Frontier C++, **physics constraints completely replace sockets**: every `ConstraintInstance` constructs a real JoltPhysics `TwoBodyConstraint` inside `PhysicsExtension`, while retaining the exact interactive authoring UX (8 tile kinds, axis selection, live pose preview slider in edit mode, `Kick` impulse button, `Connect`/`Release` latch for Dock, and floating viewport telemetry tags).

### 4.1 Mapping All 8 Constraint Kinds to JoltPhysics v5.6.0

| Kind | Accent | JoltPhysics Constraint Implementation | Authored Parameters | Live Readout & Events |
| :--- | :---: | :--- | :--- | :--- |
| **`Fixed`** | `#8a95a5` | `JPH::FixedConstraintSettings` (optional `BreakForceN` / `BreakTorqueNm` strain rupture) | `BreakForceN`, `BreakTorqueNm`, `DisableSelfCollide` | `Fixed · intact` / `ruptured at X kN` |
| **`Hinge`** | `#59b8ff` | `JPH::HingeConstraintSettings` + Angular Position/Velocity Motor | `AxisIndex`, `MinAngleDeg`, `MaxAngleDeg`, `DriveMode` (`Hold`/`Steer`/`Sway`), `StiffnessNmPerRad`, `Damping` | Live hinge angle `°` from `GetCurrentAngle()` |
| **`Slider`** | `#ffd15c` | `JPH::SliderConstraintSettings` + Linear Position Motor / Friction | `AxisIndex`, `MinTravelMeters`, `MaxTravelMeters`, `MotorMode`, `MaxFrictionN` | Live linear travel `mm` from `GetCurrentPosition()` |
| **`Ball`** | `#ff8c5a` | `JPH::SwingTwistConstraintSettings` (`NormalHalfConeAngle`, `PlaneHalfConeAngle`, `TwistMin/Max`) | `AxisIndex`, `HalfConeAngleDeg`, `TwistLimitDeg`, `FrictionTorqueNm` | Live cone deflection `°` & twist `°` |
| **`Spring`** | `#6ae092` | `JPH::SliderConstraintSettings` with `JPH::ESpringMode::FrequencyAndDamping` limits/motor + wheel bump impulse | `AxisIndex`, `NaturalFrequencyHz`, `DampingRatioZeta`, `TravelMeters`, `PreloadNormalized` | Compression `mm` + frequency `Hz` + shock force `N` |
| **`Spin`** | `#b88cff` | `JPH::HingeConstraintSettings` (unlimited rotation, continuous velocity motor `TargetRpm`) | `AxisIndex`, `TargetRpm`, `MaxMotorTorqueNm`, `DriveMode` (`Motor`/`Freewheel`) | Live `rpm` + motor torque `N·m` |
| **`Tether`** | `#ff6b8a` | `JPH::DistanceConstraintSettings` (`MinDistance = 0`, `MaxDistance = RopeLengthMeters`, zero compression resistance, rupture check) | `RopeLengthMeters`, `StiffnessHz`, `DampingRatio`, `BreakForceN` | Tension `N` + stretch `%` + slack/taut status |
| **`Dock`** | `#4dd6c4` | Managed dynamic `JPH::FixedConstraint`: engages automatically when `DockAccepts(DockKey, NestedTags)` is true AND anchor separation `<= SnapRadiusMeters`; queries Jolt constraint lambda every physics substep (`AppliedForceN = |GetTotalLambdaPosition()| / dt`) and breaks cleanly when `AppliedForceN > BreakForceN` | `DockKey`, `SnapRadiusMeters`, `BreakForceN`, `CooldownSeconds`, `Connected` | `connected at X kN` / `released at X kN` / `rejected · wrong key` viewport ping |

### 4.2 Authoring Preview vs. Physics Simulation Modes
- **Edit Mode (`Simulate = false`)**: Bodies remain kinematic/frozen at their authored transforms; dragging the `Pose` slider or clicking `Kick` exercises the constraint's analytical kinematics (`EvaluateConstraintEditPose`) and updates the `.slang` visual indicator (e.g., Spring coil compression, Hinge needle sweep) without disturbing world rigid bodies.
- **Simulate Mode (`Simulate = true`)**: `PhysicsExtension` steps at `FixedStepHz = 60 Hz` (up to `4` substeps/tick); constraints resolve real forces, road bumps, aerodynamic downforce (loading `RearWingMount` Dock until break force if exceeded), steering hinge targets, and wheel spin.

---

## 5. PBR Rendering Without GI & `.slang` Shader Architecture

To achieve 1:1 visual parity with `Experimental/ComponentViewer/` while strictly honoring Frontier's `.slang`-only viewport policy (`CLAUDE.md` §2.1), the Component Editor viewport delegates to `WorldRenderEngine::OpenPbrDirect` (`TieredWorldRenderSequence` + `VulkanWorldRenderSequence`) with GI disabled.

### 5.1 Direct OpenPBR Pipeline (`OpenPbrDirect`, Zero GI)
- **Lighting Rig**: Matches `Stage` in `Experimental/Shared/Studio/Render.js`:
  - Hemisphere sky/ground ambient fill (`#dfe8ff` sky, `#202024` ground, intensity `1.5`, zero multi-bounce GI).
  - Key Directional Sun (`#ffffff`, intensity `2.4`, `2048×2048` `PCF` / `ContactPcss` shadow map).
  - Cool Rim Directional Fill (`#9fc4ff`, intensity `0.6`).
  - Dynamic Scene `LightInstance` lights (`PointLight`, `SpotLight`, `AreaLight`) evaluated directly in `WorldViewportRadiometricCompute.slang` + `LinearCosineLightIntegration`.
- **Tone Mapping**: ACEScg linear working space -> ACES Filmic tone mapping (`exposure = 1.05`) -> sRGB output (`WorldViewportBlit.slang`).
- **Material Slots**: Supports per-slot albedo, roughness, metalness, and emissive radiance, including the 10-family `ControlVehicle` `CourseMaterial` table (`BodyCoat`, `VehicleGlass`, `VehiclePlastic`, `Tyre`, `Hub`, `Brake`, `VehicleTrim`, `Headlight`, `Taillight`, `VehicleDefault`).

### 5.2 Dedicated `.slang` Inspection Channels & Volumetric Visuals

| Channel / Visual | `.slang` Module | GPU Mechanism |
| :--- | :--- | :--- |
| **1. `Lit` (Key `1`)** | `WorldViewportRadiometricCompute.slang` (`OpenPbrDirect`) | Full direct OpenPBR microfacet BRDF + LTC lights + shadows + emissives (no GI) |
| **2. `Unlit` (Key `2`)** | `ComponentChannelInspection.slang` | Flat material slot base colour / override albedo |
| **3. `Density` (Key `3`)** | `ComponentChannelInspection.slang` | Per-triangle world area computed in shader -> blue (sparse) to red (dense) heat ramp |
| **4. `Normals` (Key `4`)** | `ComponentChannelInspection.slang` | Compiled corner normals mapped `n * 0.5 + 0.5` |
| **5. `UV checker` (Key `5`)** | `ComponentChannelInspection.slang` | Procedural 8×8 checkerboard (`A1`–`H8` SDF glyphs + HSL tile tint) or `#7a2d5a` missing-UV indicator |
| **6. `LOD clusters` (Key `6`)** | `ComponentChannelInspection.slang` | Forced LOD cluster geometry shaded with golden-angle (`137.508°`) per-cluster hue |
| **7. `Collision` (Key `7`)** | `ComponentChannelInspection.slang` + `ComponentFieldShell.slang` | Surface alpha `0.35` + green Fresnel compiled collision shell (`#46d267` or `#6aa7c7` SDF voxel dust) + amber component collider shell (`#ffb238`) |
| **Field Shells & Beams** | `ComponentFieldShell.slang` + `ComponentLightVolume.slang` | Fresnel rim (`pow(1 - |N·V|, 2.2)`), box face edge gradients, animated world-Y scan bands, SDF voxel dots, additive SpotLight/Camera frustum cones |
| **Ground Dot Floor** | `ComponentDotFloor.slang` | 1.0 m and 0.25 m world-space circular dot field fading radially around `CameraRig.FocusPoint` |
| **X-Ray Constraints** | `ComponentConstraintVisual.slang` | Layer-1 depth-cleared secondary render pass (`xray = true` when a `ConstraintInstance` is selected) drawing limit fan sectors, Spin comet trails, Dock iris rings, and glow sprites right through occluding body/tyre geometry |
| **Selection Halo** | `ComponentSelectionHalo.slang` | View-dependent amber silhouette glow (`#ffb238`) blended directly inside selected geometry shaders (no duplicate mesh extrusion) |

---

## 6. 1:1 HTML-to-C++ UI Parity Specification

Every panel, spacing token, colour swatch, and interactive widget maps 1:1 from `Experimental/ComponentViewer/` (`Exhibits/Images/ComponentEditor/HtmlReference/*.png`) into Frontier's `PixelSpace` + ImGui 8-patch UI system (`Engine/Components/`):

| Region | HTML Reference Elements | C++ UI Class & Widget Mapping |
| :--- | :--- | :--- |
| **Top Tab Strip** | Open Blueprint tabs with 128×128 thumbnail, dirty dot `•`, close `×`, centered `Frontier · Component Viewer` | `WorkspaceTabStrip` (`Engine/Components/Docking/`) + `ComponentWorkspaceDocument` tabs |
| **Left Dock (`280 px`) — `Components` Tab** | `Components` + active Blueprint caption; 2 metric tiles (`Components`, `Triangles`); white `+ Add` button (opens grouped search menu: Scene, Actor, Constraints, Content meshes) + `Search components`; `Origin [ROOT]` hierarchical tree with visibility eye button, warning icons, drag-to-reparent; `ACTOR COMPONENTS` divider; 3-hint footer (`DRAG re-parent`, `DBL-CLICK frame`, `DEL · CTRL D remove · copy`) | `ComponentTreePanel` using `ExclusiveSelector` (tab switch), `CapsuleButton`, `TextInputField`, `OutlinerPanel`-style tree rows with `VectorIconRepository` icons & `ThumbnailSheetRepository` mini icons |
| **Left Dock (`280 px`) — `Content` Tab** | `Content · compiled assets N`; white `Import object...` button + format chips (`OBJ`, `glTF`, `GLB`, `STL`, `PLY`, `FCT1`, `FBX` dimmed); asset cards with 3D thumbnail, triangle count, status chips (`Compiled`/`Stale`, `In use`, `UV`, `SDF`, `LOD`, `COLLISION`, `ICON`), `+ Add`, `Blueprint`, `Compile` buttons; `SAMPLES` list | `ComponentContentTray` connected to `GeometryInterchange` + `ContentBrowserPanel` asset repository |
| **Centre Header & Toolbar** | Title, subtitle (`Blueprint class · Parent Pawn · N components`), `COMPILED`/`DIRTY`/`ERRORS` pill, `Viewport | Construction Script` segmented switch, white `Compile` button; 7-channel segmented bar (`Lit`..`Collision`), `Simulate`/`Stop` toggle button (amber when active), `Frame`, `Grid` toggles | `ComponentViewportHost` using `ExclusiveSelector`, `ExecutionModeStrip`, `CapsuleButton`, `SimulationPlaybackControls` |
| **Centre Viewport Overlays** | Top-left mono readout (`fps`, `ms`, `tris`, `meshes`, `select`); top-right channel note + colour legend/ramp; bottom-right orientation axis orb + `35 mm` lens chip + turntable + conditional `Look through`; floating `ConstraintTag` pill + `ConstraintPing` toast; `SimHud` bar (`Throttle`, `Brake`, `km/h`, `rpm`, `m/s²`, `N·m`, `GEAR`, RPM progress bar) | `.slang`-backed `ViewportProjectionPane` + `CameraComponent` HUD + crisp vector/font overlay badges in `ComponentViewportHost` |
| **Centre Construction Script** | Interactive dark 16/128 px grid; red-header `Construction Script` event block, blue-header `Set ...` call blocks, pill getter blocks, Bezier exec (white) & object (blue) wires, pan/zoom/drag | `BlueprintScriptPane` (`Engine/Components/Panes/`) using GPU/ThorVG anti-aliased Bezier wires & interactive hit-testing |
| **Bottom Collapsible Drawer** | Handle `Compiled data` + bottom telemetry strip; Drawer tabs (`Compiled data` | `Response curves`): 5 summary cards (`Icon`, `UV charts`, `SDF`, `Cluster LOD` bar chart, `Collision`) OR interactive `Engine torque` spline (`Linear`/`Smooth spline`/`Ease`/`Step`, drag/double-click/right-click points, redline zone, live amber RPM cursor, `Reset`, `CSV` export) | `ComponentDrawerPanel` using `PanelExpansionTab` + `ScalarCurveEditor` (`Engine/Components/Controls/`) |
| **Right Dock (`320 px`) — `Details`** | Breadcrumb path; icon + inline editable component name + subtitle + Duplicate/Delete buttons; `Search details`; `CLASS DEFAULTS` (128×128 thumbnail + 2×2 metrics: `MESHES`, `LIGHTS`, `COLLIDERS`, `ACTOR COMPS`, Blueprint name, Parent class, Revision); `COMPILER RESULTS` clickable diagnostics; `TRANSFORM` (`Location m`, `Rotation °`, `Scale ×` with RGB X/Y/Z capsules & link lock); `CONSTRAINT` (4×2 kind tile grid, kind note, Axis `X|Y|Z`, sliders, Drive selector, `Connect`/`Release` or `Kick` + `Reset motion`, `LIVE` readout); typed sections (`Geometry`, `Particle`, `Fluid`, `Light`, `Camera`, `Collision`, `VehicleDynamics`, `Rendering`, `Tags`) | `ComponentDetailsPanel` using `InspectorPanel` + new `ConstraintTilePicker` & `Vector3CapsuleInput` + `ScalarSlider` + `ColourSwatchPicker` |

---

## 7. `BP_ControlVehicle` Sample Integration

The flagship default Blueprint loaded on launch is `BP_ControlVehicle` (`ControlVehicleBlueprintBuilder`):
1. **Asset Ingestion (`FacetStreamInterchange`)**: Reads binary `FCT1` streams (`Experimental/Shared/Models/ControlVehicleBody.facet`, `ControlVehicleTyre.facet`, `ControlVehicleRim.facet`), applies `Z-up -> Y-up` / metre scaling with authored pivot preserved (`Body` ~`6.22 m × 1.46 m`), assigns `CourseMaterial` slots `6..15`, and synthesizes `RearWing` (`72` tris).
2. **Component Hierarchy (25+ Components)**:
   - `Origin [ROOT]` -> `Body` (`GeometryInstance`: 8 material slots, compiled SDF + Cluster LOD + Box collision)
   - Four corners: `SuspensionFL/FR/RL/RR` (`ConstraintInstance::Spring`, Y axis, `2.00 Hz`, `ζ = 0.42`, `±0.090 m` travel) -> `SteerFL/FR` on front (`ConstraintInstance::Hinge`, Y axis, `±32°`, `Drive: Steer`) -> `WheelFL/FR/RL/RR` (`GeometryInstance`, Tyre) -> `WheelFL/FR/RL/RRRim` (`GeometryInstance`, Rim)
   - `RearWingMount` (`ConstraintInstance::Dock`, key `wing`, `3500 N` break force, `0.35 m` snap radius) -> `RearWing` (`GeometryInstance`, tag `wing`)
   - `TowStrap` (`ConstraintInstance::Tether`, `0.32 m` length, `1200 N` max load)
   - `HeadlightL`, `HeadlightR` (`LightInstance::SpotLight`), `ChaseCamera` (`CameraInstance`, `52°` FOV), `FrontBumper` (`CollisionVolumeInstance::Box`), `Forward` (`DirectionArrowInstance`)
   - Optional showcase instances: `ExhaustPlume` (`ParticleInstance`) + `RearTyreSmoke` (`FluidInstance`) demonstrating non-mesh typed Outliner instances
   - Actor component: `VehicleMovement` (`VehicleDynamicsInstance`: `1300 kg`, 18-pt torque curve peaking `520 N·m @ 4800 rpm`, `7200 rpm` redline, `1.4 m²` aero lift area, 4 wheel bindings)
3. **Interactive Verification**: Clicking `Simulate` accelerates through gears `1..6`, compresses `Spring` suspension under longitudinal load transfer and bumps, steers `Hinge` front knuckles, turns `Wheel` rims, updates the live RPM needle in the `Response curves` drawer, and loads `RearWingMount` `Dock` downforce against its `3500 N` break threshold.

---

## 8. Phased Implementation Roadmap & Validation Gates

| Phase | Deliverables | Verification Gate (`Exhibits/`) |
| :--- | :--- | :--- |
| **Phase 1: Interchange & Blueprint Core** | `FacetStreamInterchange` (FCT1 + STL/PLY/GLB extensions), `Engine/BlueprintSpace/*`, `BlueprintSpaceCodec` (`.BlueprintSpace`), `ControlVehicleBlueprintBuilder` | `ComponentBlueprintPhysicsValidation` unit checks: FCT1 decode counts, tree re-parent/cycle rejection, Blueprint compile diagnostics, `.BlueprintSpace` round-trip |
| **Phase 2: Physics Constraints & Vehicle Dynamics** | Extend `ConstraintSolver` + `PhysicsExtension` to all 8 constraint kinds (`Fixed`, `Hinge`, `Slider`, `Ball`, `Spring`, `Spin`, `Tether`, `Dock`) + `VehicleDynamicsSolver` | `ComponentBlueprintPhysicsValidation`: verify JoltPhysics spring preload/squat, hinge limits, tether tension/slack, dock auto-latch/key-reject/force-break, vehicle acceleration/shifting |
| **Phase 3: Direct PBR & `.slang` Viewport Shaders** | Compile `Engine/Shaders/ComponentEditor/*.slang` via `slangc`; implement `ComponentViewportRenderer` (`OpenPbrDirect`, 7 inspection channels, Layer-1 X-ray constraint pass, dot floor, selection halo) + `ComponentSpecimenBaker` | Render offscreen Vulkan PNGs for all 7 channels + X-ray Spring/Hinge/Dock visuals; visually compare against `HtmlReference/*.png` |
| **Phase 4: 1:1 C++ UI Workspace** | `ConstraintTilePicker`, `Vector3CapsuleInput`, `BlueprintScriptPane`, `Engine/Workspaces/ComponentEditor/*` (all 5 panes + Drawer + SimHud) | `ComponentEditorUIValidation`: capture 5 matching screenshots into `Exhibits/Images/ComponentEditor/CppParity/` (`Viewport`, `SocketDetails`, `ConstructionScript`, `DrawerSimulate`, `ContentCollision`) and verify 1:1 parity visually |
| **Phase 5: World Editor Placement & Typed Instances** | `BlueprintWorldPlacement` + `ParticleInstance` / `FluidInstance` / `LightInstance` world spawning + expandable nested `OutlinerPanel` rows + per-instance overrides | `WorkspacePanesValidation` + `Frontier --world-editor --frames=4 --capture-png=...`: drop `BP_ControlVehicle` from ContentBrowser into World Viewport, expand nested instances in Outliner, inspect live lighting/particles/physics |

---

## 9. Governance & Naming Compliance Checklist

- [x] **Zero `And`** in every directory, file, class, struct, enum, function, variable, and `.slang` symbol.
- [x] **Zero Banned Vocabulary** (`EnclosingIndex`/`FirstNestedIndex`/`NextPeerIndex` instead of kinship; `Construct` instead of `Make`/`Draw`; `GeometryInstance` instead of `Mesh`; `BlueprintScriptPane` instead of `Graph`; `Sequence`/`Rasterization` instead of `Pass`/`Stage`).
- [x] **All Viewport Overlays in `.slang`** — zero CPU `ImDrawList` overlays for field shells, beams, dot floor, constraints, or selection halos.
- [x] **3-Profile Build Isolation** — `Editor` UI strictly guarded from `Development` and `Shipping` targets.
- [x] **Live HTML Reference Verified** — 5 Chromium WebGL captures (`Exhibits/Images/ComponentEditor/HtmlReference/*.png`) and 20/20 `CheckComponentViewer.mjs` assertions verified prior to C++ implementation.
