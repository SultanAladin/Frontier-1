# UI-Controllable Items — Complete Inventory for the UI Rewrite

Everything the Frontier editor can control today, plus the full backend parameter space available
to expose. Organised by Inspector subject so each section maps 1:1 onto an Inspector card in your
prototype. Give me the adjusted prototype and I will wire the [Backend] items in.

Legend: **[UI]** = wired to a live control today · **[Backend]** = parameter exists in the engine
and takes effect when set, but has no dedicated control yet (prototype candidate) ·
**[New]** = added in the latest camera/SDF pass.

---

## 1. Sun Inspector

Source: `SolarCelestialConfiguration` (`Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h`)

| Control | Type | Range / Options | Status |
|---|---|---|---|
| Solar Enabled | toggle | on/off | [UI] via Outliner eye |
| Time of Day (Local Hours) | slider + play | 0–24 h | [UI] |
| Day Cycle Play/Pause | toggle | play/pause | [UI] |
| Cycle Speed Preset | segmented | ×1 · ×8 · ×30 · ×100 · Custom | [Backend] |
| Day Duration | slider | 0.01–168 h per full day | [Backend] |
| Sun Diameter (Angular Diameter) | slider | 0.1–5.0° (real sun 0.533°) | [Backend] |
| Sun Azimuth | slider/step | 0–360° (Manual Direction Override) | [Backend] |
| Sun Elevation | slider/step | −90–90° (Manual Direction Override) | [Backend] |
| Manual Direction Override | toggle | ephemeris ↔ manual az/el | [Backend] |
| Sun Flare / Glare Strength | slider | 0–2 (Radiant appearance mode) | [Backend] |
| Appearance Mode | segmented | Muted disc · Radiant glare veil | [Backend] |
| Tint Source | segmented | Manual RGB · Kelvin temperature | [Backend] |
| Manual RGB Tint | colour | RGB | [Backend] |
| Colour Temperature | slider | 1000–40000 K | [Backend] |
| Solar Intensity Multiplier | slider | 0–60× | [Backend] |
| Direct Gain Multiplier | slider | 0–5× | [Backend] |
| Site Latitude | numeric | −90–90° (default −26.19 Johannesburg) | [Backend] |
| Site Longitude | numeric | −180–180° (default 28.32) | [Backend] |
| Date (Day / Month / Year) | numeric | 1–31 / 1–12 / year | [Backend] |
| UTC Offset | numeric | −12–+14 h | [Backend] |
| Cast Shadows | toggle | on/off | [UI] via Outliner |

Shadow **quality** lives in the Viewport/Camera render card (§10): Shadow Algorithm, SDF Penumbra
Mode, Quality Tier.

## 2. Moon Inspector (×6 slots)

Source: `LunarBodyConfiguration` (`LunarStarfieldSequence.h`). Outliner eye drives slot 0 only.

Per slot: Visible [UI slot 0 / Backend 1–5] · Follow-Sky-Opposite-Sun toggle · Surface Preset
(Luna · Ember · Glacier · Sulfur · Shroud · Shard) · Synodic Phase 0=new … 0.5=full · Brightness
0–6× · Halo Glow 0–3× · Angular Diameter 0.1–180° · Azimuth 0–360° · Elevation −90–90° · Surface
Roll 0–360° · Surface Pitch −180–180° · Has Atmosphere toggle · Atmosphere Intensity 0–3× ·
Atmosphere Thickness Scale · Atmosphere Tint (colour). All [Backend] except slot-0 Visible.

## 3. Stars Inspector

Source: `StarfieldConfiguration` (`LunarStarfieldSequence.h`)

Starfield Enabled [UI via Outliner] · Scintillation (twinkle) Enabled · Limiting Magnitude 0–8 ·
Brightness 0–4× · Point Size 0.4–3.0× · Scintillation Depth 0–100 % · Scintillation Rate
0.2–3.0 Hz · Celestial Rotation 0–360° · Bright Star Catalog (J2000) toggle · Galactic Band
(Milky Way) toggle · Zodiacal Light toggle · Zodiacal Radiance 0–4×. Rest [Backend].

