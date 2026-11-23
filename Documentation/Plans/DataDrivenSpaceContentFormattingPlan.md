# Data-Driven `*Space` Content Formatting & Default Content Generator Plan

> **TL;DR**: `.InSpace` was an invalid abbreviated contraction of **`.InstanceSpace`** and has been purged. Below is the mandatory comparison table of **unabbreviated full-name container extensions** (**🏆 🥇 Option A: Explicit Domain `*Space` Extensions** vs. **🥈 Option B: Unified `.InstanceSpace`**) alongside the zero-abbreviation `Content/` folder and file hierarchy.

---

## 1. Decision Table — Proper Unabbreviated Replacement for `.InSpace`

> **Legend**: Clarity / codec alignment / OS file-type visibility: `🔴` = High (best) · `🚩` = Medium · `✔️` = Low. Rework cost: `✔️` = Lowest (best). Quality: plain `⭐` (`1–5`).

| Rank | Candidate Extension Scheme | Example File Names (Zero Abbreviations) | Immediate Domain Clarity | Existing `SpaceContainerCodec` Match | Rework Cost | Rating |
| :---: | :--- | :--- | :---: | :---: | :---: | :---: |
| 🏆 🥇 | **Option A — Explicit Full Domain `*Space` Extensions** | `VehicleClearSkyDefault.WorldSpace`<br>`ControlVehicleBody.GeometrySpace`<br>`ControlVehicleMetallicPaint.MaterialSpace`<br>`ClearMorningSun.PhotometrySpace`<br>`ClearSkyDaylight.VolumetricSpace`<br>`DefaultEditorViewport.RenderSpace` | 🔴 | 🔴 | ✔️ | ⭐⭐⭐⭐⭐ |
| 🥈 | **Option B — Single Unified `.InstanceSpace` Extension** *(full proper name for `.InSpace`)* | `VehicleClearSkyDefault.InstanceSpace`<br>`ControlVehicleBody.InstanceSpace`<br>`ControlVehicleMetallicPaint.InstanceSpace`<br>`ClearMorningSun.InstanceSpace` *(domain stored in TOML `['.DocumentHeader']`)* | 🚩 | 🚩 | 🚩 | ⭐⭐⭐⭐ |
| 🥉 | **Option C — Single Unified `.DocumentSpace` Extension** | `VehicleClearSkyDefault.DocumentSpace`<br>`ControlVehicleBody.DocumentSpace` | ✔️ | ✔️ | 🚩 | ⭐⭐⭐ |

### 💡 Recommendation
- If you want **one single universal extension** across every file in `Content/` (`DocumentFormatsInstancePlan.md` §1), its proper unabbreviated name is **`.InstanceSpace`** (**Option B** — never `.InSpace`).
- If you want **each file's domain visible directly in its filename** matching `EngineContent/Geometry/ControlledIntegrator.GeometrySpace`, `FontRepository.FontSpace`, and `ProjectGenerator.cpp`'s `MainWorld.WorldSpace`, use **Option A — Explicit Full Domain `*Space` Extensions** (upgrading bare `.Photometry` → `.PhotometrySpace` and `.Render` → `.RenderSpace`, and `.UVSpace` → `.SurfaceParameterSpace` if you also want `UV` spelled out in full).

---

## 2. Canonical `Content/` Folder & File Structure (Zero Abbreviations)

Shown below with **Option A (Explicit Full Domain `*Space` Extensions)**; if you select **Option B**, every extension below simply becomes **`.InstanceSpace`** while keeping the exact same folder tree and `DomainSubFormat` inside `['.DocumentHeader']`:

```text
Frontier/
└── Content/
    ├── ContentRepositoryIndex.ProjectSpace                     # Root manifest: asset hash directory, startup world & render links
    │
    ├── Worlds/                                                 # Complete openable world scenes (.WorldSpace)
    │   ├── VehicleClearSkyDefault.WorldSpace                   # Default Startup Scene: GroundPlane + ControlVehicle + Sun + ClearSky
    │   ├── CornellGalleryStudio.WorldSpace                     # Architectural Cornell Gallery (validation parity)
    │   └── NightCarParkPhotometric.WorldSpace                  # Night car-park luminaire showroom scene
    │
    ├── Components/                                             # Multi-part rigs & physics constraints (.ComponentSpace)
    │   └── ControlVehicleRig.ComponentSpace                    # Body + 4 Rims + 4 Tyres transforms, 11 slot links, wheel physics
    │
    ├── Geometry/                                               # 3D PolygonNetwork + creased normals + ClusterPyramid (.GeometrySpace)
    │   ├── Primitives/
    │   │   ├── GroundPlane.GeometrySpace                       # 24.0 x 24.0 x 0.16 m Z=0 ground slab
    │   │   ├── UnitBox.GeometrySpace                           # Canonical 1.0 m box primitive
    │   │   ├── CalibrationSphere.GeometrySpace                 # Smooth-normal sphere primitive
    │   │   ├── StandardCylinder.GeometrySpace                  # Analytic cylinder primitive
    │   │   ├── TrafficCone.GeometrySpace                       # Analytic cone primitive
    │   │   └── CalibrationTorus.GeometrySpace                  # Analytic torus primitive
    │   └── Vehicles/
    │       ├── ControlVehicleBody.GeometrySpace                # Transcoded body PolygonNetwork (creased normals + 11 part slots)
    │       ├── ControlVehicleRim.GeometrySpace                 # Transcoded wheel rim PolygonNetwork (instanced x4)
    │       └── ControlVehicleTyre.GeometrySpace                # Transcoded tyre PolygonNetwork (instanced x4)
    │
    ├── SurfaceParameters/                                      # 2D parameterization tiles, seams & pins (.SurfaceParameterSpace)
    │   ├── Primitives/
    │   │   ├── GroundPlaneParameterization.SurfaceParameterSpace
    │   │   ├── UnitBoxParameterization.SurfaceParameterSpace
    │   │   └── CalibrationSphereParameterization.SurfaceParameterSpace
    │   └── Vehicles/
    │       ├── ControlVehicleBodyParameterization.SurfaceParameterSpace
    │       ├── ControlVehicleRimParameterization.SurfaceParameterSpace
    │       └── ControlVehicleTyreParameterization.SurfaceParameterSpace
    │
    ├── Materials/                                              # Physical microfacet surface definitions (.MaterialSpace)
    │   ├── Architectural/
    │   │   ├── GroundPlaneAsphalt.MaterialSpace
    │   │   ├── GalleryNeutralPlaster.MaterialSpace
    │   │   ├── CornellCrimsonWall.MaterialSpace
    │   │   ├── CornellEmeraldWall.MaterialSpace
    │   │   ├── BrushedGoldConductor.MaterialSpace
    │   │   └── AbbeCrownGlassOrb.MaterialSpace
    │   └── Automotive/
    │       ├── ControlVehicleMetallicPaint.MaterialSpace
    │       ├── VehicleObsidianCanopy.MaterialSpace
    │       ├── VehicleTintedWindshield.MaterialSpace
    │       ├── VehicleIntakePolymer.MaterialSpace
    │       ├── VulcanizedTyreRubber.MaterialSpace
    │       ├── ForgedTitaniumRimAlloy.MaterialSpace
    │       ├── AnodizedBronzeWheelCap.MaterialSpace
    │       ├── MachinedSteelBrakeRotor.MaterialSpace
    │       ├── SatinGraphiteTrim.MaterialSpace
    │       ├── SmokedHeadlampHousing.MaterialSpace             # Daytime zero-luminance smoked lens (no nose blowout)
    │       ├── XenonHeadlampOptic.MaterialSpace                # Nighttime self-luminous optic variant
    │       └── RubyTaillampOptic.MaterialSpace
    │
    ├── Photometry/                                             # Solar ephemeris, area quads & goniophotometric profiles (.PhotometrySpace)
    │   ├── Solar/
    │   │   ├── ClearMorningSun.PhotometrySpace                 # Solar ephemeris + RGB irradiance + angular radius
    │   │   └── NocturnalZeroSun.PhotometrySpace                # Nighttime zero-irradiance solar configuration
    │   ├── AreaLuminaires/
    │   │   ├── CeilingSoftboxLuminaire.PhotometrySpace         # Clipped-horizon rectangular area luminaire
    │   │   └── ShowroomOverheadLuminaire.PhotometrySpace
    │   └── MeasuredLuminaires/
    │       ├── LowBeamHeadlamp.PhotometrySpace                 # TOML metadata + 64B-aligned 128x32 float32 candela binary
    │       ├── TailLampRed.PhotometrySpace
    │       └── CarParkDownlight.PhotometrySpace
    │
    ├── Environment/                                            # Sky scattering, celestials, static clouds, fog & wind (.VolumetricSpace)
    │   ├── Skies/
    │   │   ├── ClearSkyDaylight.VolumetricSpace                # Rayleigh/Mie/Ozone sky, CloudsEnabled = false, Wind = 0 m/s
    │   │   └── NocturnalStarrySky.VolumetricSpace              # Starfield + 6-slot lunar presets + nocturnal airglow
    │   ├── Clouds/
    │   │   ├── StaticFullResolutionCumulus.VolumetricSpace     # CloudRenderScale = 1.0, CloudMarchSteps = 64, Wind = 0 m/s
    │   │   └── CondensateNoiseVolume.VolumetricSpace           # Binary 128^3 Shape + 64^3 Erosion + 64^2 rank texels
    │   └── Fog/
    │       └── ClearHorizonHeightFog.VolumetricSpace           # Height fog & bounded local fog volume configuration
    │
    ├── Rendering/                                              # Viewport engine, shadow cascades & tone-stack (.RenderSpace)
    │   ├── DefaultEditorViewport.RenderSpace                   # Cheap 2x2 MatCap shadow, 2.85m/8.5m cascades, 4.2m Fine GDF
    │   └── NightPhotometricViewport.RenderSpace
    │
    └── Optics/                                                 # Baked optical sheets & sampling ranks (.ImageSpace / .TextureSpace)
        ├── BlueNoiseSamplingRanks.TextureSpace                 # 64B-aligned sampling rank streams
        ├── StudioShadingSphereSheet.ImageSpace                 # Studio shading sphere sheet
        ├── LensFlareBakedSheet.ImageSpace                      # Optical ghost & starburst sheet
        └── LunarSurfaceSheet.ImageSpace                        # Procedural 6-moon albedo/normal sheet
```

