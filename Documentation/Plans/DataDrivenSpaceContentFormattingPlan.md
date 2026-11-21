# Data-Driven `.InSpace` Content Formatting & Default Content Generator Plan

> **Status**: Ready for review in Markdown Studio before executing Phase 1–4 implementation.
>
> **Core Container Rule (`.InSpace` Unified Extension)**:
> All formatted files inside **`Content/`** use the unified **`.InSpace`** global container extension (case-insensitive load `.inspace` / `.InSpace`, canonical `.InSpace` write), carrying **Two-Tier Hybrid SFNT/TTF + TOML framing**:
> - **Tier 1 (Top — UTF-8 TOML Manifest)**: `['.DocumentHeader']` (`ContainerExtension = '.InSpace'`, `DomainSubFormat = '.GeometrySpace' | '.UVSpace' | '.MaterialSpace' | '.Photometry' | '.VolumetricSpace' | '.Render' | '.ComponentSpace' | '.WorldSpace'`), human-readable domain tables, and `[['.SectionDirectory']]` (`RelativeAlignedOffset`, `ByteLength`, `ChecksumFnv1a64`, `SubFormatTag`).
> - **Boundary**: `"\n# === FRONTIER_BINARY_BOUNDARY ===\n"` zero-padded to a **`64`-byte boundary**.
> - **Tier 2 (Bottom — `64B`-Aligned SFNT Binary Payload)**: Zero-copy `MemoryMappedFile` / Vulkan staging blocks (`Vertices`, `SmoothNormals`, `CornerUVs`, `CandelaGrid128x32`, `CondensateTexels`, `WorldBvh4`, `.Revision`).
>
> *(Backward compatibility: `SpaceContainerDecoder` continues reading existing `FontRepository.FontSpace`, `*.InputSpace`, and `.ProjectSpace` files seamlessly while registering `.InSpace` / `.inspace` as the primary `Content/` container.)*

---

## 1. Canonical `Content/` Folder Structure (`.InSpace` Everywhere)