## 4. Clouds Inspector (planetary deck)

Source: `CloudDeckConfiguration` (`VolumetricCloudSequence.h`)

| Control | Type | Range / Options | Status |
|---|---|---|---|
| Clouds Enabled | toggle | on/off | [UI] |
| Cloud Type (Category) | dropdown | Stratus · Stratocumulus · Cumulus · Cumulonimbus · Altostratus · Cirrus · Lenticular · Mammatus · Kelvin-Helmholtz · Asperitas · Nacreous · Pyrocumulus (12 presets) | [Backend] |
| Cloud Cover (Fraction) | slider | 0–100 % | [UI] |
| Cloud March Steps | slider/step | 16–128 steps | [Backend] |
| Cloud Render Scale | slider | 0.25–1.0× offscreen resolution | [Backend] |
| Extinction (density) | slider | 0–4× | [Backend] |
| Feature Scale | slider | 0.2–3.0× noise wavelength | [Backend] |
| Cloud Floor (base altitude) | slider | 100–15000 m | [Backend] |
| Layer Thickness | slider | 100–6500 m | [Backend] |
| Deck Ceiling | slider | 4000–22000 m | [Backend] |
| Anvil Top Spread | slider | 0–1 | [Backend] |
| Forward Scattering (phase g) | slider | −0.9–0.9 | [Backend] |
| Wind Speed | slider | 0–40 m/s (0 = static) | [UI] |
| Wind Bearing | slider | 0–360° | [Backend] |
| Wind Shear | slider | per km | [Backend] |
| Wind Veer | slider | −60–60°/km | [Backend] |
| Wind Turbulence | slider | 0–1 | [Backend] |
| Follow Wind | toggle | on/off | [Backend] |
| Wind Source | dropdown | CloudWind · coupled sources | [Backend] |

Cloud shadow sub-card (`CloudShadowConfiguration`): Shadow Enabled [UI via Outliner] · Shadow
Category (cloud type) · Shadow Fraction · Shadow Density · Shadow Scale · Shadow Anvil · Shadow
Floor (m) · Shadow Thickness (m). Rest [Backend].

## 5. Local Cloud Inspector (bounded volume, up to 8 GPU volumes)

Source: `BoundedCloudVolume`

Local Enabled [UI via Outliner] · Shape (Box / prism) · Prism Sides · Volume Centre (m, XYZ) ·
Half Extent (m, XYZ) · Yaw 0–360° · Edge Softness · Extinction · Cloud Fraction · Feature Scale
(m) · Forward Scattering g · Follow Wind · Wind Source. Mostly [Backend].

## 6. Atmosphere / Sky Inspector

Source: `AtmosphericMediumConfiguration` (`AtmosphericSkySequence.h`)

Atmosphere Enabled [UI] · Rayleigh Strength 0–4× · Mie Strength 0–6× · Mie Anisotropy g 0–0.99 ·
Ozone Strength 0–4× · Rayleigh Scale Height (m) · Mie Scale Height (m) · Atmosphere Thickness
(m) · Planet Radius (m) · Limb Glow 0–3× · Sky Brightness 0–3× · Sky Tint (colour) · Ground
Albedo (colour) · Orbital View toggle · Observer Altitude (m). Rest [Backend].

Wind (`AtmosphericWindParameters`): Speed (m/s) [partial UI via cloud wind] · bearing/shear —
see solver header for the exact field list when prototyping this card. [Backend]

## 7. Fog & God Rays Inspector

Source: `AtmosphericFogConfiguration` (`VolumetricFogSequence.h`)

Height Fog: density / falloff / start distance / tint fields (solver header). Aerial Fog: aerial
perspective fields (solver header). Local Fog volumes: centre/extent/density list. God Rays:
Enabled · Intensity 0–4× · Anisotropy g 0–0.95 · Decay 0–1 · Density 0–3× · Max Distance
10–2000 m · Sample Steps 8–48 · Interact With Clouds toggle · Interact With Fog toggle · Shaft
Tint (colour). [Backend]

