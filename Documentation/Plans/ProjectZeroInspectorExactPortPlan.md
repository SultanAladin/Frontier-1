# Project Zero Editor Inspector 1:1 Pixel-Parity Port Plan

## Objective
Port the authentic HTML/CSS/JSX Editor Inspector cards from `eosclient0001-rgb/Frontier@arena/d929ef36-frontier` (`6392b471`, `Experimental/ProjectZeroEditor/*` and `Experimental/FrontierEditor/environment-graphics.jsx`) into native C++ (`Engine/Components/Controls/InspectorSectionPrimitives.{h,cpp}` and `Engine/Editor/Inspectors/*`) **strictly 1:1 pixel-for-pixel**, with:
1. **Zero invention** — every layout, color, border radius, typography weight, icon SVG path, vector graphic coordinate, text label, and page order copied verbatim from the HTML source and rendered browser proofs (`Exhibits/ProjectZeroEditor/FollowUp46..71/`).
2. **Zero PC jerking / frame stalls** — eliminate per-frame CPU pixel loops and `ImTextureStatus_WantUpdates` → `vkQueueWaitIdle()` stalls in `ImGuiOverlayRasterization.cpp:1286` by rendering vector diagrams directly through `PixelSpace` vector commands and gating LUT baking strictly behind explicit `Bake` / `Rebake` clicks (F76 default Unbaked state).
3. **Reuse of existing C++ controls & SVG glyph repository** — reuse `ScalarSlider`, `DropdownSelector`, `ColourSwatchPicker`, `CapsuleButton`, `ExclusiveSelector`, `ControlTooltip`, `Vector3CapsuleInput`, and `WorkspaceAppearance` typography/glyphs rather than duplicating widgets.
4. **Live C++ / Slang variable bindings** — every control reads and writes live `WorldSceneSpecification`, `WorldRenderConfiguration`, `ViewportDisplaySettings`, `AtmosphericMediumConfiguration`, `SolarCelestialConfiguration`, `LunarCelestialConfiguration`, `PerspectiveCameraBasis`, `GpuAreaQuadLight`, and `GpuPhotometricLamp64` variables consumed by `WorldViewportRadiometricCompute.slang`.

---

## Root Cause of Previous Divergences & PC Stuttering
1. **Visual fabrication in prior C++ inspectors**:
   - `CameraSettingsInspector.cpp` replaced the HTML's F78 hero sphere card + 4-column `A/B/C/D` thumbnail selector + clickable chevron summary rows + color-coded stacked `BudgetBar` + 5-tier `Low | Medium | High | Ultra | Reference` bar + `Tracer` page with a generic single sphere and 4 text buttons.
   - `CameraInspector.cpp` placed `Mount` on page 4/6 instead of page 2/5, omitted the in-hero caption and visual A/B tripod/floating cards, and replaced the 3-column XYZ `Position + angle` table with stacked sliders.
   - `SolarInspector.cpp` lacked the latest (`6392b471`) `Sun direction` card (`Azimuth` + `Elevation` hand-steering sliders above the `SunGizmo` circular polar horizon grid with `celestial-orbit` header icon).
   - `InspectorSectionPrimitives.cpp` skipped the F76 unbaked default face (`Use baked image / UNAVAILABLE`, `Bake`, `Load image`) and rendered LUTs automatically on open/drag.
   - All 8 lighting inspectors used opaque CPU-rasterized textures instead of the crisp vector blueprints in `LightPanel.js` / `LightProjection.js`.
2. **PC jerking root cause**:
   - `InspectorPreviewSurface::EnsureUpdated()` ran nested CPU pixel loops (`std::pow`, `std::exp`, `sin/cos`) and set `ImTextureStatus_WantUpdates`, which executes `vkQueueWaitIdle()` inside `ImGuiOverlayRasterization.cpp:1286` on every slider drag frame.

---

## Exact Design Tokens (Verbatim from `Editor.css`, `PageCarousel.css`, `BakedMapCards.css`, `RenderSettings.css`)
- Canvas / Shell background: `#090909`, text `#f0f0f0`, muted `#8a8f98` / `#919191`, typography `DM Sans` Light (300) / Regular (400) / Medium (500) / SemiBold (600).
- Card surface: `#161616`–`#181818`, radius `16px`, hairline border `1px solid #ffffff0d`, padding `14px`.
- Slider (`input[type="range"]`): track height `26px`, radius `30px`, filled left `#454545`, unfilled right `#242424`, circular thumb `24px` `#e0e0e0` with subtle drop shadow; paired with split black `#000000` numeric pill + unit cap.
- Page carousel (`PageCarousel.css`): `26px` circular step buttons `#1c1c1f`, `#141416` pill rail, `26px` pills with `18px` circular number badge (`#2a2a2e` idle, `#ececef` active with `#141416` ink), active pill `#34343a`, right counter `1 / N`.
- Baked map accents (`BakedMapCards.css`): baked badge `#3ecf8e1a` fill / `#3ecf8e40` border / `#7fe0b2` ink; active resolution card & `Bake` action `#ffb2381a`–`#ffb23826` fill / `#ffb238aa` border / `#ffd38a` ink; hover tooltip `250px` wide `#18191c`.
- Render budget colors (`RenderComponents.jsx`): Shadows `#7ab8ff`, GI `#7be0ad`, Reflections `#c5a3ff`, Occlusion `#e0b07b`; active A/B/C/D letter badge `#ff3b30`.

