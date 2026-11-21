# Data-Driven `.Space` Content Formatting & Default Content Generator Plan

> **Scope**: **Architecture & Migration Plan Only** (zero mass deletion of current hardcoded C++ test rigs in this revision).
>
> **Governing Specifications**:
> - [DocumentFormatsInstancePlan.md](DocumentFormatsInstancePlan.md) — Two-Tier Hybrid `.Space` container (`TOML` header + `FRONTIER_BINARY_BOUNDARY` + `64`-byte-aligned SFNT/OpenType-inspired binary sections, `64`-bit FNV-1a checksums, `mmap` zero-copy arrays).
> - [DocumentSpacesRevisionPlan.md](DocumentSpacesRevisionPlan.md) — `ComponentSpace` vs. `Instance` two-register memory architecture, `Content/` folder layout, and `SpaceContainerCodec` (`Engine/Codecs/SpaceContainerCodec.{h,cpp}`).
> - [../BannedWordsVocabulary.md](../BannedWordsVocabulary.md) & [../CodingStandards.md](../CodingStandards.md) — Strict Frontier nomenclature across every C++ class, struct, function, file name, and `Content/` directory.

---

## 1. Problem Audit: Hardcoded C++ Assets Today

Across the current C++ codebase, geometry, UV parameterizations, materials, luminaires, solar ephemeris, atmospheric scattering, volumetric clouds, and world scenes are constructed procedurally inside C++ translation units rather than loaded from `Content/`:

| Hardcoded C++ Source | Embedded / Procedural Content Currently Living in C++ | Target `.Space` Replacement in `Content/` |
| :--- | :--- | :--- |
| `ShowcaseGalleryScene.cpp` (`BuildVehicleClearSkyDefaultScene`, `BuildShowcaseGalleryScene`) | Ground plane & Cornell room boxes/spheres, `12+` OpenPBR `SurfaceMaterial` definitions, LTC quad softbox, solar direction/irradiance, clear-sky / cloud / fog toggles, camera presets | `Content/Scenes/VehicleClearSkyDefault.ComponentSpace`<br>`Content/Scenes/CornellGalleryStudio.ComponentSpace` |
| `EngineContent/EnvironmentArchives/ControlVehicle/*.face` + `ControlVehicleSurfaceDecoder.cpp` | Legacy `FCT1` `.face` binary blobs (`Body`, `WheelFrontLeft`, `TyreFrontLeft`), hardcoded wheel transforms, `PartGroupCode -> VehicleSurfaceSlot` table | `Content/Vehicles/ControlVehicle/ControlVehicleBody.GeometrySpace` + `.UVSpace` + `ControlVehicleRig.ComponentSpace` |
| `Exhibits/DirectPbrWorldValidation.cpp` & `PhotometricProfile.cpp` | Inline LM-63 `.ies` ASCII strings, procedural showroom floor/walls, `11` car materials, floating point/photometric lamp rigs | `Content/Lighting/Photometry/*.Photometry`<br>`Content/Scenes/DirectPbrShowroom/*.ComponentSpace` |
| `AtmosphericSkySequence.h`, `VolumetricCloudSequence.h`, `CelestialEphemerisSolver.h`, `CloudNoiseVolume.h` | Default Rayleigh/Mie/Ozone constants, solar ephemeris lat/lon/UTC/day, moon orbital slots, starfield magnitude, `128³ + 64³` Perlin-Worley condensate volume | `Content/Environment/ClearSkyDaylight.VolumetricSpace`<br>`Content/Environment/CumulusStaticDeck.VolumetricSpace`<br>`Content/Lighting/SolarStandardDay.Photometry` |
| `ViewportPanel.h` & `TieredWorldRenderSequence.h` | Default render-tier, shadow mode, exposure/ACES tone-stack, lens-flare optics, camera FOV/clip defaults | `Content/Rendering/DefaultViewport.Render` |

---

## 2. Canonical Two-Tier `.Space` Framing (SFNT / TTF + TOML)

Every file inside `Content/` uses the existing `Frontier::Codecs::SpaceContainerCodec` two-tier framing modeled directly after TrueType/OpenType (`SFNT`) font table directories (`EngineContent/FontsArchives/FontRepository.FontSpace`):