---

## 3. Two-Tier Hybrid Wire Format (`TOML` Manifest + `64`-Byte Binary Table)

Every file uses `SpaceContainerCodec` (`['.DocumentHeader']` + `[['.SectionDirectory']]` + `# === FRONTIER_BINARY_BOUNDARY ===` + `64`-byte-aligned binary blocks verified by `64`-bit FNV-1a checksums):

```toml
# Frontier Two-Tier Hybrid Container (Canonical PascalCase Manifest)
['.DocumentHeader']
AuthoringWorkspace = 'Frontier.Editor'
BinaryAlignmentBytes = 64
ContainerExtension = '.GeometrySpace'   # Or '.InstanceSpace' if Option B is chosen
DocumentTitle = 'Control Vehicle Body'
DomainSubFormat = '.GeometrySpace'
SchemaVersion = 1

[GeometryMetrics]
CreaseAngleDegrees = 38.0
FaceCount = 12132
PartSlotCount = 11
SurfaceParameterPath = 'Content/SurfaceParameters/Vehicles/ControlVehicleBodyParameterization.SurfaceParameterSpace'
VertexCount = 6418

[['SurfaceSlots']]
MaterialPath = 'Content/Materials/Automotive/ControlVehicleMetallicPaint.MaterialSpace'
PartGroupOrdinal = 0
SlotTitle = 'BodyCoat'

[['.SectionDirectory']]
ByteLength = 77016
ChecksumFnv1a64 = '0x8f3a1904c271d05b'
RelativeAlignedOffset = 0
SectionIdentifier = 'Vertices'
SubFormatTag = '.PolygonNetwork'

[['.SectionDirectory']]
ByteLength = 436752
ChecksumFnv1a64 = '0x51a0c8127e4903d1'
RelativeAlignedOffset = 77056
SectionIdentifier = 'SmoothNormals'
SubFormatTag = '.PolygonNetwork'

# === FRONTIER_BINARY_BOUNDARY ===
# <0..63 zero-padding bytes to 64-byte boundary, followed by 64-byte-aligned binary sections>
```

---

## 4. C++ Codecs & Automatic Default Generator (`FrontierDefaultContentGenerator`)

| C++ Unit | File Path | Responsibility |
| :--- | :--- | :--- |
| `SpaceContainerCodec` | `Engine/Codecs/SpaceContainerCodec.{h,cpp}` | Register `.WorldSpace`, `.PhotometrySpace`, `.RenderSpace`, `.SurfaceParameterSpace` (or `.InstanceSpace` under Option B) with case-insensitive decode and canonical `PascalCase` encode. |
| `PhotometrySpaceCodec` | `Engine/Codecs/PhotometrySpaceCodec.{h,cpp}` | Encodes/decodes Solar ephemeris, rectangular area luminaires, and `64B`-aligned `128×32` candela grids. |
| `VolumetricSpaceCodec` | `Engine/Codecs/VolumetricSpaceCodec.{h,cpp}` | Encodes/decodes Sky, Moon/Starfield, Static Cloud Deck, Fog, Wind, and `CondensateNoiseVolume`. |
| `RenderSpaceCodec` | `Engine/Codecs/RenderSpaceCodec.{h,cpp}` | Encodes/decodes viewport engine, cheap `2×2` MatCap shadow, cascade spans, distance-field bounds, and tone-stack. |
| `WorldSpaceCodec` | `Engine/Codecs/WorldSpaceCodec.{h,cpp}` | Loads/saves complete `Content/Worlds/*` scenes into `ShowcaseWorldSceneBundle` + `OutlinerPanel`. |
| `DefaultContentGenerator` | `Engine/Tools/DefaultContentGenerator.{h,cpp}` + `DefaultContentGeneratorMain.cpp` | Standalone C++ executable **`FrontierDefaultContentGenerator`** (`FrontierGenerateDefaultContent` CMake target + automatic cold-boot fallback) that generates the entire `Content/` tree deterministically and verifies every `64B` offset and FNV-1a checksum. |