```text
Frontier/
└── Content/
    ├── ContentIndex.InSpace                             # Root manifest: UUID/hash directory, startup world & render preset links
    │
    ├── Worlds/                                          # Complete openable world scenes (DomainSubFormat = '.WorldSpace')
    │   ├── VehicleClearSkyDefault.InSpace               # Default Startup Scene: GroundPlane24m + ControlVehicle + Sun + ClearSky
    │   ├── CornellGalleryStudio.InSpace                 # 10-specimen Cornell Gallery (preserved for validation parity)
    │   └── NightCarParkPhotometric.InSpace              # Night car-park .ies luminaire showroom scene
    │
    ├── Components/                                      # Multi-part hierarchical rigs & Jolt constraints ('.ComponentSpace')
    │   └── ControlVehicleRig.InSpace                    # Body + 4 Rims + 4 Tyres transforms, 11 slot links, wheel physics setup
    │
    ├── Geometry/                                        # 3D PolygonNetwork + creased normals + ClusterPyramid ('.GeometrySpace')
    │   ├── Primitives/
    │   │   ├── GroundPlane24m.InSpace                   # 24.0 x 24.0 x 0.16 m Z=0 ground slab
    │   │   ├── UnitBox1m.InSpace                        # Canonical 1 m box primitive
    │   │   ├── CalibrationSphere.InSpace                # 32x20 smooth-normal sphere primitive
    │   │   ├── StandardCylinder.InSpace                 # Analytic cylinder primitive
    │   │   ├── TrafficCone.InSpace                      # Analytic cone primitive
    │   │   └── ShaderTorus.InSpace                      # Analytic torus primitive
    │   └── Vehicles/
    │       ├── ControlVehicleBody.InSpace               # Transcoded from FCT1 body .face (creased normals + 11 part slots)
    │       ├── ControlVehicleRim.InSpace                # Transcoded from FCT1 rim .face (instanced x4 with mirror chirality)
    │       └── ControlVehicleTyre.InSpace               # Transcoded from FCT1 tyre .face (instanced x4)
    │
    ├── UVLayouts/                                       # 2D UVTile parameterizations, seams, pins & UDIMs ('.UVSpace')
    │   ├── Primitives/
    │   │   ├── GroundPlane24mUV.InSpace
    │   │   ├── UnitBox1mUV.InSpace
    │   │   └── CalibrationSphereUV.InSpace
    │   └── Vehicles/
    │       ├── ControlVehicleBodyUV.InSpace             # 878 UVTile islands, UDIM 1001 sheet, seam/pin edge masks
    │       ├── ControlVehicleRimUV.InSpace
    │       └── ControlVehicleTyreUV.InSpace
    │
    ├── Materials/                                       # OpenPBR surface shading definitions ('.MaterialSpace')
    │   ├── Architectural/
    │   │   ├── GroundPlaneAsphalt.InSpace
    │   │   ├── GalleryNeutralPlaster.InSpace
    │   │   ├── CornellCrimsonWall.InSpace
    │   │   ├── CornellEmeraldWall.InSpace
    │   │   ├── BrushedGoldConductor.InSpace
    │   │   └── AbbeCrownGlassOrb.InSpace
    │   └── Automotive/
    │       ├── ControlVehicleMetallicPaint.InSpace
    │       ├── VehicleObsidianCanopy.InSpace
    │       ├── VehicleTintedWindshield.InSpace
    │       ├── VehicleIntakePolymer.InSpace
    │       ├── VulcanizedTyreRubber.InSpace
    │       ├── ForgedTitaniumRimAlloy.InSpace
    │       ├── AnodizedBronzeHubCap.InSpace
    │       ├── MachinedSteelBrakeRotor.InSpace
    │       ├── SatinGraphiteTrim.InSpace
    │       ├── SmokedHeadlampHousing.InSpace            # Daytime 0-nit smoked lens (prevents full-nose white blowout)
    │       ├── XenonHeadlampEmissive.InSpace            # Nighttime emissive optic variant
    │       └── RubyTaillampOptic.InSpace
    │
    ├── Photometry/                                      # Sun ephemeris, LTC area quads & LM-63 .ies profiles ('.Photometry')
    │   ├── Solar/
    │   │   ├── ClearMorningSun1030.InSpace              # Lat/Lon/Day/10.5h solar ephemeris + RGB irradiance + disc angle
    │   │   └── NocturnalZeroSun.InSpace                 # Nighttime 0-irradiance solar config
    │   ├── AreaLuminaires/
    │   │   ├── CeilingSoftbox170x130.InSpace            # Clipped-horizon LTC rectangular quad luminaire
    │   │   └── ShowroomOverheadSoftbox.InSpace
    │   └── IesProfiles/
    │       ├── LowBeamHeadlamp.InSpace                  # TOML LM-63 metadata + 64B-aligned 128x32 float32 candela binary
    │       ├── TailLampRed.InSpace
    │       └── CarParkDownlight.InSpace
    │
    ├── Environment/                                     # Sky scattering, celestials, static clouds, fog & wind ('.VolumetricSpace')
    │   ├── Skies/
    │   │   ├── ClearSkyDaylight.InSpace                 # Rayleigh/Mie/Ozone sky, CloudsEnabled = false, Wind = 0 m/s
    │   │   └── NocturnalStarrySky.InSpace               # Starfield + 6-slot lunar presets + nocturnal airglow
    │   ├── Clouds/
    │   │   ├── StaticHighResCumulus.InSpace             # CloudRenderScale = 1.0, CloudMarchSteps = 64, Wind = 0 m/s
    │   │   └── CondensateNoiseVolume.InSpace            # Binary 128^3 Shape + 64^3 Erosion + 64^2 Blue-Noise packed texels
    │   └── Fog/
    │       └── ExponentialHeightFogOff.InSpace          # Height fog & bounded local fog volume defaults
    │
    ├── Rendering/                                       # Viewport engine, shadow cascade, GDF & tone-stack presets ('.Render')
    │   ├── DefaultEditorViewport.InSpace                # Cheap 2x2 MatCap shadow, 2.85m/8.5m cascades, 4.2m Fine GDF, ACES
    │   └── NightPhotometricViewport.InSpace
    │
    └── Optics/                                          # Baked optical sheets & sampling ranks ('.ImageSpace' / '.TextureSpace')
        ├── BlueNoiseSamplingRanks.InSpace               # 64B-aligned Owen-scrambled Sobol/rank tables (replaces raw .bin)
        ├── MatCapStudioSheet.InSpace                    # Replaces raw MatCapStudioAtlas.rgba + MatCapStudioCatalog.json
        ├── LensFlareBakedSheet.InSpace                  # Optical ghost/starburst sheet
        └── LunarSurfaceSheet.InSpace                    # Procedural 6-moon albedo/normal sheet
```