1. **Tier 1 — Human-Readable UTF-8 TOML Header** (top of file, diffable in Git, inspectable in any text editor):
   - `[SpaceHeader]`: `FormatVersion`, `SpaceCategory`, `SubFormatTag`, `AssetUUID`, `DisplayTitle`, `AuthorTimestampUtc`, `CoordinateConvention = "FrontierWorldZUp"`, `UnitScaleMeters = 1.0`.
   - Domain-specific TOML tables (`[SolarEphemeris]`, `[AtmosphereMedium]`, `[CloudDeck]`, `[SurfaceMaterial]`, `[Luminaire]`, `[CameraOptics]`, `[[BinaryDirectory]]`).
   - `[[BinaryDirectory]]` mirrors the SFNT `TableRecord` directory: each entry declares `SectionTag` (`4`-CC / string identifier), `ByteOffset` (relative to `64`-byte aligned payload base), `ByteLength`, `ElementCount`, `ElementStrideBytes`, and `ChecksumFnv1a64`.
2. **Canonical Boundary Marker**:
   - Exact sentinel `"\n# === FRONTIER_BINARY_BOUNDARY ===\n"`, padded with zero bytes to the next **`64`-byte cache-line / SIMD / Vulkan `minStorageBufferOffsetAlignment` boundary**.
3. **Tier 2 — `64`-Byte-Aligned Binary Section Payload** (omitted for pure-parameter `.Space` files whose payload is `0` bytes; mandatory for heavy vertex/index/UV/texel/IES/SDF streams):
   - Every `BinarySectionBlock` starts on a `64`-byte boundary so `Platform::MemoryMappedFile` maps vertex positions, smooth normals, `UVAxes` streams, BCn/RGBA16F mip chains, `128×32` photometric candela grids, and `64³` distance/noise volumes directly into CPU `std::span<const T>` or Vulkan staging buffers with **zero text parsing and zero heap re-formatting**.

---

## 3. Target `Content/` Directory Hierarchy

```text
Content/
├── ContentManifest.ProjectSpace                 # Root content index, UUID -> relative path table, dependency hashes
├── Geometry/
│   ├── Primitives/
│   │   ├── GroundPlane24m.GeometrySpace         # Half-edge PolygonNetwork + creased normals + ClusterPyramid
│   │   ├── UnitBox1m.GeometrySpace
│   │   └── CalibrationSphere.GeometrySpace
│   └── Vehicles/
│       ├── ControlVehicleBody.GeometrySpace     # 12,132 tris, 11 PartGroupOrdinal surface slots, 38-deg creased normals
│       ├── ControlVehicleRim.GeometrySpace      # 1,704 tris per rim (instanced ×4 with mirror chirality)
│       └── ControlVehicleTyre.GeometrySpace     # 1,530 tris per tyre (instanced ×4)
├── UV/
│   ├── GroundPlane24m.UVSpace                   # Per-corner UVAxes, seam/pin edge bitmasks, UDIM tile bounds
│   ├── ControlVehicleBody.UVSpace
│   ├── ControlVehicleRim.UVSpace
│   └── ControlVehicleTyre.UVSpace
├── Materials/
│   ├── Architectural/
│   │   └── GroundPlaneAsphalt.MaterialSpace     # OpenPBR diffuse/specular/coat/subsurface/fuzz/thin-film/blackbody
│   └── Automotive/
│       ├── MetallicCobaltPaint.MaterialSpace
│       ├── CarbonAeroTrim.MaterialSpace
│       ├── PolishedChromeTrim.MaterialSpace
│       ├── TintedAutomotiveGlazing.MaterialSpace
│       ├── SmokedHeadlampHousing.MaterialSpace
│       ├── RubyTaillampLens.MaterialSpace
│       ├── CastIronBrakeRotor.MaterialSpace
│       ├── PerformanceBrakeCaliper.MaterialSpace
│       ├── ForgedAlloyWheelRim.MaterialSpace
│       ├── VulcanizedRubberTyre.MaterialSpace
│       └── AlcantaraInteriorCabin.MaterialSpace
├── Lighting/
│   ├── SunClearMorning1030.Photometry           # Solar ephemeris + spectral/RGB irradiance + angular radius
│   ├── CeilingSoftbox170x130.Photometry         # Analytic LTC polygonal quad emitter
│   ├── LowBeamHeadlamp.Photometry               # Parsed LM-63 header in TOML + 128×32 float32 candela grid in Binary
│   ├── TailLampRed.Photometry
│   └── CarParkDownlight.Photometry
├── Environment/
│   ├── ClearSkyStudio.VolumetricSpace           # Rayleigh/Mie/Ozone medium, clouds off, fog off, static wind = 0 m/s
│   ├── HighResStaticCumulus.VolumetricSpace     # Scale = 1.0, 64 march steps, wind = 0 m/s + 128³/64³ noise ref
│   └── CondensatePerlinWorley.VolumetricSpace   # Binary 128³ Shape + 64³ Erosion + 64² Blue-Noise packed texels
├── Rendering/
│   ├── DefaultViewport.Render                   # Engine default, cheap MatCap shadow config, GTX GDF bounds, ACES/exposure
│   └── NightPhotometricShowroom.Render
└── Scenes/
    ├── VehicleClearSkyDefault.ComponentSpace    # Default Startup Scene: GroundPlane24m + ControlVehicle + Sun + ClearSky
    ├── CornellGalleryStudio.ComponentSpace      # 10-specimen Cornell Gallery preserved for validation parity
    └── NightCarParkPhotometric.ComponentSpace   # Night .ies showroom validation scene
```

