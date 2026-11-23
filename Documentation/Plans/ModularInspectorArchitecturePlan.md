# Modular Generic Inspector Architecture & 1:1 Parity Plan

> **Status**: Approved & Active (`2026-11-23`)
> **Branch**: `arena/55f26d94-frontier` (synced with `egoistinbox-hue/Frontier@arena/2809d087-frontier`, commit `ab3b8ea`)
> **HTML Reference**: `ProjectZeroEditor@2fd4c62` (exact Chromium `getComputedStyle` measurements at `1440x900`)

---

## 1. Core Architectural Invariants

1. **Zero Monolithic Buckets**:
   - No `WorldEnvironmentInspector`, no `EnvironmentInspectorPanels`, no `SceneObjectInspectorPanels`, and no monolithic `InspectorOpticalPreviews`.
   - Every Inspector is a standalone, self-contained `.h`/`.cpp` pair owning its own pages, parameter bindings, and (where applicable) its own retained preview rasterizer built on `InspectorPreviewSurface`.
2. **Zero Hardcoded Folders & Zero Artificial Singletons**:
   - Folders are generic Outliner containers (`FolderInspector`), never hardcoded to `"ENVIRONMENT"`.
   - Inspector breadcrumbs are resolved dynamically from the actual enclosing Outliner folder hierarchy (e.g. `WORLD`, `MY FOLDER`, `SKY RIG`, etc.).
   - Multiple instances of **any** construct (`2+` Suns, `2+` Moons, multiple Winds, multiple Starfields, multiple Fogs, multiple Cloud layers, multiple Cameras, multiple Lights, multiple Post-Process volumes, multiple Geometry subjects) can coexist anywhere in the Outliner, each retaining independent per-entry parameters in `InspectorInstanceRegister` (keyed by `OutlinerSlotToken`) and combining cleanly into `WorldSceneSpecification`.
3. **Shared Retained On-Demand Preview Surface**:
   - Every visual preview (LUTs, planetary limb, solar sky, lunar globe, star chart/sheet, cloud deck cross-section, camera lens, photometric beam, geometry PBR specimen) uses the generic `InspectorPreviewSurface` (`ImTextureData` + 64-bit `RetainedSignature`).
   - Pixels are ONLY re-rasterized when input parameters change; otherwise the already-uploaded texture is presented directly.
4. **1:1 Visual Parity with Chromium Reference (No Nested Heavy Workspaces)**:
   - Exact Chromium-measured tokens: `340px` pane (`34px` horizontal padding -> `272px` content column), `25px` section radius, `#202020` surface, `1px` `rgba(255,255,255,0.03)` border, `23/24/22` padding, `16px` section gap, `34px` `PageStrip` (`26x28` steppers + `18x18` numbered circles + `1 / N` counter), `106x100` `17px`-radius `Quick controls` grid with `#5BDB87` active count, `92x28` split numeric readout + slider, `44x24` toggle (`#34C759` active), `#24303F`/`#BCD8FF` tag chips, `86px` notes field, and real LUT previews on `Baking` pages.
   - Heavy secondary-viewport nested Workspaces (Fracture, LOD, UV, Collision, Cluster) are intentionally omitted from `GeometryInspector`.
5. **Strict Governance (`Documentation/BannedWordsVocabulary.md`)**:
   - Zero occurrences of banned tokens (`Kind`, `Card`, `Carousel`, `Pill`, `Badge`, `Glyph`, `Graph`, `Draw`, `Make`, `State`, `Value`, `Flag`, `Shell`, `Cache`, `Store`, `Mesh`, `Object`, `Studio`, `Domain`, `Config`, `Table`, `Parent`, `Child`, `Base`, `Emit`, `Emission`, `Map`, `Item`, `Node`, `Menu`, `Popup`, `Context`, `And` in identifiers, `Is*`/`Has*`).

---

## 2. Locked Folder & File Layout

### Layer A — Generic Reusable UI Primitives (`Engine/Components/Controls/`)
| File Pair | Responsibility |
| :--- | :--- |
| `InspectorSectionPrimitives.{h,cpp}` | Generic CSS-exact `InspectorMetrics`, `InspectorPreviewSurface` (on-demand retained RGBA32 `ImTextureData`), header, `PageStrip`, section surface, metric readout, KPI metric grid, `Quick controls` grid, scalar/toggle/colour/choice rows, curve plot, LUT preview section, tags, notes, footer |
| `PolarDirectionDial.{h,cpp}` | Generic reusable interactive compass / sky-dome azimuth & elevation dial control |

