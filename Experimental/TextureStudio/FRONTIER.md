# Frontier native application integration

This directory preserves the updated React/Three interaction reference from PR #1 at
`85be136ba213be6768b3acdd4c0a68cbe24c6bd2`, built on the original
`streamlinkinbox/Frontier@773b2f5a2672ae530accff000d65d779e9059b66` import.
See `Exhibits/TextureStudio/Results/ReferenceImport.json` for the selective update.

`Native/WindowsTextureStudio.cpp` is the new standalone C++ application composition. Reusable catalogue/source
operations are in `Engine/ContentInterchange/MaterialLibrary`; native UI is in `UI/TextureStudio`. Frontier and
Texture Studio share `Engine/Interface/WindowsApplication` rather than duplicated startup/presentation loops.

Native `.material` documents contain TOML. The reference `.texture.json` projects retain their original semantics;
there is no implicit conversion. The native app now defaults to the HTML's 127-preset catalogue or takes `--library <ContentDirectory>`.
GPU preview/atlas integration is implemented; a linked Windows x64 test package is now available, while Windows runtime/hardware acceptance remains pending.

Plan: `Documentation/Plans/Ongoing/TextureStudio.md`. Checks and execution limits: `Exhibits/TextureStudio/README.md`.
The local `site/` bundle is historical; the current preview is generated from source with `npm run build`.

## Docked native workspace and GPU thumbnails

The native Material Library now registers three real dock panels rather than a fixed list/source split.
Cards and scalar sliders reuse `ControlKit`; structured scalar edits preserve authored TOML comments.
The Engine owns `MaterialLibraryRendering` and `MaterialThumbnailAtlas`. One device-local sampled atlas serves
resident card thumbnails, baked through the existing Vulkan material evaluation and GPU copies with gutters.
A separate shader-ball preview uses the same programs and geometry. Native composition registers two ImGui image
views (atlas and preview), not a texture/descriptor per card. Readback is confined to Exhibits validation.

Workspace layout uses the existing personal `.projectspace` with a separate `texture_studio_layout` section.
No React window substitutes for the native app. Complete HTML visual parity, additional material controls and
Windows runtime/multi-window acceptance remain pending; see the single tracker and current Exhibits reports.

## HTML library/style correction

The native catalogue now comes from the actual 127 HTML presets, not renderer-validation specimens. An exported
`Library.toml` retains complete normalized reference parameters, order and recipe controls; native `.material`
definitions retain supported physical coefficients and the complete executable automotive paint block. Other procedural kernels are not shader-equivalent.
Native defaults copy this library to personal application content without overwriting user files; explicit library
arguments are left untouched. The shared dockspace/cards/sliders remain, with reference panel measurements,
colours, category pills, card geometry, DM Sans font faces and shader-ball profile. Full 1:1 acceptance remains open.


The native automotive family now executes cellular flakes, buried binder tint, linear-space minification and
independent orange-peel clearcoat relief for all seven paint presets. The shared specimen includes the pedestal,
foot and trim ring. Inspector cards are scrollable/collapsible and general editor SVG icons are separated from
Texture-only and Fluid-only catalogues with domain guards. Evidence and Windows test instructions remain in
`Exhibits/TextureStudio`; exact HTML parity and the other procedural families are still open.