> **Vocabulary Note**: Although `SpaceContainerCodec.h` retains `SpaceSuffixCategory::Assembly` for legacy enum compatibility, all newly authored multi-entity world/rig files and C++ loader classes use `.ComponentSpace` (`ComponentSpaceCodec`) rather than banned words.

---

## 4. Per-Domain `.Space` Schemas (TOML + Binary Sections)

### 4.1 UV & Surface Geometry (`.GeometrySpace` + `.UVSpace`)

Separating `.GeometrySpace` and `.UVSpace` (or referencing an external `.UVSpace` UUID from `.GeometrySpace`) lets UV unwrapping (`UVSpace` workspace) iterate on seam masks, pins, and UDIM islands without rewriting 3D vertex/BVH buffers:

- **`.GeometrySpace` TOML**:
  - `[GeometryMetrics]`: `VertexCount`, `DirectedEdgeCount`, `FaceCount`, `TriangleCount`, `PartSlotCount`, `SharpCreaseAngleDeg`, `BoundsMinMeters`, `BoundsMaxMeters`.
  - `[[SurfaceSlotMap]]`: maps `PartGroupOrdinal` (`0..N-1`) to default `.MaterialSpace` UUID and human-readable slot name (`"BodyPaint"`, `"HeadlightLens"`, …), plus separate sub-part segmentation metadata where needed (e.g., splitting front nose fascia trim from actual optical headlamp emitters).
  - `[[BinaryDirectory]]` sections (`64B` aligned):
    1. `"VERT"` — `Axis3D[VertexCount]` (`12 B` stride)
    2. `"NORM"` — `TriangleSmoothNormals[TriangleCount]` (`36 B` stride, precomputed creased smooth normals)
    3. `"INDX"` — `uint32_t[TriangleCount * 3]` + `uint16_t[TriangleCount]` (`PartGroupOrdinal`)
    4. `"HEDG"` — Compact half-edge `DirectedEdge` table for `GeometrySpace` topological editing
    5. `"CLST"` — Prebuilt `ClusterPyramid` LOD hierarchy for instant zero-rebuild startup
- **`.UVSpace` TOML + Binary**:
  - `[UVParameterization]`: `TargetGeometryUUID`, `UDIMTileCount`, `ChartCount`, `TexelDensityPxPerMeter`.
  - Binary sections:
    1. `"UV2F"` — `UVAxes[CornerCount]` (`8 B` `float2` stride)
    2. `"SEAM"` — `uint8_t[DirectedEdgeCount]` (`EdgeAttributeMask`: seam, sharp-crease, corner-pin, border)
    3. `"UDIM"` — `uint16_t[FaceCount]` derived UDIM tile numbers (`1001..1099`)

