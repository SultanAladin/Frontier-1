# Islamic and African traditional pattern additions

## Twenty new cards — 10 October 2026

The shared Pattern library adds **10 Islamic geometry** and **10 African traditional** presets. The public catalogue now has **103 cards: 102 designs plus Blank**. The previous 83 cards are retained; colours, presets and alternate URL spellings do not create duplicate cards. This does not reinstate the rejected historical rug offering or its withdrawn larger-library quota.

These are **original vector studies inspired by named traditions**, not reproductions of historic objects, authenticated traditional designs, ceremonial motifs or encoded cultural messages. African descriptions identify the particular community/region rather than treating the continent as one visual style. The Kufic-inspired study is abstract geometry, not readable calligraphy or religious text.

## Islamic geometry

| Preset                        | Construction                                                               |
| ----------------------------- | -------------------------------------------------------------------------- |
| Zellige Eight-Point Stars     | Moroccan-inspired eight-point stars, lozenge joints and glaze tracery      |
| Girih Decagon Rosettes        | Persian-inspired decagonal rosettes, rhombic petals and bow-tie connectors |
| Persian Twelve-Point Rosettes | Central twelvefold rosette with quarter rosettes at the corners            |
| Seljuk Star-and-Cross         | Eight-point stars alternating with angular cross infills                   |
| Andalusian Interlaced Squares | Square/diagonal straps with separated over-and-under crossings             |
| Maghrebi Honeycomb Stars      | Sixfold star flowers on a regular hexagonal lattice                        |
| Mamluk Compass Medallions     | Sixteenfold radial panels, polygon frames and satellite stars              |
| Ottoman Rumi Arabesque        | Mirrored curling stems, split leaves and palmettes                         |
| Moroccan Laced Diamonds       | Connected diamond straps with inset lozenges and star knots                |
| Kufic-Inspired Square Maze    | Non-text rectilinear maze; no quotation or religious lettering             |

## African traditional

| Preset                    | Named inspiration                                                            |
| ------------------------- | ---------------------------------------------------------------------------- |
| Asante Kente Blocks       | Asante, Ghana — joined woven strips, bars and stepped lozenges               |
| Ewe Kente Zigzags         | Ewe, Ghana/Togo — stepped weft zigzags and narrow warp guards                |
| Bamana Bogolanfini Dashes | Bamana, Mali — contrasting mudcloth-inspired panels and geometric marks      |
| Kuba Raffia Steps         | Kuba, DR Congo — stepped lozenges and offset hook/stitch lines               |
| Ndebele Painted Chevrons  | Ndebele, South Africa — bold painted architectural geometry                  |
| Yoruba Adire Rings        | Yoruba, Nigeria — indigo-resist-inspired rings and dotted halos              |
| Yoruba Aso-Oke Stripes    | Yoruba, Nigeria — warp bands, transverse ribs and lozenge accents            |
| Cameroonian Ndop Lattice  | Grassfields, Cameroon — indigo-resist-inspired cells and hourglasses         |
| Ethiopian Tibeb Border    | Ethiopia/Eritrea — nested diamonds and stepped embroidery-inspired bands     |
| Swahili Kanga Rosettes    | Swahili coast, East Africa — floral-geometric printed fields and frame bands |

## Use in the existing editor

1. Open **Pattern**, then choose **Islamic geometry** or **African traditional** in **Pattern collection**. Each collection has ten cards with actual SVG previews.
2. Search by name, motif, community or region; for example **Mali**, **Ghana**, **Ndebele**, **girih** or **palmettes**. Human-readable pattern URLs use the existing `?studio=pattern&pattern=…` route.
3. Click a card and edit its named native path layers. Existing colour/finish/opacity, size/position/rotation, visibility, drawing tools, Design palette, stitch overlays and undo/redo remain available.
4. Use **3D material** for the actual shared renderer and **Apply to material** to return the design to Material. Save/reload JSON, SVG/PNG and procedural material exports use the same pattern document/runtime.

The palette-variant selector is hidden in these two collections: their original reference colours can be edited through **Design palette**, without suggesting that a background palette change creates a new construction. The hexagonal design retains its physical aspect; Tibeb is intentionally a **horizontal border**, not a two-way repeat.

## Implementation and bounds

- `src/heritagePatterns.js` defines all twenty deterministic, DOM-free vector factories, names, construction IDs and descriptions. No generated images, stock art, external fonts, scripts or network resources are used as pattern content.
- `patternDocument.js` registers them alongside the existing public families, preserving `alloy.pattern.v1` and legacy factories. Each document has 4–12 editable native path layers, safely below 64 layers and with room for the existing three-layer stitch overlay. Compound paths are complete and below 100,000 characters per layer.
- `materialExport.js` embeds the same factories into the self-contained procedural shader export. Exported `patternStarter()` can regenerate all twenty designs without this app or a second runtime.
- Existing textile counts remain ten weave families and ten stitch families. Prior geometric constructions and retired-project compatibility are not rewritten.

## Verification

```sh
npm run test:heritage
npm run build
npm run build:githack
```

- **30 new checks passed** (24 model + six browser), 2.2 minutes. They cover the exact 10+10 additions, 103-card catalogue, retained/retired boundaries, complete deterministic vectors, validated round-trips, structural identity, palette edits, geometry/finish editing, stitch overlays, physical hexagons, searchable cultures, real 3D preview/apply/export and phone access.
- **66 targeted existing pattern/layout/runtime checks passed**, 10.5 minutes. They exercise Sets 02–04 and their analytic geometry, existing textile/stitch/weave controls, source reuse, undo/import/export, legacy saved rugs/fades, all five shared workspaces at desktop/tablet widths, phone docks, cached reloads and iframe switching. Three development-optimizer-specific runtime cases were skipped on the compiled preview. Six older development-source bake/export cases were not selected for that preview; three historical byte-comparison cases also require Git commits absent from this clone. This is a targeted regression result, not a claim that the entire repository suite ran.
- **Three focused offline cases passed**, 1.4 minutes: the new Islamic/African collections plus the preserved Stamp/Decal and part-material/UV workflows. With every HTTP asset request blocked, the new case saves/reloads edited native vectors, exports SVG, renders/applies the actual material and checks that the exported standalone factory regenerates all twenty presets. No external-resource requests or browser errors occurred.
- **Both production and standalone builds passed**. The refreshed `site/index.html` contains all JavaScript/styles/fonts inline (3,868,811 bytes; SHA-256 `332aafef78c82fbcb4ede6c30d8cc008a0b07704be302f95c9223183f7d8d1bf`). Existing React/Three deduplication, preview host support and no-store protection are retained.
- Actual SVG galleries of all twenty designs, the shared editor, live material preview and reachable 390px phone canvas were visually inspected. The phone check explicitly scrolls back from the library to the editable artboard and verifies its visible, contained >300px width.

Ignored QA artifacts: `.playwright/heritage-patterns/final-suite.log`, `compatibility.log`, `offline.log`, and `phone-final.log`. No audited bake data, unrelated editor implementation or Git history was changed.
