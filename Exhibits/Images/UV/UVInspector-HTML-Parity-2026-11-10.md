# UV Inspector HTML parity — 10 November 2026

## Completed in this pass

- Rendered and saved the HTML reference states for Unwrap, UDIM, Shading, and Check (`.reference-*.png`).
- Updated the native Unwrap renderer with transform controls, unwrap/stacking/projection actions, angle threshold, Live Unwrap, and seam operations.
- Rebuilt the UDIM tab around independent metric cards, tile controls, resolution selection, and backdrop presets (Color, UV Code, Checker, Slate).
- Rebuilt Check around separate overlap/flipped/stretch KPIs and UV integrity/repair actions.
- Reworked Shading to use the reference's material hero and 2×2 telemetry cards, followed by native MatCap and material-finish controls.
- Corrected the Inspector header/body vertical rhythm and replaced unsupported text glyphs in Shading KPIs with repository vector icons.
- Captured the native states in `UVInspector-{Unwrap,UDIM,Shading,Check}-native.png`.

## Verification

`cmake --build build/Development --target FrontierUV -j2` succeeds. Each of the four native tab states was separately selected, built, and captured; the session header was restored to its default Packing tab afterward. `git diff --check` is clean. No GitHub Actions were run.

## Layout polish follow-up

- Standardized the UV workspace dropdowns on the Perspective control: shared icon/caret button, option sheet, 31px control height, and consistent spacing (Inspector, Outliner, and Shading selector).
- Fixed slider rows to reserve the full label + value/track height, spaced each row consistently, and adjusted the metric-card gutters/type size to prevent label clipping and crowding.
- Refreshed native Packing, Shading, and Check captures: `UVInspector-{Packing,Shading,Check}-native.png`.

## MatCap parity follow-up

- Baked the exact 16-slot pixel maps from `Experimental/UVEditor/Engine/EngineeringMatcaps.js` into `EngineContent/MatCaps/MatCapStudioAtlas.png` (reviewable atlas) and `MatCapStudioAtlas.rgba` (runtime upload data), with a slot-name catalog JSON.
- The native MatCap menu now draws thumbnails from the shared atlas instead of hand-painted approximate circles.
- The 3D viewport now samples the same atlas. Surface Material remains the default mode; the Inspector Shader control and viewport shading cycle expose Surface Material, MatCap (Per-Face), and Material + MatCap modes.
- `UVMatCapPreview-native-2026-11-10.png` shows the MatCap viewport and Inspector menu together.

## Remaining phases

1. Compare the three native material modes interactively against the equivalent HTML modes and refine lighting/tonemapping only where the raster pipelines still diverge.
2. Align KPI metric semantics (HTML angle-stretch and texel-density values versus the native GPU summary) and do a final pixel-level spacing/type pass.
3. Continue remaining UV workspace parity work, then run the project's broader validation suite before release.
