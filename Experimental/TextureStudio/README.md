# Alloy — Material, Pattern, Texture, Stamp & Baking Studio

A real-time procedural material editor built with React, Vite and Three.js. Most library surfaces are analytic. **V7 adds a generated SVG leather atlas and editable SVG/image pattern sources**, with procedural material shading. No external HDRIs or downloaded 3D assets are required. The charcoal interface uses rounded panels and desaturated accents. Fonts are self-hosted.

## Run

```sh
npm ci
npm run studio
```

`studio` builds and serves the compiled application on `0.0.0.0:5173`, including Arena preview hosts. Use it for a stable live preview. `npm run dev` starts the development/HMR server; `npm run build` creates the conventional production build without starting a server.

If an already-open preview reports an invalid hook or null `useState`, **reload the preview document once** (or use **Reload studio** in the recovery panel). Do not clear saved projects or sign out: older development tabs can retain a different React dependency-cache generation. The compiled preview avoids those optimizer URLs; development dependencies are explicitly prebundled and deduplicated with non-cacheable responses. See [runtime safeguards and regression checks](docs/preview-runtime.md).

```sh
npm run test:runtime
```

## Texture layer evaluation and mesh support

Materials and decals, including native glass/wax transmission, now follow the same **bottom-to-top live layer order**: an upper material covers a lower stamp; masks, opacity and visibility reveal the lower result. A library drop on a Material row saves a global snapshot, while mesh drops stay part-scoped. **Layer 0** is an automatic locked, hidden, white system fallback outside editable user layers. Open Baking from Texture for matching **512** high/low AO, tangent-normal and bevel-normal support; actual compatible maps persist in the portable project and remain active after the white color is replaced. Optical surfaces sample only the already evaluated lower linear lit history, keeping native transmission/IOR/attenuation rather than substituting fake alpha. The UV view shows the same evaluated **lit appearance**, not exported channel atlases or nested-volume refraction.

**Digital Paint / Erase / Fill now works on layers and masks in Lit and packed UV view.** Hover for an unclipped, screen-space **size/texture footprint** at pointer X/Y; a round tip stays circular through perspective/UV distortion and writes all visible touched triangles across chart/UDIM seams. Explicit selections, part targets and locks remain restrictions; high-DPI rendering and stabilized stroke endpoints remain aligned with the cursor. Open the brush studio for channel toggles, section-card properties and a real shared-engine stroke preview. See [digital painting](docs/digital-painting.md) and [layer evaluation](docs/texture-layer-evaluation.md). Physical-media simulation, general imported-source composition and composited PBR atlas export remain outside this phase.