### 4.2 Sun, LTC Area Luminaires & LM-63 Photometry (`.Photometry`)

Unifies directional solar sources, analytic polygonal LTC luminaires, omni point lights, and measured IESNA LM-63 goniophotometric luminaires under one physical radiometric container:

- **TOML `[LuminaireHeader]`**: `LuminaireKind = "SolarEphemeris" | "PolygonalLtcQuad" | "PointOmnidirectional" | "PhotometricIes"`.
- **`[SolarSource]`**: `LatitudeDegrees`, `LongitudeDegrees`, `DayOfYear`, `LocalSolarHours`, `UtcOffsetHours`, `NorthAzimuthDegrees`, `SolarIrradianceRgb`, `AngularDiameterDegrees`, `SolarEnabled`.
- **`[LtcAreaQuad]`**: `WidthMeters`, `HeightMeters`, `EmittedRadianceNitsRgb`, `TwoSided`.
- **`[PhotometricIes]`**: `PeakCandela`, `NormalizedFluxSteradians`, `CutoffHalfAngleRadians`, `EmitterRadiusMeters`, `RangeMeters`, `CorrelatedColorTempKelvin`, `TintLinearRgb`.
- **Binary Section `"IESG"`** (only when `LuminaireKind == "PhotometricIes"`):
  - `float32[128 * 32]` (`16,384` bytes, `64B` aligned) resampled equirectangular horizontal-azimuth × polar-angle candela grid, ready for direct upload into `PhotometricProfileBuffer` with zero ASCII `.ies` parsing at runtime.

### 4.3 Sky, Atmosphere, Clouds & Wind (`.VolumetricSpace`)

Captures the entire planetary sky, celestial bodies, static/dynamic volumetric cloud deck, bounded local cloud volumes, height fog, and wind field:

- **TOML Tables**:
  - `[AtmosphereMedium]`: `AtmosphereEnabled`, `OrbitalViewEnabled`, `PlanetRadiusMeters`, `AtmosphereThicknessMeters`, `ObserverAltitudeMeters`, `RayleighStrength`, `RayleighScaleHeightMeters`, `MieStrength`, `MieScaleHeightMeters`, `MieAnisotropyG`, `OzoneStrength`, `SkyBrightnessMultiplier`, `SkyTintRgb`, `GroundAlbedoRgb`.
  - `[CelestialBodies]`: `StarfieldEnabled`, `LimitingMagnitude`, `ScintillationStrength`, `[[Moons]]` (`PresetName`, `Visible`, `AngularDiameterDegrees`, `OrbitalPhase01`, `AlbedoTintRgb`).
  - `[CloudDeck]`: `CloudsEnabled` (`false` in `ClearSkyStudio`), `FollowWindEnabled` (`false` static default), `Category`, `CloudFraction`, `ExtinctionMultiplier`, `FeatureScaleMultiplier`, `CloudFloorMeters`, `LayerThicknessMeters`, `AnvilTopSpread`, `ForwardScatterAnisotropy`, `WindSpeedMetersPerSecond` (`0.0`), `CloudRenderScale` (`1.0`), `CloudMarchSteps` (`64`), `NoiseVolumeUUID`.
  - `[HeightFog]` & `[[LocalClouds]]` & `[[LocalFogs]]`.
- **Binary Sections** (inside `CondensatePerlinWorley.VolumetricSpace` or baked GDF volumes):
  - `"CLDN"` — Packed `uint32_t[TexelCount]` `128³` Perlin-Worley shape + `64³` Worley erosion + `64²` void-and-cluster rank volume (`CondensateNoise`), mapped directly onto Vulkan staging memory instead of CPU startup synthesis.

### 4.4 OpenPBR Surface Materials (`.MaterialSpace`)

- **TOML Tables**: `[BaseDiffuse]`, `[SpecularLobe]`, `[ClearCoat]`, `[Transmission]`, `[Subsurface]`, `[FuzzSheen]`, `[ThinFilmIridescence]`, `[PorosityBlackbody]`, `[TextureBindings]` (`AlbedoTextureUUID`, `NormalTextureUUID`, `RoughnessMetalnessUUID`, `UVSpaceUUID`).
- Zero binary payload required when procedural/parametric; optional `"LUTB"` binary block for measured BRDF / spectral IOR curves.

