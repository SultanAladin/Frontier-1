# Celestial, Atmospheric Sky & Volumetric Clouds System Plan

**Status:** Approved Design Specification (Turn 1 — HTML Inspector Prototype + Folder Structure & Architecture Plan; **C++20 + `.slang` Engine Implementation in Turn 2 without C++ Inspector UI**; **Native C++20 Inspector Panels in Turn 3**)  
**HTML Inspector Prototype:** `Experimental/CelestialInspectorPrototype/index.html`  
**Reference Prototypes:** `Experimental/FrontierWorldMirror/{AtmosphereSky.js,Solar.mjs,MoonAtlasLoader.js,CloudSpecification.js,CloudDeckPanel.jsx}`  
**Naming Compliance:** 100% `Zero-And` and `BannedWordsVocabulary.md` compliant (avoids `Coverage`, `Base`, `Flow`, `Horizon`, `Zenith`, `Map`, `Data`, `Info`, `Frame`, `Pass`, `Pipeline`, `Stage`, `Cache`, `Emitter`, `Probe`, `Bake`).

---

## 1. Execution Roadmap (Three Clean Phases)

| Phase | Scope | Status | Deliverable |
| :--- | :--- | :--- | :--- |
| **Turn 1 (Current)** | **HTML Inspector Design + Folder & Engine Plan** | 🟢 Complete | `Experimental/CelestialInspectorPrototype/index.html` + `Documentation/Plans/CelestialSkyCloudsSystemPlan.md` |
| **Turn 2 (Next)** | **C++20 + `.slang` Celestial, Atmosphere & Volumetric Cloud Engine (NO C++ Inspector UI yet)** | 🚩 Next | `Engine/GeometricRaster/Atmosphere/*`, `Engine/Shaders/Atmosphere/*`, `WorldViewportRadiometricCompute.slang` sky/sun/moon/stars/clouds integration, `ViewportPanel` quick toggles/presets, `Exhibits/CelestialCloudsValidation.cpp` + `.png` proofs |
| **Turn 3 (After HTML Sign-off)** | **Native C++20 Inspector Panels (1:1 with HTML Design)** | 🏳️ Planned | `Engine/Editor/Inspectors/{Sun,Moon,Stars,Clouds}InspectorView.{h,cpp}` wired into `InspectorPanel` |

---

## 2. Exact Folder Structure To Be Added

All new directories and files fit directly inside Frontier's canonical `Documentation/FolderStructure.md` tree:

```text
Frontier/
├── Engine/
│   ├── GeometricRaster/
│   │   └── Atmosphere/                                  // NEW DOMAIN MODULE (Turn 2)
│   │       ├── CelestialEphemerisSolver.h               // Astronomical solar/lunar azimuth, elevation, declination, sidereal solver
│   │       ├── CelestialEphemerisSolver.cpp             // Port of Solar.mjs + blackbody Kelvin -> LinearRGB
│   │       ├── AtmosphericSkySequence.h                 // Bruneton/Hillaire Rayleigh + Mie + Ozone transmittance & SkyView LUT
│   │       ├── AtmosphericSkySequence.cpp               // CPU + GPU SkyView LUT builder & analytical solar transmittance
│   │       ├── VolumetricCloudSequence.h                // Global cloud deck + bounded LocalCloudVolume specifications & presets
│   │       ├── VolumetricCloudSequence.cpp              // CPU 3D FBM + Beer-Powder single/multi-scatter + shadow field parity
│   │       ├── LunarStarfieldSequence.h                 // 4-slot Moon instances (6 surface presets) + deterministic starfield specs
│   │       └── LunarStarfieldSequence.cpp               // Procedural/atlas 6-moon lunar phase evaluation & star scintillation
│   │
│   ├── Shaders/
│   │   └── Atmosphere/                                  // NEW SHADER DIRECTORY (Turn 2, .slang -> .comp.spv)
│   │       ├── SkyViewTransmittanceCompute.slang        // 96x48 / 128x32 Bruneton-Hillaire Rayleigh + Mie + Ozone SkyView LUT
│   │       ├── VolumetricCloudCompute.slang             // Raymarched 3D FBM cloud layer + bounded LocalCloud + Henyey-Greenstein
│   │       └── CelestialDiscStarCompute.slang           // Sun disc + corona, 4x Moon phase/crater/halo discs & twinkling starfield
│   │
│   └── Editor/
│       └── Inspectors/                                  // NEW EDITOR INSPECTOR DIRECTORY (Turn 3 ONLY)
│           ├── SunInspectorView.h / .cpp                // Sun orbit dial, solar disc sliders, ephemeris & illuminance curve
│           ├── MoonInspectorView.h / .cpp               // 4-slot moon selector, 6-preset catalogue, phase disc, spherical dial
│           ├── StarsInspectorView.h / .cpp              // Equatorial chart, twinkle signal monitor, celestial pole dial
│           └── CloudsInspectorView.h / .cpp             // Vertical cloud deck drag section, optical parameters, wind & shadows
│
├── EngineContent/
│   └── Skies/                                           // NEW STOCK CELESTIAL ASSETS (Turn 2)
│       └── LunarSurfaceAtlas.ImageSpace                 // 6-tile equirectangular lunar albedo atlas (Luna, Ember, Glacier, Sulfur, Shroud, Shard)
│
├── Exhibits/
│   ├── CelestialCloudsValidation.cpp                    // NEW VALIDATION BINARY (Turn 2)
│   └── Images/
│       └── Atmosphere/                                  // NEW PNG PROOF OUTPUT DIRECTORY (Turn 2)
│           ├── 01-DaylightSkyAndCumulusClouds.png
│           ├── 02-GoldenSunsetRayleighOzone.png
│           ├── 03-StarlitNightMoonPhases.png
│           └── 04-CpuVsVulkanSlangSkyParity.png
│
└── Experimental/
    └── CelestialInspectorPrototype/                     // CREATED IN TURN 1
        └── index.html                                 // Standalone interactive HTML Inspector Design (Sun, Moon, Stars, Clouds, Local Cloud)
```

