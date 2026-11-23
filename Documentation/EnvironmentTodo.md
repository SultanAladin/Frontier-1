# Environment appearance follow-up

Reconstructed from the retained conversation on 2026-10-24; this is not recovery of the original missing document.
These are requested follow-ups; only the explicitly recorded checkpoint below is implemented and verified.

- Preserve the satisfactory Sun, lens flare and ordinary stars.
- Redo the blocky clouds and improve sky appearance.
- Render cloud-shadowed light shafts and illuminated fog, not only ground shadows or a constant-colour overlay.
- Improve the Milky Way, add appropriately licensed stellar catalogues, and deterministic extrasolar populations.
- Check dawn, sunset and the whitish horizon with actual native sequences.
- Add a rendered Moon: observer/time-based position, apparent size, topocentric parallax, phases, orientation,
  licensed surface content, moonlight, atmospheric extinction, cloud occlusion and consistent night exposure.
- Verify lunar rise/set and crescent/quarter/full phases. Eclipse and earthshine fidelity remain separate work.

The recovered `CelestialModel` already contains an approximate lunar-age/illuminated-fraction calculation.
That does not establish that a Moon disc, surface texture or moonlight is connected to the renderer.

## Executed cloud checkpoint — 2026-10-25

- Vulkan per-camera-ray volumetric clouds, a GPU cloud cube for indirect illumination and an observer-local
  Sun-transmittance factor are checked against native references. Clear/cloud/wind/3° low-Sun captures use the
  original ControlVehicle. Actual readbacks and numerical/device scope: `Exhibits/LightCompute/README.md`.
- Existing native Sun, flare and star paths are unchanged. CPU atmospheric precomputation remains; no GPU
  atmosphere, new star catalogue, lunar rendering or observer/time ephemeris is implied.
- Cloud appearance is not final: regular-march horizon structure, approximate isotropic ambient illumination,
  cube seams/resolution and finite-Sun penumbrae still need work. Dawn/night/rise-set sequences remain open.

## Spatial cloud-shadow continuation — 2026-10-25

- Surface and secondary-hit Sun extinction replaces the observer-wide lighting factor. Exact/clustered,
  reservoir/spatial reuse, non-Solar-source preservation and wind-history refresh are checked. Four actual
  ControlVehicle/ground wind captures accompany the native-reference results in `Exhibits/LightCompute/README.md`.
- Fixed the omitted ambient RGB upload from the first cloud checkpoint and strengthened its uniform-medium
  check to require nonzero incident radiance. Updated cloud images must be generated from corrected readbacks.
- Regular cloud stepping, the flat slab, indirect cube seams, physical GPU/Windows acceptance and Moon remain open.

## Shadowed fog and light-shaft checkpoint — 2026-10-25

- Added camera-path single scattering with cloud and geometry visibility, point/IES/spot and directional
  illumination, analytic camera/light optical depth and source-to-surface fog extinction. Earlier cloud density
  was reused, not replaced by a new shape model. Legacy analytic colour fog alone was not enough for shafts.
- Actual 320×200 comparisons and native checks are in `Exhibits/LightCompute/README.md`: broken-cloud soft shafts,
  the original ControlVehicle and local-light cones. Preserve both lighting paths and existing celestial code.
- Remaining: sampling bands/aliasing, temporal fog reconstruction, full inter-bounce/coupled media transport,
  emissive-volume scattering, multiple scattering, physical GPU/Windows acceptance and final sky appearance.

## Fog shadow quadrature refinement — 2026-10-25

Two fixed-camera ControlVehicle/wind fixtures compare 16/32/64/128 shadow samples at 32 view midpoints.
Actual 160×100 readbacks and 936 independent native probes are retained in `Exhibits/LightCompute`.
64-step full-image RGB RMSE against finite 128-step quadrature decreased 96.57% / 96.39% versus 16;
the exhibit preset is now 64. This is 4× shadow samples, not a speed or full-convergence claim.
`Results/FogSampling.json`, `Images/FogSamplingComparison.png` and `RunFogSampling.sh` retain/reproduce it.
Earlier 320×200 evidence remains unchanged at 16 steps. Historical medium prose was incorrect: source and
captures actually used 0.0015/m and 450 m, not 0.0007/m and 650 m. Metadata corrected, images unchanged.
View marching/aliasing, temporal fog, coupled/multiple scattering, final cloud appearance, Moon and physical
GPU/Windows acceptance remain open; preserve both lighting paths and existing Sun/flare/star code.

