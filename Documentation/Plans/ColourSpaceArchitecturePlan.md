# Frontier ColourSpace Modular Architecture Plan

> **Scope:** Establish **`Engine/ColourSpace/`** (`Frontier::ColourSpace`) as a first-class, workspace-agnostic Frontier domain for colorimetry, radiometric swatches, modular colour sources (solid pigments, linear fills, multi-stop gradients, quantized ramps, blackbody thermal ramps, discrete tint sets, spatial noise fills), GPU LUT packing, **`ColourCodec`** (`.ColourSpace`, `.ColourRamp`, `.ColourSwatch`), **`ColourInterchange`** (HTML Studio JSON/TOML, CSS, SVG, GIMP `.gpl`/`.ggr`, Adobe `.ase`/`.aco`, `.cube` 1D LUT), shared `.slang` shader sampling, reusable **`Engine/Components/`** UI editors, and **Content Browser** drag-drop swatch & ramp presets — so **`ParticleSpace`**, **`MaterialSpace`**, **`Texturing`**, **`ImageSpace`**, **`VectorSpace`**, **`VolumetricSpace`**, and **`ForceFields`** consume one unified Colour domain with zero duplication.

---

## 1. Architectural Rationale & Domain Boundaries

Colour is a cross-cutting radiometric & artistic primitive shared by every visual workspace in Frontier. Placing colour ramp or gradient ownership inside `ParticleSpace` would duplicate colorimetry, stop interpolation, GPU LUT packing, file serialization, external format parsing, and Content Browser drag-drop across workspaces.

| Layer | Responsibility | Consumers |
| :--- | :--- | :--- |
| **`Engine/ColourSpace/`** | Pure radiometric colorimetry, modular `ColourSourceSpec` types, ramp sampling, GPU LUT packing, built-in ramp/swatch repository, `ColourDocument`, `ColourExtension` | `ParticleSpace`, `MaterialSpace`, `Texturing`, `ImageSpace`, `VectorSpace`, `VolumetricSpace`, `SimulationSpace/ForceFields`, `ContentBrowser` |
| **`Engine/Codecs/ColourCodec.{h,cpp}`** | Native Two-Tier Hybrid `.ColourSpace` container (`SpaceExtensionCategory::ColourSpace = 1`), singular `.ColourRamp` / `.ColourSwatch` files, and inline manifest helpers (`SerializeSourceToManifest`) | `ParticleCodec`, `MaterialCodec`, `TextureCodec`, `VectorCodec`, `ImageCodec`, `VolumetricCodec`, `ColourCodex` |
| **`Engine/Interchange/ColourInterchange.{h,cpp}`** | External format import/export (HTML Studio JSON/TOML colour fields, CSS gradients, SVG `<linearGradient>`, GIMP `.gpl`/`.ggr`, Adobe `.ase`/`.aco`, `.cube` 1D LUT) | `ParticleInterchange`, `MaterialInterchange`, `VectorInterchange`, `ContentBrowser` external drops |
| **`Engine/Shaders/Colour/ColourRampSampling.slang`** | Reusable Slang GPU functions for 64-entry / 256-entry LUT sampling, wrap modes, interpolation curves, Oklab/Linear conversions, Planckian Kelvin blackbody, and 3D lattice noise | `Engine/Shaders/Particles/*.slang`, `ForceFields`, `MaterialSpace`, `VolumetricSpace` |
| **`Engine/Components/Controls/` & `ContentBrowser/`** | Reusable `ColourRampEditor`, `ColourSwatchPicker`, Content Browser swatch & ramp thumbnails (`ContentCategory::Colours`), and `RegisterColourRampSlot` drag-drop targets | Every Frontier workspace inspector |

### Strict Boundary With `ParticleSpace`