---

## 2. Two-Tier `.InSpace` Wire Format (SFNT/TTF Binary Table + TOML)

Every `.InSpace` file is a self-describing Two-Tier Hybrid Container (`SpaceContainerCodec`):

```toml
# Frontier Two-Tier Hybrid .InSpace Container (Canonical PascalCase Manifest)
['.DocumentHeader']
AuthoringWorkspace = 'Frontier.Editor'
BinaryAlignmentBytes = 64
ContainerExtension = '.InSpace'
DocumentTitle = 'Control Vehicle Body'
DomainSubFormat = '.GeometrySpace'
SchemaVersion = 1

[GeometryMetrics]
CreaseAngleDegrees = 38.0
FaceCount = 12132
PartSlotCount = 11
UVLayoutPath = 'Content/UVLayouts/Vehicles/ControlVehicleBodyUV.InSpace'
VertexCount = 6418

[['SurfaceSlots']]
MaterialPath = 'Content/Materials/Automotive/ControlVehicleMetallicPaint.InSpace'
PartGroupOrdinal = 0
SlotName = 'BodyCoat'

[['.SectionDirectory']]
ByteLength = 77016
ChecksumFnv1a64 = '0x8f3a1904c271d05b'
RelativeAlignedOffset = 0               # Multiple of 64 bytes from Tier 2 start
SectionIdentifier = 'Vertices'
SubFormatTag = '.PolygonNetwork'

[['.SectionDirectory']]
ByteLength = 436752
ChecksumFnv1a64 = '0x51a0c8127e4903d1'
RelativeAlignedOffset = 77056           # Padded to next 64-byte boundary
SectionIdentifier = 'SmoothNormals'
SubFormatTag = '.PolygonNetwork'

# === FRONTIER_BINARY_BOUNDARY ===
# <0..63 zero-padding bytes to 64B boundary, followed by 64B-aligned binary sections>
```

### Embedded `DomainSubFormat` & Binary Sections Inside `.InSpace`

