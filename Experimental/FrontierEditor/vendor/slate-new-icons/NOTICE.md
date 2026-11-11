# Slate / NewIcons — source attribution

Imported at the user's request from **SultanAladin/Slate**:
https://github.com/SultanAladin/Slate/tree/master/References/NewIcons

Pinned revision: `aaa87a26945c64d712005707a56ecfecf7a6275f`.
Original source: `References/NewIcons/src/App.tsx` (preserved here verbatim).

`icons.json` contains the 102 components displayed by the upstream gallery, in its original order, rendered to SVG with React. Two unused ClothSim component variants are not displayed upstream and are not imported. The upstream `single-icons.html` still contains unevaluated JSX and malformed SVG filter attribute casing, so the TSX components are the authoritative artwork source.

The importer namespaces IDs and local references and adds accessibility metadata. At the user’s request, selected artwork is enlarged with independent bottom-corner badge transforms. Original shapes and colors are preserved except for explicit fixture adaptations in `scripts/slate_refinements.py`: upright Spotlight/LED Panel, dimensional Spotlight housing and holder, and the rebuilt shaded Lantern. The old page-wide shadow is omitted to avoid clipping; local filters remain. No artwork is rasterized. Upstream UI, application code, configuration files, and environment variables are not run or copied into the app.

These assets are attributed to their upstream source, not claimed as original Frontier artwork. No explicit repository license was found at the pinned revision; inclusion does not imply a new license grant. Verify upstream permission/terms before external redistribution.

Regenerate the rendered snapshot after reviewing a changed upstream source:
`node scripts/import-slate-icons.mjs`
Then regenerate the gallery with `python3 scripts/build-icon-gallery.py`.

## Active selection

`selection.json` lists the 26 user-approved imports. Only these are published in the gallery and SVG exports; the other 76 are excluded and pruned on regeneration. The full 102-component rendered snapshot and original TSX are retained here for provenance, not included in the shipped gallery.

## Regenerating adapted layouts

Run `npm ci`, then `node scripts/measure-slate-layout.mjs`, `python3 scripts/build-icon-gallery.py`, and `npm run build`. Layout bounds and transforms are stored in `layout.json`. The source TSX and `icons.json` snapshot are unchanged. The approved Spotlight beam path, emission ellipse and previous framing are explicitly preserved; tests assert these values.