### 4.5 Viewport & Renderer Preset (`.Render`)

- **TOML Tables**: `[RenderEngine]` (`DefaultEngine = "MatCap" | "OpenPbrDirect" | "RealTimeGiGtxLowEnd" | "RealTimeGiHighEnd" | "PathTracer"`, `MatCapShadowMode = "BilinearCascade2x2"`), `[ShadowCascades]` (`FineHalfSpanMeters = 2.85`, `CoarseHalfSpanMeters = 8.50`, `ResolutionTexels = 1024`), `[GlobalDistanceField]` (`FineHalfSpanClampMeters = [3.2, 4.2]`, `VoxelResolution = 64`), `[ToneStack]` (`ExposureEv`, `ToneCurve = "ACESFitted"`), `[LensFlare]`.

### 4.6 World Scene Composition (`.ComponentSpace`)

Replaces `BuildVehicleClearSkyDefaultScene` and `BuildShowcaseGalleryScene` with a declarative scene manifest referencing UUIDs/paths in `Content/`:

- **TOML Tables**:
  - `[SceneMetadata]`: `SceneTitle`, `EnvironmentUUID` (`ClearSkyStudio.VolumetricSpace`), `PrimarySunUUID` (`SunClearMorning1030.Photometry`), `RenderPresetUUID` (`DefaultViewport.Render`).
  - `[EditorCamera]`: `OriginMeters`, `TargetMeters`, `WorldUpAxis`, `VerticalFovDegrees`, `NearClipMeters`, `FarClipMeters`, plus `[[CameraPresets]]` (`Top`, `Front`, `Right`, `Perspective`).
  - `[[OutlinerFolders]]`: folder hierarchy, tint, subtitle, expanded flag.
  - `[[GeometryPlacements]]`: `StableIdentifier`, `DisplayTitle`, `FolderId`, `GeometryUUID`, `UVSpaceUUID`, `MaterialOverrides`, `TranslationMeters`, `RotationEulerDegrees`, `ScaleXYZ`, `VisibilityEnabled`, `CastShadows`, `PrimarySelected`.
  - `[[LuminairePlacements]]`: references `.Photometry` assets with world position, aim axis, zero-azimuth reference axis, and intensity scale.
- **Optional Binary Section `"BVH4"` / `"GDFC"`**: cached world `WideBoundingTree` or static `64³` GDF clip for sub-millisecond cold-boot scene open.

---

## 5. C++ Default-Asset Generator Tool (`FrontierDefaultContentGenerator`)

To guarantee `Content/` is **100% reproducible from clean Git checkouts**, never drifts from C++ struct versions, and eliminates hand-edited binary blobs, we will introduce a standalone C++ command-line build/utility target **`FrontierDefaultContentGenerator`** (`Engine/Tools/DefaultContentGenerator/DefaultContentGeneratorMain.cpp`):

### 5.1 Responsibilities of `FrontierDefaultContentGenerator`

1. **Deterministic UUID & Checksum Synthesis**:
   - Derives stable `128`-bit asset UUIDs from canonical `Content/` relative paths via `64`-bit FNV-1a / SHA-based domain hashing so re-running the generator produces byte-identical `.Space` files (`0`-byte Git diff when inputs are unchanged).
2. **Geometry & UV Extraction**:
   - Converts `AnalyticPrimitives` (`GroundPlane24m`, `UnitBox1m`, `CalibrationSphere`, `CornellRoom`) AND transcodes legacy `EngineContent/EnvironmentArchives/ControlVehicle/*.face` (`ControlVehicleSurfaceDecoder`) into standalone `.GeometrySpace` + `.UVSpace` files in `Content/Geometry/` and `Content/UV/`.
   - Simultaneously emits an optional clean headlamp-emitter sub-slot split so daytime/nighttime headlamp lenses and fascia panels can be addressed independently without blowing out the nose bar.