| Folder | `DomainSubFormat` in `['.DocumentHeader']` | Tier 1 TOML Tables | Tier 2 `64B`-Aligned Binary Sections (`SectionIdentifier` : `SubFormatTag`) |
| :--- | :--- | :--- | :--- |
| `Content/Geometry/**` | `'.GeometrySpace'` | `[GeometryMetrics]`, `[['SurfaceSlots']]` (`PartGroupOrdinal`, `MaterialPath`) | `Vertices`, `SmoothNormals`, `FaceIndices` (`.PolygonNetwork`), `HalfEdges` (`.Baseline`), `ClusterLod` (`.ClusterHierarchy`) |
| `Content/UVLayouts/**` | `'.UVSpace'` | `[UVMetrics]` (`TargetGeometryPath`, `ChartCount`, `UDIMUtilization`) | `CornerUVs` (`.UVTile`), `EdgeSeamPinMask` (`.UVTile`), `FaceUDIMOrdinals` (`.UDIM`) |
| `Content/Materials/**` | `'.MaterialSpace'` | `[Diffuse]`, `[Specular]`, `[Clearcoat]`, `[Transmission]`, `[Subsurface]`, `[Sheen]`, `[ThinFilm]`, `[PorosityLight]`, `[TextureLinks]` | Pure TOML (`0` binary sections) for parametric OpenPBR; optional `SpectralCurve` (`.Spectrum`) |
| `Content/Photometry/**` | `'.Photometry'` | `[LuminaireHeader]`, plus `[SolarEphemeris]`, `[PolygonalLtcQuad]`, or `[PhotometricIes]` | `0` binary sections for Sun & LTC Quads; `CandelaGrid128x32` (`16,384 B` `float32`, `.Spectrum`) for `.ies` |
| `Content/Environment/**` | `'.VolumetricSpace'` | `[AtmosphereMedium]`, `[Starfield]`, `[['LunarBodies']]`, `[CloudDeck]`, `[HeightFog]`, `[WindField]`, `[['LocalVolumes']]` | `0` binary sections for sky/cloud presets; `CondensateTexels` (`128³ + 64³ + 64²` `uint32_t`, `.TexelLayer`) for noise volume |
| `Content/Rendering/**` | `'.Render'` | `[EngineDefaults]`, `[MatCapShadow]`, `[SolarCascades]`, `[GlobalDistanceField]`, `[RadiometricToneStack]`, `[LensFlareOptics]` | Pure TOML (`0` binary sections) |
| `Content/Components/**` | `'.ComponentSpace'` | `[ComponentHeader]`, `[['PartInstances']]` (geometry/UV/material `.InSpace` paths + local transforms), `[['Constraints']]` | Optional `ColliderConvexHulls` (`.ComponentConstraints`) |
| `Content/Worlds/**` | `'.WorldSpace'` | `[WorldHeader]` (`EnvironmentPath`, `SunPath`, `RenderPresetPath`), `[EditorCamera]`, `[['CameraPresets']]`, `[['OutlinerFolders']]`, `[['PlacedInstances']]`, `[['PlacedLuminaires']]` | Optional cached `WorldBvh4` (`.WideBoundingTree`) for instant zero-rebuild startup |

---

## 3. Hardcoded C++ Sources Eliminated by `Content/*.InSpace`

| Current Hardcoded C++ / Raw File | Replaced By `.InSpace` File(s) in `Content/` |
| :--- | :--- |
| `ShowcaseGalleryScene.cpp` (`BuildVehicleClearSkyDefaultScene`) | `Content/Worlds/VehicleClearSkyDefault.InSpace` + `GroundPlane24m.InSpace` + `ControlVehicleRig.InSpace` |
| `ShowcaseGalleryScene.cpp` (`BuildShowcaseGalleryScene`) | `Content/Worlds/CornellGalleryStudio.InSpace` |
| `EngineContent/EnvironmentArchives/ControlVehicle/*.face` + hardcoded wheel transforms | `Content/Geometry/Vehicles/ControlVehicle{Body,Rim,Tyre}.InSpace` + `Content/UVLayouts/Vehicles/*UV.InSpace` + `Content/Components/ControlVehicleRig.InSpace` |
| `EngineContent/Photometry/*.ies` + inline C++ `.ies` strings | `Content/Photometry/IesProfiles/{LowBeamHeadlamp,TailLampRed,CarParkDownlight}.InSpace` |
| Hardcoded sun/sky/cloud structs in `EditorLifecycle.cpp` & `ShowcaseGalleryScene.cpp` | `Content/Photometry/Solar/ClearMorningSun1030.InSpace` + `Content/Environment/Skies/ClearSkyDaylight.InSpace` + `Content/Environment/Clouds/StaticHighResCumulus.InSpace` |
| CPU startup synthesis of `CloudNoiseVolume.h` (`CondensateNoise`) | `Content/Environment/Clouds/CondensateNoiseVolume.InSpace` (`mmap` direct upload) |
| Raw `BlueNoiseRanks.bin` & `MatCapStudioAtlas.rgba/.json` | `Content/Optics/BlueNoiseSamplingRanks.InSpace` & `Content/Optics/MatCapStudioSheet.InSpace` |