### Layer B — Generic Multi-Instance Register & Dispatch (`Engine/Editor/Inspectors/`)
| File Pair | Responsibility |
| :--- | :--- |
| `InspectorInstanceRegister.{h,cpp}` | Per-entry (`OutlinerSlotToken`) independent parameter register for all multi-instance types + combination pass syncing active visible entries into `WorldSceneSpecification` & `ViewportDisplaySettings` |
| `InspectorRegistry.{h,cpp}` | Generic dispatch registry mapping any selected `OutlinerRecordEntry` to its dedicated standalone Inspector, resolving dynamic enclosing-folder breadcrumbs, handling scroll/Reset/Delete |

### Layer C — Standalone Per-Type Inspectors (`Engine/Editor/Inspectors/`)
| # | File Pair | Pages | Own Retained Preview(s) | Multi-Instance |
| :---: | :--- | :--- | :--- | :---: |
| 1 | `AtmosphereInspector.{h,cpp}` | `Overview`, `Scattering`, `Density`, `Sky colour`, `View`, `Baking` | Planetary limb, Orbital full disc, `256x64` Transmittance LUT, `192x96` Sky-View LUT | Yes |
| 2 | `SolarInspector.{h,cpp}` | `Overview`, `Temperature`, `Sun disc`, `Dynamic`, `Baking` | `Sun now` sky picture (`A`/`B`/`C`), `192x96` Solar Sky-View bake | Yes (`2+` Suns) |
| 3 | `CloudInspector.{h,cpp}` | `Overview`, `Coverage`, `Layer`, `Body`, `Wind`, `Quality` | `224x176` vertical altitude cloud-deck cross-section | Yes |
| 4 | `LocalCloudInspector.{h,cpp}` | `Overview`, `Coverage`, `Layer`, `Body` | Bounded volume extent / density preview | Yes |
| 5 | `LunarInspector.{h,cpp}` | `Overview`, `Appearance`, `Position` | Textured ray-cast Lunar Globe (`LunarSurfaceAtlas`), 6 preset globes, night-sky halo + `0.52 deg` natural ring, orientation globe | Yes (`2+` Moons) |
| 6 | `StarfieldInspector.{h,cpp}` | `Overview`, `Motion`, `Renderer`, `Baking` | 300-star deterministic equatorial chart, `256x128` equirectangular Star Sheet bake | Yes |
| 7 | `WindInspector.{h,cpp}` | `Overview`, `Air` | Interactive `PolarDirectionDial` compass | Yes (vector-summed) |
| 8 | `HeightFogInspector.{h,cpp}` | `Overview`, `Density`, `Light` | Altitude extinction curve plot | Yes |
| 9 | `AtmosphericFogInspector.{h,cpp}` | `Overview`, `Density`, `Light` | Distance aerial extinction curve plot | Yes |
| 10 | `LocalFogInspector.{h,cpp}` | `Overview`, `Density`, `Light` | Bounded volume falloff curve | Yes |
| 11 | `GodRaysInspector.{h,cpp}` | `Overview`, `Optics` | Scattering anisotropy phase curve | Yes |
| 12 | `PrecipitationInspector.{h,cpp}` | `Overview`, `Weather` | Rate / wind-slant profile | Yes |
| 13 | `RainbowInspector.{h,cpp}` | `Overview`, `Optics` | Primary / secondary bow dispersion curve | Yes |
| 14 | `CameraInspector.{h,cpp}` | `Overview`, `Lens`, `Body`, `Motion blur`, `Lens flare` | Retained optical glass lens preview | Yes |
| 15 | `LensFlareInspector.{h,cpp}` | `Overview`, `Optics` | Optical glare & ghost response preview | Yes |
| 16 | `MotionBlurInspector.{h,cpp}` | `Overview`, `Shutter` | Shutter angle exposure profile | Yes |
| 17 | `LightInspector.{h,cpp}` | `Overview`, `Intensity`, `Beam`, `Shadows` | Retained photometric beam-cone preview | Yes |
| 18 | `EnvironmentLightInspector.{h,cpp}` | `Overview`, `Indirect` | Retained hemisphere irradiance preview | Yes |
| 19 | `PostProcessInspector.{h,cpp}` | `Overview`, `Tone`, `Colour` | Tone-curve response plot | Yes |
| 20 | `AnalyticPrimitiveInspector.{h,cpp}` | `Overview`, `Shape`, `Transform`, `Material`, `Physics` | Category 1 Geometry: retained analytic raycast preview (Sphere/Box/Cylinder/Cone/Torus/Capsule/Plane) | Yes |
| 21 | `NativeGeometryInspector.{h,cpp}` | `Overview`, `Geometry`, `Transform`, `Material`, `Physics` | Category 2 Geometry: retained native `.GeometrySpace` preview + Cluster-LOD / Local SDF KPIs (universal fallback) | Yes |
| 22 | `ImportedGeometryInspector.{h,cpp}` | `Overview`, `Import`, `Transform`, `Material`, `Physics` | Category 3 Geometry: retained exchange preview + glTF/FBX/OBJ/USD/STEP/STL import pipeline & re-import | Yes |
| 23 | `ProceduralGeometryInspector.{h,cpp}` | `Overview`, `Generator`, `Transform`, `Material`, `Physics` | Category 4 Geometry: retained procedural preview + Sweep/Terrain/Boolean/Spline/Voronoi generator & rebuild | Yes |
| 24 | `FolderInspector.{h,cpp}` | `Overview`, `Contents` | Generic container summary for any Outliner folder | Yes |

