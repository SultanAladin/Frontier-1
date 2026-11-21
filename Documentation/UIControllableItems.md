# UI-Controllable Items — Complete Inventory

Everything the Frontier editor can control today, plus the full backend parameter space that is
available to expose. Use this to adjust your UI prototype before handing it over.

Legend: **[UI]** = wired to a live control in the current editor UI · **[Backend]** = parameter exists
in the engine and takes effect when changed, but has no dedicated control yet (prototype candidate).

---

## 1. Viewport (toolbar, header & footer)

Source: `ViewportDisplaySettings` (`Engine/Components/Panes/ViewportPanel.h`).

| Item | Type | Values / Range | Status |
|---|---|---|---|
| Render Engine | cycle | MatCap · OpenPBR Direct · GTX SDF GI · HighEnd BVH4 · Game Realtime (PT) · Scene Final (PT) | [UI] |
| Quality Tier | segmented | Low · Med · Std · High · Ultra | [UI] |
| Debug Channel | cycle | Off · Depth · Albedo · Shading Nrm · Roughness · Metalness · Cluster ID · Geom Nrm · Sun Shadow · Ambient Occ · Direct Lit · Specular | [UI] |
| Shadow Algorithm | cycle | Contact PCSS · PCSS Plane-Aware · Shadow Ray March · PCF Bilinear · WideTree RayQuery | [UI] |
| SDF Shadow / Penumbra Mode | cycle | 0 Point SDF · 1 Analytic Cone · 2 Voxel Clipmap · 3 Voxel+Cone GI · 4 Hybrid Sobol · 5 MC PT Ref | [UI] |
| Reflection Source | cycle | WideTree Raytrace · Sky Environment | [UI] |
| Trace Resolution | cycle | Full · ½ · ¼ | [UI] |
| Render Scale | slider | 50–200 % | [UI] |
| Reflections Enabled | toggle | on/off | [UI] |
| Anti-Aliasing Enabled | toggle | on/off | [UI] |
| Raytracing Enabled | toggle | on/off | [UI] |
| Atmosphere Sky Enabled | toggle | on/off | [UI] |
| Volumetric Clouds Enabled | toggle | on/off | [UI] |
| Cloud Shadows Enabled | toggle | on/off | [UI] |
| Celestial Stars Enabled | toggle | on/off | [UI] |
| Day Cycle Playing | toggle | play/pause | [UI] |
| Solar Time of Day | slider/step | 0–24 h | [UI] |
| Cloud Cover (Fraction) | slider/step | 0–100 % | [UI] |
| Cloud Wind Speed | slider | m/s | [UI] |
| Camera Fly Speed | mouse wheel | ×0.10 – ×16.0 (footer readout `Fly Speed x.xx`) | [UI] |
| Camera movement | keys | WASD/QE fly, Shift = sprint ×3, Alt+RMB orbit-drag, F = frame selected | [UI] |

## 2. Outliner — World folder rows

Rows: **Sun · Moon · Stars · Volumetric Clouds · Local Cloud** (plus **Ceiling Softbox** under
Lighting when the scene has area lights, and every scene object). Source:
`RegisterShowcaseGalleryInOutliner` / `SynchronizeShowcaseWorldFromOutliner`.

| Row | Eye toggle drives | Shadow toggle drives |
|---|---|---|
| Sun | `Solar.SolarEnabled` | sun cast-shadows |
| Moon | `Moons[0].Visible` (slot 0) | — |
| Stars | `Stars.StarfieldEnabled` | — |
| Volumetric Clouds | `Clouds.CloudsEnabled` | `Clouds.CloudShadow.ShadowEnabled` |
| Local Cloud | `LocalCloud.LocalEnabled` | — |
| Ceiling Softbox (area light) | area-light visibility | — |

## 3. Inspector — generic (every Outliner entry)

Source: `InspectorPanel`.

- **Enabled** (visibility) and **Cast shadows** capability tiles [UI]
- **Transform**: Position (m), Rotation (deg), Scale (×) — numeric fields with per-row reset
  buttons and precision hint [UI]
- Identity readout: title, category label, subtitle, author notes, position summary [UI]

## 4. Inspector — Editor Camera custom cards

Source: `EditorLifecycle` camera Inspector section (mirrors the `Inspectors.jsx` prototype cards).

**Card 1 · Render Settings** [UI]: Engine (cycle, 6) · SDF Shadow (cycle, 6) · Quality
(Low/Med/Std/High/Ultra segmented) · Raytracing (ON/OFF).

**Card 2 · Sky, Sun, Moon & Clouds** [UI]: Solar Time (steps +2 h, wraps 22→6) · Cloud Cover
(steps +20 %, wraps 80→0, also toggles Volumetric Clouds).

## 5. Sun / Solar ephemeris (backend space)

Source: `SolarCelestialConfiguration` (`CelestialEphemerisSolver.h`).

| Parameter | Range / Options | Status |
|---|---|---|
| Solar Enabled | on/off | [UI] via Outliner |
| Local Hours (time of day) | 0–24 h | [UI] |
| Cycle Animation Enabled + speed preset (×1/×8/×30/×100/Custom) + Day Duration | 0.01–168 h | [Backend] (play toggle is [UI]) |
| Angular Diameter | 0.1–5.0° | [Backend] |
| Tint Source (RGB tint / Kelvin) + Manual RGB Tint | — | [Backend] |
| Color Temperature | 1000–40000 K | [Backend] |
| Solar Intensity Multiplier | 0–60× | [Backend] |
| Direct Gain Multiplier | 0–5× | [Backend] |
| Site Latitude / Longitude | −90–90° / −180–180° (default Johannesburg) | [Backend] |
| Date (Day / Month / Year) + UTC Offset | — | [Backend] |
| Manual Direction Override + Azimuth / Elevation | 0–360° / −90–90° | [Backend] |
| Appearance Mode (Muted / Radiant) + Sun Glare Strength | 0–2 | [Backend] |

## 6. Moon — 6 slots (backend space)

Source: `LunarBodyConfiguration` (`LunarStarfieldSequence.h`). Outliner eye controls slot 0 only.

Per slot: Visible · Follow-Sky-Opposite-Sun · Surface Preset (Luna, Ember, Glacier, Sulfur,
Shroud, Shard) · Synodic Phase (0=new … 0.5=full) · Brightness (0–6×) · Halo Glow (0–3×) ·
Angular Diameter (0.1–180°) · Azimuth / Elevation · Surface Roll / Pitch · Has
...[truncated 3881 chars]