## 8. Lens Flare Inspector (camera-attached)

Source: `LensFlareConfiguration`

Enabled · Style (Anamorphic · Streaks · Halos) · Flare Intensity 0–3× · Flare Spread 0.2–2.0× ·
Halo Intensity 0–2.5× · Ghost Intensity 0–2.5× · Ghost Spacing 0.2–1.8× · Ghost Count 0–16 ·
Ghost Sides (0=round, 6=hex, 8=oct) · Streak Rotation 0–360°. [Backend]
(Physical `CameraComponent` lens effects — flare/DoF/vignette/chromatic aberration/motion blur —
live in `CameraConfiguration`, §9.)

## 9. Editor Camera Inspector — optics & movement

Sources: `PerspectiveCameraBasis` (live editor camera), `CameraConfiguration` / `CameraComponent`,
`CameraLagComponent` **[New]**, `EditorLifecycle` fly controls.

Movement (live today, one Unreal model): RMB mouse-look · WASD/QE fly · Shift sprint ×3 · Ctrl
slow ×0.3 · MMB / LMB+RMB pan · Alt+LMB orbit · Alt+RMB dolly · Wheel = fly speed ×0.10–×16
(footer readout) · Arrow keys + PgUp/PgDn flight · F frame selected · W/E/R/Q gizmo mode keys
(when not flying).

| Control | Type | Range | Status |
|---|---|---|---|
| Camera Lag Enabled (location) | toggle | on/off | [New][Backend-UI] |
| Camera Lag Enabled (rotation) | toggle | on/off | [New][Backend-UI] |
| Location Lag Speed | slider | 1/s (higher = tighter) | [New][Backend-UI] |
| Rotation Lag Speed | slider | 1/s | [New][Backend-UI] |
| Maximum Lag Distance | slider | m (0 = uncapped) | [New][Backend-UI] |
| Fly Speed Scalar | wheel | ×0.10–×16 | [UI] |
| Focal Length | slider | mm (thin-lens) | [Backend] |
| Sensor Gate | dropdown | Vista 35 mm · IMAX 70 mm · … | [Backend] |
| Vertical FOV | slider | derived from focal/gate | [Backend] |
| Aperture f-stop | slider | f/0.95–f/32 | [Backend] |
| Focus Distance | slider | m | [Backend] |
| Near / Far Clip | numeric | m | [Backend] |
| Operating Mode | dropdown | Orbit · Fly · FirstPerson · Follow · Spline · Planar (6) | [Backend] |
| Projection | dropdown | Perspective · Orthographic (+ Top/Front/Side presets) | [UI presets] |
| Orbit/Pan/Zoom Smoothing Rates | sliders | 1/s | [Backend] |
| Fly Speed / Sprint Multiplier | sliders | m/s · × | [Backend] |
| Pitch Limit | slider | ±87° pole guard | [Backend] |
| Follow Spring Frequency | slider | rad/s | [Backend] |
| Depth of Field / Vignette / Chromatic Aberration / Motion Blur / Heat Shimmer / Glitch | sub-cards | per-effect fields in `CameraConfiguration` | [Backend] |

## 10. Editor Camera Inspector — Render Settings card + Viewport header

Source: `ViewportDisplaySettings` (`ViewportPanel.h`)