---

## 3. Multi-Instance Combination Semantics (`InspectorInstanceRegister::CombineIntoScene`)

When multiple instances of the same construct are visible in the Outliner (regardless of which folder contains them):
- **Suns**: Primary active Sun drives ephemeris sky disc; additional visible Suns contribute summed solar irradiance / directional radiance (`Scene.Sun.SolarIrradiance` + combined tint) and additional directional/high-altitude photometric lights so `2+` Suns work cleanly in both Inspector and Renderer.
- **Moons**: Every visible Moon instance maps sequentially into `Scene.Atmosphere.Moons[0..5]` (up to 6 simultaneous active moons in the renderer, with unlimited independent Outliner entries retained in the register).
- **Winds**: Visible Wind instances vector-sum their `(Speed * sin(Bearing), Speed * cos(Bearing))` velocity vectors and combine shear/turbulence into `Scene.Atmosphere.Wind`.
- **Starfields**: Visible Starfield instances combine limiting magnitude (`max`), brightness (`sum`, clamped), and boolean feature unions (`Milky Way`, `Zodiacal light`, `Twinkle`) into `Scene.Atmosphere.Stars`.
- **Local Clouds / Local Fogs**: Visible instances populate `Scene.Atmosphere.LocalClouds` and `Scene.Atmosphere.Fog.LocalFogs` vectors directly.
- **Lights / Cameras / Geometry / PostProcess**: Each entry stores its own independent parameters keyed by `OutlinerSlotToken`; editing one never mutates another.

---

## 4. Verification Gates

1. `BannedWordsVocabulary.md` & zero `Kind`/`KIND` automated grep audit across every added/modified file (`0` violations).
2. Clean Release Editor build of `Frontier`, `WorkspacePanesValidation`, and `EnvironmentConstructValidation` (`0` errors).
3. Functional assertions in `EnvironmentConstructValidation`:
   - Multi-instance test: 2 independent Suns + 2 independent Moons + 2 independent Winds + 2 independent Starfields placed inside a custom generic folder (`"Sky Rig"`) — verify distinct per-instance parameters, dynamic breadcrumb `"SKY RIG"`, and combined vector/radiance output in `WorldSceneSpecification`.
   - Retained preview stability test: verify `InspectorPreviewSurface::QueryUploadCount()` does NOT increment across repeated frames with unchanged parameters, and DOES increment by exactly `1` when a parameter changes.
4. Visual 1:1 inspection of rendered PNG exhibits via `read_file` covering Atmosphere (Overview + Baking LUTs), Sun (Overview + Baking), Clouds (Overview + Layer), Moon (Overview + Appearance), Stars (Overview + Baking), Wind, Camera, Light, PostProcess, Geometry specimen, and custom Folder.
