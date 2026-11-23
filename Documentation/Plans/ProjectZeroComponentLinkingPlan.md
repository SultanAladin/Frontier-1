# 🧩 Project Zero UI Component Linking Plan

> **TL;DR**: Base repository work from `unassignedinbox/Frontier` (`arena/5922d41b-frontier`) is merged onto `arena/67d436a2-frontier-1`. This plan maps the latest Project Zero Editor UI (`eosclient0001-rgb/Frontier`, `arena/d929ef36-frontier` Follow-Ups 61–65) onto Frontier's existing C++20 `Engine/Components/`, `Engine/ComponentClass/`, `Engine/Workspaces/ComponentEditor/`, `Engine/GeometricRaster/`, and `Engine/Lifecycle/EditorLifecycle` while strictly enforcing Frontier's naming, modularity, C++20 concept, `.slang`-only viewport overlay, and 3-profile build rules.

---

## 1. Architectural Audit & Modularity Assessment

### 1.1 Current State vs. Target UI Parity

| Domain | Existing C++20 Baseline (`unassignedinbox/Frontier`) | Latest UI Reference (`eosclient0001-rgb/Frontier` F61–F65) | Linking Strategy |
| :--- | :--- | :--- | :--- |
| **World Inspector Routing** | `EditorLifecycle::ConfigureCustomSectionCallback` exposes only a 2-card stub (`Render Settings` + `Sky/Cloud` cyclers) on `EditorCameraToken` | `Inspectors.jsx`, `InspectorTabs.jsx`, `PageCarousel.jsx` (`Overview` with `QuickControls`, `Tags`, `Notes` + numbered page strip) | Replace 2-card stub with modular `WorldInspectorRouter` + per-domain `Engine/Editor/Inspectors/*Section` units |
| **Shared UI Controls** | `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `ScalarSlider`, `TextInputField`, `ScalarCurveEditor`, `ColourSwatchPicker`, `ConstraintTilePicker`, `Vector3CapsuleInput` | `PageCarousel` (`‹ [1 ...] › n/N`), `QuickControls` green/neutral switch tiles, `Tags` + `Notes` shared footer sections | Add `PagedSectionStrip`, `QuickToggleGrid`, `TagNoteSection` in `Engine/Components/Controls/` (reused across all workspaces) |
| **Outliner Behaviour** | `OutlinerPanel` expands all folders at startup | Folded startup (`World` folder open; vehicle sub-components live inside Component Viewer, not World Outliner) | Extend `OutlinerPanel` with folded-init directive + auto-expand on selection |
| **Atmosphere & Celestial** | `SolarCelestialConfiguration`, `AtmosphericMediumConfiguration`, `LunarBodyConfiguration[6]`, `StarfieldConfiguration`, `CloudDeckConfiguration`, `BoundedCloudVolume`, `AtmosphericFogConfiguration`, `LensFlareConfiguration`, `AtmosphericWindParameters` | Paged inspectors for Sun, Atmosphere, Moon (with 3D-pitched `MoonGlobe` preview), Stars, Clouds, Local Cloud, Height/Aerial/Local Fog, God Rays, Wind, Lens Flare + live LUT previews | Bind every slider/toggle/colour/preset directly to the existing `Engine/GeometricRaster/Atmosphere/` configuration structs & trigger LUT/scene resync |
| **Camera & Optics** | `CameraConfiguration` (`OpticalLensParameters`, `CameraKinematicsParameters`, `CameraLagComponent`, `CameraOpticsEncoder`), `ViewportDisplaySettings` | `Camera` (`Overview`/`Focus`/`Body`/`Render`), per-renderer `Render Engine` (`Engine`/`PBR`/`SDF`/`Ray`/`MatCap`), `PostEffects` (`MotionBlur`, `HeatShimmer`, `Glitch`, `ChromaticAberration`, `FilmGrain`, `LensDistortion`, `Scanlines`, `Sharpen`) | Bind `CameraInspectorSection` + `OpticsEffectInspectorSection` to `CameraConfiguration` & `ViewportDisplaySettings` |
| **Showcase Primitives** | `ShowcaseWorldAssetEntry` (`PolygonNetwork`, `ClusterPyramid`, `SurfaceMaterial`) + `UVWorkspace`, `FractureWorkspace` | `PrimitiveInspector` (`Overview`, `Components` stack [`Mass`/`RigidBody`/`Collision`], `Geometry`, `Material`, `UV`, `Fracture`, `SDF`, `LOD`) | Link `ShowcasePrimitiveInspectorSection` to `ShowcaseWorldAssetEntry`, `RigidBodyDescriptor`, `SurfaceMaterial`, and workspace launchers |
| **Vehicle & Component Viewer** | `Engine/ComponentClass/` (`ControlVehicleClassBuilder`, `ComponentInstanceStorage`) + `Engine/Workspaces/ComponentEditor/` (`ComponentWorkspaceHost`, `ComponentTreePanel`, `ComponentDetailsPanel`) | `VehicleAssetInspector` (`Model`/`Size`/`Drive` + `Open Component Viewer` button), `VehicleComponentViewer` (19-row corner tree, COM debug, Socket/Constraint debug, transparent part renders) | Wire `VehicleAssetInspectorSection` to `ComponentWorldPlacement`; launch `ComponentWorkspaceHost` exclusively via button; sync COM & constraint debug flags |
| **Viewport Props & Debug** | `TransformGizmo`, `WorldGizmoOverlayRasterization`, `ComponentViewportRenderer` | Light fixture props (lamp heads pick first), COM checkered spheres + plumb lines, X-ray constraint visuals | Render 100% on GPU via `.slang` (`CenterOfMassMarker.slang`, `LuminaireFixtureProp.slang`); zero CPU `ImDrawList` overlays |

---

## 2. Governance, Naming & Design Rules Enforced

| Rule Source | Hard Mandate | Concrete Application in This Phase |
| :--- | :--- | :--- |
| **`SKILL-Naming.md` & `BannedWordsVocabulary.md`** | Zero `And` conjunction; zero banned nouns/verbs (`Card`, `Carousel`, `Pill`, `Menu`, `Popup`, `Palette`, `Table`, `Badge`, `Glyph`, `Draw`, `Make`, `Get`, `Set`, `Commit`, `Compose`, `Parent`, `Child`, `Node`, `Graph`, `Mesh`, `Frame`, `Pass`, `Stage`, `State`, `Value`, `Flag`, `Data`, `Info`, `Object`, `Item`, `Element`, `Store`, `Context`, `Binding`, `Coverage`, `Emission`, `Facet`, `Topology`, `Assembly`, `Socket`) | Translate every unapproved HTML/JS prototype name into mechanism-first C++20 identifiers (`PageCarousel` → `PagedSectionStrip`, `QuickControls` → `QuickToggleGrid`, `TagsAndNotes` → `TagNoteSection`, `Socket` → `ConstraintInstance`, `LightProps` → `LuminaireFixtureProp`) |
| **`CodingStandards.md`** | ISO C++20 (`cxx_std_20`), explicit `FUNCTION PROTOTYPES` blocks, `concept`-constrained templates over `virtual` hot-path dispatch, zero raw `new`/`delete`, `std::span` views, no hot-path exceptions/RTTI | Parameterize inspector section binders via a `WorldInspectorSectionConcept` / lightweight descriptor span; keep third-party headers (`imgui.h`, `vulkan.h`) isolated to `.cpp` files |
| **`SKILL-Formatting.md` & `SKILL-Emoji.md`** | 140-col `===` header banners, 120-col `---` sections, Allman braces, 4 spaces, flat `namespace Frontier`, aligned `[unit]` comments, approved emojis only | Apply standard Frontier banner & unit comments on every new `.h`, `.cpp`, and `.slang` file |
| **`CLAUDE.md` §2.1 & `ImportantNotes.md` §6`** | Step 0 HTML screenshots before C++ edits; 1:1 UI parity via `Engine/Components/`; zero GI/SDF stub regressions; side-by-side `Exhibits/Images/` `.png` proofs | Capture live browser screenshots of synced `Experimental/ProjectZeroEditor/` first; render matching offscreen C++ `.png` proofs via `ProjectZeroInspectorValidation` |
| **`CLAUDE.md` §2.2 & §2.3** | All viewport overlays & 3D gizmos/markers 100% on GPU via `.slang`; zero CPU `ImDrawList` viewport drawing | Author `CenterOfMassMarker.slang` and `LuminaireFixtureProp.slang` compiled via `slangc` |
| **`CLAUDE.md` §2.5 & §2.9** | 3-profile isolation (`Editor` / `Development` / `Shipping`); reuse existing `Engine/` modules before adding files | Place editor-only inspectors in `Engine/Editor/Inspectors/` (excluded from `Development` & `Shipping`); reuse `Engine/Workspaces/ComponentEditor/` and `Engine/Components/` |