| Control | Type | Values | Status |
|---|---|---|---|
| Render Engine | cycle | MatCap · OpenPBR Direct · GTX SDF GI · HighEnd BVH4 GI · PT Game Realtime · PT Scene Final | [UI] |
| Quality Tier | segmented | Low · Medium · Standard · High · Ultra | [UI] |
| Debug View | dropdown | 00 Lit · 01 Depth · 02 Albedo · 03 Shading Normal · 04 Roughness · 05 Metalness · 06 Cluster ID · 07 Geometric Normal · 08 Sun Shadow · 09 Ambient Occlusion (filtered) · 10 Direct Lighting · 11 Specular · **12 SDF Distance · 13 SDF with Materials · 14 SDF Normal · 15 SDF AO Raw (unfiltered) · 16 SDF Cascade** [New] | [UI] |
| Shadow Algorithm | cycle | Contact PCSS · PCSS Plane-Aware · Shadow Ray March · PCF Bilinear · WideTree RayQuery | [UI] |
| SDF Shadow/Penumbra Mode | cycle | Point SDF · Analytic Cone · Voxel Clipmap · Voxel+Cone GI · Hybrid Sobol Cap · SDF MC Ref 96 spp | [UI] |
| Reflection Source | cycle | WideTree Raytrace · Sky Environment | [UI] |
| Trace Resolution | cycle | Full · ½ · ¼ | [UI] |
| Render Scale | slider | 50–200 % | [UI] |
| Reflections | toggle | on/off | [UI] |
| Anti-Aliasing | toggle | on/off | [UI] |
| Raytracing | toggle | on/off | [UI] |
| XeGTAO (PBR AO, no GI) | toggle | on/off | [New][UI] settings sheet |
| XeGTAO Strength | slider | 0–2× | [New][Backend-UI] (value plumbed) |
| Atmosphere Sky | toggle | on/off | [UI] |
| Volumetric Clouds | toggle | on/off | [UI] |
| Cloud Shadows | toggle | on/off | [UI] |
| Celestial Stars | toggle | on/off | [UI] |
| Split Viewport | toggle | single / dual + per-pane projection | [UI] |
| Diagnostics Overlay | toggle + tabs | Timing · Scene · Render · Profiler | [UI] |
| Execution Mode | segmented | Edit · Simulate · Play/Pause | [UI] |

## 11. Material Inspector (selected object)

Source: `SurfaceMaterial` / `CompactMaterialRegister` (authored `.MaterialSpace`)

Albedo (colour) · Roughness · Metalness · Dielectric IOR · Anisotropy · Edge Tint (F82 colour) ·
Coat Weight · Coat Roughness · Transmission Weight · Abbe Dispersion · Emissive (blackbody
radiance/temperature). Mostly [Backend] in the World viewport (full editing lives in the Material
workspace); MatCap preset selection is [UI] via Content Browser drop.

## 12. Lights Inspector

Point lights: Position · Range (m) · Peak Intensity · IES photometric profile (LM-63) · Beam
axis/radius. Area (quad) lights: Centre · Half Width/Height · Right/Up axes · Two-sided toggle ·
Emissive Radiance (colour) · Ceiling Softbox preset [UI via Outliner visibility]. Sun = §1.
[Backend] for numeric fields.

## 13. Transform & Gizmo (selected object)

[UI]: Position (m, XYZ) · Rotation (deg) · Scale (×) numeric fields with reset · Enabled toggle ·
Cast Shadows toggle. Gizmo: Universal/Translate/Rotate/Scale handle filter [UI keys W/E/R/Q] ·
Ctrl snap: Move Step 0.25 m · Scale Step 0.10 · Rotation Step 5° (`GizmoSnapSettings`) [Backend]
· Gizmo hover = GPU pick with CPU ray fallback [New].

## 14. Project Browser

[UI]: **Default Project only** in the library (others removed; no Cornell box anywhere) · **+ New
Project → blank scene (single ground plane + clear sky), opens in Frontier only** — no UV /
Texture / other-workspace launch buttons · Create & Open in Frontier · Open existing
`.projectspace` / path entry · Search · Version (revision) selector.

## 15. Diagnostics & Thought Log [New]