> **Integration Touchpoints in Existing Files (Turn 2):**
> - `CMakeLists.txt`: Register `Engine/GeometricRaster/Atmosphere/*.cpp`, compile `Engine/Shaders/Atmosphere/*.slang` via `frontier_compile_slang_shader`, and register `CelestialCloudsValidation`.
> - `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.{h,cpp}`: Embed `WorldAtmosphereConfiguration` (Sun, Moons[4], Stars, GlobalClouds, LocalCloud, AtmosphericMedium) into `WorldSceneSpecification` & `WorldRenderConfiguration`; replace flat `SkyRadiance` misses with physical SkyView + Sun + Moon + Stars + Volumetric Clouds (both primary camera rays and secondary reflection/GI sky sampling!).
> - `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.{h,cpp}` & `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang`: Add uniform block parameters & GPU Sky/Sun/Moon/Star/Cloud evaluation for sky miss rays, aerial haze, and sky IBL reflections.
> - `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}`: Add outdoor/sky dome support (plus toggleable Cornell studio roof opening or outdoor sky mode) and register `Clouds`, `Moon`, `Stars` alongside `Sun` in `OutlinerPanel`.
> - `Engine/Components/Panes/ViewportPanel.{h,cpp}` & `Engine/Lifecycle/EditorLifecycle.{h,cpp}`: Add clean quick toggles & time-of-day sliders in `RenderSettingsSheet` (`Show Sun Disc`, `Show Moon`, `Show Stars`, `Volumetric Clouds`, `Local Hours` slider) — **without** building C++ Inspector views until Turn 3.

---

## 3. Data Model, Sliders & Toggles Inventory (Zero C++ Inspector Yet)

### 3.1 Sun (`SolarCelestialConfiguration`)
| Parameter / Toggle | Control Type | Range / Options | Default | C++ Field |
| :--- | :--- | :--- | :--- | :--- |
| **Enabled** | Toggle | `true` / `false` | `true` | `SolarEnabled` |
| **Angular Diameter** | Slider | `0.10 – 5.00 deg` | `0.533 deg` | `AngularDiameterDegrees` |
| **Colour Source** | Selector | `RgbTint` / `KelvinTemperature` | `RgbTint` | `TintSource` |
| **Sun Tint** | LinearRGB | `#FFFFFF` | `{1.0, 1.0, 1.0}` | `ManualRgbTint` |
| **Temperature** | Slider | `1000 – 40000 K` | `6500 K` | `ColorTemperatureKelvin` |
| **Intensity** | Slider | `0.0 – 60.0 x` | `22.0 x` | `SolarIntensityMultiplier` |
| **Direct Gain** | Slider | `0.0 – 5.0 x` | `2.50 x` | `DirectGainMultiplier` |
| **Local Hours** | Slider | `0.00 – 24.00 h` | `15.50 h` | `LocalHours` |
| **Day Cycle Animate** | Toggle | `true` / `false` | `false` | `CycleAnimationEnabled` |
| **Cycle Speed** | Selector | `x1`, `x8`, `x30`, `x100`, `Custom` | `x8` | `CycleSpeedPreset` |
| **Day Duration** | Slider | `0.01 – 168.0 h` | `3.00 h` | `DayDurationHours` |
| **Latitude** | Slider | `-90.0 – +90.0 deg` | `-26.19 deg` | `ObserverLatitudeDegrees` |
| **Longitude** | Slider | `-180.0 – +180.0 deg` | `28.32 deg` | `ObserverLongitudeDegrees` |
| **Day / Month / UTC** | Sliders | `1..31`, `1..12`, `-12..+14 h` | `10 / 9 / +2` | `DayOfMonth`, `Month`, `UtcOffsetHours` |
| **Manual Direction Override** | Toggle + Az/El | `0..360 deg`, `-90..+90 deg` | `false` | `ManualDirectionOverride` |