---

## 3. Target Folder Structure (Additions & Extensions)

```text
Frontier-1/
├── Documentation/
│   └── Plans/
│       └── ProjectZeroComponentLinkingPlan.md          // [ADDED] This architecture & linking plan
│
├── Experimental/
│   └── ProjectZeroEditor/                              // [SYNCED in Phase 0] Latest UI reference (F61-F65) from eosclient0001-rgb/Frontier
│
├── Engine/
│   ├── Components/
│   │   ├── Controls/
│   │   │   ├── PagedSectionStrip.{h,cpp}               // [ADDED] Numbered ‹ [1 Title] ... › n/N horizontal page strip + cross-slide
│   │   │   ├── QuickToggleGrid.{h,cpp}                 // [ADDED] Shared green/neutral quick-switch tile grid + active fraction readout
│   │   │   └── TagNoteSection.{h,cpp}                  // [ADDED] Shared Tag chip editor + expandable Note text section
│   │   └── Panes/
│   │       ├── OutlinerPanel.{h,cpp}                   // [EXTENDED] Folded startup option, auto-expand on selection, transparent icon tiles
│   │       ├── InspectorPanel.{h,cpp}                  // [EXTENDED] PagedSectionStrip host, domain section router hook, shared scroll frame
│   │       └── ViewportPanel.{h,cpp}                   // [EXTENDED] CenterOfMassDebugEnabled, ConstraintDebugEnabled, debug channel 00..16 sync
│   │
│   ├── Editor/                                         // [Compiled ONLY under FRONTIER_PROFILE_EDITOR=1]
│   │   ├── Explorer/
│   │   │   └── ContentBrowser.{h,cpp}                  // [EXTENDED] Expose .ComponentSpace classes & paint/material presets for drop placement
│   │   └── Inspectors/                                 // [ADDED] Modular domain inspector binders (one responsibility per unit)
│   │       ├── WorldInspectorRouter.{h,cpp}            // Routes OutlinerSlotToken -> active domain section + remembers active page per subject
│   │       ├── SunInspectorSection.{h,cpp}             // SolarCelestialConfiguration + CelestialEphemerisSolver + GodRays link
│   │       ├── AtmosphereInspectorSection.{h,cpp}      // AtmosphericMediumConfiguration + AtmosphericSkySequence LUT controls
│   │       ├── MoonInspectorSection.{h,cpp}            // 6-slot LunarBodyConfiguration + pitched/rolled lunar surface preview
│   │       ├── StarfieldInspectorSection.{h,cpp}       // StarfieldConfiguration + J2000BrightStarRepository + zodiacal/galactic toggles
│   │       ├── CloudInspectorSection.{h,cpp}           // CloudDeckConfiguration + BoundedCloudVolume + CloudShadowConfiguration
│   │       ├── FogInspectorSection.{h,cpp}             // AtmosphericFogConfiguration (Height/Aerial/Local) + AtmosphericWindParameters
│   │       ├── LensFlareInspectorSection.{h,cpp}       // LensFlareConfiguration + LensFlareSequence optical archetypes & ghost blades
│   │       ├── CameraInspectorSection.{h,cpp}          // OpticalLensParameters, CameraLagComponent, ViewportDisplaySettings (PBR/SDF/Ray/MatCap)
│   │       ├── OpticsEffectInspectorSection.{h,cpp}    // CameraOpticsEncoder + PostProcessSequence (MotionBlur, Shimmer, Glitch, Aberration, Grain, Distortion, Scanlines, Sharpen)
│   │       ├── LuminaireInspectorSection.{h,cpp}       // ShowcaseAreaLightLink, PhotometricLightTree, IES profile & SkyAmbientLight controls
│   │       ├── ShowcasePrimitiveInspectorSection.{h,cpp}// ShowcaseWorldAssetEntry, Mass/RigidBody/Collision stack, Material & workspace previews
│   │       ├── VehicleAssetInspectorSection.{h,cpp}    // ControlVehicle Model/Size/Drive pages + Open Component Viewer launch trigger
│   │       └── EnvironmentPreviewStudy.{h,cpp}         // Analytical/LUT study thumbnail generators rendered inside inspector sections
│   │
│   ├── Workspaces/
│   │   └── ComponentEditor/                            // [EXTENDED] Linked to VehicleAssetInspectorSection & ShowcaseWorldSceneBundle
│   │       ├── ComponentWorkspaceSession.{h,cpp}       // Button-only open-on-component routing, ControlVehicle corner tree alignment
│   │       ├── ComponentWorkspaceHost.{h,cpp}          // Bidirectional CenterOfMassDebug & ConstraintDebug sync with World Viewport
│   │       ├── ComponentTreePanel.{h,cpp}              // Corner hierarchy (Suspension -> Steer -> Tyre -> Rim), zero obsolete Collision/Movement clutter
│   │       ├── ComponentDetailsPanel.{h,cpp}           // Per-component Mass/COM section, Constraint editor, Light/Tyre/Rim parameters
│   │       └── ComponentViewportHost.{h,cpp}           // COM & Constraint debug toolbar buttons + part picking
│   │
│   ├── GeometricRaster/
│   │   ├── ComponentEditor/
│   │   │   ├── ComponentViewportRenderer.{h,cpp}       // [EXTENDED] Upload per-part & vehicle COM buffers + constraint debug flags
│   │   │   └── ComponentSpecimenBaker.{h,cpp}          // [EXTENDED] Transparent RGBA8 part thumbnails with dark-rubber exposure boost
│   │   └── DirectLighting/
│   │       └── LuminaireFixtureRasterization.{h,cpp}   // [ADDED] GPU studio luminaire fixture prop pass + lamp-head priority pick
│   │
│   ├── Shaders/
│   │   └── ComponentEditor/
│   │       ├── CenterOfMassMarker.slang                // [ADDED] GPU checkered COM sphere + equatorial ring + plumb line
│   │       └── LuminaireFixtureProp.slang              // [ADDED] GPU studio luminaire stand/boom/head geometry + beam cone
│   │
│   └── Lifecycle/
│       └── EditorLifecycle.{h,cpp}                     // [EXTENDED] Mount WorldInspectorRouter, folded Outliner init, live Atmosphere/Render/ComponentEditor sync
│
└── Exhibits/
    ├── ProjectZeroInspectorValidation.cpp              // [ADDED] Offscreen C++ validation rendering all paged inspectors + Component Viewer parity
    └── Images/
        └── ProjectZeroParity/                          // [ADDED] Step 0 HTML captures vs. C++ side-by-side .png visual proofs
```

