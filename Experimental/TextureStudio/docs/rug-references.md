> **Historical research only.** The rug, African and Islamic collections described below were rejected and removed from the V7.7 public library. They are not recommendations or evidence of user approval. Retained code supports existing saved documents.

# Rug and textile studies — v7.6

## Scope and originality

The earlier work below was rejected. Its recipes are retained only for old saved-document and exported-factory compatibility. The former inventory claim is withdrawn; no geometry hash or research citation establishes user acceptance. See [the current guide](pattern-studio.md).

No reference photographs are shipped as material inputs. These notes record historical research, not current starter recommendations.

### Additional research for the supplied references

- The Met's discussion of African beadwork documents glass-bead/hide/thread constructions and geometrically organized Ndebele aprons and capes. It informed the use of bead-level color assignment and a filled geometric field, not an assertion that the user's unlabelled image is a particular authenticated object. [1](https://www.metmuseum.org/perspectives/beadwork-in-arts-of-africa-and-beyond)
- The V&A's Islamic tile activity illustrates interlocking eight-pointed star and cross tiles with floral decoration. This informed a joined field with decorated interstices instead of isolated star stamps. [2](https://www.vam.ac.uk/articles/design-and-make-your-own-islamic-tile-and-printed-pattern)
- Brian Wichmann's tiling introduction discusses connected geometric networks, rosettes and strapwork. The new field follows the connected-network principle; it is not a claim of reconstructing a named historical tiling or its exact interlace. [3](https://tilingsearch.mit.edu/intro.pdf)

### What was drawn and checked

- **Woven reference:** wide format, three nested diamonds/shields, stepped black/ivory/blue contours, tiny outline checks and bead marks, complete split panel background and narrow green/black selvedges.
- **Bead reference:** large nested colored bands crossing a staggered field of thousands of oval bead bodies, separate highlights/edge shading and pierced centers. The shared runtime adds rounded profile relief, hole depressions and matte interstices.
- **Vault reference:** connected radial faceted panels, alternating ivory/navy/turquoise grounds, gold ribs, leaves, palmettes, blossoms and scrollwork clipped to panel boundaries. This is a flat surface design; no architectural vault geometry is claimed.
- **Carpet reference:** a connected eightfold star/cross layout with branching floral/leaf/scroll decoration in both tile types and narrow outer guards.

Actual SVG rasterizations and studio screenshots were inspected, including the beadwork in live 3D. The renders were revised when the first Islamic fills were too sparse. This is visual inspection, not user approval or a quantitative measure of authenticity.

## Research that informed the designs

### African textile structures

- **International Quilt Museum, _From Kente to Kuba_.** Asante/Ewe kente uses joined narrow woven strips with offset warp-/weft-faced sections. The exhibition also describes raffia patchwork/appliqué and Yoruba adire resist techniques. This informed offset strip blocks, alternating patterned bands and indigo compartment studies, rather than one generic “African” repeating triangle. The library's titles do not claim the meanings of particular named motifs. [1](https://www.internationalquiltmuseum.org/exhibition/kente-kuba)
- **V&A, Kuba cloth, O89315.** The museum describes embroidered raffia, cut-pile and uncut topstitching, interlocking angular motifs and intentional individual variation. This informed high-contrast angular paths, nested/interrupted lozenges, spirals and unequal patchwork compartments. Wool finish in the studio is a rug interpretation; it is not a physical reconstruction of raffia manufacture. [4](https://collections.vam.ac.uk/item/O89315/kuba-cloth-kuba-cloth-unknown/)
- **Minneapolis Institute of Art, Asante Kente Cloth.** The examples emphasize strip structures, supplementary weft patterning, contrasting colors and shifts in geometric blocks. The strip studies use those structural ideas, without reproducing the institution's named cloths or asserting their symbolism. [5](https://new.artsmia.org/programs/teachers-and-students/teaching-the-arts/artwork-in-focus/asante-kente-cloth)
- **FIT Fashion History Timeline, bògòlanfini.** The article discusses patterned grids, narrow strips, chevrons, crosses, dashes and decorated end borders. These informed earth-tone panel/mark arrangements—not an attempt to reproduce culturally specific ceremonial motifs. [4](https://fashionhistory.fitnyc.edu/bogolanfini/)
- **V&A, _Africa fashion: cloth of a continent_.** Its bògòlanfini discussion describes geometric negative-space patterning and the off-white/brown palette associated with the dyeing process. That informed the contrast treatment of the Earthmark studies. [2](https://www.vam.ac.uk/articles/cloth-of-a-continent-africa-fashion)

The earlier vivid nested-diamond study has been replaced by the new wide-format construction described above; the earlier sparse field is not retained as another catalog entry.

### Islamic carpets and rug structures

- **The Metropolitan Museum of Art, _Carpets from the Islamic World, 1600–1800_.** The essay and object images distinguish medallion, vase, garden, geometric and flowing vegetal carpet designs. This informed separate composition plans instead of treating Islamic carpet design as nested stars alone. [1](https://www.metmuseum.org/essays/carpets-from-the-islamic-world-1600-1800)
- **The Met, Carpet with a Compartment Design, 10.61.3.** Its description relates geometric star-based compartments to curvilinear ornament, cartouches and bookbinding-style borders. The new compartment studies use those organizational ideas without copying the artifact's figural imagery. [2](https://www.metmuseum.org/art/collection/search/445996)
- **The Met, “Medallion Ushak” Carpet, 08.173.13.** Its repeating ogival medallions and smaller stars, cut by a border, informed ogival fields and alternating focal arrangements. These are labeled studies, not historical Ushak reproductions. [4](https://www.metmuseum.org/art/collection/search/445311)
- **Smarthistory, The Ardabil Carpet.** The article and detail photographs show the relationship between a central medallion, surrounding ovals, dense floral decoration, cartouche borders and echoed corner pieces. This informed the radial plan and ornament hierarchy of **Saffron Rosette Court**. No inscriptions, lamps or photographic fragments were copied. [5](https://smarthistory.org/the-ardabil-carpet/)
- **The Met, Dragon Carpet.** The museum contrasts the strong geometry of village Caucasian carpets with flowing court florals. This informed the hooked/stepped geometric rug group and its distinction from the floral salon designs; it does not claim to reproduce the artifact's dragons. [1](https://www.metmuseum.org/art/collection/search/447580)
- **Kilim, Turkish rug overview.** Its discussion of stars, diamonds, hooked forms and medallions supplied additional vocabulary for the geometric rug studies. No symbolic or authenticity claims are attached to the generated motifs. [3](https://www.kilim.com/kilim-wiki/what-is-a-turkish-rug)

## Implementation and editing

`src/ornamentalConstructions.js` defines the 63 new compartment plans and their interior constructions; `src/ornamentDrawing.js` supplies cubic foliage, strapwork and compound inlays. The shared path builder clips concave and convex panels, fits local ornament and preserves semantic depth order.

`src/referencePatterns.js` contains the four hand-authored reference studies, including explicit wide-format shield/panel coordinates, the faceted polar construction with convex polygon clipping, the connected star/cross field and bead geometry. There are no runtime network requests, random variation packs or embedded photographs. Paths are grouped by drawing stage and pigment; stage order is retained so outlines and knockouts are not destroyed by color bucketing.

`src/patternDocument.js` owns the canonical public catalog. `src/patternLibrary.js` retains historical colorway constructors for loading old saved inputs. These compatibility constructors are not public cards or additional designs. `src/rugDesigns.js` / `src/rugCompositions.js` retain the older blueprints, revised in place and classified under Compositions; the Chromatic recipe now dispatches to the new reconstruction.

The reference studies are fixed, editable path documents. The legacy Composition designer is deliberately absent for them rather than pretending its generic controls describe the new art. Palette, material assignments, layer editing, stitches, undo, save/import, SVG, shader exports and baking remain available. Generated compound paths represent semantic pigment/depth groups, not one layer per blossom or bead. Maximum generated layer count remains below 61, leaving room for three stitch layers.

`designAspect` changes the physical artboard and Rug preview while preserving normalized document coordinates. New fields are validated and survive JSON roundtrips. Old documents retain default behavior. Other mesh previews still use their own UVs.

## Rendering limits and interpretation

- The prior inventory claim is withdrawn. Passing tests do not establish artistic quality or historical authenticity.
- The SVG artwork remains scalable, but live color/parameter/finish maps are finite resolution. Very fine ornament needs a close view and sufficiently large bakes.
- Bead relief is a derivative-filtered fixed lattice, not separate physical glass bead meshes, transmission or thread simulation. Moving individual pigment layers does not move that procedural relief grid; transform the complete document for coherent beadwork.
- The vault design is flat ornament, not a muqarnas/dome model. Wool uses relief and sheen, not groomed fibers, antique wear, fringe or loom simulation.
- Finite rugs use `tileAxes: "none"`. Repeat scaling can crop or stretch terminal pixels; it does not make them seamless.
- Existing pure leather, scratch, botanical and analytic fabric kernels are not changed. Rug preview aspect is optional and does not change the unpatterned leather swatch.

See `tests/reference-patterns.spec.js` for explicit card-accounting, legacy loading, full path/metadata serialization, artboard aspect, actual bead preview and baked-channel checks. Existing regression tests remain useful for functionality; unique hashes or passing tests are not proof of visual quality.

## Validation of this revision

28 distinct targeted tests passed across the construction/library/composition run and the editor/import/fade/reference regression run. The four reference tests were repeated after final visual adjustments. The offline standalone check also passed with external assets blocked, including construction of the new inlay generator and beadwork from exported shader helpers. SVG galleries, studio screenshots and actual 3D views were inspected separately; sparse and incorrectly fitted compartment decoration was revised during that inspection. These are targeted checks, not the full historical material suite or an endorsement of every design's aesthetic merit.

Additional primary research: the Met's **Double Prestige Panel** describes interlocking geometric raffia designs and irregular/asymmetrical arrangement. It informs the compound angular vocabulary and unequal older patchwork studies, not a claim that the new regular inlay grids reproduce a particular Kuba object. [2](https://www.metmuseum.org/art/collection/search/318398)

The Met's **Islamic Art and Geometric Design** teaching publication discusses circle/grid construction and the combination of geometric and vegetal ornament. It informed the distinction between a connected panel framework and its curved infill. [5](https://resources.metmuseum.org/resources/metpublications/pdf/Islamic_Art_and_Geometric_Design_Activities_for_Learning.pdf)
