# Frontier World Mirror — Inspector Settings Carousel Report

**Branch:** `arena/01ea1b85-frontier`  
**Date:** 2026-11-20  
**Status:** ✅ Complete — All 19 Inspector Panel Types Verified (jsdom + SSR + 19-Suite Validation)

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

## 5. C++ Mirror Parity Note

Per `CLAUDE.md` §2.1, the HTML studio is the UI ground truth that `Engine/Editor/` (`InspectorPanel`
& co.) mirrors. This change moves the HTML layout contract: the native Inspector now has a
settings-menu carousel (segmented A/B/C strip + paged content) to reproduce 1:1 when the next
C++ UI port is scheduled. No C++ unit was modified in this turn; the existing 19-suite parity
gate remains green.

---

## 6. Serve

- Live mirror: `python3 Experimental/FrontierWorldMirror/Serve.py 8098` → `http://0.0.0.0:8098/` (running).
- Markdown Studio: `python3 Automation/ServeMarkdownStudio.py` → `0.0.0.0:8080`.
