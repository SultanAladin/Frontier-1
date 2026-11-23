# Particle editor

A standalone web app for GPU particle effects, using the Frontier Editor layout: outliner on the left, viewport with its bar and readout in the
middle, inspector on the right, status bar along the bottom. It has ten looks, and each one is a layer. Turn on as many layers as you like and edit
each one live. The folder is self-contained, so it can be served or deployed on its own.

## Run
```
python3 Experimental/ParticleEditor/ServeParticleEditor.py 8097
```
Open `http://localhost:8097/`. Any static server rooted at this folder also works, e.g. `npx serve Experimental/ParticleEditor`. A server is
needed because ES modules and the car models do not load from `file://`. WebGL2 is required.

## What it edits
- **Layers** (outliner): Light speed, Digital twin, Airflow, Particular, Clone field, Charging, Orbits, Flora, Vehicle transition, Fibre wave.
  The eye shows or hides a layer, and each layer is one draw call. The **lead** layer (green dot; amber = on but not lead) drives the car body in
  Auto, the backdrop and the caption.
- **Layer inspector**: Visible / Lead / Bokeh tiles.
  - Emission: count 5k–160k, speed.
  - Shape: size, trail, turbulence, plus the look's own variant (Airflow fill or smoke rakes, clone bars or dots).
  - Colour: 8 palettes, then each of the 4 tones.
  - Lens & intensity: depth of field, gain.
  - Timeline: cycle, phase, restart, aim camera, solo, defaults.
- **Scene**:
  - Camera: auto orbit, yaw, pitch, distance, view presets.
  - ControlVehicle: Auto / Paint / Ghost / Glass / Hidden, plus coat and restyle colours.
  - Backdrop: follows the lead layer, or a custom gradient.
- **Post**: Bloom & tone (strength, threshold, exposure). Time (pause, jump, restart).
- **Recipe**: four built-in stacks, plus export and import as JSON. The stacks are:
  - Launch reveal: Vehicle transition + Light speed.
  - Aero study: Airflow + Digital twin.
  - Charging bay: Charging + Clone field.
  - Orbit garden: Orbits + Flora.

  Every edit is kept in `localStorage` (`ParticleEditor.Recipe`).

Keys: 1–9, 0 select a layer · E show/hide · Space pause · R restart · O auto orbit · P columns · V clean view · H readout. Drag the viewport to orbit
and use the wheel to zoom.

URL options: `?fresh=1` (ignore the saved scene), `?recipe=Launch|Aero|Charging|Reference`, `?solo=Flora`, `?select=Camera`, `?time=6`,
`?capture=1` (headless: no animation loop, one frame per seek). Test hook `window.ParticleEditor`: `Select(Id)`, `LoadStock(Key)`, `Solo(Slot)`,
`WriteRecipe()`, `ReadRecipe(Recipe)`, `Seek(Seconds)`.

## Files
| Path | Contents |
|---|---|
| `index.html` | Page: the Frontier Editor skeleton (`#shell`, `#outliner`, `#viewport-column`, `#inspector`, `#status`). |
| `ServeParticleEditor.py` | Static server for this folder (no-store caching). |
| `Source/ParticleEditor.js` | Outliner entries, inspector pages, viewport bar, status bar, recipes, keys, `window.ParticleEditor`. |
| `Source/Particles/FilamentRig.js` | `RaiseRig`: renderer, backdrop dome, glass sphere, car body modes, shared instanced particle buffer, one layer (draw call, uniforms, timeline) per look, orbit camera, bloom. |
| `Source/Particles/FilamentLooks.js` | GLSL prelude, capsule projection and fragment, six looks' `Track` functions, `Looks`, `Palettes`. |
| `Source/Particles/FilamentFibres.js` | GLSL for Orbits, Flora, `SweepJag`, Vehicle transition, Fibre wave. |
| `Source/Particles/SurfaceScatter.js` | Seeded points on the car surface, silhouette profile. |
| `Source/Particles/BloomChain.js` | Half-float target, 5-level dual-filter bloom, ACES, vignette, grain. |
| `Source/Vehicle/VehicleFacets.js` | ControlVehicle from the FCT1 facets (copy of `CabinHmi/Shared/VehicleAssembly.js` with its facet reader, normals and axle mounts brought in). |
| `Source/Interface/EditorControls.js`, `EditorIcons.js` | Frontier Editor inspector builders and lucide glyphs (ISC), copied unchanged from `SlateReference/A/Exhibits/VehicleProvingGrounds/Source/`. |
| `Style/FrontierEditor.css`, `Style/Fonts/` | Frontier Editor stylesheet and DM Sans (OFL), copied unchanged from the same exhibit. |
| `Style/ParticleEditor.css` | The few additions: caption chip, palette chips. |
| `Models/` | `ControlVehicleBody/Tyre/Rim.facet`, copied from `Experimental/Shared/Models/`. |
| `Vendor/three.esm.min.js` | three.js r187 (MIT, `LICENSE-three.txt`). |

The CabinHmi Filament showroom (`Experimental/CabinHmi/Filament/Filament.html`) also runs on `Source/Particles/FilamentRig.js`.

Checks: `Exhibits/ParticleEditor/` (`Code/ParticleEditorCheck.cjs`, `Images/`).

⚠️ Verified only in headless Chromium with the software renderer (swiftshader). The airflow is a look, not CFD.
