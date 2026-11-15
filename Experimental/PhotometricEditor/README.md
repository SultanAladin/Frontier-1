# Frontier · Photometric & Light Studio (`Experimental/PhotometricEditor`)

Unified light, spot light, IES goniometric distribution, and environment lighting studio combining **Project Zero** light & environment inspectors with real **LM-63 Type-C `.ies`** photometry and an interactive Three.js WebGL2 ACES photometric calibration stage.

## Coverage

- **7 Physical Luminaire Inspectors (`Project Zero` `LightPanel` + `LightProjection`):**
  - **Point Light (`pointlight`)**: Intensity (`cd`), distance reach (`m`), inverse-square/exponential decay (`1/d^n`), 1/2/5/10 m analytical distance table.
  - **Spot Light (`spotlight`)**: Beam envelope (`cd`), half-cone angle, penumbra softness, beam section diameter @ 5 m.
  - **IES Photometric (`ieslight`)**: All 8 Project Zero analytical distributions (`Downlight`, `Wall wash`, `Batwing`, `ECE Low Beam`, `SAE Low Beam`, `High Beam`, `Fog Lamp`, `Parking Lamp`), all 7 measured LM-63 `.ies` profiles (`Profiles/*.ies`), custom `.ies` upload/export, cut-off pitch (`-5..+5°`), C0°/C90° dual polar candela web, and live projected `SpotLight.map` goniometric texture in 3D.
  - **Area / Softbox (`arealight`)**: Rectangle/Disk aperture, dimensions, cosine spread, two-sided emission, aperture exitance (`lm/m²`).
  - **Tube Light (`tubelight`)**: Linear lumen density (`lm/m`), radius (`mm`), Kelvin chromaticity.
  - **LED Emitter (`ledlight`)**: Driver watts, efficacy (`lm/W`), dimmer, COB phosphor schematic.
  - **LED Strip (`ledstrip`)**: Ribbon flux/load per metre, emitter density (`/m`), voltage, exposed-die vs opal diffuser, electrical load (`W` / `A`).
- **7 Environment Light & Optical Inspectors:**
  - **HDRI / Sky Dome (`envdome`)**, **Directional Sun (`sun`)**, **Lunar Bodies (`moon` — 6 worlds + phase/horizon dial)**, **Star Field (`stars` — magnitude filter + twinkle waveform)**, **Lens Flare (`lensflare` — interactive optical axis)**, **Atmosphere & Height Fog (`atmosphere`)**, **Post Exposure & False-Colour Isolux (`post`)**.
- **Interactive 3D Stage & Tools:**
  - Lenses: `ACES PBR`, `False-Colour Lux`, `Goniometer`, `Homologation`.
  - Serve locally via `python3 "Experimental/PhotometricEditor/Serve.py"` (`http://0.0.0.0:8084/Experimental/PhotometricEditor/`).
  - Run validation via `node "Experimental/PhotometricEditor/PhotometricValidation.mjs"`.

## Multi-view lighting render proofs — 2026-11-09

Four crops show the Photometric Studio's viewport stage in Orbit, Top Plan, Wall Screen, and Roadway camera presets; the fifth capture is the full 1920×1080 workspace window. They use one roadway lighting rig with the ACES PBR lens. Captured through the live Three.js WebGL2 renderer on SwiftShader: 3/8 luminaires active, 5.19 klm, 28.3 lx at screen, no GL errors, lost context, or browser errors.

| Region | Evidence |
|---|---|
| Orbit viewport | [`01-OrbitViewport.png`](Images/RenderProofs-2026-11-09/01-OrbitViewport.png) |
| Top Plan viewport | [`02-TopPlanViewport.png`](Images/RenderProofs-2026-11-09/02-TopPlanViewport.png) |
| Wall Screen viewport | [`03-WallScreenViewport.png`](Images/RenderProofs-2026-11-09/03-WallScreenViewport.png) |
| Roadway viewport | [`04-RoadwayViewport.png`](Images/RenderProofs-2026-11-09/04-RoadwayViewport.png) |
| Full workspace window | [`05-WorkspaceWindow.png`](Images/RenderProofs-2026-11-09/05-WorkspaceWindow.png) |

These images document the separate browser editor and its bundled measured LowBeam profile; they do not exercise the native C++ import dialog. The native Type-C importer and Photometric Workspace integration are validated independently by `PhotometricWorkspaceValidation` and `Exhibits/PhotometricWorkspace/Fixtures/TypeC_Imported.ies`.