- **`ParticleSpace` owns ZERO colour math, ramp presets, gradient codecs, or external gradient parsers.**
- `ParticleSpace` keeps only **`Engine/ParticleSpace/ParticleTintDriver.{h,cpp}`**:
  - Maps the **15 particle-specific scalar channels** (`ModeDefault`, `NormalizedLifetime`, `ParticleAgeSeconds`, `VelocitySpeed`, `VerticalVelocity`, `OriginAzimuthAngle`, `VelocityHeadingAngle`, `DistanceFromOrigin`, `WorldElevation`, `SpatialNoise3D`, `PerParticleRandomSeed`, `SpinOrientationAngle`, `DistanceToCamera`, `ParticleSizeMeters`, `AssignedLifespan`) to a scalar coordinate.
  - Embeds `ColourSpace::ColourSourceProfile` directly inside `ParticleRig` and calls `ColourSpace::SampleColourSource` / `ColourSpace::PackColourRampLut64`.
  - `ParticleCodec` delegates `[ParticleRig.Colour]` to `Codecs::ColourCodec` inline TOML helpers (or links an external `.ColourRamp` / `.ColourSpace` asset path).
  - `ParticleInterchange` delegates HTML Studio colour keys (`colorStops`, `colA`/`colB`/`colC`, `colorRampPreset`, `colorWrap`, `colorInterp`, `colorRepeat`, `colorPhase`) directly to `Interchange::ColourInterchange`.

---

## 2. Canonical Folder & File Structure (Banned-Word Audited)

Extends the reserved `Engine/ColourSpace/` slot already defined in `Documentation/Plans/DocumentSpacesRevisionPlan.md` §3:

```text
Engine/
├── ColourSpace/                                       // First-class reusable radiometric & gradient domain (Frontier::ColourSpace)
│   ├── ColourTypes.h                                  // 16B-aligned LinearRgba, SrgbRgba8, OklabTriplet, HslTriplet, CmykQuadruplet, Primaries, OpticalBlendEquation
│   ├── ChromaticitySpace.h / .cpp                     // CIE XYZ, Linear Rec.709/sRGB, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex conversions
│   ├── TransferFunction.h / .cpp                      // Piecewise sRGB EOTF/OETF, Gamma 2.2 parity helper, ST 2084 PQ, HLG, WCAG 2.1 relative luminance & contrast ink
│   ├── WhitePointAdaptation.h / .cpp                  // Bradford & CAT02 chromatic adaptation (D65, D60, D50)
│   ├── SpectralDistribution.h / .cpp                  // Nanometer spectral power integration & Planckian blackbody Kelvin-to-LinearRgba (700 K .. 15,000 K)
│   ├── GamutCompression.h / .cpp                      // Out-of-gamut perceptual chroma compression for HDR linear colours
│   ├── ColourSourceTypes.h                            // Modular ColourSourceSpec (7 source variants), ColourStop, ColourWrapBehaviour, ColourBlendCurve, ColourRangeTransform
│   ├── ColourRampSampler.h / .cpp                     // Arbitrary-stop sampling (Linear/Smooth/Stepped, LinearSrgb/Oklab), 3D lattice noise & 64/256-entry GPU LUT packing
│   ├── ColourPresetRepository.h / .cpp                // Built-in & project swatches + built-in ramps (6 Studio ramps: Spectrum, Blackbody, Aurora, Plasma, Ocean, SunsetNeon + scientific/artistic sets)
│   ├── ColourCodex.h / .cpp                           // Unified engine-wide named alias codex resolving solid pigments ("Blue" -> Royal Blue #2454FF, "Pure Blue" -> #0000FF), gradients ("Aurora", "Fire"), blackbody illuminants ("Tungsten", "D65", "4500K"), hex literals & custom modular ColourSourceProfiles into any colour space (Linear, ACEScg, Rec2020, DisplayP3, sRGB8, Oklab, Oklch, HSL, CMYK, Spectrum)
│   ├── ColourDocument.h / .cpp                        // In-memory .ColourSpace document holding primaries, white point, swatches, modular ramps, spectral curves & baked LUTs
│   └── ColourExtension.h / .cpp                       // Designated lifecycle coordinator (Initialize, Tick, Suspend, Resume, Finalize, LUT cache, WorkspaceExchange live sync)
│
├── Codecs/
│   ├── SpaceContainerCodec.h / .cpp                   // Registers .ColourRamp & .ColourLut sub-format tags alongside existing .Swatch, .Ramp, .Spectrum (SpaceExtensionCategory::ColourSpace = 1)
│   └── ColourCodec.h / .cpp                           // UPGRADED: Two-Tier Hybrid .ColourSpace + singular .ColourRamp / .ColourSwatch + inline TOML helpers for Particle/Material/Texture/Vector/Image/Volumetric codecs
│
├── Interchange/
│   └── ColourInterchange.h / .cpp                     // External HTML Studio JSON/TOML colour blocks, CSS linear-gradient, SVG <linearGradient>, GIMP .gpl/.ggr, Adobe .ase/.aco, .cube 1D LUT
│
├── Shaders/
│   └── Colour/
│       └── ColourRampSampling.slang                   // Shared pure .slang GPU colour ramp LUT sampling, wrap/curve math, Oklab/LinearSrgb, Kelvin blackbody & 3D lattice noise
│
└── Components/
    ├── Controls/
    │   ├── ColourRampEditor.h / .cpp                  // Reusable interactive multi-stop gradient/fill/ramp editor + ContentBrowser drop-target slot
    │   └── ColourSwatchPicker.h / .cpp                // Reusable Linear/sRGB/Oklab/HEX/Kelvin swatch editor + ContentBrowser swatch drop-target slot
    └── ContentBrowser/
        ├── ColourCodex.h / .cpp                       // Unifies swatch math onto Engine/ColourSpace & adds ColourRampPresetEntry repository (All / Swatches / Ramps filter)
        ├── ThumbnailSheetRepository.h / .cpp          // Renders smooth horizontal gradient-strip thumbnails for ColourRamp presets under ContentCategory::Colours
        └── ContentBrowserPanel.h / .cpp               // Adds RegisterColourRampSlot(ColourRampSlotRegistration) for direct drag-drop onto any ColourRampEditor slot
```

