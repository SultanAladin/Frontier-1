# Texture layer evaluation and locked mesh support

## Implemented scope

Texture's native material snapshots (including glass/wax) and 3D/UV decal components use one shared **bottom-to-top surface-pass order**. An opaque material above a stamp covers it; moving that material below the stamp puts the stamp on top. The order is independent of the selected inspector row. Opacity, visibility, ancestor opacity and supported layer/folder masks reveal the lower result rather than discard an entire part.

Materials use their existing native procedural/PBR recipes. A library drop onto an ordinary Material row now saves and renders a **global material snapshot**. A drop onto the actual mesh still targets only the hit Body, Lid, Handle or Spout. Existing part snapshots and old authoring rows are not renamed or deleted. Built-in legacy material-source IDs without snapshots can still be resolved; missing custom snapshots require reassignment rather than an invented material.

The renderer forces ordinary opaque/alpha authored material surfaces and decals into the **same transparent render queue**, even for fully opaque material passes. Changing only numeric `renderOrder` would not fix Three.js's separate opaque/transparent queues. A depth-only geometry pass preserves correct self-occlusion of the teapot without adding a color layer. A layer's material precedes its own decal components, and all its components precede the next higher layer.

### Ordered native transmission

A bounded **linear lit-history** renderer evaluates the same surface order without relying on Three's separate transmissive/transparent queues. Ordinary consecutive passes are batched. Before each optical surface, the accumulated **lower** result is copied into a separate mipmapped history target. Its native physical shader reads that target; higher material/stamp rows never enter its refraction background. The resulting surface is then blended into the accumulation before higher rows are drawn. All lit surface passes use the same linear accumulation and one final display transform, including the zero-transmission case, so hiding/masking an optical row does not switch the colour space of lower paint.

Native `transmission` remains positive: glass retains `0.97`, and wax retains its recipe's translucency. IOR, thickness, attenuation, dispersion and native roughness/refraction math are not replaced with fake alpha. Only the native shader's framebuffer sampler is redirected. Three's required auxiliary transmission allocation is kept small and its renderer setting restored afterward. Roughness sampling is clamped to allocated mip levels. The reader and writer are always separate targets. Completely buried surfaces are conservatively culled only when higher, full, unmasked nonoptical material coverage proves every affected component covered; masks and partial opacity do not qualify.

The history pair is capped at **1,048,576 pixels / 2,048 pixels per edge**, independently cached for native and UV views, resized on demand and disposed with the viewport. Half-float linear storage is used when supported; hardware without float colour-buffer support uses bounded linear RGBA8 with limited highlight range. Shadows are primed from the complete visible scene, then frozen for the individual colour passes so higher rows cannot leak into history. Depth-only mesh support preserves self-occlusion. Helpers are drawn after display mapping. Renderer, visibility, native material side and parent traversal state are restored with `try/finally` on success or failure; masks and wireframe retain their independent direct preview paths.

This is **ordered screen-space native transmission**, not a new volumetric medium solver: thickness remains the native recipe approximation, screen-edge/offscreen information is unavailable, and multiple optical rows sample previously lit surfaces rather than tracing rays through nested solids. White fallback is conservatively retained under transmission and cutouts.

This remains ordered, lit surface **alpha-over evaluation**, not a general arbitrary-material mixing solver. **Digital Paint / Erase / Fill is now implemented**: sparse ordered coatings or native-row PBR field edits, with channel-specific erasing, masks and lower-only raw-field inheritance. See [digital painting](digital-painting.md) for exact consumers and bounds. Native material blend modes beyond Normal, general imported Texture/Generator-source evaluation and composited PBR atlas export remain outside that phase. The selected gradient-field source preview is retained when no authored material is active.

## Layer 0

**Layer 0 · Mesh base** is derived system data, outside the 64 editable user layers and their drag/drop/history controls:

- white `#ffffff`, locked, hidden from the editable stack, nonpaintable;
- inspectable using the small locked system row at the bottom of Layers;
- automatically bypassed on parts covered by fully opaque, unmasked Normal materials;
- retained behind partial opacity, masks and cutouts, so their uncovered pixels have a valid lower result;
- restored automatically when upper coverage is hidden, removed or set to zero opacity;
- not selectable, movable, duplicable, deletable or unlockable as a user layer.

Saved projects do not need an injected editable Layer 0 record. Their existing Base material authoring row remains their own editable data. The system white fallback is separate from that legacy row.

## Genuine baked AO and normals