---

## 4. Phased Implementation Plan

### Phase 0 — UI Reference Sync & Step 0 Visual Baseline
1. **Sync `Experimental/ProjectZeroEditor/`**: Copy the updated F61–F65 browser studio from `eosclient0001-rgb/Frontier` (`arena/d929ef36-frontier`) into `Experimental/ProjectZeroEditor/` (preserving all engine C++ modules untouched).
2. **Automatic Step 0 Captures**: Serve `Experimental/ProjectZeroEditor/` locally and capture baseline `.png` screenshots across every inspector (`Sun`, `Atmosphere`, `Moon`, `Stars`, `Clouds`, `Fog`, `LensFlare`, `Camera`, `Light`, `ShowcasePrimitive`, `VehicleAsset`, `ComponentViewer`) into `Exhibits/Images/ProjectZeroParity/HtmlReference/` and inspect via `read_file`.

### Phase 1 — Shared UI Primitives & Outliner/Inspector Infrastructure (`Engine/Components/`)
1. **Add `PagedSectionStrip.{h,cpp}`**: Implement the horizontal numbered pill strip (`‹ [1 Title] [2 ...] › n / N`), overflow edge fade, keyboard `←`/`→` navigation, and smooth horizontal cross-slide transition driven by `SpringIntegrator`.
2. **Add `QuickToggleGrid.{h,cpp}` & `TagNoteSection.{h,cpp}`**: Build reusable grid of green/neutral capability tiles (`n / N on` badge-free count) and shared `Tags` + `Notes` sections using `VectorIconRepository` and `TextInputField`.
3. **Extend `OutlinerPanel` & `InspectorPanel`**:
   - Add folded-start configuration (only `World` expanded on initial load; auto-unfold parent chain on selection).
   - Replace the single-scroll `InspectorSectionRenderCallback` bottleneck with multi-page `PagedSectionStrip` hosting and per-subject active page memory.