*(Note: `Engine/Mathematics/LinearColors.h` (`LinearRGB`) remains intact and interoperates zero-copy with `ColourSpace::LinearRgba`.)*

---

## 3. Modular `ColourSourceSpec` Architecture

Every workspace that needs colour — whether a single flat tint, a two-stop fill, an arbitrary multi-stop gradient, a posterized quantized ramp, a Kelvin thermal glow, discrete species tints, or a 3D spatial noise field — holds one uniform `ColourSpace::ColourSourceProfile`:

### 3.1 Seven Modular `ColourSourceCategory` Types (`std::variant`)

| # | Type | Fields & Purpose | Example Consumers |
| :-: | :--- | :--- | :--- |
| 0 | **`SolidPigmentSource`** | Uniform scene-linear `LinearRgba` | Flat tints, monochrome solids, swatch-bound UI slots |
| 1 | **`LinearFillSource`** | `PrimaryLinear` (`ColA`), `SecondaryLinear` (`ColB`), optional `MidpointLinear` (`ColC`), `MidpointEnabled` | Legacy 2-stop / 3-stop HTML Particle presets, simple shader blends |
| 2 | **`MultiStopGradientSource`** | Ordered `std::vector<ColourStop>` (`Position01`, `LinearRgba`) + optional `LinkedPresetIdentifier` | Content Browser drag-drop ramps, 6 Studio presets, custom authored `.ColourRamp` assets, CSS/SVG gradients |
| 3 | **`QuantizedRampSource`** | Ordered `std::vector<ColourStop>` evaluated with stepped band quantization | Toon/cel shading, heat contours, MoGraph elevation bands |
| 4 | **`BlackbodyThermalSource`** | `ColdKelvin`, `HotKelvin`, `IntensityGain`, `TailOpacity` | Fire, sparks, welding, embers, flares, plasma, volumetric combustion |
| 5 | **`DiscreteTintSetSource`** | Fixed/indexed linear tint entries (4..16 slots) indexed by discrete category ordinal | Chemistry species (`A`/`B`/`C`), spiderweb silk/dew, trunk/canopy foliage, firework sub-shells |
| 6 | **`SpatialNoiseFillSource`** | `NoiseFrequency`, `EvolutionRate`, ordered `std::vector<ColourStop>` | 3D spatial value-noise pigment modulation (HTML `rampVNoise3` bit-parity), marble/cloud tinting |