---

## Phased Implementation Plan

### Phase 1 — Zero-Jerk Vector Graphics & Exact Shared Primitives (`InspectorSectionPrimitives.{h,cpp}`)
1. Port verbatim HTML SVG icon glyphs (`celestial-orbit`, `bake`, `check`, `rebake`, `save`, `lock`, `power`, `camera`, `plus`, `chevron-right`) into `InspectorPrimitives::ConstructHtmlGlyph(...)`.
2. Port verbatim 3-face F76 `ConstructBakedLutSection(...)`:
   - **Unbaked default face**: left `Use baked image` (`UNAVAILABLE`) tile + right `Bake` (with `bake` icon), disabled `Load image`, `Not baked · W × H`. **Does not rasterize or upload textures until user clicks Bake.**
   - **Bake settings face**: `Bake settings` + map name, 2×2 resolution cards (`64 × 16`, `128 × 32`, `256 × 64` default, `512 × 128` with gold active highlight), `RESOLUTION / FORMAT / TEXELS / SIZE / X / Y` fact table, `Cancel` + gold `Bake` button.
   - **Baked face**: green `✓ Baked` capsule (hover renders floating `250px` `#18191c` details tooltip), resolution text, `Rebake` (resets to Unbaked face) & `Save` icon buttons, axis-labeled LUT canvas with golden marker + cursor readout.
3. Add pure-vector HTML graphic helpers (0 textures, 0 `vkQueueWaitIdle`):
   - `ConstructSunOrbitGizmo(...)` — verbatim `environment-graphics.jsx::SunGizmo` (`360×280` polar horizon grid, 3 concentric rings, 12 radial spokes, golden day arc, dashed night arc, glowing sun disc, horizon ring, `12h/24h/N/S/W/E`, interactive pointer drag).
   - `ConstructCameraMountPicture(...)` — verbatim `CameraMount.jsx::MountPicture` (large `300×170` & small `72×52` cine camera body + lens + dual reels, tripod legs / floating dashed crane path + ground ellipse, gold height callout).
   - `ConstructRenderTypeSphere(...)` — verbatim `RenderComponents.jsx::TypeThumb` (PBR blue sphere, SDF mint sphere + concentric SDF rings, Ray traced lavender sphere + dashed rays, MatCap warm clay sphere).
   - `ConstructRenderBudgetBar(...)` + `ConstructPenumbraStudy(...)` + `ConstructHemisphereStudy(...)` + `ConstructBounceStudy(...)` — verbatim `RenderStudies.jsx`.
   - `ConstructOpticalSchematic(...)` — verbatim `OpticalGraphic.jsx` camera lens/sensor ray diagram.
   - `ConstructLightBlueprint(...)` — verbatim `LightPanel.js` / `LightProjection.js` vector diagrams for all 8 light types.
   - `ConstructInspectorHeaderEx(...)` — supports HTML `+ Add notes` dashed pill button and clickable parent component link card (`Editor Camera >`).
   - `ConstructTransformTable(...)` — verbatim `TransformPanel.jsx` 3-column XYZ (`X` red, `Y` green, `Z` blue) compact grid with `↺` reset buttons.

### Phase 2 — Sun Direction (`6392b471`) & Solar Inspector (`SolarInspector.{h,cpp}`)
- Replace `Sun direction` card with exact `FollowUp71_SunDirectionSolved.png` / `SunDirectionSteered.png` layout:
  - Gold `celestial-orbit` icon + `Sun direction` header.
  - `Azimuth` (`0..360 deg`) & `Elevation` (`-90..90 deg`) sliders; moving either slider or dragging `SunGizmo` smoothly hands off into `ManualSolarDirectionEnabled = true` from current solved angles (zero jump) and disables `UseSystemClockTime` & `DayCycleAnimated`.
  - Pure-vector `ConstructSunOrbitGizmo` directly below sliders.
  - Exact status note (`Above/Below the horizon · solved from the date, time and place` vs `manual direction, the solved angles are overridden`).
- Bind `Sunlight intensity` (`IlluminanceCurve` vector graphic) and F76 3-face `BakedMap` (default Unbaked) directly to `Scene.Atmosphere.Solar` & `WorldViewportRadiometricCompute.slang` uniforms (`SunDirConeTan`, `SolarIrradiancePad`).