Viewport Diagnostics overlay [UI]: frame-time sparkline, per-stage GPU/CPU ms (viewport raster,
gizmo overlay, ImGui present, host frame), scene/render telemetry.
**Thought Log** [New]: every timed scope (Host.Frame, Host.EditorTick, Editor.Tick,
Editor.ViewportRaster, Editor.ViewportInteraction, Editor.GizmoOverlayRefresh,
Editor.ImGuiPresent, Render.WorldViewportRgba8, Render.WorldViewportWithEngine, + any
`FRONTIER_THOUGHT_SCOPE`) is recorded **in RAM only** (lock-free ring + background aggregator
thread; zero disk I/O while running) and written **once when the application closes** to
`Logs/FrontierThoughtLog.txt` with calls / total / avg / min / max / P50 / P95 per scope — use it
to target optimisations.

---

## 16. Debug Views — every channel, what it shows, values it requires

Source: `ViewportDisplaySettings.DebugChannelIndex` (`ViewportPanel.h`) → `WorldRenderConfiguration.DebugChannel`
→ shader `LightGlassMaterialDebug.w`. Control: header dropdown `00–16` + Settings sheet dropdown. All [UI].

| # | View | What you see | Requires (engine state / buffers) | Value meaning |
|---|---|---|---|---|
| 00 | Lit (Off) | Final ACES-tonemapped render | Full lighting path | — |
| 01 | Depth | Greyscale linearised depth | Primary hit distance | `pow(saturate(1 − dist/11 m), 1.8)`; white = near |
| 02 | Albedo | Raw diffuse base colour | Material register | sRGB albedo 0–1 per channel |
| 03 | Shading Normal | Smooth shading normal | Interpolated normals | `N×0.5+0.5` RGB |
| 04 | Roughness | Greyscale specular roughness | Material `SpecParams.x` | 0 = mirror … 1 = matte |
| 05 | Metalness | Greyscale metalness | Material `SpecParams.y` | 0 = dielectric … 1 = metal |
| 06 | Cluster ID | Per-material false colour | Material slot id | Hash colour per slot+1 |
| 07 | Geometric Normal | Flat face normal | Triangle geometry | `N×0.5+0.5` RGB |
| 08 | Sun Shadow | Filtered sun visibility | Shadow pass + spatial filter | 0 = shadowed … 1 = lit |
| 09 | Ambient Occlusion | **Filtered** SDF cone AO (à-trous) | GDF ready (`GdfReadyMask`), engines 1–2; BVH AO on engine 3 | 0 = occluded … 1 = open |
| 10 | Direct Lighting | ACES(direct sun + area + IES only) | Direct pass, no GI/specular | HDR direct radiance |
| 11 | Specular | ACES(reflections × specular AO) | Reflection source (BVH4 or sky) | HDR specular radiance |
| 12 | SDF Distance [New] | Signed-distance heat at hit | GDF baked + ready | Red inside (<0) · green at surface (0) · blue ≥ 10 voxels away |
| 13 | SDF with Materials [New] | Albedo lit by SDF gradient normal | GDF + materials + sun direction | Albedo × (0.18 + 0.82 × wrap N·L) |
| 14 | SDF Normal [New] | GDF gradient normal (central differences, ε = 0.75 voxel) | GDF baked + ready | `∇SDF×0.5+0.5` RGB |
| 15 | SDF AO Raw [New] | Cone AO **before** filtering | Same as 09, bypasses filter | 0–1; compare vs 09 to separate AO noise from filter blur |
| 16 | SDF Cascade [New] | Which GDF cascade covers the hit | Both 64³ cascades baked | Green fine · orange coarse · red outside both |

Notes for your prototype: channels 12–16 need a "GDF not baked" fallback state in the UI
(shader returns albedo/black today); glass pixels render black on 01–07/12–16 (pre-existing);
CPU Tiered path implements 13/15 only.

## 17. Diagnostics overlay (F3) — readouts & values they require

Source: `ViewportPanel::RenderDiagnosticsPanel` + `ViewportTimingBreakdown`
(`Engine/Components/Diagnostics/ViewportTimingTelemetry.h`). Toggle: header button / F3.
Panel 324×432 px, 4 tabs, pause button freezes sampling. History: 120 samples ring buffer,
sparkline shows 60, target line at 16.67 ms (60 fps).

