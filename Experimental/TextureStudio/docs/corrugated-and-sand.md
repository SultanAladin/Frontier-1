# Corrugated sheets and granular sand

Four additional materials are available in the library:

| Preset           | Category | Construction                                                                                                                 |
| ---------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Corrugated Zinc  | Metal    | Rounded corrugation with a zinc conductor appearance, spangle and adjustable finish.                                         |
| Corrugated Steel | Metal    | Zinc-coated garage/cladding sheet, initially an IBR-style box rib; rounded and roller-door rib profiles are also selectable. |
| Sand             | Nature   | Individual seeded mineral grains with size distribution, cap/facet relief, color, roughness and specular variation.          |
| Layered Sand     | Nature   | Up to four independent grain beds, composited top-to-bottom or exposed as sediment bands.                                    |

The existing Corrugated Aluminium pipe preset remains unchanged.

## Corrugated sheets

The new sheets default to a **Panel** preview. In the right inspector, **Sheet corrugation** exposes profile, rib frequency, depth and direction. Surface detail controls weathering and spangle character. Optional metal scratches remain available with their own direction.

The steel preset represents **galvanised steel with a zinc coating**, rather than implying exposed bare steel everywhere. IBR is a practical garage/cladding reference; it is not a certified sheet-profile model or structural simulation. South African roofing listings identify the IBR sheets as galvanised, zinc-coated steel. [1](https://www.cashbuild.co.za/shop/ibr/16424-galvanized-roof-sheeting-ibr-profile-54m-2050000070075.html)

The originally requested “tin” preset was corrected to **Corrugated Zinc** at the user's clarification. It is a zinc-sheet appearance, distinct from the zinc-coated steel garage profile. Legacy `corrugated-tin` URLs resolve to the corrected preset; legacy `corrugatedTin` recipe settings remain readable without erasing saved custom names or controls. These are artistic PBR approximations, not measured zinc/steel BRDFs.

Ribs are **normal/height relief**. They do not change sheet thickness, topology or the smooth panel silhouette. Exported normal/height maps include the selected profile; height range expands with rib depth instead of silently clipping the new deeper ribs.

## Sand: each grain is distinct

Sand borrows the useful idea of independently seeded particles from the car-paint flake system, **not its metallic optics or clearcoat**. The new grain field uses jittered, size-weighted cellular sites with variable radii, contact/gap masks, rounded caps and seeded facet directions. Size, color/mineral identity, roughness and specular use separate deterministic random values tied to the same grain identity.

Controls include:

- Nominal grain size and size variation.
- Grain roughness plus grain-to-grain variation.
- Grain specular strength plus grain-to-grain variation.
- Mineral IOR, roundness, facet tilt, height relief and coverage.
- Repetition and seed.

Sand is clamped to **metalness 0, clearcoat 0** and no automotive iridescence/transmission. Its specular response uses dielectric Fresnel `F0 = ((IOR − 1) / (IOR + 1))²`, modulated by the grain's specular strength. This is evaluated per grain in the real material shader, not simulated by putting bright dots into albedo.

The nominal size range is **0.0625–2 mm**, following the standard sand-size boundary. [1](https://geologyistheway.com/sedimentary/grain-size/) Quartz defaults to IOR **1.544**; quartz is commonly listed around 1.544–1.553. The wider inspector range accommodates other mineral/art-direction choices, not a claim of measuring their optical properties. [3](https://www.gemselect.com/gem-info/refractive-index.php)

Size assumes **0.01 scene units per mm** at repetition 1 (one scene unit = 100 mm). It is a nominal procedural size/distribution, not a guarantee that each cellular contour is a mathematically perfect physical sphere of that diameter. Changing repetition changes the apparent physical scale.

Use **Macro** or optical zoom to inspect individual grains: a full sheet at normal zoom can contain sub-pixel grains. Unresolved grains blend to bounded aggregate color/roughness/specular, and their normal tilt is filtered to reduce distant aliasing.

## Layered sand

The material-local **Sand layer beds** editor lives in the right inspector; it does not replace or erase the separate Texture Studio layer project.

- Select, name, add, remove, enable/disable and reorder up to four beds.
- Each bed owns its mineral colors, grain size/distribution, roughness/variation, specular/variation, IOR, roundness, facet tilt, relief and coverage.
- **Stacked grain beds:** bed 1 is uppermost; coverage/individual grain gaps expose lower grains. No paint-like clearcoat is introduced.
- **Sediment bands:** each band uses its own bed properties. Relative thickness, repetition, waviness and direction control the strata.

The initial beds are quartz-rich surface grains, a finer pale bed and a coarser warm bed. Edits to one bed do not change the properties of the others. Zero specular, zero variation/relief and disabled beds are preserved in saved presets.

## Rendering and exports

The same kernels are used by the viewport, procedural material-map baker and portable Three.js shader export. Material JSON and saved presets contain the full grain-bed settings. Standalone shader exports embed the normalizers and grain kernels; they do not need this repository at runtime, apart from the existing Three.js dependency.

The **six-map patch export** retains base color, roughness, metalness, normal, height and emission plus the complete recipe. Sand's metalness map is black. Individual grain IOR/specular and the layer composition are retained in JSON/shader properties, but there is **no separate specular texture in the six-map export**; static maps alone cannot reproduce that spatial/view-dependent optical response. No studio lighting or ambient-occlusion shadows are painted into the albedo.

These are bounded **surface/normal shader approximations**, not millions of grain meshes, granular-fluid simulation, physical erosion, volumetric translucency, a displaced silhouette or measured material scans. Sand height remains 8-bit in map exports; use the live/portable shader for close-up surface detail.

## Tests

```sh
npm run test:granular
npm run build
npm run test:standalone
```

Coverage includes finite controls/zero values, independent bed settings and limits, conductor versus dielectric response, real GPU rendering, corrugation profiles, bed operations, saved JSON, executable portable shader construction and actual PNG/ZIP channel variation for all four materials.