### Phase 2 — Domain Inspector Sections & Engine Struct Linking (`Engine/Editor/Inspectors/`)
1. **Atmosphere & Celestial Sections**: Wire `SunInspectorSection`, `AtmosphereInspectorSection`, `MoonInspectorSection` (6 slots + pitched/rolled lunar surface preview), `StarfieldInspectorSection`, `CloudInspectorSection` (planetary + 8 bounded `LocalCloud` volumes), `FogInspectorSection` (Height / Aerial / Local + God Rays + Wind), and `LensFlareInspectorSection` directly to `ShowcaseBundle->Scene.Atmosphere` and call `ShowcaseRenderer->SynchronizeWorldAtmosphere(...)` + `GpuWorldRenderer->SynchronizeSceneResources(...)` on mutation.
2. **Camera, Render Settings & Optics Sections**: Wire `CameraInspectorSection` to `PerspectiveCameraBasis`, `CameraComponent`, `CameraLagComponent`, and `ViewportDisplaySettings` (splitting per-renderer `PBR`, `SDF`, `Ray traced`, `MatCap` shadow & AO fields cleanly without losing inactive renderer settings). Wire `OpticsEffectInspectorSection` to `CameraOpticsEncoder` and `PostProcessSequence`.
3. **Lights, Showcase Primitives & Workspace Previews**: Wire `LuminaireInspectorSection` to `ShowcaseAreaLightLink` & `PhotometricLightTree` (including `SkyAmbientLight`), and `ShowcasePrimitiveInspectorSection` to `ShowcaseWorldAssetEntry` (`Mass`/`RigidBody`/`Collision` component stack + `UV`/`Fracture`/`SDF`/`LOD` preview links).
4. **Mount `WorldInspectorRouter` in `EditorLifecycle.cpp`**: Replace the 2-card camera stub (`EditorLifecycle.cpp:470`) with `WorldInspectorRouter` and route every edit through dirty-flag sync + `RetainedCanvasCache` invalidation.