---

## 4. C++ Codecs & Automatic Default Generator (`FrontierDefaultContentGenerator`)

| C++ Module | Path | Role |
| :--- | :--- | :--- |
| `SpaceContainerCodec` update | `Engine/Codecs/SpaceContainerCodec.{h,cpp}` | Register `SpaceSuffixCategory::InSpace` (`".InSpace"` / `".inspace"`) + `DomainSubFormat` header field + `.WorldSpace` sub-format tag while preserving all existing `.Space` readers. |
| `PhotometrySpaceCodec` | `Engine/Codecs/PhotometrySpaceCodec.{h,cpp}` | Encodes/decodes Solar ephemeris, LTC area quads, and `64B`-aligned `128×32` `.ies` candela grids in `.InSpace`. |
| `VolumetricSpaceCodec` | `Engine/Codecs/VolumetricSpaceCodec.{h,cpp}` | Encodes/decodes Sky, Moon/Starfield, Static Cloud Deck, Fog, Wind, and `CondensateNoiseVolume` in `.InSpace`. |
| `RenderPresetCodec` | `Engine/Codecs/RenderPresetCodec.{h,cpp}` | Encodes/decodes viewport engine, cheap `2×2` MatCap shadow, cascade spans, GDF clip bounds, and ACES exposure in `.InSpace`. |
| `WorldSceneCodec` | `Engine/Codecs/WorldSceneCodec.{h,cpp}` | Loads/saves complete `Content/Worlds/*.InSpace` scenes into `ShowcaseWorldSceneBundle` + `OutlinerPanel` by resolving linked `.InSpace` geometry, UV, material, sun, sky, and render files. |
| `DefaultContentGenerator` | `Engine/Tools/DefaultContentGenerator.{h,cpp}` + `DefaultContentGeneratorMain.cpp` | Standalone C++ CLI executable **`FrontierDefaultContentGenerator`** (and CMake target `FrontierGenerateDefaultContent`, plus automatic first-boot fallback) that deterministically generates **every default `.InSpace` file** in `Content/` and verifies `64B` alignment + `64`-bit FNV-1a checksums. |

---

## 5. Phased Implementation Sequence

1. **Phase 1 — `.InSpace` Registration & Domain Codecs**:
   - Add `SpaceSuffixCategory::InSpace` (`".InSpace"` / `".inspace"`) and `DomainSubFormat` to `SpaceContainerCodec`, complete `NetworkCodec` / `UVCodec` / `MaterialCodec` `.InSpace` support, and implement `PhotometrySpaceCodec`, `VolumetricSpaceCodec`, `RenderPresetCodec`, and `WorldSceneCodec`.
2. **Phase 2 — `FrontierDefaultContentGenerator` C++ Script/Executable**:
   - Implement `DefaultContentGenerator` and run it to populate the full `Content/` folder hierarchy above with verified `.InSpace` assets (`ContentIndex.InSpace`, `Worlds/`, `Components/`, `Geometry/`, `UVLayouts/`, `Materials/`, `Photometry/`, `Environment/`, `Rendering/`, `Optics/`).
3. **Phase 3 — `FrontierEditor` & `ContentBrowser` Data-Driven Boot**:
   - Wire `EditorLifecycle` to open `Content/Worlds/VehicleClearSkyDefault.InSpace` through `WorldSceneCodec` on startup (auto-invoking `DefaultContentGenerator` if `Content/ContentIndex.InSpace` is missing), and scan `Content/**/*.InSpace` in `ThumbnailSheetRepository` by reading each file's `DomainSubFormat` header.
4. **Phase 4 — Validation & Legacy Cleanup**:
   - Add `SpaceContentFormattingValidation` testing deterministic `.InSpace` generation, `64B` `mmap` alignment, FNV-1a integrity, and GPU render parity on `VehicleClearSkyDefault.InSpace` and `CornellGalleryStudio.InSpace`.