### 3.2 `ColourRangeTransform` & GPU LUT Packing

- **`ColourRangeTransform`**: `RangeMin`, `RangeMax`, `RepeatCount` (`>= 0.05`), `PhaseOffset`, `WrapBehaviour` (`Clamp`, `Repeat`, `Mirror`), `BlendCurve` (`Linear`, `Smooth` Hermite `3t² - 2t³`, `Stepped`), `InterpolationSpace` (`LinearSrgb`, `Oklab`, `ACEScg`).
- **Pre-Baked GPU LUTs**:
  - `ColourRampLut64` (`alignas(16)`, `64 × LinearRgba = 1024 B`): Matches WebGPU/HTML Particle Studio fast uniform/SSBO ramp lookup.
  - `ColourRampLut256` (`alignas(16)`, `256 × LinearRgba = 4096 B`): High-precision transfer function LUT for `VolumetricSpace`, `ImageSpace`, and `.cube` 1D LUT export.
- **Bit-Parity Guarantee**: When `InterpolationSpace == LinearSrgb`, `TransferCurve == Gamma22`, and `SampleLatticeNoise3D` is selected, `ColourRampSampler` reproduces `Experimental/ParticleEditor Final/Engine/presets.js` (`PE.evalColorRampAt`, `PE.rampVNoise3`, `PE.evalParticleColorCPU`) bit-for-bit.

### 3.3 Built-In `ColourPresetRepository` Ramp Presets

Ships all **6 HTML Studio presets verbatim** plus curated Frontier scientific/artistic ramps — immediately browsable and draggable inside `ContentBrowserPanel` (`ContentCategory::Colours`):

| Identifier | Display Title | Stops (HEX sRGB → Linear) | Origin |
| :--- | :--- | :--- | :--- |
| `rainbow` | `Spectrum (7 stops)` | `#ff3355`, `#ff8822`, `#ffe033`, `#38e86b`, `#22ccff`, `#5566ff`, `#d44cff` | HTML Studio verbatim |
| `thermal` | `Blackbody (6 stops)` | `#140505`, `#961408`, `#f05a14`, `#ffbe38`, `#fff6c8`, `#9be2ff` | HTML Studio verbatim |
| `aurora` | `Aurora (5 stops)` | `#1a1464`, `#10b981`, `#38f2d6`, `#a3ff4d`, `#a855f7` | HTML Studio verbatim |
| `plasma` | `Plasma (6 stops)` | `#0d0887`, `#6a00a8`, `#b12a90`, `#e16462`, `#fca636`, `#f0f921` | HTML Studio verbatim |
| `ocean` | `Ocean (5 stops)` | `#071e3d`, `#134e6f`, `#1e96a8`, `#7be3d0`, `#eefbff` | HTML Studio verbatim |
| `sunset` | `Sunset Neon (6 stops)` | `#2b1055`, `#9d1763`, `#f0544f`, `#ffad42`, `#ffd57e`, `#3b82f6` | HTML Studio verbatim |
| `viridis` | `Viridis Scientific (6 stops)` | `#440154`, `#414487`, `#2a788e`, `#22a884`, `#7ad151`, `#fde725` | Frontier Scientific |
| `magma` | `Magma Thermal (6 stops)` | `#000004`, `#3b0f70`, `#8c2981`, `#de4968`, `#fe9f6d`, `#fcfdbf` | Frontier Scientific |
| `grayscale` | `Linear Luminance (2 stops)` | `#000000`, `#ffffff` | Frontier Utility |