## Emissive-volume single scattering — 2026-10-25

Shared Slang fog now optionally gathers emissive triangle radiance, not just emitter-to-surface attenuation.
Area/luminance CDF samples use solid-angle conversion, HG phase, triangle visibility and analytic source-to-volume
extinction. Both lighting types remain. Fog[15] selects 0–64 samples; default 0 preserves older presets.
Actual 160×100 original-ControlVehicle/ground captures add an explicit 2×2 m calibration panel (not a new model
or hidden source boost). Isolated emissive-only and combined analytic/emissive volume lighting are retained in
`Exhibits/LightCompute/Images/FogFacetComparison.png` and `FogFacetCombined.png`. 468 capture and 448 sanitizer
native camera probes passed; capture maximum RGB error 1.59192e-5. A 131,072-sample native rectangle integral
matches independent analytic solid angle within 0.0446%. Cold/zero-density/empty-CDF equality, surface isolation,
geometry occlusion and invalid inputs checked; external-driver leak checks disabled.
Reproduce serially with `RunFogFacets.sh`; see exhibit README and `Results/FogFacets.json` for exact scope.
Emissive-volume single scattering is no longer wholly absent, but remains opt-in and finitely sampled. Multiple
scattering, coupled inter-bounce transport, temporal fog, final clouds, Moon and physical GPU/Windows/viewport
acceptance remain open. Sun/flare/stars are unchanged. No speed or real-time claim.

## Working cloud budget and material source decision — 2026-10-25

User requested lower cloud sampling for iteration. Current working fog scenes use cloud32 and fog-shadow32,
self-shadow6 and view32. This supersedes the former 64-shadow working preset. High-budget fixtures remain
explicit reference tools; `RunCloudPreview.sh` defaults to low-only, `COMPARE=1` requests reference renders.
Actual two-wind 160×100 comparison RMSE to finite64: 0.00207406 / 0.00187121; appearance inspected and accepted
for iteration, not final quality. Fresh Release/native tests passed, no fresh sanitizer or hardware-speed claim.
See `Exhibits/LightCompute/README.md` and `Results/CloudBudget.json` / `CloudPreview.json`.

Reuse materials from SultanAladin/Frontier arena/5e581235-frontier, **except rewrite glints/flakes/glitter**.
Pinned source audit at be1d0a8e45d39738f37ccf711522a745e326e149: 452/455 selected files already identical,
including all 399 material archive files and the material/paint shaders. Three newer upstream vector-gradient
files use a different ABI; no partial import. Full layered material evaluation still needs scene integration.
Existing glint research and executed adapter comparison are retained; no physical-GPU winner or completed
rewrite is implied. Licensing-safe implementation, sampling/PDF consistency and temporal/energy checks remain.
Audit/reproduction: `Exhibits/TextureStudio/MaterialReuse.md` and `Code/MaterialReuseAudit.py`.

## Sun disc, lens flare, measured Moon, atmosphere parity — 2026-10-26

- Sun disc redone as production optics: limb darkening + blurred analytic limb, aperture ghosts,
  starburst, anamorphic streak, halo, glow, veiling glare (`LensFlareModel.h`), measured rendered
  in `Exhibits/NightSky` (33/33). The satisfactory ghost placement, Sun position and stars are preserved.
- Moon is now rendered from the measured albedo map in `EngineContent/CelestialMaps` with pole
  orientation, optical libration, Lommel–Seeliger phases, earthshine and atmospheric tint.
  Still open: topocentric parallax, moonlight in scene lighting, relief/Hapke shading, eclipse.
- Atmosphere constants verified identical to the demonstration WGSL; Mie extinction split and
  multiple-scattering magnitude corrected (details in the NightSky exhibit README). GPU sky-view
  form and photographic colour verification remain open, as do clouds (unchanged) and dawn sequences.