### Phase 3 — Camera Settings Inspector (`F77`/`F78`, `CameraSettingsInspector.{h,cpp}`)
- Match `FollowUp68_RenderTypeCard.png` & `FollowUp68_RayTracerPage.png` 1:1:
  - Breadcrumb `Inspector / Editor Camera`, `+ Add notes` dashed pill, `Editor Camera` linked component card (`Component of this camera · values are stored on it >`).
  - Dynamic per-renderer page carousel (`Overview`, optional `Tracer`, `Shadows`, `GI`, `AO`, `Reflections`, `Lighting`, `MatCap type`, `Output`).
  - `Render type` card: dark radial hero stage with large sphere + `A · PBR` / `B · SDF` / `C · Ray traced` / `D · MatCap` + description + feature pills, 4-column visual selector row (`A/B/C/D` with red active letter), renderer caption.
  - Renderer summary card (`PBR raster` / `SDF tracing` / `Ray tracing` / `MatCap`): large title + engine subtitle, `Each line opens its page`, clickable chevron rows jumping directly to each page.
  - `Quality` card: large `{N} samples / pixel`, tier caption, color-coded stacked `BudgetBar`, 5-segment `Low | Medium | High | Ultra | Reference` bar, footer `Source defaults · this camera only` + `Reset`.
  - `Tracer` page (`BVH4 GI` / `PT game` / `PT final` description cards + rays/pixel budget bar) and per-renderer study pages (`PenumbraStudy`, `HemisphereStudy`, `BounceStudy`).
- Bind directly to `ViewportDisplaySettings` / `WorldRenderConfiguration` → `WorldViewportRadiometricCompute.slang` (`EngineShadowReflPad`, `CameraExposureFlags`, `AtmosphereMaskPad`).

### Phase 4 — Camera Inspector & Camera Mount (`F78`, `CameraInspector.{h,cpp}`)
- Match `FollowUp68_CameraMountTripod.png`, `FollowUp68_CameraMountFloating.png`, and `FollowUp55_Camera-Overview.png` 1:1:
  - 5-page carousel: `1 Overview`, `2 Mount`, `3 Focus`, `4 Motion blur`, `5 Effects`.
  - Overview page: pure-vector `Lens + field of view` optical schematic (`35.0 mm`, sensor/aperture/cone schematic, `Focal Length` slider, vertical FOV & frame dimensions), `Quick controls`, `Tags`, `Notes`.
  - Mount page (Page 2/5): `Mount` card with dark navy `MountPicture` hero (`A · On tripod` / `B · Floating` + caption inside hero), 2 visual choice cards (`A On tripod` / `B Floating`), `Tripod height` / `Height` slider (+ `Roll` when floating), exact descriptive note; `Position + angle` card with large `{H:.2f} m high`, 3-column XYZ `TransformTable` (`Camera Position`, `Camera Angle`), `↺ Camera Position` / `↺ Camera Angle` buttons, `Aim at Showcase` green toggle, `↺ Back on its arc` button.
- Bind directly to `OutlinerRecordEntry`, `CameraRigConfiguration`, and `PerspectiveCameraBasis` (zero texture uploads!).

### Phase 5 — All 8 Standalone Lighting Inspectors (`AreaLight`, `PointLight`, `SpotLight`, `IesLight`, `TubeLight`, `LedLight`, `LedStrip`, `EnvironmentLight`)
- Match `FollowUp55_Light-Overview.png` & `LightPanel.js` 1:1 with **pure-vector photometric blueprints** (0 CPU pixel loops, 0 `vkQueueWaitIdle()` stalls!):
  - Exact card structure: Hero luminous card with light-type icon, split large metric (`2,400` bright `.0` dim `lm`), caption, pure-vector light blueprint (`APERTURE / FRONT ELEVATION` for Area, radial rings for Point, cone beam for Spot, polar IES web for IES, cylinder elevation for Tube, diode dome for LED, LED pitch strip for Strip, sky/ground hemisphere for Environment), 2-up stat tiles, `Source & colour` group (`Luminous flux` / `Intensity` slider + `Emission tint` color swatch), `Scene participation` (`FLAGS`: `Cast shadows`, `Draw emitter`), `Quick controls` (`Visible`, `GI`), `Tags`, `Notes`.
- Bind directly to `Scene.AreaLights` and `Scene.PhotometricLamps` (consumed by `EvaluateAreaQuadLight` & `EvaluatePhotometricLampGpu` in `WorldViewportRadiometricCompute.slang`).

### Phase 6 — Atmosphere & Starfield Baking (F76 Default Unbaked) + Zero-Stall Validation
- Update `AtmosphereInspector` and `StarfieldInspector` Baking pages to F76 3-face `BakedMap` (Unbaked by default; bakes only on explicit `Bake` click; `Rebake` returns to Unbaked) and pure-vector `ScatteringGraph` & `WindCompass` (`environment-graphics.jsx`).
- Verify zero slider-drag texture upload stalls across all inspectors and validate compilation + unit tests + visual captures.
