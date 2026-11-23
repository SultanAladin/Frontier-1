# Surface reference notes — Alloy 06.2

Research checked 2026-10-06. These sources guide the **procedural construction**, not material input maps. No reference image is embedded, sampled, traced into a bitmap, or required at runtime. This is an artistic real-time approximation, not a species-specific anatomical simulation or a scanned leather reproduction.

## Leather: scale organization before noise

The two commercial photographs inspected were a [brown belly-leather belt](https://ebay.com/itm/146853838423) and a [light-colored hide listing](https://www.ebay.com/itm/366061360520). They guided flatter plate interiors, relatively thin joints, and a transition from broad rectangles to smaller rounded scales. Listing descriptions/authenticity claims were not treated as scientific evidence.

Implementation: a smoothly deformed row/column field, small corner radii in belly regions, shallow edge bevels, fine grain inside the plates, and crease-specific detail between them. The coarse outlined-cell pattern is not used for regular leather. The scale pores are an art-directed feature rather than an authentication model.

## Leaf epidermis: not a uniform polygon mosaic

The review _Cell biology of the leaf epidermis_ describes variation among epidermal cell types; many Arabidopsis pavement cells have interlocking lobes and necks, whereas other regions/cell types can be elongated or more rectangular. This is not a universal shape for every leaf. [1](https://pmc.ncbi.nlm.nih.gov/articles/PMC8774078/)

Implementation: optional smooth spatial lobing of microscopic cell boundaries, sparse elliptical stomatal depressions with small guard-cell ridges, and separate macro-scale veins. These details are intentionally subtle and filtered at distance. The model does not infer upper/lower epidermis from a mesh or claim to place stomata anatomically for a named species.

## Petal epidermis: a different surface from a leaf

The Scientific Reports microscopy study documents tabular, convex conical and convex papillate cells among its samples, with cuticular folds in some species. Its findings do not support treating one cell shape as universal across flowers. [1](https://www.nature.com/articles/s41598-020-67663-6)

Implementation: softly peaked cell relief and derivative-filtered fine ridges on the petal family; pigments/gradients and basal speckles remain independently controlled. This is geometric/roughness-scale shading detail, not a simulation of nanostructure optics or biological light focusing.

## Cactus epidermis and areoles

The botanical overview describes areoles as the origin of spines and usually flowers; areoles may have felt and long hairs, with arrangement depending on cactus structure/species. [2](https://eduscapes.com/nature/cactus/index1.htm)

Implementation: waxy body pigment, slightly varied areole centers/sizes, fine felt-like shading within areoles, and rib-column alignment. The exported material does not generate spines or adjust the user's geometry. The procedural preview cactus is only a demonstration mesh.

## Asset and export boundary

All plant materials remain full opaque UV0 surfaces. Leaf/blade/petal V runs base to tip; U spans the organ width. For leaf midribs, U=.5 is the expected center. Fruit/cactus/stem U wraps the body and V follows its length. UV islands packed into an atlas require remapping; the editor does not infer those transforms from a mesh it has not received.

Normal relief cannot alter silhouettes. Six static maps cannot preserve every shader lobe or subsurface effect. The 8-bit height output is not a precision displacement file and may quantize the smallest cells; normal maps retain more of their shading effect. No scan-quality or species-perfect claim is made.

## Leather correction follow-up — v6.3

Inspected the cinnamon and pecan supplier photographs from [The Leather Guy](https://theleatherguy.org/products/alligator-skin-belly-various-colors-genuine-leather-hide), explicitly sold as **alligator**, as supplementary references for finish and crease behavior—not proof of crocodile anatomy. Search results also returned stock snake images; those were rejected as inappropriate targets.

The implementation no longer uses separate rounded boxes for the plates. It models one height field with shared softly depressed creases, little albedo contrast across joints, local finish variation and fine grain. The regular-leather clearcoat was also corrected to follow its relief. A flexed preview swatch is accompanied by flat-panel and planar-bake checks. Reference photos remain research only, not application assets.

## V7: permitted vector sources and pattern references

The user explicitly permitted an SVG/source-map exception for leather and image/SVG sources for pattern design. `leatherSource.js` now authors one vector height atlas; it is not derived from a photograph. The supplied floral print, geometric pattern collection and cube lattice inform original editable starter motifs in Pattern Studio. Those reference JPEGs are not embedded as application textures. User imports are a separate, opt-in document feature.

The first offset-blended leather sampling approach doubled some crocodile crease lines. Visual inspection prompted replacement with a continuous coordinate warp and softened vector height profiles. Seam/gutter tests and shader compilation are functional checks, not proof of hide realism.