Open Baking from Texture (or use **Inspect locked system Layer 0 → Bake matching teapot support · 512**). This selects the existing real high/low mesh baker, with a **matching 20-segment low teapot** and the existing 24-segment high teapot. The default support job is 512 with AO, tangent normal and bevel normal enabled. Ordinary Baking opened outside Texture keeps its existing demo geometry and settings behavior; audited bake deliverables and algorithms are not regenerated or modified by this feature.

Successful compatible results are embedded in the current Texture project's optional `meshMaps` field. This does not replace, clear or reorder user layers. The cross-workspace handoff persists the project; it does not create a Texture undo entry. Re-baking one role merges that role while retaining other saved roles. Imported meshes are not assumed compatible by name: automatic handoff requires the built-in low geometry's explicit chart and tessellation tags.

Baking uses an 8 × 4 atlas of 32 patches; Texture uses four authoring UDIM tiles. The shader explicitly remaps authoring part/chart coordinates to Baking's original patch atlas. UV0 remains unchanged. Matching low tessellation avoids treating normals baked in a different low-mesh tangent basis as interchangeable.

- **AO:** linear non-color data; multiplies indirect diffuse/specular illumination, including indirect clearcoat and sheen. It does not multiply albedo, emission or normal RGB.
- **Normals:** linear tangent vectors; +Y OpenGL / −Y DirectX conventions are retained independently per normal map.
- **Bevel + normal + native material detail:** reoriented normal mapping (RNM), then transformed into view-space lighting. Native procedural relief/flakes and clearcoat-normal hooks are preserved.
- **Replacement:** every rendered material/decal pass receives mesh support. Replacing Layer 0's white color does **not** remove AO or normals.

No maps are fabricated at startup. The system row explicitly says when no actual baked maps are attached. PNG support is bounded to square 16–1024 power-of-two maps, a 700 KB combined support budget, and the existing 1 MB complete project limit. External URLs, wrong layouts, invalid headers/dimensions or unknown roles are rejected. Storage/budget failure leaves the previous Texture project intact and the completed bake ZIP available. Texture decode swaps are atomic: failed or superseded bundles do not dispose live support, and returning to the displayed snapshot cancels a pending replacement. This is a portable authoring bridge, not a new general mesh importer or UDIM-file loader.

## UV appearance

The packed UV view receives an offscreen render of the **same ordered material/decal/digital-paint passes**. It no longer redraws lower stamp artwork on top of an upper material. Mask view still uses its independent grayscale atlas, and decal transform outlines, hit tests and UV navigation remain geometry-backed.

This is a **lit appearance preview**, not an exported Base Color / Roughness / Normal atlas. Optical UV rendering samples its lower result at the same chart pixel with mip level zero, rather than reusing a perspective camera's screen-space ray coordinates or bleeding roughness mips across adjacent charts. It still uses the native material's lighting/attenuation recipe; it is not a volumetric UV refraction atlas. Display mapping is applied once, with the SRGB8 UV framebuffer's hardware transfer taken into account. A temporary UV projection and depth/wireframe/visibility/frustum/shadow state are restored with `try/finally`, including failed renders. Onscreen camera culling does not omit offscreen charts or decals from the atlas. UV updates can run while native onscreen drawing is paused.

## Optical/evaluation checkpoint verification (before digital painting)

### Ordered-transmission continuation · 2026-10-10