### Phase 3 — Vehicle Inspector, Component Viewer & `.slang` Debug Overlays
1. **Link `VehicleAssetInspectorSection` ↔ `ComponentWorkspaceHost`**:
   - Expose `Model` (paint swatch overrides), `Size` (wheelbase/track/mass metrics), and `Drive` (component selection + `Open Component Viewer` button that opens `ComponentWorkspaceHost` focused on the selected `ComponentInstanceToken`).
   - Enforce World Outliner cleanliness (vehicle sub-components stay inside `ComponentWorkspaceHost`, never cluttering the World Outliner).
2. **Align `Engine/Workspaces/ComponentEditor/`**:
   - Match the corner hierarchy (`SpatialOrigin [ROOT]` → `Body` → `SuspensionXX [Spring]` → `SteerXX [Hinge]` → `WheelXX` → `WheelXXRim`, plus `TowHitch [Dock]`, `Headlights`, `BrakeLights`), remove redundant `ChassisCollision`/`VehicleMovement` clutter, and render transparent part thumbnails (`ComponentSpecimenBaker`) with exposure boost for dark tyre/rim geometry.
3. **GPU `.slang` Overlays**:
   - Author `CenterOfMassMarker.slang` (per-part & weighted vehicle COM spheres + plumb lines) and `LuminaireFixtureProp.slang` + `LuminaireFixtureRasterization` (studio luminaire fixtures with lamp-head priority GPU picking), gated by `CenterOfMassDebugEnabled` & `ConstraintDebugEnabled` in `ViewportDisplaySettings`.

### Phase 4 — Verification, CMake Integration & Side-by-Side Visual Proofs
1. **CMake & 3-Profile Verification**: Register all new sources under `FRONTIER_PROFILE=Editor` in `CMakeLists.txt`; verify `Development` and `Shipping` builds link zero `Engine/Editor/` or `ComponentEditor/` symbols.
2. **Validation Executable (`Exhibits/ProjectZeroInspectorValidation.cpp`)**: Exercise every inspector page, switch sync, live engine parameter propagation, COM/constraint debug toggle, and `ControlVehicle` Component Viewer launch; emit side-by-side `.png` captures into `Exhibits/Images/ProjectZeroParity/` and compare visually against `HtmlReference/*.png`.

---

## 5. Browser-to-Frontier Identifier Translation Ledger

| Prototype Identifier (`Experimental/ProjectZeroEditor`) | Frontier-Compliant C++20 Identifier | Reason for Translation |
| :--- | :--- | :--- |
| `PageCarousel` / `PageStrip` / `pc-pill` | `PagedSectionStrip` | `Carousel` and `Pill` are barred by `BannedWordsVocabulary.md` |
| `QuickControls` | `QuickToggleGrid` | Mechanism-first naming (grid of boolean toggle tiles) |
| `TagsAndNotes` | `TagNoteSection` | Conjunction `And` is strictly forbidden (`SKILL-Naming.md` Gate ⑦) |
| `Socket` (`VehicleComponents`) | `ConstraintInstance` | Frontier physics constraints replace kinematic socket stubs |
| `LightProps` | `LuminaireFixtureProp` / `LuminaireFixtureRasterization` | GPU `.slang` mechanism suffix required (`SKILL-Naming.md` §2) |
| `ObjectComponents` | `PrimitiveComponentStack` | `Object` is barred (`BannedWordsVocabulary.md`) |
| `PostEffects` | `OpticsEffectInspectorSection` | Concrete domain naming bound to `CameraOpticsEncoder` |