### 3.2 Moon (`LunarBodyConfiguration[4]`)
| Parameter / Toggle | Control Type | Range / Options | Default | C++ Field |
| :--- | :--- | :--- | :--- | :--- |
| **Active Slot** | Selector | `Slot 0..3` | `0` | `ActiveMoonSlot` |
| **Visible** | Toggle | `true` / `false` | `true` (Slot 0) | `Visible` |
| **Follow Sky** | Toggle | `true` / `false` (anti-solar lock) | `false` | `FollowSkyOppositeSun` |
| **Surface Preset** | Selector (6) | `Luna`, `Ember`, `Glacier`, `Sulfur`, `Shroud`, `Shard` | `Luna (0)` | `SurfacePreset` |
| **Phase** | Slider | `0.00 – 1.00` (`0–29.53 d`) | `0.62` | `SynodicPhaseFraction` |
| **Brightness** | Slider | `0.00 – 6.00 x` | `1.60 x` | `BrightnessMultiplier` |
| **Halo Glow** | Slider | `0.00 – 3.00 x` | `0.80 x` | `HaloGlowMultiplier` |
| **Angular Size** | Slider | `0.10 – 180.00 deg` | `0.52 deg` | `AngularDiameterDegrees` |
| **Azimuth** | Slider | `0.0 – 360.0 deg` | `300.0 deg` | `AzimuthDegrees` |
| **Elevation** | Slider | `-90.0 – +90.0 deg` | `28.0 deg` | `ElevationDegrees` |
| **Roll / Pitch** | Sliders | `0..360 deg`, `-180..+180 deg` | `0.0 / 0.0` | `SurfaceRollDegrees`, `SurfacePitchDegrees` |

### 3.3 Stars (`StarfieldConfiguration`)
| Parameter / Toggle | Control Type | Range / Options | Default | C++ Field |
| :--- | :--- | :--- | :--- | :--- |
| **Star Field** | Toggle | `true` / `false` | `true` | `StarfieldEnabled` |
| **Twinkle** | Toggle | `true` / `false` | `true` | `ScintillationEnabled` |
| **Limiting Magnitude** | Slider | `0.0 – 8.0 mag` | `5.5 mag` | `LimitingMagnitude` |
| **Brightness** | Slider | `0.00 – 4.00 x` | `1.00 x` | `BrightnessMultiplier` |
| **Point Size** | Slider | `0.40 – 3.00 x` | `1.00 x` | `PointSizeScale` |
| **Twinkle Depth** | Slider | `0 – 100 %` | `30 %` | `ScintillationDepthPercent` |
| **Twinkle Rate** | Slider | `0.20 – 3.00 Hz` | `1.20 Hz` | `ScintillationRateHertz` |
| **Celestial Rotation** | Slider | `0.0 – 360.0 deg` | `0.0 deg` | `CelestialRotationDegrees` |