| Tab | Readout | Unit | Derived from |
|---|---|---|---|
| Timing | Current frame time (big) | ms + fps | Latest Δt; fps = 1000/avg |
| Timing | Average / Minimum / 95th pct / Maximum | ms | Sorted 120-sample history (P95 = index 95%) |
| Scene | Constructed slots (big) | count | Outliner constructed entries |
| Scene | Visible / Hidden | count | Constructed − visible |
| Render | Render Scale (big) | % | `RenderScalePercent` 50–200 |
| Render | Quality tier name | label | Low/Med/Std/High/Ultra |
| Profiler | Frame total (big) | ms | `TotalFrameMs` (or current Δt fallback) |
| Profiler | Resolution + Engine/Shadow labels | px + labels | `RenderWidthPx×RenderHeightPx`, engine/shadow enums |
| Profiler | GPU World (primary trace) | ms | `GpuPrimaryTraceMs` (Vk timestamps) |
| Profiler | GPU Denoise | ms | `GpuDenoiseMs` |
| Profiler | GPU Clouds+Sky | ms | `GpuCloudsMs + GpuSkyLutMs` |
| Profiler | GPU Readback | ms | `GpuReadbackCopyMs` |
| Profiler | GPU Frame Total | ms | `GpuTotalPassMs` |
| Profiler | UI + Gizmo | ms | `DesktopPollAndUiMs + GizmoOverlayMs` |
| Profiler | Present / Swap | ms | `SwapchainPresentMs` |
| Profiler | Scene sync (footer) | ms | `SceneSyncGpuMs`; log path label `FrontierTiming.log` |

Other timing fields captured but **not yet displayed** (prototype candidates): `FrameIndex`,
`HardwareGpuActive`, `GpuTimestampsValid`, `WorldDispatched`, `AdapterName`, `EditorTickMs`,
`GpuHostSubmitWaitMs`, `TimingLogPath`.

**Thought Log** (RAM → `Logs/FrontierThoughtLog.txt` at close): scopes `Host.Frame`,
`Host.EditorTick`, `Editor.Tick`, `Editor.ViewportRaster`, `Editor.ViewportInteraction`,
`Editor.GizmoOverlayRefresh`, `Editor.ImGuiPresent`, `Render.WorldViewportRgba8`,
`Render.WorldViewportWithEngine`. Per scope: Calls · Total ms · Avg µs · P50 µs · P95 µs ·
Max µs · Share %. Constants needing possible UI: ring capacity 1M samples, reservoir 1024/scope.

## 18. Other settings NOT in the Inspector that need UI (with required values)

### 18.1 Viewport header / toolbar (all [UI] today, keep in prototype)
Execution Mode: Edit · Simulate · Play/Pause (segmented) · Split Viewport toggle + Active Pane
(0/1) · Primary/Secondary Projection per pane: Perspective · Orthographic · Top · Front ·
Right · Navigation capsule: Zoom In / Zoom Out / Fit View · Construct sheet open + category
rail (All / Scene Primitives / Organization) + search text · Scene Title (text) · Probe/footer
readout: `Fly Speed ×N.NN [· Sprint ×3]` (text) · Settings sheet open · Diagnostics open (F3).

### 18.2 Viewport Settings sheet (gear) — exact controls & ranges
Quality segmented Low/Med/Std/High/Ultra (0–4) · Toggles: Reflections, Anti-Aliasing,
Raytracing, **XeGTAO (PBR AO)**, BVH4 Specular Trace (= Reflection Source WideTree/Sky),
Physical Sky, Volumetric Clouds, Cloud Shadows, Sun/Moons/Stars, Day Cycle · Trace Resolution
segmented Full / ½ / ¼ · Render Scale slider **50–200 % in 5 % steps** · Dropdowns: Engine (6),
Shadow (5), SDF Penumbra (6), Debug View (17) · Time of Day stepper (hours/minutes from
`SolarTimeOfDayHours` 0–24 h) · Cloud Cover / Wind steppers (see §4).