- The focused pre-fix glass regression failed with **zero framebuffer difference** when hiding glass above car paint, proving the separate-queue bug. The same regression now passes with positive native transmission.
- **153 shared model cases pass (55.2 seconds)**, including all 20 support/evaluation cases and 13 new optical ordering, sampler/native-parameter, bounded allocation, exception restoration, resize/cache/disposal and read/write feedback checks.
- **13 distinct targeted browser cases verified** across the final compatibility run and focused follow-ups: the six original evaluation cases, four native optical cases, and Lit cancellation, 390px phone and Wireframe mask checks. The 11-case compatibility run passed 10; wax hit a too-short 20-second software-GPU readback deadline. Measured blocked shader/readback intervals reached **27.4 / 15.7 / 51.1 seconds** with concurrent SwiftShader jobs. A follow-up passed every wax framebuffer comparison but exhausted the six-minute case deadline at final save. With the pixel thresholds unchanged, a 120-second readback deadline and isolated ten-minute case budget, the wax case passed (**3.1 minutes**), including hide/zero/black mask, partial opacity, reorder, undo and JSON save. AO/RNM and the genuine 512 Bake → Texture handoff passed separately (**2 cases**).
- Optical checks cover glass/wax above lower paint and buried below opaque paint, stamps above/below glass without a covered lower stamp leaking into history, multiple optical rows with baked support, paused UV updates and viewport resize/restoration. Native shader values are read from actual rendered materials; profile-normalized IOR is respected rather than assuming an unbounded input overrides a recipe.
- **3 targeted offline cases verified** on the rebuilt standalone page: independent mask painting/state-only Material mode, ordered car-paint/stamp plus embedded three-role mesh support, and native ordered glass plus UV/history/undo. The initial offline run passed two and failed the new glass test's incorrect literal IOR expectation; its corrected profile-normalized expectation passed in **1.8 minutes** with **zero external requests**.
- **All 6 cached-runtime cases pass (2.2 minutes)**, including deliberately replayed stale React generations and project-preserving recovery. No project clearing, second runtime, ordinary mesh painting or per-channel atlas export was added.
- Production and standalone builds, syntax, Prettier and whitespace checks pass (existing chunk-size warnings only). Preview-host HTTP status is **200**. The rebuilt `site/index.html` is **3,929,201 bytes**, SHA-256 **`368da494ca499f8c1c11b9ef4901c6dcbb477f764d7f09db72209ad0476eb30e`**. This is local/offline verification, not a live publication check. No commit or push was requested/performed.

### Earlier opaque/alpha/support checkpoint

The following records the completed earlier checkpoint, including its previous artifact hash, not the rebuilt ordered-transmission artifact.

New regressions live in `tests/texture-stack-evaluation-{model,studio}.spec.js` and `tests/texture-optical-stack-model.spec.js`; `npm run test:stack` runs them. They check layer order, global snapshots, protected fallback, inherited coverage, undo/redo, actual framebuffer coverage, UV evaluation, AO/RNM shader consumers, bake provenance, portable persistence and exception-safe renderer restoration.

The 20 evaluation model regressions pass, including async decode cancellation/resource preservation and exception-safe UV frustum restoration. The initial 44-case browser compatibility sweep passed 41 cases (including all six evaluation cases); its phone layout, alpha-aware Wireframe readback and Lit-cancellation checks passed a focused follow-up. The final post-review run passed all **9 browser cases** (all six evaluation cases plus phone, wireframe and Lit cancellation), including genuine 512-map JSON download after returning from Baking. Shared authoring/document models passed **109** cases, instrument configuration **7**, and heritage patterns **24** (140 distinct model cases). After the final indirect-clearcoat/sheen AO correction, the AO/RNM browser case passed again (**1 case, 1.2 minutes**). The standalone compatibility run passed **6 of 7** cases; the remaining check expected the obsolete “preview is not composited” label. Its corrected source-paint scope assertion and the new offline ordered-stack/embedded-support case then both passed (**2 focused cases, 1.1 minutes**). The latter checks actual native material/stamp framebuffer coverage with embedded AO/normal/bevel consumers and zero external requests. All **6 cached-runtime checks passed (2.0 minutes)**: frozen React/Three dependencies, cached reloads and workspace switching, deferred imports, deliberately replayed stale Pattern and Texture React generations with project-preserving recovery, and iframe preview reloads.

Production and single-file builds pass (only existing chunk-size warnings). `site/index.html` is **3,919,893 bytes**, SHA-256 **`f8a82243bb552b5108052c7a853ea460eab5403e145c942e709c45ae0745b82c`**. This is local/offline artifact verification, not a new publication or a live GitHack verification.

A genuine default **512 × 512** worker job with **low 20 / high 24** produced inspected AO, tangent-normal and bevel-normal PNGs. The saved support is **388,728 bytes** (700,000-byte budget); the complete default Texture project is **393,593 bytes** (1,048,576-byte budget). PNG sizes: AO **40,601**, normal **119,915**, bevel normal **130,715** bytes. Inspection artifacts are kept in ignored `.playwright/layer-evaluation/bake-512/`, not audited deliverables. PNG CRCs/dimensions and linear vector quantization were checked: normal lengths are **0.99576–1.00413**, bevel-normal lengths **0.99514–1.00492**, before the shader's renormalization. Near-flat tangent normals are expected for this smooth low/high teapot pair; bevel response is distinct. Existing open patch boundaries, chart gutters and UV edge rasterization remain properties of the source mesh/baker, not a watertightness claim. Synthetic PNG fixtures test the shader/math; the Bake → Texture test separately checks genuine worker-produced high/low maps. Passing these tests is not a claim that ordinary painting or full per-channel composition has been implemented.