3. **Material, Photometry, Sky & Environment Generation**:
   - Serializes the `12` default architectural + automotive OpenPBR `SurfaceMaterial` definitions into `Content/Materials/**/*.MaterialSpace`.
   - Compiles solar presets, LTC softboxes, and resampled `128×32` LM-63 `.ies` candela tables (`LowBeamHeadlamp`, `TailLampRed`, `CarParkDownlight`) into `Content/Lighting/*.Photometry`.
   - Packages `ClearSkyStudio.VolumetricSpace`, `HighResStaticCumulus.VolumetricSpace`, and the `128³ + 64³` `CondensatePerlinWorley.VolumetricSpace` 3D noise volume into `Content/Environment/`.
4. **Default & Validation Scene Generation**:
   - Emits `Content/Scenes/VehicleClearSkyDefault.ComponentSpace` (Default Editor startup scene: `GroundPlane24m` + `ControlVehicle` + `SunClearMorning1030` + `ClearSkyStudio`), `CornellGalleryStudio.ComponentSpace`, and `NightCarParkPhotometric.ComponentSpace`, plus `Content/ContentManifest.ProjectSpace`.
5. **Round-Trip Verification Gate**:
   - Immediately re-opens every generated `.Space` file via `SpaceContainerCodec::InspectHeaderOnly` and `MemoryMappedFile`, verifying `64`-byte binary alignment, `ChecksumFnv1a64` integrity, triangle counts, UV seam counts, and radiometric bounds before exiting `0`.

### 5.2 CMake & Editor Startup Integration

- **CMake Target**: `add_executable(FrontierDefaultContentGenerator ...)` + explicit custom target `FrontierGenerateDefaultContent` (`cmake --build Build/Editor --target FrontierGenerateDefaultContent`).
- **Auto-Bootstrap Fallback**: If a developer launches `FrontierEditor` in a fresh workspace where `Content/ContentManifest.ProjectSpace` is missing, `DefaultContentBootstrapSequence` invokes the same library entry point once in-process to populate `Content/` before opening `Content/Scenes/VehicleClearSkyDefault.ComponentSpace`.

---

## 6. Phased Execution Roadmap (For Approval Before Implementation)

| Phase | Deliverable | Verification Gate |
| :--- | :--- | :--- |
| **Phase 1** | Extend `NetworkCodec`, `UVCodec`, `MaterialCodec`, and `PhotometricProfileCodec` with `SpaceContainerCodec` `64B`-aligned binary section read/write (`"VERT"`, `"NORM"`, `"INDX"`, `"UV2F"`, `"SEAM"`, `"IESG"`), plus new `VolumetricSpaceCodec` and `WorldSceneSpaceCodec`. | Dedicated unit round-trip validation (`SpaceContentCodecValidation`) asserting byte-identical reload and `64B` `mmap` alignment. |
| **Phase 2** | Build `FrontierDefaultContentGenerator` C++ executable and generate the complete initial `Content/` tree (`Geometry/`, `UV/`, `Materials/`, `Lighting/`, `Environment/`, `Rendering/`, `Scenes/`, `ContentManifest.ProjectSpace`). | Zero-diff idempotency check (`run twice -> sha256 identical`) + `ContentRepositoryScanner` discovery of all generated `.Space` assets in Content Browser. |
| **Phase 3** | Wire `EditorLifecycle` and `ProjectBrowser` to load `Content/Scenes/VehicleClearSkyDefault.ComponentSpace` via `WorldSceneSpaceCodec`, and add `"Save Scene (.ComponentSpace)"` / `"Open Scene"` round-tripping in the Editor. | `FrontierEditor` boots directly from `Content/Scenes/VehicleClearSkyDefault.ComponentSpace` with zero hardcoded C++ scene builders on the startup path. |
| **Phase 4** | Migrate `DirectPbrWorldValidation`, `TieredWorldRenderValidation`, `GtxGlobalDistanceFieldViewportValidation`, and `AtmosphereAuditProof` to load `CornellGalleryStudio.ComponentSpace` and `NightCarParkPhotometric.ComponentSpace` from `Content/`, then retire legacy `.face` blobs and procedural C++ gallery builders. | All `72` CMake targets and image exhibits pass parity checks cleanly. |