### 18.3 Settings that exist in code but have NO control yet [Backend-UI candidates]
| Setting | Field / location | Type | Range / default |
|---|---|---|---|
| XeGTAO Strength | `XeGtaoStrength` | slider | 0–2×, default 1.0 (plumbed, no slider yet) |
| XeGTAO Search Radius | shader constant | slider | fixed 0.85 m (4 slices × 2 sides × 4 steps fixed) |
| SDF AO Cone Radius | shader `Vox0×16` clamp | slider | 0.40–2.75 m effective |
| SDF AO Strength | shader `×0.94` remap | slider | fixed 0.94, floor 0.06 |
| Path Trace Samples | `PathTraceSamples` | stepper | default 4 spp (engines 4–5) |
| GDF Grid Resolution | shader `GdfGridRes` | dropdown | fixed 64³ ×2 cascades (rebuild cost) |
| GDF Fine/Coarse Span | `GlobalDistanceFine/CenterSpan` | numeric m | scene-dependent, auto-centred on camera |
| Camera Fly Base Speed | fly solver constant | slider | 6.0 m/s × FlySpeedScalar (0.10–16) × sprint 3 / slow 0.3 |
| Gizmo Snap Steps | `GizmoSnapSettings` | numeric | Move 0.25 m · Scale 0.10 · Rotate 5° (Ctrl-drag) |
| Editor Max Step | `MaximumStep` | numeric s | 0.100 s clamp |
| Window Size | `WindowDescriptor` | numeric px | 1600×980 default |
| Diagnostics History | `IntervalHistoryMs` | stepper | fixed 120 samples |
| Thought Log Capacity | ThoughtLog ring | stepper | fixed 1M samples / 1024 reservoir |

### 18.4 Control Centre (top shade, not Inspector)
Interface Scale **75–150 %** (`InterfaceScale/100`, clamped 0.75–1.5) · Theme: 8 colour schemes
+ corner radius + accent/semantic swatches (`ThemeRepository`) · Typography size/weight
repository · Appearance tab (Display scale / Typography / Theme). Values: scale %, theme index,
radius px — see `ControlCentrePanel` AppliedAppearance.

---

## 19. Path Tracing convergence (Viewport Settings, PT engines only) [New]

Engines 4 (PT Game Realtime) / 5 (PT Scene Final) now render **continuously until converged**:
the editor re-dispatches every frame, the shader running-mean accumulates, and accumulation
resets on camera move, scene edit, or any display-settings change. Footer readout:
`PT N / Target spp [· converged]` or `PT N spp · continuous`.

| Control | Type | Values | Status |
|---|---|---|---|
| PT Target SPP | segmented | 64 · 128 · 256 · 512 · 1024 · **Inf** (0 = no cap, render forever) | [New][UI] |
| PT Batch / Frame | segmented | 1 · 2 · 4 · 8 spp per frame (HTML lab `SampleBatch` parity) | [New][UI] |

Backend fields: `PathTraceTargetSpp`, `PathTraceBatchSpp` (`ViewportDisplaySettings`) →
`PathTraceTargetSpp`, `PathTraceSamples` (`WorldRenderConfiguration`). CPU Tiered PT uses the
batch per refresh; continuous dispatch is GPU-path today.

## 20. Nanite-like Cluster LOD bake (per asset) [New]

Pipeline: `PolygonNetwork` → ≤128-tri Morton clusters → QEM levels with seam/border locks
(`ClusterPyramid`). Per-asset recipe `ClusterLodBakeConfiguration`:

| Control | Type | Values | Status |
|---|---|---|---|
| Bake Maximum Levels | stepper | 1–8 levels incl. LOD0 | [Backend-UI] (`MaximumLevels`) |
| **Last Level (Coarsest Allowed)** | stepper | 0 = finest only … N; glass assets ship capped at **1** | [New][Backend-UI] (`CoarsestAllowedLevel`) |
| Lock Material Boundaries | toggle | on: vertices shared by faces of different material slots never collapse | [New][Backend-UI] |
| Runtime Pixel Error | slider | `MaxAllowedPixelError`, default 1.25 px (cut selection) | [Backend-UI] |
| Normal-Cone Backface Cull | toggle | cut option | [Backend-UI] |

Guarantees: every surviving triangle keeps its own face material slot at every level
(boundary vertices locked, collapse only remaps vertex indices); the runtime monotonic cut
never activates a cluster coarser than the asset's last level (capped clusters become
terminal). Bake report per level: clusters / triangles / max error (m) / material slots
present (`SummarizeLevels()`), serialized with the pyramid (format v2; v1 loads).

### 20.1 Viewport status update — Nanite LOD is LIVE in the editor viewport

Previously only the CPU Tiered renderer used the cut; the editor's Vulkan viewport traced the
full-resolution BVH. Now `VulkanWorldRenderSequence` rebuilds its GPU BVH from the same
per-camera `SelectMonotonicCut` whenever the active cluster set changes (signature-cached).
Controls in the viewport Settings sheet: **Nanite LOD** On/Off toggle and **LOD Error (px)**
0.5 / 1.25 / 2.5 / 5 (`ClusterLodEnabled`, `ClusterLodPixelError` →
`NaniteLodEnabled`, `NanitePixelError`). Viewport footer readout:
`Nanite LOD N tris · M clusters (full K)`. Glass instances stay analytic (not in the BVH),
per-face material slots ride every pyramid triangle. Baker now welds soup vertices by exact
position and derives border locks from welded topology (unlinked soup attribute flags are
ignored), so decoded assets like the ControlVehicle bake real levels:
25,068 → 13,994 → 10,270 tris with 10 material slots at every level.
Proof: `NaniteViewportVehicleValidation` + `Exhibits/Images/NaniteViewport/` PNGs
(near 19,846 tris traced, far 11,418 with L2 dominant, LOD-off restores 25,080).

### 20.2 Colour debug visualizations (Debug View menu)

Two new viewport debug channels (GPU + CPU renderers), fed by per-triangle data packed into
the shaded-triangle pad lanes (level in `Corner0Pad.w`, cluster ordinal in `Edge01Pad.w`):

| Channel | View | Colours |
|---|---|---|
| 17 | **Nanite LOD Level** | L0 green · L1 yellow · L2 orange · L3 red · L4+ purple |
| 18 | **Nanite Clusters** | one vivid colour per cluster (hashed ordinal, form-shaded) |

Proof sheet `Exhibits/Images/NaniteViewport/VehicleLodContactSheet.png` (rows Near/Far,
columns Lit / LOD Level / Clusters): near is L0-green with L1 yellow at the wheels, far is
L1/L2 yellow-orange; cluster patches grow with distance. Triangle density is visible as
cluster-patch size + the footer triangle readout.

### 20.3 Material-pure clusters + face identity

Clustering is **material-partitioned**: LOD0 sorts triangles by (material slot, Morton) and
starts a new cluster at every material change; coarser merge groups only combine clusters of
the same material slot that are also spatially adjacent. Result: **no cluster is ever shared
between materials** — one material owns many clusters (128-tri cap), never a mixed one.
Every triangle carries its source face identity (`SourceFaceIndex`) and that face's material
slot (`MaterialOrdinal`) at all levels; debug channel 6 (Material / Cluster ID) shows each
face filled with its slot colour. Small materials with a single L0 cluster stay terminal
(they never coarsen) — intentional protection for small parts. Vehicle L0 clusters per
material are printed by `NaniteViewportVehicleValidation` (slot 6 body: 124 clusters,
slot 7: 41, glass/trim slots: 1–13 each); homogeneity is asserted for every cluster at
every level in both LOD validations.
