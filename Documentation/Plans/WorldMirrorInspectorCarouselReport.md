# Frontier World Mirror — Inspector Settings Carousel Report

**Branch:** `arena/01ea1b85-frontier`  
**Date:** 2026-11-20  
**Status:** ✅ Complete — Carousel Verified, C++ Control Audit Closed (no missing settings), Card Clipping Fixed

---

## 1. Request & Ruling

The HTML mirror (`Experimental/FrontierWorldMirror/`) presented every subject's settings as one
long vertical scroll in the right dock. The user directed a cleaner arrangement using a carousel —
`{ Menu A | Menu B | Menu C }` — **each menu carrying its own settings group**. Ruling: the
Inspector now paginates each subject's settings into named A/B/C menus behind a fixed segmented
control; the subject header (title, breadcrumbs, description) stays pinned above the menu strip,
and the page footer description strip is unchanged.

---

## 2. Mechanism Added

| Unit | Role |
| --- | --- |
| `Experimental/FrontierWorldMirror/InspectorCarousel.jsx` | `InspectorCarousel` component: segmented A/B/C strip, prev/next arrows, `n / m` counter, pointer swipe (horizontal-only, edge damping), ArrowLeft/Right + Home/End keys (deferred to native arrow behaviour on `input`/`select`/`textarea`), `role="tablist"/"tab"/"tabpanel"` + `inert` on hidden pages. Single-page requests render bare (no chrome). |
| `Experimental/FrontierWorldMirror/InspectorCarousel.css` | Theme-matched chrome (`#141414` segment well, `#2f3338` active pill, 16px key discs) + flex-height chain (`.inspector-scroll` → inspector root → `.insp-carousel` → scrollable `.insp-carousel-viewport`) so the menu strip stays pinned while each page scrolls independently. ≤380px docks collapse to key discs only. |
| `Experimental/FrontierWorldMirror/Build.mjs` | Asset-source guard: the full binary-asset build path (fonts, moon textures, shader-ball mesh) now also runs when those sources are absent from the checkout, falling back to the embedded `window.NativeAssets` in the prebuilt `index.html` (the build's documented reuse path). |

No scene, rendering, or C++ contract changed — layout only. All existing controls, handlers,
reference cards, graphs, and footers are preserved byte-for-byte inside their pages.

---

## 3. Settings Menu Map (Per Subject Panel)

| Panel | Menu A | Menu B | Menu C |
| --- | --- | --- | --- |
| `geometry` | Transform (+ quick controls) | Material (+ per-face colours) | Fracture (+ remaining sheet groups) |
| `light` | Settings (reference summary, unlock, transform, quick controls) | Lighting (reference detail + sheet groups) | — |
| `post` | Settings (transform, quick controls) | Effects (sheet groups + property graphs) | — |
| `sun` | Direction (tiles, world-direction transform, sun gizmo, local hours) | Light (intensity, temperature, daylight cycle, sun disc, dynamic settings) | Effects (lens-flare subcomponent, reference cards, baking) |
| `camera` | Transform (+ editor-camera note, reset canonical pose) | Lens (sensor field-of-view schematic) | Sensor (aperture study, subject plane + sharpness, sensor support) |
| `render-settings` | Quality (summary bar + reset, pipeline preset) | Lighting (shadows, GI, reflections) | Output (exposure, resolution scale, AA) |
| `stars` | Field (star field, catalogue toggles) | Motion (twinkle, celestial rotation, renderer scale) | Baking (star field bake) |
| `moon` | Phase (lunar phase, instance select) | Appearance (moon catalogue, moonlight, moon size) | Position (lunar position dial, moon rotation) |
| `atmosphere` | Scattering (lab, Rayleigh/Mie/Ozone profiles) | Density (falloff, ground reflectance, native controls) | Baking (atmosphere bake, baked dome) |
| `flare` | Composite (drag preview, toggles, source) | Layers (flare layers, lens ghosts, halo response) | Presets (legacy presets, baking) |
| `wind` | Controls (reference, enable/shear tiles, wind inspector) | Modifiers (native draft fields) | — |
| `precipitation` | Type (type picker, emission + collision) | Behavior (fall + density, particle scale) | Simulation (accumulation, particles, snow depth) |
| `rainbow` | Preview (optical preview, visibility) | Response (bow response, bake) | — |
| `clouds` | Settings (reference, coverage) | Volume (base, thickness, body) | Shadows (GPU cloud shadows, wind binding) |
| `local-cloud` | Settings (reference, coverage) | Volume (base, thickness, local bounds, body) | Wind (wind binding) |
| `height-fog` / `aerial-fog` / `local-fog` | Fog (toggles, visibility graph) | Model (medium, shape/spectrum/height visual) | Wind (wind binding) |
| `force` | ForceFieldPanel (self-contained editor — no carousel chrome) | — | — |
| `group` | FolderInspector tree (unchanged) | — | — |

---

## 4. Verification (Headless — No Browser In Sandbox)

| Suite | Command | Result |
| --- | --- | --- |
| 19-Suite Mirror Parity | `node Experimental/FrontierWorldMirror/WorldMirrorValidation.mjs` | **ALL 19 SUITES PASSED** |
| Live-bundle jsdom carousel smoke | `_ClaudeScratch/mirror-carousel-smoke.mjs` (built `index.html`, CPU-GTX fallback, 8 outliner subjects) | **ALL CHECKS PASSED** — exact menu labels, segment clicks, inert pages, arrow stepping + disabled states, zero console errors |
| SSR render of all 19 panel types | `_ClaudeScratch/ssr-panels-run.mjs` (esbuild + `react-dom/server`) | **ALL PANELS RENDER CLEANLY** |
| Rebuild | `node Experimental/FrontierWorldMirror/Build.mjs` | 5.30 MiB standalone `index.html`, 161 icons, 57 glyphs, 6 moons, shader-ball mesh |

`_ClaudeScratch/` is the git-ignored scratchpad (test harness only — not part of the deliverable).

---

## 5. C++ Control Audit — "see all the controls we need and do the same in the HTML"

The user directed: look in the C++ engine, find **all the settings/controls we need** (sun, moon,
editor camera, and so on), and mirror them in the HTML — **adding only the settings we need**.
Per `CLAUDE.md` §2.1 the HTML studio is the UI ground truth that `Engine/Editor/` mirrors, so this
audit treats the C++ inspector as the reference inventory and verifies the HTML already carries
every control it exposes (the HTML frequently extends it).

### 5.1 C++ Reference Inventory

The C++ inspector is drawn imperatively (no standalone per-object panel widgets remain in
`Engine/Editor/`, which now holds only `Explorer/` + `README.md`). Two authoritative sources:

| C++ Source | What it defines |
| --- | --- |
| `Experimental/FrontierWorldMirror/NativePanels.json` | Faithful, field-by-field capture of every native inspector panel (`SunInspectorPanel.cpp`, `MoonInspectorPanel.cpp`, `CameraInspectorPanel.cpp`, `CloudsInspectorPanel.cpp`, `FogInspectorPanel.cpp`, `StarsInspectorPanel.cpp`, `LensFlareInspectorPanel.cpp`, `WeatherInspectorPanel.cpp`, `SolidArcInspectorPanel.cpp`, `AtmosphereSkyInspectorPanel.cpp`, `InspectorPanel.cpp` + the `DisplayPresentation` solvers). 18 panel types, per-field `Label` / `Control` / `Group` / min / max / default. |
| `Engine/Components/Panes/ViewportPanel.h` → `ViewportDisplaySettings` | The canonical 19-field render-settings struct: `QualityTier`, `DebugChannelIndex`, `RenderEngineIndex` (6), `ShadowAlgorithmIndex` (5), `SdfShadowPenumbraIndex` (6), `ReflectionSourceIndex`, `TraceResolutionIndex`, `RenderScalePercent`, `ReflectionsEnabled`, `AntiAliasingEnabled`, `RaytracingEnabled`, `AtmosphereSkyEnabled`, `VolumetricCloudsEnabled`, `CloudShadowsEnabled`, `CelestialStarsEnabled`, `DayCyclePlaying`, `SolarTimeOfDayHours`, `CloudFraction`, `CloudWindSpeedMps`. |
| `Engine/Lifecycle/EditorLifecycle.cpp` → `ConfigureCustomSectionCallback` | The native **Editor Camera** inspector: Card 1 "Render Settings" (Engine 6-way, SDF Shadow 6-mode, Quality 5-seg, Raytracing) + Card 2 "Sky, Sun, Moon & Clouds" (Solar Time +2h stepper, Cloud Cover +20% stepper). |

### 5.2 Audit Method (headless)

The SSR harness (`_ClaudeScratch/ssr-panels-entry.jsx`) was extended to render all 18 C++ panel
types through the real `Inspector` component and assert that **every `Label` recorded in
`NativePanels.json` appears in the rendered HTML** for its panel (entity-escaping aware). This
checks the *rendered output*, not the source — because many fields are emitted at runtime from
`NativePanels.json` via `F(Field.Label)`, a source-grep would under-count.

### 5.3 Result — Every C++ Control Is Present in the HTML

| Coverage | Panels (all C++ fields rendered) |
| --- | --- |
| 100% label-identical | `sun` (18/18), `stars` (10/10), `precipitation` (12/12), `rainbow` (6/6), `flare` (21/21), `post` (5/5), `light` (3/3), `camera` (4/4), `group` (2/2), `atmosphere` (18/18), `aerial-fog` (6/6), `height-fog` (7/7) |
| Functionally complete, HTML-native label/architecture | `moon`, `geometry`, `wind`, `clouds`, `local-cloud`, `local-fog` (see 5.4) |

**Editor Camera + Render Settings:** the HTML `camera` panel (Transform / Lens / Sensor) is a
superset of the C++ `CameraInspectorPanel` (Focal Length, Sensor Width, Aperture, Subject
Distance). The C++ camera inspector's "Render Settings" card and "Sky, Sun, Moon & Clouds" card map
to the HTML `render-settings` panel — a strict superset of `ViewportDisplaySettings` (5 shadow
algorithms, 3 GI algorithms, 2 reflection algorithms, 5 quality presets, penumbra softness, PCSS
taps, SMRT rays, plane filter, half-res shadow pass, GI cones/samples/bounce intensity, sky fill
boost, reflection rays/bounces, camera exposure gain, **render scale**, **edge anti-aliasing**).
Solar Time / Cloud Cover live on the `sun` / `clouds` panels. A quick-jump button on the camera
panel opens the `render-settings` subject.

### 5.4 Intentional HTML-UI Adaptations (NOT missing controls)

These are the only C++ labels that do not appear verbatim. In each case the **function is present**
under the HTML ground-truth's own naming/architecture, so no control is missing and none is added
(adding a C++-labeled duplicate would be a redundant dead control):

| C++ field (panel) | HTML control that covers it |
| --- | --- |
| `Speed`, `Bearing` — Flow group (`wind`) | Per wind-field `Strength (m/s)` + `Bearing (°)` (`WindPanel.jsx`) |
| `Wind source`, `Owned wind component` — Wind binding (`clouds`, `local-cloud`) | "Wind field" select + `Follow Wind` tile (`WindBinding`, `WindPanel.jsx`) |
| `Preset` — Moon group (`moon`) | Moon catalogue (Luna / Ember / Glacier / Sulfur / Shroud / Shard) + active-instance select |
| `Albedo` — Surface group (`geometry`) | Material `colour` channel (base colour) in `MaterialPanel.jsx` |
| `Half Size` — Transform group (`local-fog`) | Fog shape mesh (`Centre` + derived Min/Max bounds, `FogShapePanel.jsx`) |

**Ruling:** the audit closes with **nothing to add** — every C++ inspector control the user named
(sun, moon, editor camera) and "and so on" (atmosphere, clouds, fog, stars, flare, wind,
precipitation, rainbow, post, geometry, light) is already present and functional in the HTML
mirror. Per "add the setting that we need only," no redundant settings were introduced.

---

## 6. Inspector Card Clipping — Diagnosis & Fix

**Symptom:** inspector cards were visibly cut off at the bottom edge of the panel (e.g. the
Atmosphere "from space" card's dark preview box clipped at the panel's lower edge), reading as a
broken clip with no cue that more settings continued below.

**Diagnosis:** the carousel page viewport's height was derived from a nested `flex-basis: auto`
chain. When a page's content (e.g. Atmosphere "Scattering" ≈ 2,500px) exceeded the visible fold,
the viewport had no deterministic sizing and no scroll affordance, so a card crossing the bottom
edge looked clipped rather than scrollable.

**Fix** (`InspectorCarousel.jsx` + `InspectorCarousel.css`):

1. **Deterministic scroll frame** — the page viewport is now `position: absolute; inset: 0` inside
   a `flex: 1 1 0%; min-height: 0; position: relative` frame (`.insp-carousel-scroll`). The scroll
   container's size can never be inflated by page content, so every card on every page is reachable
   by scrolling — no hard clip.
2. **"More below" fade** — a `ResizeObserver`/scroll-driven `has-more-below` state shows a 34px
   bottom gradient (theme-aware via `var(--ws-dock)`, dark fallback) **only while the active page
   continues under the fold**, so a card cut at the bottom edge reads as scrollable, not broken.
3. **Visible thin scrollbar** (`scrollbar-color` + webkit thumb) so pages are clearly scrollable.
4. **`scroll-snap-type: y proximity`** + `scroll-snap-align: start` on page children — cards settle
   against the top edge instead of resting cut off mid-box at the bottom.
5. **Page switch resets `scrollTop` to 0** — switching A/B/C never strands the user mid-scroll on a
   long page.

All existing behaviour (segmented A/B/C strip, arrows, swipe, keys, `inert` pages, single-page bare
render) is preserved; the smoke suite passes with zero console errors.

---

## 7. Serve

- Live mirror: `python3 Experimental/FrontierWorldMirror/Serve.py 8098` → `http://0.0.0.0:8098/` (running).
- Markdown Studio: `python3 Automation/ServeMarkdownStudio.py` → `0.0.0.0:8080`.