---

## 4. `ColourCodec` Upgrade Plan (`.ColourSpace`, `.ColourRamp`, `.ColourSwatch`)

Upgrades existing `Engine/Codecs/ColourCodec.{h,cpp}` while preserving 100% backward compatibility with `Exhibits/WorkspaceRevisionValidation.cpp`:

### 4.1 Native `.ColourSpace` Two-Tier Hybrid Container (`SpaceExtensionCategory::ColourSpace = 1`)

- **Tier 1 (PascalCase TOML Manifest)**:
  - `[.Ramp]` — Preserves `ReferencePrimaries` (`"ACEScg"` / `"LinearSrgb"` / `"Rec2020"`) & `WhitePointKelvin` (`6504.0`), plus `TransferCurve` & `DefaultInterpolationSpace`.
  - `[[.Swatch]]` — Preserves existing `RadiometricSwatchEntry` fields (`SwatchName`, `LinearRed`, `LinearGreen`, `LinearBlue`, `Opacity`) and adds optional `SeriesTitle` / `FilterCategory`.
  - `[[.ColourRamp]]` — Full modular `ColourSourceProfile` entries (`RampIdentifier`, `DisplayTitle`, `SourceCategory`, `WrapBehaviour`, `BlendCurve`, `InterpolationSpace`, `RepeatCount`, `PhaseOffset`, `ColdKelvin`, `HotKelvin`, `NoiseFrequency`, `EvolutionRate`, `Stops = [[pos, r, g, b, a], ...]`, `DiscreteTints = [[r, g, b, a], ...]`).
- **Tier 2 (64-Byte-Aligned Binary Sections with 64-bit FNV-1a Checksums)**:
  - `.Spectrum` — Existing nanometer float32 spectral power curve samples.
  - `.ColourLut` — Optional pre-baked `ColourRampLut64` or `ColourRampLut256` RGBA32F tables for instant zero-CPU GPU upload.
  - `.Revision` — Optional `RevisionTracker` history via `RevisionCodec`.

### 4.2 Singular Preset Containers & Inline Helpers (Option 1A + Option 2A)

- **Case-insensitive load, canonical PascalCase write**:
  - **`.ColourSpace`** — Full multi-swatch, multi-ramp, spectral & LUT Two-Tier Hybrid container.
  - **`.ColourRamp`** — Standalone single-ramp TOML preset file (draggable in Content Browser, importable/exportable from `ColourRampEditor`).
  - **`.ColourSwatch`** — Existing single named swatch format (`ColourCodex::CanonicalSwatchExtension`), now parsed/emitted through shared `ColourCodec` helpers.
- **Inline Embedding API (zero duplication across sibling codecs)**:
  - `ColourEncoder::SerializeSourceToManifest(const ColourSpace::ColourSourceProfile&)` → `toml::table`
  - `ColourDecoder::ExtractSourceFromManifest(const toml::table&, ColourSpace::ColourSourceProfile&, std::string&)` → `bool`
  - Called directly by `ParticleCodec`, `MaterialCodec`, `TextureCodec`, `VectorCodec`, `ImageCodec`, and `VolumetricCodec`.

---

## 5. `ColourInterchange` Plan (`Engine/Interchange/ColourInterchange.{h,cpp}`)

Parallels `Engine/Interchange/GeometryInterchange.{h,cpp}` for all external non-Frontier colour formats:

| External Format | Direction | Exact Support |
| :--- | :---: | :--- |
| **HTML Studio JSON / TOML Colour Block** | Import / Export | Parses & emits `colorStops`, `colA`, `colB`, `colC`, `colorRampPreset`, `colorWrap`, `colorInterp`, `colorRepeat`, `colorPhase`, `blend`; invoked directly by `ParticleInterchange` |
| **CSS Linear Gradient (`linear-gradient(...)`)** | Import / Export | Parses `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, percentage stop offsets; exports clean CSS strings |
| **SVG `<linearGradient>`** | Import / Export | Parses/emits `<stop offset="..." stop-color="..." stop-opacity="..."/>` for `VectorSpace` & web workflows |
| **GIMP `.gpl` Swatches & `.ggr` Gradients** | Import / Export | Full text swatch tables and segmented linear/curved gradient files |
| **Adobe `.ase` & `.aco` Swatches** | Import / Export | Big-endian RGB/CMYK/Lab swatch blocks converted into `ColourSpace::ColourDocument` |
| **Iridas / Resolve `.cube` 1D LUT** | Import / Export | `LUT_1D_SIZE` linear/sRGB transfer tables backed by `ColourRampLut256` |

---

## 6. Content Browser & Reusable UI Integration

1. **Promote `ColourCodex` into `Engine/ColourSpace/ColourCodex.{h,cpp}` with ContentBrowser facade**:
   - **`Frontier::ColourSpace::ColourCodex`** is the canonical engine-wide alias repository owned by `ColourExtension`, unifying named solid pigments (`"Blue"`, `"Royal Blue"`, `"Fig Purple"`, `"Pure Blue"`), multi-stop gradients (`"Aurora"`, `"Fire"`, `"Spectrum"`), blackbody illuminants (`"Tungsten"`, `"D65"`, `"4500K"`), `#RRGGBB` literals, and custom modular `ColourSourceProfile` aliases — resolvable into ANY colour format (`ColourSourceProfile`, Linear Rec.709, ACEScg, Rec.2020, Display-P3, sRGB8, Oklab, Oklch, HSL, CMYK, or 81-bin `SpectralCurveProfile`).
   - `Engine/Components/ContentBrowser/ColourCodex.{h,cpp}` delegates directly to `Frontier::ColourSpace::ColourCodex` (zero duplicated pigment data), keeping its UI filter-bucket API (`ColourCodexEntry`) intact for zero caller regression.
   - Adds a subtle mode pill inside `ContentCategory::Colours`: **`All` · `Swatches` · `Ramps`** so artists can browse both single swatches and multi-stop colour ramps cleanly in one place.
2. **Gradient-Strip Thumbnails (`ThumbnailSheetRepository`)**:
   - Pre-bakes smooth horizontal anti-aliased RGBA gradient-strip tiles (with subtle stop-tick markers and contrast title label via `TransferFunction::QueryContrastInk`) into the Content Browser thumbnail sheet for every `ColourRampPresetEntry`.
3. **Drag-Drop Slot Registration (`ContentBrowserPanel`)**:
   - Parallels existing `RegisterColourSlot(ColourSlotRegistration)` and `RegisterMatCapSlot(MatCapSlotRegistration)`:
     ```cpp
     struct ColourRampSlotRegistration
     {
         std::string                                                                      SlotIdentifier         {};
         std::string                                                                      DisplayTitle           {};
         Rectangle2D                                                                      ScreenBounds           {};
         ColourSpace::ColourSourceProfile*                                                TargetProfile          = nullptr;
         std::function<void(const ColourSpace::ColourSourceProfile&, std::string_view)>   NotifyRampAssigned     {};
         std::function<void(const ColourSpace::ColourSourceProfile&, std::string_view, Point2D)> NotifyRampAssignedAt {};
     };
     ```
   - Dragging any Colour Ramp tile out of the bottom-shelf Content Browser highlights every registered `ColourRampSlotRegistration` across the open workspace (e.g. `ColourRampEditor` in Particle, Material, Texturing, Vector, or ForceField) and assigns the ramp immediately on drop — recording a reversible operation into `RevisionTracker`.
