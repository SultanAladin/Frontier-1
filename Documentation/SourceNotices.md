# Selective UI Port — Source Notices

Imported Frontier source and SVGs originate from `SultanAladin/Frontier-` at
`338293863d7df4e585dec0c4acae45fe9c524ed5`. Exact imported paths and hashes are in `ImportManifest.json`.
No project-wide licence was present in that imported Frontier snapshot. This port does not invent a licence grant.
The owner must establish applicable permissions before external redistribution or relicensing.

Original icon attribution and compatibility notes remain in `EngineContent/Icons/Slate-NOTICE.md` and
`EngineContent/Icons/ThorVG/manifest.json`. These are inherited provenance records, not local regeneration instructions:
referenced upstream gallery scripts and sources have not all been ported. Manifest reference paths identify
upstream originals; they are not promises of files in this checkout.

Lucide-related paths retain the upstream ISC notice in `Engine/Interface/Lucide.NOTICE`.
ImGui is pinned at `3bae66c735670619baf51391eba7f3d90a25d125` with its MIT licence.
ThorVG is pinned at `6715f99ac106b6d4587f384a78c59fe957cfd2a3` with its MIT licence.
Downloaded archives preserve their licence files. The adapted official Win32/DX11 exhibit retains ImGui's
licence at `Exhibits/UIFoundation/Code/ImGui.LICENSE`; dependency notices are included by Windows installation.
Third-party code is not represented as original Frontier work.

## Lunar ephemeris

`Frontier/Engine/Rendering/CelestialModel.cpp` adapts Sonia Keys' Meeus chapter 47 Moon-position series and
coefficient tables from `github.com/soniakeys/meeus` revision `bfbd9ac2c7f709c94f1dee190c633d24a723f628`.
Copyright 2013 Sonia Keys; the complete upstream MIT notice is retained at
`Documentation/Notices/SoniaKeysMeeus.LICENSE`. JPL DE421 (installed via `skyfield-data`) and Astropy/jplephem
are used only as offline validation dependencies; they are not copied or bundled into the engine.

The inherited six ImGui patches were originally cut against `83f6686`; they apply cleanly to the locked `3bae66c`
pin used here. ThorVG's staged initializer adjustment is inherited and leaves its downloaded source untouched.

## Control Centre and fonts

The subsequent appearance port adapts `AppearanceInspector`, `FontCodec`, and `TypefaceRegistry` from the same baseline.
The exhibit-only CPU command rasterization fixture originates from `Exhibits/Workbench/IconArt/CpuDraw.h` there.
The UI-only Control Centre deliberately excludes the old host's renderer/material/fidelity dependencies.

Font downloads are unmodified Inter, Archivo, Space Grotesk, and JetBrains Mono static faces from that baseline.
`Automation/FontSources.json` pins each font and licence file by Git blob and SHA-256.
Each family's SIL Open Font License 1.1 notice is retained beside the downloaded fonts and in the Windows bundle.
General Sans and Clash Display are not fetched as part of this open-font selection.
Toml++ is pinned in `Automation/UIDependencies.json`; its MIT licence is retained with the download and installation.
Zig 0.13.0 is used only as the cross-toolchain; its downloaded distribution stays outside Git in ignored scratch.
No source licence is replaced or extended by the generated binary package.

The drawer correction additionally adapts the same baseline's `ControlCentreHost.{h,cpp}` geometry and gestures
and imports `MotionIntegrator.{h,cpp}` for bounded spring integration. Renderer controls are excluded.
GLFW 3.4 is revision/SHA-pinned in `Automation/UIDependencies.json`; its zlib/libpng licence is retained in
`ExternalPackages/glfw/LICENSE.md` and copied into the Windows bundle as `Notices/GLFW-LICENSE.md`.
Original SVG filenames may retain historical `asset-` prefixes to preserve artwork provenance;
production icon identifiers now use Content terminology.

## Vulkan UI presentation

Vulkan UI presentation adapts the pinned ImGui GLFW/Vulkan example's presentation sequence and reuses its adapter.
ImGui's MIT notice is retained and installed. Vulkan-Headers v1.4.321 is archive/SHA-256 pinned in
`Automation/UIDependencies.json`; its original `LICENSES/` directory is retained under ExternalPackages and installed
under `Notices/Vulkan-Headers/`. No Vulkan loader or driver binary is bundled. The runtime uses GLFW's system loader.


## Material PNG/JPEG ingestion

`Engine/Render/SurfaceMaterial.cpp` uses `stb_image` from `nothings/stb`, pinned at
`2c980bb59875b0d32144a71867fbdebb2f77cd20`. Archive SHA-256 and provisioning URL are recorded in
`Automation/UIDependencies.json`. The upstream dual MIT/public-domain licence remains in
`ExternalPackages/stb/LICENSE` and is installed as `Notices/Stb-LICENSE.txt`. Sources are provisioned,
not copied into authored Engine code. PNG and JPEG only are enabled; texture files remain authored content.

## Texture Studio DM Sans faces

The Material workspace additionally uses the same DM Sans static faces as its HTML reference,
`@fontsource/dm-sans` 5.3.0 (SIL OFL 1.1). `Automation/FontSources.json` pins the npm archive/member paths and
recovered TTF checksums. `PrepareFonts.py` losslessly unwraps WOFF to TTF using pinned fontTools; the original OFL
notice is retained beside the faces. This does not install OS fonts or redistribute a Linux application package.

### Scoped native editor/workspace icons

Shared editor line icons under `EngineContent/Icons/Editor` reuse Lucide-compatible primitives (MIT notice retained
in `Engine/Interface/Lucide.NOTICE`). Texture UV/paint/decal/flake/clearcoat and Fluid water line artwork is authored
for this integration. Scoped copies of the existing Fluid/smoke/water-folder SVGs and ThorVG compatibility variants
retain the existing `EngineContent/Icons/Slate-NOTICE.md` provenance. Catalogue separation does not relicense art.