The digital suite now contains **53 focused painting/cursor/DPI/device cases**. The earlier digital phase passed **198 distinct selected model/Canvas/UI/runtime/offline regressions**; latest cursor verification is documented separately. Both normal and embedded offline builds succeed; see the [verified scope and artifact](docs/digital-painting.md#verification).

```sh
npm run test:stack
```

## Continue the Slate material editor

Imported from [`unassignedinbox/Slate`, `arena/835eeb10-slate`](https://github.com/unassignedinbox/Slate/tree/arena%2F835eeb10-slate), source commit `a6df214c54bf51963846fc685794548199e2f010`. This workspace adds a real 2D vector authoring canvas to the existing pattern generator and preserves its material renderer, pattern catalogue, textile controls and export/bake pipeline.

Open **Pattern studio**, or start directly with `/?studio=pattern&pattern=blank&material=natural-cotton`.

- **Draw:** drag-to-create rectangles, ellipses, lines, triangles, diamonds, polygons and stars; freehand paths, precision Bézier pen paths and a click-through curvature tool (U).
- **Edit:** multi-selection/marquee, eight anchored resize handles, rotation, numeric properties, point/control-handle editing, numeric tangents and point types, precise curve insertion/bending, endpoint continuation, split/join/reverse, independent fill/stroke, rounded corners and editable polygon/star parameters.
- **Compose:** grid/smart snapping, rulers, zoom/pan, grouping, alignment, equal-gap distribution, a pinned layer list with lock/visibility and drag reorder, reflected/radial copies and centered repeat arrays.
- **Keep your work:** transactional undo/redo, an in-editor clipboard, browser-local draft recovery and portable JSON/SVG/PNG exports.
- **Render:** switch to **3D material**, then **Apply to material ↗** to send the same editable document and finishes to the main material viewer. Save the resulting material as a preset or export its shader/maps.

**Pen & Curve:** a dedicated path workbench provides visible stroke/fill controls, draft undo/redo, automatic curvature/tension, independent or aligned handles and recovery of unfinished paths. Target snapping covers anchors, curves/edges, objects, grids and angles. **SVG tools:** editable multiline **Text (X)** with portable outlined exports, **Arc (C)**, **Eyedropper (I)**, Bézier union/difference/intersection/exclude, and a validated **Paste/Edit SVG source** panel for self-contained artwork—not executable scripts. **[Complete editor guide, shortcuts, formats and limits](docs/shape-editor.md)**. The current format supports 64 layers and 400 editable points per path; compound SVG sources remain intact. Browser-local drafts are not a cloud backup.

### Self-contained application

`site/index.html` embeds JavaScript, CSS, fonts, frozen cloth geometry and shader recipes. It needs no development server or runtime CDN imports. Rebuild it after source changes:

```sh
npm run build:githack
npm run test:standalone
```

This repository has not been published to GitHack as part of the editor work. Links or artifact checksums in older Slate reference documents describe upstream versions, not the current build.

## Islamic and African traditional pattern additions

The existing **Pattern collection** picker now includes **Islamic geometry** (10 new vector presets) and **African traditional** (10 new vector presets), bringing the library to **103 cards including Blank**. These are editable, named-tradition-inspired vector studies, not raster images or duplicate colourways. They support the existing 2D tools, Design palette, stitch overlays, 3D material preview, apply/save/SVG/PNG and portable material exports. See [the twenty-pattern catalogue and usage guide](docs/heritage-patterns.md).

```sh
npm run test:heritage
```

## Part-scoped Texture materials and UV / UDIM authoring

In **Texture**, drag a Material card onto **Body / Lid / Handle / Spout**: the actual hit part receives an embedded native material snapshot, not a whole-teapot recolour. The matching right-side Material recipe controls edit that part; assignments respect history, locks, visibility and inherited opacity.

**UV / Texture** joins Shaded and Wireframe. Its geometry-backed **Authoring UDIMs 1001–1004** contain 32 non-overlapping charts; unchanged **Source UV0** is available for read-only overlap inspection. Click or drop Stamp artwork onto a UV island to create a real part-clipped 3D decal. UV components have U/V and UV-unit size controls, move/rotate/scale, safe re-anchoring and single-transaction confirm/cancel. The shared browser still overlays an invariant scene.

This is live part material / UV decal authoring, **not** a full source-layer compositor, imported UDIM texture set or baked decal atlas. Existing Material/Pattern exports, source painting and audited baking remain separate. See [Texture part materials and UVs](docs/texture-parts-uv.md).

```sh
npm run test:parts-uv
```

## Stamp workspace, surface decals and layer drag/drop

Open **Stamp** (`/?studio=stamp`) for a simpler Text/SVG/Image composition workspace. Rich text supports mixed fonts, sizes, semibold weights and colours **within one text component**, using bundled fonts and real outlined exports. SVG can be embedded or imported as validated static markup, never user JavaScript. Save/open/copy/delete reusable stamp presets, choose any of the 14 channel targets or mask interpretation, and export editable JSON, SVG or transparent PNG.

In **Texture**, attach decal components to a regular layer or add a dedicated **Decal layer** containing many placements. **Stamp brush** aims at the real mesh and commits one stamp per click; **Transform** anchors to the surface and provides actual 3D move/rotate/scale handles and re-anchoring. Presets are embedded as shared immutable snapshots; explicit replacement preserves an instance's transform. Opacity, channels, inherited protections and projected stamp masks are live, undoable and saved with the Texture project.

Dedicated layer grips now expose **before / after / inside / root** drag targets with subtree, cycle, depth, lock and one-gesture history safeguards. The overlay Content Browser's **list rows fill the centre pane horizontally**, while its fixed scrolling grid and scene/dock dimensions stay unchanged. The browser now includes Stamp presets.

These are real clipped surface decals and selected fill/mask previews, **not a whole-stack UV painter/compositor**. Height/normal are derived shading; Subsurface is an honest wrap-light approximation, not volumetric scattering. The existing material/pattern Apply/export, 2D source strokes, baking and audited teapot outputs are retained. **[Workflow, renderer scope, storage limits and checks](docs/stamps-and-decals.md)**.

```sh
npm run test:stamps
```

## Shared studio UI and Baking Studio

Material, Pattern, Texture, Stamp and Baking share the newer ProjectZero/Texture Studio theme: workspace pills, angled dock tabs, charcoal panels, rounded inspector cards, neutral controls and light typography. All five studios place libraries/outliners/layers/bake setup on the **left**, the viewport in the middle and inspectors on the **right**. Existing material recipes, pattern tools, Apply and exports are retained.

Workspace routing is separate from modal dialogs. `?studio=baking` and `?studio=bake` open the real Baking Studio; unknown routes fall back to Material. Failed workspace components show recovery controls rather than an unexplained black pane.

**Baking Studio** provides a bounded static-mesh high-to-low workflow: OBJ / self-contained GLB/glTF / PLY imports, high-only STL, per-object texture sets, UV validation, name matching, ray or nearest projection, PNG/ZIP export, cancellable workers and 3D/2D previews. **Map selection is a clickable grid; enabled tiles are green. No node editor is required or exposed.** Its **27 working outputs** include tangent/world/object and bent normals, bevel normals/mask, AO/secondary AO, curvature/convexity/cavity, thickness, height/vector displacement, position, object/UV-island IDs, UV coordinates/wireframe, coverage/hit alpha, vertex color, dust/dirt/edge-wear masks. Bevel changes normals, not topology; weathering masks are derived starting points, not simulations. The existing six-channel procedural material-patch baker remains a separate mode.

This is not full Blender/Substance parity: custom cages, MikkTSpace certification, UDIM, animated/skinned geometry, FBX/USD, geometric beveling, high-material texture transfer and precision/EXR maps are not implemented. Models stay in the session; recipe files contain settings/graph, not embedded meshes. See **[Baking Studio guide, capabilities and limits](docs/baking-studio.md)**.

```sh
npm run test:baking
```

## Texture studio — first UI / layer pass

Open **Texture studio**, or `/?studio=texture`. Material, Pattern and Texture workspaces are available in the same application.

The layout follows the **newer `Experimental/ProjectZeroEditor`** in the requested Frontier reference, rather than the older `FrontierEditor` design: docked tabs, charcoal panels, pill controls and rounded inspector cards. **Layer stack is on the left and the inspector on the right** of a real Three.js teapot viewport. Tablet docks retain this order with narrower widths; phone docks use tabs below the scene.

Layers support add, selection, reorder, search, inherited visibility/locking, subtree duplicate/delete, **nested folders**, channel targets, layer/folder **Fill and Colour masks**, undo/redo, local draft recovery and validated `.texture.json` project files. Imported and painted 2D rasters have real bounded source thumbnails and source-only colour-mask previews. The right dock's **Inspector / History** tabs expose actual edit states, undo/redo/restore and actual last digital/mask or 2D source-operation footprints; phones keep all three Layers/Inspector/History views in the same dock. The scene supports orbit, zoom, frame, grid and wireframe controls.

**Digital layer and mask painting is now implemented** with sparse ordered surface passes, channel-specific erasing, real geometry-scoped fills, pressure/dynamics/shape/grain and per-gesture undo. Native Material rows retain their native recipe; empty Paint/Fill rows produce physical digital coatings that inherit bounded raw PBR fields from lower rows. Read [the workflow, channel consumers and limits](docs/digital-painting.md). The old 2D source-painting workflow remains separate: imported Texture/Generator sources are not automatically deposited onto the mesh, and a project save is not an exported set of composited PBR maps. Mesh baking remains a separate workspace.

The older repository's PBR-channel plan and painting prototypes were reviewed. The **[revised engine-specific painting plan](docs/texture-painting-plan.md)** records their useful lessons and sequences chart validation, shader consumers, composition, future strokes and later interchange against our current Three.js engine. In particular, the stock teapot's repeated patch UVs must be fixed before painting.

```sh
npm run test:texture
```

## 512 teapot audit, 3D SDF and surface-map tiles

Baking now offers **Mesh maps**, **Material patch** and **Volume SDF**. Surface-map choices use green clickable tiles with icons/mask swatches before baking and actual PNG thumbnails afterwards; selected subsets export their correct channels.

The Utah teapot is audited at **512×512, all 27 maps, 64 AO samples**. Projection selects the nearest eligible front-facing high intersection rather than capturing a neighbouring back surface from the cage. Ray misses may use a bounded oriented nearest fallback; counters are reported. AO uses cosine-weighted hemisphere sampling with world-space rotation; thickness distances beyond the chosen display range saturate white rather than becoming false holes.

**Volume SDF** is separate from texture resolution: 16–128 voxels per axis, unclamped Float32 Euclidean triangle distances, explicit voxel-centre/layout metadata and a three-ray sign-agreement mask. Strict signing requires a closed source. The stock Utah teapot has **384 boundary edges**, so only explicitly approximate or unsigned volumes are offered for it—no silent hole sealing or certified-solid claim.

See [SDF and audit details](docs/volume-sdf-and-bake-audit.md). `npm run audit:teapot`, `npm run audit:sdf` and `npm run test:sdf` reproduce the checks. Generated audit intermediates live outside Git in `bake-audits/`; requested bake deliverables are retained in `deliverables/`.

## New corrugated sheets and sand

**Corrugated Zinc** and **Corrugated Steel** appear under Metal. Steel uses a galvanised garage/cladding finish with IBR-style ribs; rounded and roller-door profiles are selectable. Frequency, rib depth/direction, weathering, spangle and optional scratches are editable. These remain normal/height-relief materials, not new sheet geometry.

**Sand** and **Layered Sand** appear under Nature. Each seeded mineral grain has size variation, facet direction, roughness and dielectric specular response—not automotive metalness. Layered Sand has up to four editable beds with independent color, size, specular/roughness variation, relief, coverage and ordering; choose stacked grain beds or wavy sediment bands. Use **Macro** for individual grains. JSON/shader exports preserve the beds; the existing six-map bake does not add a separate specular texture.

See [controls, scale, research and limitations](docs/corrugated-and-sand.md). `npm run test:granular` checks controls, GPU renders, independent beds and actual portable exports.

## Material-aware inspector

**127 procedural presets** across 16 categories. The library is open-ended, not a count target. Presets share purpose-built shader families; this is not a claim of 100 unrelated BRDF models or measured industrial finishes.

| Category                 |     Count | Examples                                                                                                                                                     |
| ------------------------ | --------: | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Paint                    |         7 | Multicolor flakes, pearl, three thin-film iridescent paints                                                                                                  |
| Metal                    |        25 | Aluminium, 24K/18K/14K gold, rose/white gold, copper, maraging/cast/stainless steel, iron/rust, chromium, bronze, brass, nickel, titanium, zinc, silver, tin |
| Plastic                  |        17 | PVC-U/PVC-P, PP, HDPE/LDPE, PET/PBT, PTFE, POM, PA6, glass-filled PA66, PC, PMMA, PEEK; three existing polymers                                              |
| Fabric                   |        19 | Nine weave constructions, indigo/raw/washed/black denim, two jersey knits, cotton, linen, wool, silk, satin, suede, velvet, carbon composite                 |
| Leather                  |         5 | Nappa, cognac, bull-grain hides and crocodile-style belly leather                                                                                            |
| Clay                     |         4 | Terracotta, wet potter’s clay, kaolin, sculpting clay                                                                                                        |
| Wax                      |         4 | Beeswax, paraffin, soy, sealing wax                                                                                                                          |
| Skin                     |         6 | Pigment variants with pores, undertone and bounded wrap-scattering controls                                                                                  |
| Paper                    |         6 | Cotton rag, kraft, coated art stock, newsprint, corrugated card, mulberry                                                                                    |
| Technical                |         9 | Three solar modules, two golf-ball covers, two LED arrays, corrugated aluminium, isolated scratches                                                          |
| Ceramic / Rubber / Glass | 3 / 1 / 1 | Carbon ceramic, two glazed-tile constructions, performance rubber, crystal glass                                                                             |
| Stone                    |         4 | Two granites and two marbles                                                                                                                                 |
| Wall                     |         3 | Cast concrete, board-form concrete, lime stucco                                                                                                              |
| Nature                   |        13 | Mesh leaves, grass blade, rose/lily petals, apple/citrus/berry skins, cactus epidermis and plant stem                                                        |

**Interpretation:** “PPT” was treated as PP (polypropylene); PET and PBT are also included. Alloy/polymer grades and skin/wax optics are representative visual approximations, not certified spectral or mechanical data. Gold variants, steels, bronze and brass are correctly treated as alloys rather than all being described as pure metals.

The inspector shows only a material's useful controls. Friendly **0–100% art-direction sliders** drive bounded internal physical values, often together. It does not expose a universal wall of metallic/roughness/IOR/clearcoat controls. `src/materialProfiles.js` defines the controls, mappings and fixed optical properties; the same rules apply to rendering, saved presets and shader exports.

Examples:

- **Velvet Roughness:** maps 0–100% to roughness 0.78–0.97 and sheen roughness 0.30–0.85. Only dye, roughness, pile softness and pile length are shown. Velvet remains nonmetallic and uncoated.
- **Silk Roughness:** maps to 0.12–0.38. Silk lustre couples anisotropy, dielectric specular and sheen within fabric-appropriate ranges.
- **Metal polish:** maps from broad to sharp conductor reflections. Metalness stays metallic.
- **Glass clarity:** maps from softly frosted to clear; thickness and edge refraction stay within useful glass ranges.
- **Polymer wear:** lowers the raised grain, with separate abrasion softness and worn-surface polish.
- **Paint:** bounded paint roughness, depth and clearcoat gloss. Flake coverage, size, sparkle and reflectivity are separate. Sparkle drives flake micro-roughness/orientation; reflectivity independently drives the metallic range. Surface-detail controls are collapsible.

Dimensional size and repetition controls are **not** reduced to arbitrary 0–1 ranges:

| Control                                        | Slider range                | Typed range           |
| ---------------------------------------------- | --------------------------- | --------------------- |
| Flake size                                     | 0.001–1,000 µm, logarithmic | 0.000001–1,000,000 µm |
| Thread / grain / peel / flake repetition scale | 0.01–1,000×, logarithmic    | 0.000001–1,000,000×   |
| Yarn direction                                 | −180–180°                   | −180–180°             |

The dimensional convention is **1 scene unit = 100 mm**. Extended inputs expand the slider domain. Fine features become subpixel and are filtered; increasingly small features need not remain individually visible.

## Fabric construction and color

All three foundational constructions—**plain, twill and satin**—plus basket, rib, herringbone, Oxford, houndstooth and warp-faced denim are selectable on woven textiles. This is nine procedural constructions, not a claim to model every specialized industrial weave.

- Independent **warp and weft** color pickers and hex inputs follow the over/under yarn pattern.
- Houndstooth alternates dark/light yarn bands within a 2×2 twill, rather than simply tinting each yarn orientation.
- Thread scale, direction and weave definition affect the actual shader.
- Cloth UVs carry the weave through the folds; rigid objects use triplanar mapping.
- Sheen is material-specific and dye informs the pile highlight. Additional fiber geometry appears in macro views and is hidden when unresolved.
- Presets include natural/basket/ribbed cotton, plain linen, Oxford, indigo denim, herringbone/houndstooth wool, champagne silk, midnight satin, woven upholstery, suede and velvet.

## Paint and surface detail

- **Iridescent paint:** Aurora Flip, Sunset Prism and Opal Pearl use Three.js's native thin-film interference, not a rainbow diffuse-color overlay. Color-shift strength, film phase (160–850 nm) and angle response control the optical model. A low-frequency procedural thickness variation adds subtle surface variation. Fresnel controls angular reflection; wavelength-dependent thin-film interference produces the shifting colors.
- **Cellular flakes:** filled 3D Worley/Voronoi cells, domain-warped and composited through 1–4 independently transformed depth layers. Upper flakes occlude lower ones; buried flakes pick up the binder tint. No cell-centered dot grid or emissive glitter overlay. Subpixel coverage is filtered.
- **Flake colors:** up to 12 individually editable colors, with palette, single-color and interpolated ramp modes. Ramp stops can be moved, added or removed.
- **Orange peel:** bounded amplitude and extended repetition scale, affecting the clearcoat normal and, more subtly, the substrate.
- **Carbon fiber:** 2-over/2-under twill with rounded bundles, sub-fibers and alternating anisotropic reflections beneath resin.
- **Polymers and leather:** a feathered wear field smoothly truncates raised grain. Removed height drives polishing and restrained color change; recessed pockets retain their texture. Leather adds irregular cellular pebble grain and fine pores.

## Architecture, vegetation and scratch study (v6)

- **Corrugated Aluminium:** reuses conductor optics, adding axial rib relief and a hollow-pipe preview. Ribs affect normals; the pipe silhouette remains smooth.
- **Porcelain / zellige tiles:** glazed faces, rounded edge relief and recessed matte grout. Grout changes base color, height, roughness and clearcoat coverage together.
- **Granite / marble:** irregular quartz/feldspar/mica grain fields; multiscale warped veins. These are procedural stone studies, not scans of named commercial slabs.
- **Concrete / stucco:** cement mottling, aggregate, air voids, board-form marks and raised plaster grain.
- **Leaves / grass:** now full opaque UV surfaces for existing meshes, with shaped mesh previews. The older cutout kernels remain only for saved v6 type-27/28 recipes; the current catalogue no longer uses them.
- **LED matrix:** recessed housings, rounded lens domes, emitter dies, small bond wires, contact pads and PCB traces. The contacts have their own metalness; lenses have their own roughness/coat response. Emission stays separate from the hardware.
- **Pure metals:** only **Tooling scale** was removed from the uncoated-metal inspector. Metal polish and surface tooth are unchanged; internal scale values remain compatible with existing presets.

### Scratches: dedicated study

**Scratches** remains a dedicated study, and the same field is now available as an opt-in layer on metal surfaces. It searches neighboring seeded cells for finite line/arc segments, allowing cuts to cross cell boundaries. Occupancy, length, width, depth, direction and curvature vary independently. Tapered endpoints prevent endless stripes; a negative groove profile and small raised lips alter surface normals, while roughness broadens inside the cut. Derivative-aware filtering reduces subpixel sparkle. The density-zero setting is an exact bypass.

Use **Panel** for the clearest assessment, orbit the light reflection, then use **Macro**. Controls include density, length, width, depth, direction spread, preferred angle, curvature, field scale and a reproducible seed. This is a surface-normal/height study, not geometric damage to the silhouette. Existing worn-polymer detail is unchanged. Metal families expose an **Enable scratches** switch; it defaults off so older presets retain their original finish.

Shader variants now compile the material-family ID as a constant, so drivers can eliminate unrelated kernels. Programs are retained through the thumbnail batch for reuse. Progress still reports real completed previews, not a fictitious GPU percentage.

## Pattern studio and vector leather (v7)

Open **Pattern studio** in the top navigation (or append `?studio=pattern`). The [2D editor](docs/shape-editor.md) supports native shapes, point-editable Bézier/freehand/SVG paths, grouped SVG imports and embedded PNG/JPEG/WebP images. The inherited library and generators below remain available alongside the new authoring tools.

**V7.10 adds Geometric Set 04: eight constructions.** Search **Set 04** in **Geometric constructions** for crossed hexagon nets, Poincaré-disk geodesics, an exact periodic Voronoi tessellation, level-three Koch islands, Archimedean counterspirals, fourth/sixth-power superellipses, Bernoulli figure-eights and a depth-coded torus-knot projection. Every design is editable vector geometry with color, finish, repeat, SVG/JSON and material export. No rug collections or palette-count variants were added.

**V7.9 Set 03** remains searchable via **Set 03**; **Set 02** and the original six geometric designs remain as well.

**V7.8 Set 02** remains available via search **Set 02**: linked racetrack loops, hexagon-square junctions, pinwheel squares, tangram, Sierpinski lace, vesica net, stepped corner inlay and ruled saddle lattice. The six earlier geometric constructions are unchanged.

**V7.7 removes the rejected rug, African and Islamic collections from the public library and deep links**, including African Diamond Carpet, Islamic Medallion Carpet, Medallion rug and Golden Cube Fade. Existing saved documents retain their embedded artwork and editing controls. Legacy factories remain for compatibility, not public recommendations.

**One thread/yarn color.** Ten stitch construction cards replace the palette-qualified entries; upper passes use the same thread color, not painted highlights. Needle points are explicit, rather than inferred from every curve bend. Ten weave family cards contain draft settings (basket sizes, twill ratios, satin shafts, rib direction), not extra pattern counts. Warp and weft use the same yarn color. The drawdown inspector identifies actual over/under crossings; color, background and direction changes do not add designs.

**Six additional geometric constructions:** Truchet circuits, octagon-square tiling, hexagon-triangle tiling, dodecagon-triangle tiling, herringbone parquet and a Hilbert meander. The existing geometric and printed designs remain. There is no replacement quota-completion claim.

Stitches and weaves are editable vector surface-relief models, not individual fiber geometry or loom-ready simulation. The 2D view uses a subtle preview-only shadow; color exports and albedo maps do not contain that shadow. Inspect relief with **3D material**. Save/import, undo, material assignments, the retained Diamond Dissolve size fade and map baking remain available. Approved leather, scratch and plant shaders are unchanged.

Each motif has a **material assignment**, not only a color: printed dye, cotton, wool pile, glazed ceramic or metal inlay, with independent roughness, metalness and signed relief. Apply to the current material, fabrics, floor tiles or a dedicated continuous pottery glaze; a **Teapot** preview is now available. Save editable JSON, export repeat or one-way border SVG, save the composed material as a preset, or export its shader and six surface channels.

All five leathers share **one generated SVG height atlas**, with distinct nappa/full-grain/bull/belly fields. Continuous coordinate distortion breaks up repeats without crossfading duplicate crease networks. The vector source is downloadable. This changes the earlier zero-input-map constraint by explicit request; the other existing shader families remain procedural.

**[Workflow, formats, import safety and limitations](docs/pattern-studio.md)**. Live vector sources are rasterized at finite resolution. Pattern slots are selected finish models, not arbitrary shader graphs; wool uses relief/sheen rather than groomed strand geometry. Arbitrary photo edges or physical bake crops are not automatically seamless. SVG import supports a safe subset, not every SVG feature.

The v6.3 grain-following clearcoat fix, finite-thickness swatch and flat-panel checks are retained. Unpatterned leather uses a signed 0.012-scene-unit height range; patterned materials use at least 0.05. Exported materials expose an asynchronous `material.userData.ready` promise—await it before a static render. See the linked guide.

## Leather and mesh-ready botany (v6.1)

**Regular leather uses rounded grain, sparse folds and fine pores**, retaining existing patina controls. **Crocodile Belly Leather** now uses a continuous hide field with shared soft creases; see the v7 replacement above. Those analytic fields have been superseded by the v7 vector source described above. Neither version is a scan or reproduction of a particular hide.

### Mapping onto your existing plant geometry

| Surface                          | Required UV convention                            | Detail                                                                                                |
| -------------------------------- | ------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Leaf                             | U across width, midrib at U=.5; V=0 stem, V=1 tip | Tapered midrib, asymmetric secondary veins, tertiary detail, subtle epidermal cells and pigment aging |
| Grass blade                      | U across **one blade**, V root to tip             | Parallel ribs, central ridge, tip gradient and fine cells                                             |
| Rose / lily petal                | U across **one petal**, V throat to tip           | Two-color gradient, delicate striations, microscopic cells, optional basal speckling                  |
| Apple / orange / strawberry skin | U wraps body, V pole to pole                      | Blush/lenticels, recessed citrus oil glands, or seeds in recessed pockets                             |
| Cactus / stem                    | U around body, V along growth axis                | Waxy cells and areoles, or longitudinal fibers, cork maturity and lenticels                           |

The catalogue's plant materials and their six baked maps are **full opaque surfaces—not silhouettes, cutouts, packed atlases or collections of stamped leaves**. Map one UV island per organ into the unit square, or reuse that square for repeated organs. Arbitrary existing UV layouts will not automatically align a midrib or petal base. Fruit pore sampling wraps around U; this does not imply every material tiles in both axes. Cactus **Rib columns** should match your actual mesh. The preview cactus adjusts its real rib geometry to that control, but the material export does not deform your model or generate spines.

Choose **Leaf**, **Grass blade**, **Petal**, **Stem** or **Cactus** in the preview selector. These are actual generated silhouettes, not masked planes. Leaf/petal normals and soft curvature are geometric; epidermal and vein detail remains procedural shader relief. Fruit skins use a sphere preview. Preview geometry is not included in the material-module export.

**Realism boundary:** species-specific venation, complex UV layouts, full plant subsurface transport, cactus spines and separate fruit-seed geometry are not inferred by these shaders. Fine cellular detail is intended for close inspection; it is filtered at distance. The 8-bit height channel may not retain the tiniest relief, while the normal map preserves more of its shading effect. Use higher-resolution bakes for close-ups.

**Scratch revision (v6.2):** only density was increased further—the accepted cut shape, bow, taper, widths and depth profile remain unchanged. Density now spans 0–12 expected cuts per cell; the first four layers retain exactly their former sampling and additional batches of four activate above the old maximum. A GPU regression compares the previous release's field at density 2.6 with the revised field and permits at most one 8-bit value of height difference. The default study density is now 6.

### Optional scratches on metals

In the inspector, open **Metal scratches → Enable scratches**. This is available on conductor/metal presets, rusty metal, the brake rotor and corrugated aluminium/zinc/steel—not paint, plants, leather, solar cells or LED packages. It defaults **off**, with no changes to polish, grain, metalness or coat values. **Tooling scale** remains absent from the pure-metal inspector.

The cut height is added to the material's existing relief, so corrugations and tooling do not disappear. Roughness and base-color response follow the same cut mask as the study. A separate **Scratch direction** avoids changing the brushed-metal direction. Settings travel with saved presets, JSON, standalone JS and the six-channel bake. Density zero is a tested exact bypass; disabling the layer restores the original appearance. This does not simulate coating removal, oxidation removal or silhouette damage.

### Reference-led surface refinement

- **Crocodile leather:** flatter belly plates transitioning toward smaller flank scales, thin folded joints, subdued fine grain and small scale pores. It no longer uses strongly domed, widely separated tile-like plates.
- **Leaves:** lobed microscopic cell walls and sparse stomatal relief; **Cell wall lobing** and **Stomatal detail** controls are under Surface detail. Still a whole UV surface, with no alpha silhouette.
- **Petals:** papillate cell relief plus filtered fine ridges, rather than reusing the leaf-cell height profile. Existing color gradients and basal spots remain.
- **Fruit/cactus:** variable citrus gland sizes, curved berry seed centers, and slightly irregular felted areoles. No bitmap input, new texture dependency or modeled spines.

See [reference notes](docs/surface-reference-notes.md) for research links and which aspects are approximated. Reference photos are never loaded by the shader or embedded in the application.

## Macro inspection

- **10–10,000% optical zoom:** mouse wheel, pinch, buttons or logarithmic slider.
- **Macro** jumps to 800%; double-click a visible point to inspect it closely.
- Drag to orbit; right-drag or shift-drag to pan. **Fit** or **R** restores framing.
- Optical zoom keeps the camera outside the surface.
- Sixteen preview assets: grooved shader ball, frozen draped cloth, rounded cube, torus knot, perforated brake rotor, smooth sphere, flat panel, hollow pipe, legacy foliage card, leaf, grass blade, flower petal, stem, ribbed cactus a flexed leather swatch and a teapot. Solar/paper/LED presets select the panel; golf-ball covers select the sphere.
- Four studio-light setups, auto rotation, wireframe and focus mode.

## Frozen cloth asset

Textile presets automatically select **Draped cloth**. It is also available for other materials. A deterministic, offline position-based simulation uses structural/shear/bending constraints, a sphere collider, floor/pedestal collisions and a small center pin area. The settled mesh is baked into `src/assets/draped-cloth.json`; **no live physics runs in the viewer**. Material edits do not change the folds. A thin rim gives the cloth visible thickness.

Reproduce the bake with `npm run bake:cloth`. The small generated geometry is intentionally included; it is not a texture or an externally sourced asset. The simulation does not implement self-collision. This is a fixed preview sample, not a general-purpose cloth solver.

## Presets and exports

Search/category filters, local presets and favorites are supported. Older presets receive v6 defaults and are bounded to their material recipe; physically incompatible legacy settings may therefore change. `recipeId` preserves a saved material's family even when its name or ID changes. Art-direction positions are retained in `tuning`.

Exports: **v6 JSON**, standalone Three.js material module, six-channel PNG/ZIP surface baking, and PNG viewport snapshot. The JavaScript export includes the pure recipe module, shared surface kernels, normalization and unminified shader source, so it remains self-contained after production bundling. It expects Three.js 0.180+ and a lit scene/environment. Exports retain the cloth/object-space mapping mode. The shader includes procedural nap and sheen; additional preview fiber geometry belongs to the preview asset, not the exported material module.

Keyboard: `/` search, `R` reset camera, `F` focus, `Space` auto rotation, `Ctrl/Cmd+S` save, `?` shortcuts.

## Rendering notes

These are **browser PBR approximations**, not Unreal Substrate slabs or measured automotive BRDFs. Optical constants are handled by material recipes. Clearcoat Fresnel replaces Three.js's fixed F0 with `((ior - 1)/(ior + 1))²`; this is not a full multi-interface spectral slab solver. Glass uses screen-space transmission.

Skin and wax use bounded **wrap-scattering approximations**, with native transmission for wax; they are not true multilayer subsurface solvers. Jersey uses V-shaped loop fields rather than a woven twill. Denim uses warp-faced 3/1 construction, undyed weft, along-yarn slub and raised-thread fading, at a finer default yarn density. Golf-ball dimples perturb normals rather than geometry. Solar cells include separators, busbars and collection fingers; LEDs have patterned emission but do not cast light onto neighboring objects or produce bloom.

The environment cubemap is generated from studio-light geometry, not fetched from a texture asset. The renderer runs on demand when idle. Library thumbnails are renders of the actual procedural materials.

## Shader preparation progress

The library compiles and renders incrementally. Its progress bar counts **completed material previews out of the current catalogue**, not an invented percentage of GPU compiler work. Each job yields to the UI; `compileAsync` uses parallel driver compilation where supported. A separate material-compilation notice appears when the active recipe changes. Rapid changes are serialized/coalesced so obsolete results do not overwrite the current selection. Retired fiber materials are kept alive until pending compilation finishes, avoiding a stalled compile when switching rapidly from cloth to paint. Driver/linking failures produce an error state rather than silently reporting success.

## Baking procedural surface channels

Choose **Export material → Bake procedural maps**. Select a 256, 512, 1024 or 2048 square resolution and a physical patch width of 1–1,000 mm. The shader is evaluated on a flat XY patch using **the same surface kernels as the viewport**. The six-channel ZIP contains (mesh-oriented plant maps are fully opaque; legacy card recipes retain their alpha):

- `base-color.png` and `emission.png`: sRGB encoded; emission intensity is recorded separately.
- `roughness.png` and `metalness.png`: linear, unlit scalar channels.
- `normal.png`: linear OpenGL tangent-space normals, +Y.
- `height.png`: linear, 8-bit signed height encoded around 0.5; decode range is in the manifest.
- `material.json` and `README.txt`: original procedural recipe, scale, color-space conventions and limitations.

Most original families use **zero input bitmap maps**. V7 leather samples a generated vector height source; user patterns may sample embedded SVG/image sources. Baked output maps are not fed back into the live material. The baker has real per-channel progress, cancellation between GPU passes, and resource cleanup.

**Limits:** botanical mesh materials always cover UV0 from 0 to 1 regardless of patch width. Width changes the physical scale of relief/normal generation; choose a width matching your asset. These are ready-to-map organ surfaces, not an unwrap or rebake of imported geometry. Other materials use a planar material swatch. Seamless tiling is not guaranteed. Height has 8-bit precision and clips to the documented range. Clearcoat, transmission, sheen, anisotropy, scattering and angle-dependent iridescence remain shader/recipe properties; static maps alone cannot reproduce the full appearance. No studio lighting or ambient occlusion is baked into base color.

## Tests

```sh
npx playwright install chromium
npm test
# Focused editor, geometry and pattern-to-material regressions
npm run test:editor
```

The development suite covers the growing catalogue, all material families, progress notifications, ZIP/PNG contents and channel values, bake cancellation, bounded recipe mappings, context-relevant inspector controls, all nine live weave patterns, independent yarn colors, thin-film uniforms and live iridescence, frozen geometry, height-aware wear, local persistence, JSON/JavaScript/PNG exports, macro zoom, extended ranges, color ramps and mobile layouts. Use `PLAYWRIGHT_EXECUTABLE_PATH` for an existing Chromium executable; software-rendering launch flags are included.

### Standalone and publication checks

```sh
# Rebuild, then exercise only the generated HTML—without a Vite server.
npm run test:standalone

# Verify GitHub holds the exact local page bytes and print its immutable URL.
npm run verify:published
```

The standalone test blocks unexpected HTTP asset requests, checks the full material library, edits cloth construction/yarn colors, checks live iridescence, and executes the exported Three.js shader. It uses an intercepted test origin, not a dev-server fallback. Browser executable/launch settings are shared with the main suite.

To exercise the actual hosted page instead, set its URL explicitly:

```sh
ALLOY_PUBLIC_URL='https://raw.githack.com/unassignedinbox/Slate/d914e1efa51ddcc4013654ce4256c5ab59e4ad12/site/index.html' npm run test:standalone
```

The remote mode confirms GitHack's notice if present. Connection failures **fail the test**; it never substitutes a local copy. Successful artifact verification is not a claim that GitHack's live runtime was tested. Some sandbox networks block direct connections to GitHack.

## Painting tools and draggable content browser

In **Texture**, the right inspector now uses the supplied channel-card pattern: all **14 channel targets**, chip removal/clear/add, retained values, **Value / Texture / Generator** sources and grouped generators. Channel cards share the editor's rounded cards, sliders and pill controls. PNG/JPEG/WebP Texture imports store actual portable PNG sources, bounded to 256 px per edge. The top-bar brush button or viewport context menu opens the **Digital brush studio**: Paint / Erase / Fill, five basic digital presets, channel toggles, brush section cards and a real live stroke canvas. The separately marked **Instrument catalogue** retains all **102 configured instruments / 10 families** for legacy source authoring, not mesh simulation. Brush/channel/source settings and real digital operations have local save, JSON round-trip, undo/redo and inherited-lock protection.

Drag the **Content browser overlay** upward for **Outliner | asset grid/list | preview + inspector together**. The material pane reuses the existing renderer, full Material inspector, controls, shared library/favorites/preset service and portable JSON/shader exporters—different layout, not a second editor or bundled prototype app. Browse materials, brushes and generators; assign a source link or drag it onto an unlocked layer. The scene/dock bounds remain unchanged under the overlay. The left Asset library uses tree rows; the full grid scrolls with fixed-size contained previews instead of pagination or height-driven cropping. Only visible/prefetch assets request bounded real shader thumbnail batches; pending thumbnails are labeled.

Layer folders support nesting, collapse/search, inherited locks/visibility, sibling moves, subtree copy/delete and ungroup. Layers and folders accept Fill or Colour masks, with colour/tolerance/softness/inversion/strength controls; zero is preserved. Colour previews evaluate imported source pixels, not teapot coverage. Source thumbnails replace generic glyphs when available.

Choose **Texture → Paint texture** to draw into a bounded 2D channel source. Pointer gestures use a basic round tip (including paint/erase, opacity/flow/strength and pressure); each completed gesture is one undo step. The saved source appears in the layer stack and **History → Last stroke** previews the actual isolated footprint. Cancellation, zero controls and inherited locks do not write pixels/history. The last gesture and source survive project-file save/reopen; the 80-step undo timeline is session-only.

**Scope:** digital mesh/UV layer and mask painting, region fills, enabled-channel PBR consumers, real brush previews, material asset preview/editing/export and legacy bounded 2D source authoring are implemented. Physical-media instrument simulation, general imported-source composition and composited PBR atlas export are **not** implemented. Native material/decal/paint surfaces share ordered live evaluation. See [digital painting](docs/digital-painting.md) for precise channel, coating, subsurface-tint and resolution limits. Mesh baking and volume SDF remain separate.

See [reference provenance, workflow and authoring model](docs/paint-tools-and-content-browser.md). Run `npm run test:painting`; production and standalone builds include the same UI.

## Shared instrument controls, Material generators and surface-point gradients

Instrument properties use the same rounded cards, shared `Slider` component and pills as the channel/Material UI. The old “Tool colour” input is replaced by an HSV **Colour picker** with saturation/brightness, hue, hex and swatches; zero opacity/flow still work.

Choose **Generator → Material studio · custom source** in a channel card. Use the current Material-studio material, a library/saved preset, or edit its shared recipe controls. The existing procedural surface renderer produces an actual bounded PNG field (including Scratch cavities, height, roughness, metalness and base colour), not a shaded thumbnail or fabricated mesh map. Parameters, output/levels/gamma/inversion and pixels round-trip in the project. Copies do not mutate the Material studio; stale/aborted jobs cannot overwrite a changed/locked source.

Choose **Base color → Gradient → Edit in viewport / Place point** for a live selected-source fill. Click the teapot to place a ball, drag balls on its surface, or move selected balls with arrow keys. Each of up to 12 points owns **colour, influence radius and weight**. Fields blend linear-RGB colours by Gaussian object-space distance; zero radius/weight mutes a point. Points stay in normalized object space when the camera orbits, rather than being screen/UV paint. Each completed drag is one undo transaction.

Layer/folder **Mask type → Gradient** uses the same editable point field. Point-colour luminance defines black/white coverage; enable, inversion and zero strength are evaluated in the selected gradient/value-fill surface preview, including ancestor folder masks and opacity. Inspector/stack thumbnails are explicitly labeled XY field slices, not a UV bake. This is a narrow selected-field preview—not a compositor for arbitrary painted layers or all blend modes.

Verification for this follow-up: **66 painting/Texture cases pass** and **11 offline standalone cases pass**. Production and single-file builds pass; `site/index.html` is **3.47 MB**.

## Texture mask painting and preview modes

Texture now has independent **Material / Mask** previews and **Lit / Wireframe / UV / Texture** views. Select a layer/folder mask to enter **Mask mode** and paint actual black/white UV coverage in Lit or packed UV space. **Face / UV Island / Object** selections use real teapot triangles/charts/components; **UV Tile** selection is available in UV mode. Object means **Body, Lid, Handle or Spout**, not the whole teapot. Fill a picked region or brush within it. New digital Paint/Erase uses a screen-space footprint across visible surface components when no region/part target is selected; the legacy UV mask brush retains its hit-component scope.

The selected mask has genuine grayscale feedback for Fill/Paint, object-space gradients, source-RGB colour fields, and decal masks. Undo/redo, cancel, locks, JSON saving and local recovery apply to mask records. On phones, Brush settings expand separately to preserve a useful canvas. Ordinary **Material / Paint / Fill layer** selection stores **Paint mode** only — it does not enable ordinary mesh/channel deposition. Existing decal placement/transforms remain functional. This is not a full PBR UV-stack compositor or a change to audited Baking.

See [mask painting, preview semantics and limits](docs/texture-mask-painting.md). Run `npm run test:masks`.