4. **Reusable `ColourRampEditor` & `ColourSwatchPicker` (`Engine/Components/Controls/`)**:
   - Built solely from Frontier `PixelSpace`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `ScalarSlider`, `TextInputField`, `ThemeRepository`, and `StudioIconSet`.
   - `ColourRampEditor` automatically registers its ramp strip rectangle as a `ColourRampSlotRegistration` drop target AND allows clicking any stop to open `ColourSwatchPicker` (which accepts `.ColourSwatch` drops from `RegisterColourSlot`).

---

## 7. Phased Delivery & Validation Gates

`ColourSpace` lands as **Phase 1A** immediately before `ParticleSpace` Phase 1B so every downstream domain builds on the real, tested Colour foundation from day one:

| Step | Deliverables | Verification Gate |
| :--- | :--- | :--- |
| **Phase 1A-1 — `Engine/ColourSpace/` Core** | `ColourTypes.h`, `ChromaticitySpace`, `TransferFunction`, `WhitePointAdaptation`, `SpectralDistribution`, `GamutCompression`, `ColourSourceTypes.h`, `ColourRampSampler`, `ColourPresetRepository`, `ColourDocument`, `ColourExtension` | `ColourSpaceValidation` exhibit verifies exact round-trip colorimetry (`sRGB ↔ XYZ ↔ ACEScg ↔ Rec2020 ↔ Oklab`), Planckian Kelvin curve, all 7 `ColourSourceCategory` variants, bit-parity with HTML `PE.evalColorRampAt` & `rampVNoise3`, and 64/256-entry LUT packing. Renders gradient strip contact sheet to `Exhibits/Images/ColourSpace/ColourRampPresetsSheet.png` and inspects visually. |
| **Phase 1A-2 — `ColourCodec` & `ColourInterchange`** | Upgraded `Engine/Codecs/ColourCodec.{h,cpp}`, `SpaceContainerCodec` `.ColourRamp`/`.ColourLut` tags, `Engine/Interchange/ColourInterchange.{h,cpp}` | `ColourCodecInterchangeValidation` proves backward compatibility with `WorkspaceRevisionValidation`, bit-exact `.ColourSpace` + `.ColourRamp` + `.ColourSwatch` round-trip (FNV-1a checksums + 64B alignment), and lossless round-trip across HTML Studio JSON/TOML, CSS, SVG, `.gpl`/`.ggr`, `.ase`/`.aco`, and `.cube` 1D LUTs. |
| **Phase 1A-3 — `ColourRampSampling.slang`** | `Engine/Shaders/Colour/ColourRampSampling.slang` compiled cleanly via `slangc` | Verified via `slangc` SPIR-V compilation + CPU-vs-GPU ramp sampling tolerance `< 1e-4` across all 6 Studio presets & wrap/curve modes. |
| **Phase 1A-4 — Content Browser & Reusable UI** | `ColourCodex` ramp entries, `ThumbnailSheetRepository` gradient-strip tiles, `ContentBrowserPanel::RegisterColourRampSlot`, `ColourRampEditor`, `ColourSwatchPicker` | Offscreen UI exhibit (`ColourComponentsValidation`) renders Content Browser `Colours` (`Swatches` + `Ramps`), active drag ghost, and `ColourRampEditor` drop-target state to `.png` under `Exhibits/Images/ColourSpace/` and verifies visually. |
| **Phase 1B+ — `ParticleSpace` Adoption** | `Engine/ParticleSpace/` builds directly on `Engine/ColourSpace/`, `ColourCodec`, `ColourInterchange`, `ColourRampSampling.slang`, and `ColourRampEditor` (zero duplicated colour code inside `ParticleSpace`) | Feeds seamlessly into `ParticleSpaceModularPortPlan.md` Phases 1B–6. |