### 3.4 Volumetric Clouds & Local Cloud (`CloudDeckConfiguration` & `BoundedCloudVolume`)
| Parameter / Toggle | Control Type | Range / Options | Default | C++ Field |
| :--- | :--- | :--- | :--- | :--- |
| **Clouds Enabled** | Toggle | `true` / `false` | `true` | `CloudsEnabled` |
| **Follow Wind** | Toggle | `true` / `false` | `true` | `FollowWindEnabled` |
| **Cloud Type** | Selector (6) | `Stratus`, `Stratocumulus`, `Cumulus`, `Cumulonimbus`, `Altostratus`, `Cirrus` | `Cumulus (2)` | `CloudClassification` |
| **Cloud Fraction** | Slider | `0.00 – 1.00` | `0.34` | `CloudFraction` |
| **Density** | Slider | `0.00 – 4.00 x` | `2.40 x` | `ExtinctionMultiplier` |
| **Feature Scale** | Slider | `0.20 – 3.00 x` | `0.35 x` | `FeatureScaleMultiplier` |
| **Layer Floor** | Slider | `100 – 12000 m` | `1400 m` | `CloudFloorMeters` |
| **Layer Thickness** | Slider | `100 – 6000 m` | `1100 m` | `LayerThicknessMeters` |
| **Ceiling** | Slider | `4000 – 20000 m` | `14000 m` | `DeckCeilingMeters` |
| **Anvil Spread** | Slider | `0.00 – 1.00` | `0.50` | `AnvilTopSpread` |
| **Mie Anisotropy (g)** | Slider | `-0.90 – +0.90` | `0.45` | `ForwardScatterAnisotropy` |
| **Wind Speed / Bearing** | Sliders | `0..40 m/s`, `0..360 deg` | `7.0 m/s`, `250 deg` | `WindSpeedMetersPerSecond`, `WindBearingDegrees` |
| **Cloud Shadows** | Toggle + Sliders | `ShadowFraction`, `ShadowDensity`, `ShadowScale`, `ShadowFloor`, `ShadowThickness` | `true` | `CloudShadowConfiguration` |
| **Local Cloud Volume** | Toggle + Vectors | `LocalEnabled`, `VolumeCenterMeters[3]`, `HalfExtentMeters[3]`, `Density`, `CloudFraction`, `ScaleMeters`, `Anisotropy` | `false` | `BoundedCloudVolume` |

---

## 4. Physics & Rendering Architecture (Turn 2 `.slang` + CPU Parity)

1. **Right-Handed World Coordinate Alignment (`+X` Forward, `+Y` Left, `+Z` Up):**
   - `CelestialEphemerisSolver` converts `(Azimuth, Elevation)` to Frontier's `+Z` Up world unit vector so Sun, Moon, and Stars match `WideBoundingTree` and `PerspectiveCameraFrame` exactly.
2. **Physical Bruneton/Hillaire Sky-View & Transmittance LUT (`SkyViewTransmittanceCompute.slang`):**
   - Precomputes `128x32` optical-depth transmittance LUT and `96x48` non-linear sky-view LUT (Rayleigh + Henyey-Greenstein Mie + Ozone absorption + Hillaire isotropic multi-scattering + twilight airglow + ground bounce) whenever Sun/Atmosphere parameters change.
   - Feeds **both** sky background rays and environment IBL for metallic/glass reflections and DDGI/PathTracer sky bounces!
3. **Volumetric 3D FBM Cloud Raymarch (`VolumetricCloudCompute.slang`):**
   - Raymarches curved planetary shell `[EarthRadius + CloudFloorMeters, EarthRadius + CloudFloorMeters + LayerThicknessMeters]` (and bounded `BoundedCloudVolume` AABB when enabled) using 4-octave 3D value-noise FBM, dual-lobe Henyey-Greenstein phase (`0.78 * HG(g) + 0.22 * HG(-0.25)`), Beer-Powder self-shadowing toward `SunDirection`, and distant aerial perspective blend.
   - Optional sun transmittance attenuation (`CloudShadowConfiguration`) modulates `SolarIrradiance` cleanly without touching SDF shadows.
4. **Lunar Discs & Deterministic Starfield (`CelestialDiscStarCompute.slang`):**
   - Renders up to 4 moon discs with 3D spherical terminator phase shading, mare/crater procedural + atlas detail, roll/pitch tangent rotation, and atmospheric halo glow, plus a deterministic 3D-grid nocturnal starfield modulated by `LimitingMagnitude`, daylight extinction, and `ScintillationRateHertz`.
5. **Non-Degrading Quality Gate:**
   - All 5 unified world render engines (`MatCap`, `OpenPbrDirect`, `DdgiRaytracing`, `SdfGlobalIllumination`, `DeterministicPathTrace`) integrate the sky/cloud/celestial radiance seamlessly while preserving **<= 1–5% pixel delta** on indoor Cornell scenes when sky is occluded and delivering physically accurate outdoor lighting when exposed.
