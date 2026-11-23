# Digital painting: layers, masks and brush studio

## Workflow

Open **Texture** (`/?studio=texture`). Select an unlocked, visible layer and choose **Paint**, **Erase** or **Fill** in the viewport toolbar. The brush button in the top bar, **Brush studio** in the inspector, or a viewport context click opens the **Digital brush studio** overlay. It does not shrink the scene or load another editor/runtime.

- **Paint:** a digital tip writes enabled channel values into the selected layer, in Lit or packed UV view.
- **Erase:** removes that layer's authored pixels, only in enabled channels. It never erases lower layers or rewrites a native material recipe.
- **Fill:** click a real mesh component, UV island, face or (in UV view) tile. **Object** means Body, Lid, Handle or Spout individually, not the whole assembled teapot.
- **Masks:** select the layer/folder mask. The same digital tools now edit grayscale coverage. White/black ink selects inclusion/exclusion; **Erase removes the painted overlay**, restoring the original fill, gradient or colour source, not substituting black. Existing mask region selections constrain digital strokes/fills. The original region selection and legacy mask controls remain available.
- **Navigate:** returns pointer gestures to orbit/pan. Alt/control navigation is not deposited as a stroke. Wireframe and overlapping Source UV0 remain inspection/navigation views for the new digital tools.

Painting does not arm just because a layer was selected or a file was opened. Choose a toolbar tool or **Use paint / erase / fill**. New Paint/Erase gestures use a **screen-space footprint at mouse/pointer X/Y**, in CSS pixels. UVs store the result; they do not define the brush's diameter or shape. A round tip remains a circle through perspective, UV distortion, UV zoom and CSS scaling. Free gestures project into **all visible triangles under the footprint**, across charts, tiles and component seams. Explicit face/island/tile/object mask selections, part-targeted layers, inherited masks and locks still constrain writing. Asset drops and region Fill remain part/region scoped.

### Screen-space cursor and surface projection

While Paint or Erase is armed, hover the Lit or packed UV canvas to see a **textured brush footprint**, a contrasting outline and a small exact-input cross. The readout reports the effective screen-pixel diameter (and width × height for flattened tips). The footprint uses the same procedural tip, hardness, grain, rotation, roundness, pressure, taper and seeded dynamics as deposition. Hover predicts the next click's seed; a gesture displays its actual final dab. Intentional scatter can move that dab away from the input cross. Fill instead highlights the allowed hit component/face/chart/tile.

The Paint/Erase hint is an **unclipped transparent screen canvas**, centered on the input cross unless configured scatter intentionally shifts the tip. Its circle and texture never warp into an oval or disappear at a chart edge. The actual Lit deposit uses a frozen local-mesh-to-clip transform and front-surface depth, then transfers coverage into every touched authoring-UV triangle. It does not paint through an excluded foreground occluder; triangles crossing the near plane are clipped rather than discarded. UV gestures use the frozen displayed UV-to-CSS affine transform and can start in a chart gutter if the footprint overlaps adjacent surfaces. Both update during strokes and when the camera/zoom/brush changes, with constant CSS-pixel diameter. Masks use black/white tips; erasers use a neutral removal hint. The translucent hint previews **brush coverage**, not final lighting, refraction or the resulting scalar-channel value. It does not create a stroke, modify history, enter baked/evaluated atlases or contaminate lower-only optical/PBR captures. Locked/invisible targets, wireframe, source UV0 and zero-work brushes hide it. A footprint can extend over background/gutters, but only real covered surface texels are authored; a gesture with no surface coverage creates no history. Fill retains its region-highlight preview.

High-DPI off-screen viewports now use physical framebuffer dimensions **without applying device scale twice**. The old double scaling displaced/cropped Lit surfaces and UV paint relative to correct ray/input coordinates. UV input also converts fractional/CSS-scaled DOM rectangles to the logical coordinate system of the **last drawn camera**, independently of DPR. Stabilization smooths interior samples but leaves the first/live endpoints on the cursor; dense sub-pixel moves retain the final position. Pointer-up includes its final contact location. Orbit damping is held during a gesture and zoom navigation cancels it atomically.

The inspector's **Clear paint** removes the selected layer's complete digital overlay, without removing its native recipe, sources, channels, mask or decals. This is undoable and releases the operation budget.

## Brush studio

Five digital presets—**Studio round, Soft round, Flat tip, Pixel pen and Grain round**—use one deterministic dab engine. These are basic digital tips, not physical-media simulations. The instrument catalogue is retained separately for old source-authoring projects and explicitly marked **configuration only**; crayons, physical brushes, sprays and other catalogue families are not simulated on the mesh.

Brush properties use the editor's shared slider and colour-picker controls in section cards:

| Card                 | Implemented controls                                                                                    |
| -------------------- | ------------------------------------------------------------------------------------------------------- |
| Stroke path          | CSS screen-pixel diameter, stroke opacity, dab flow, arc-length spacing, stabilization, start/end taper |
| Shape                | Round/ellipse or square, hardness, roundness, rotation, rotation jitter, follow stroke                  |
| Grain                | None, seeded noise, lines or checker; amount and scale                                                  |
| Stroke dynamics      | Size jitter, opacity jitter and scatter                                                                 |
| Colour dynamics      | Seeded stroke/dab hue, saturation and brightness; pressure brightness                                   |
| Pressure and surface | Pointer pressure to size/opacity; painted Height to Normal strength                                     |

The **live preview is a real canvas stroke**, rendered with `digitalBrushDabs` and `drawDigitalDabs`, the same functions used by layer and mask rasterization. Its sample path varies pressure. Draw your own test stroke in the preview or reset it; preview gestures do not modify the document's layers. Erase and fill have corresponding real previews. Colour dynamics apply to Base color; masks remain grayscale. Tip-only controls are inactive for Fill. Zero size, flow or opacity writes no stroke.

Pen contact pressure changes the effective footprint and deposition together; a zero-pressure lift retains the last contact pressure at the release position. A pen and mouse may both report themselves as primary pointers, so an active gesture owns its pointer ID: another device cannot move the input marker or cancel its footprint. Touch previews exist during active contact gestures, not as stale hover hints after pinch/capture cancellation. Lit pinch navigation cancels the paint draft before moving the camera.

Stroke opacity is applied once to a complete temporary stroke. Dab flow/pressure and jitter operate within it. Increasing the number of collinear pointer events cannot make a half-opacity stroke opaque. Random seeds and pressure samples are stored with operations, so reload/undo does not invent new grain, positions or colours.

The channel panel enables/disables all 14 existing targets while retaining disabled values and strokes. Normal enables Height; disabling Height also disables Normal. Base color uses the brush colour. Other channels use the selected layer's saved scalar/colour **values**, not its separately imported Texture/Generator source. Value changes affect the next stroke; values are snapshotted into each operation. Disabling a channel bypasses its existing field without destroying it. With no writable channels, painting is a no-op.

## Layer evaluation and PBR consumers

`layer.paint` contains bounded, declarative UV operations under `alloy.digital-paint.v1`. Operation IDs are unique per layer; imported stroke samples must remain in their declared chart or face. No user JavaScript, external brush/image URLs, serialized GPU textures or executable SVG are stored. `digitalPainting` stores the current brush configuration. `lastDigitalStroke` stores one bounded PNG **operation footprint** for the real History timeline; legacy 2D source strokes remain distinct.

- Painting on an existing **native Material** row overrides its enabled PBR fields before lighting. Erasing restores its native shader fields. Its transmission, thickness, attenuation and other recipe parameters are not replaced with a fake-alpha material.
- Painting/filling an otherwise empty **Paint/Fill** row creates a sparse physical digital surface pass at that row's order. It is a digital coating, not a duplicated glass volume. Its union channel coverage, layer/ancestor opacity and inherited masks control visibility. Decals owned by the row remain above its surface pass; a higher opaque material still covers the paint.
- Sparse rows inherit **raw, linear PBR fields from lower rows only**, using an RGBA8 texture-array snapshot in authoring UV space. This is not a readback of already lit/tone-mapped RGB used as albedo. The snapshot includes underlying decal colour and painted fields; higher rows and the row itself are excluded. Captures run bottom-to-top, are cached independently of changes to the row's own stroke data, and restore renderer, materials and visibility with `try/finally`.
- One texture-array sampler holds channel fields and independent alpha. Colour values are converted to linear and premultiplied before filtering; scalar fields are premultiplied too. Shader sampling unpremultiplies, avoiding transparent-edge colour halos. Sparse channel coverage is normalized inside its union pass so stroke opacity is not applied twice. Native-owner uniform bindings survive disable/re-enable, and disabled samplers are reset before texture disposal. Native glass/wax still use the existing separate lower-only linear lit-history renderer.
- Base color, metalness, roughness, opacity, emission, occlusion, clearcoat, IOR, sheen and anisotropy/angle have physical shader consumers. Painted Height derives a tangent-space normal, RNM-composed with native/baked normal detail when Normal is enabled. It does **not** tessellate or displace the mesh. Occlusion affects indirect illumination, not albedo.
- **Subsurface is a bounded diffuse tint**, not volumetric scattering. IOR affects dielectric reflection and existing native refraction where applicable; a separate digital coat is not a new refracting volume.
- Digital Base color blends support Normal, Multiply, Screen, Overlay, Add and Darken. Scalar channels use alpha interpolation. Native material blend modes beyond Normal remain metadata; no arbitrary physical-material mixing solver is claimed.

The UV viewer renders the same evaluated lit passes as the 3D viewport. This change does not implement composited PBR atlas export, an imported-mesh painter, a UDIM-file loader, a physical-media brush engine or general composition of legacy imported Texture/Generator sources.

## Bounds and persistence

- 256 raster pixels per authoring tile, four tiles (1024 × 256), 32 real teapot charts. Sparse underlay snapshots use **128 pixels per tile**, eight packed RGBA8 field slices; small underlying details/HDR values have the corresponding resolution/range limit.
- 128 operations/segments per layer; at most 512 pressure samples per segment and 8192 per layer. New screen strokes split only at bounded sample/operation limits, not chart/part/miss transitions, and commit as one history transaction. Legacy UV records keep their original units/scopes.
- New projected records store version 1, view kind, CSS dimensions and a finite 16-number Lit MVP or 6-number UV affine matrix. Their point pairs are local CSS X/Y divided by 256, with pressure; no current camera, screenshot or executable code is needed to replay them after navigation/reload. Legacy records without a projection continue to replay as UV strokes.
- Front-depth and stencil buffers are bounded to 1,048,576 pixels / 2048 per edge; geometry mapping retains overlapping UV contributors and unions their visible coverage. Projected layer and mask strokes extrude one boundary texel into empty UV gutters for continuous linear filtering; they never dilate into another occupied face/chart or excluded component. UV storage is still 256 texels/tile, so extremely zoomed or tiny marks have that texel-resolution limit. The hint is separate from the atlas, bounded to 4 Mi pixels.
- At most 16384 dabs per segment and 262144 channel-weighted dabs per layer; bounded spacing and shape/grain parameters; texture-tip cache limited to 128 entries.
- **64 active digital channel slices per project**, including union slices; disabled data is retained without allocating its field. This bounds channel-array storage to 64 MiB and sparse underlay storage to a further 64 MiB at maximum occupancy.
- Original **64 user rows and 1 MiB complete project** budget remain in force, including mesh maps, stamps, sources and footprint preview. Budget errors reject the complete gesture/import; they do not truncate data, clear projects or change prior history.
- Pointer cancellation, Escape, blur, hidden document, view/source changes, changed target/settings, visibility and inherited locks discard drafts. Completed gestures are atomic undo/redo transactions and survive project save/import/local recovery. Session undo history itself is not exported.

## Verification

The original focused regression files are `digital-paint-model.spec.js`, `digital-paint-raster.spec.js` and `digital-paint-studio.spec.js`; cursor coverage adds the three `digital-brush-cursor-*.spec.js` files and `render-target-dpi.spec.js`. They exercise deterministic real tips, pressure/dynamics, premultiplied fields, independent channel erasing, original-source mask erasing, region fills, limits, native optical parameter preservation, lower-only inheritance, Lit/UV pixels, atomic cancellation/undo, saved vectors and phone/focus behavior. Existing layer/mask/source/runtime suites remain regression coverage, not a claim of physical-media simulation.

Run `npm run test:digital-paint` for **62 focused cases**: the original 53 cursor/DPI/device/digital cases, updated for screen units, plus nine screen-projection model, raster, mask/depth and actual application cases. Screen tests cover distorted/disconnected charts and UDIMs, gutter-centered clicks, front/near-plane depth, masks restoring their source, exact circular XY hints and actual Lit deposits, fixed diameter at UV zoom/CSS scaling, undo/redo/reload, legacy UV replay and geometry-replacement atlas invalidation. The following older checkpoints describe the pre-screen-space implementation, not verification of the latest design. The cursor correction adds native and UV pixel checks at DPR 2, CSS-scaled/fractional coordinates, stabilization endpoints, textured hover/zoom/gesture footprints, scoped mask Fill/Erase, locks/cancel and unchanged hover history/atlas. A real framebuffer test checks pixel positions and nested viewport restoration at DPR 1, 1.25 and 2.

The **earlier digital-phase** checkpoints on 11 October 2026 passed **198 distinct selected regressions**, not the entire repository/browser suite:

| Checkpoint                                | Passed | Coverage                                                                                                                                                                          |
| ----------------------------------------- | -----: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Shared authoring/document models          |    175 | Includes 22 digital models and preserved optical/order, mask, source, parts, stamp, hierarchy, history and support models                                                         |
| Chromium Canvas/controller cases          |      5 | Real tips/dynamics/opacity, linear fields, selected-channel erase, original mask restoration, lower-only cache, stable bindings, injected capture failure and disposal            |
| Digital application/WebGL cases           |      5 | Lit Paint/Erase/undo/cancel/reload/clear; UV Lid Fill and channel toggles/order; digital masks; phone/preview/focus/zero; native-glass roughness painting and recipe preservation |
| Existing UI/optical/support/runtime cases |      9 | Catalogue/source history, phone mask selection and canvas, baked AO/RNM, wax and glass/stamp order, cache reloads and single React/Three generation                               |
| Embedded offline cases                    |      4 | Instrument/channel UI, evaluated stack with embedded support, native lower-only glass, actual digital layer Fill/Erase and black mask Paint/reload                                |

The new full application run passed its four original cases after the shader fixes; the added native-glass case passed in the preservation run after correcting its test target/tool selection. The phone mask regression was repaired without weakening the canvas-size assertion and passed alongside the digital phone case. Two offline assertions were corrected to the current status text and actual mask-button name, then both passed. Every selected application/offline case checks page/console errors; offline checks abort and assert zero unexpected external requests.

`npm run build` and `npm run build:githack` both succeed. They report only the existing bundle-size warnings. `site/index.html` embeds the complete app, fonts and runtime assets. The earlier artifact was tested directly under a synthetic origin with all external HTTP assets blocked; the cursor/DPI correction rebuilds the standalone file. It has not been published by this change.

### Earlier UV-space cursor/DPI correction checkpoint

The final correction run passed **62/62 cases in 15.8 minutes**: all 49 focused digital cases plus 13 optical-preservation models. The eight live application cases include actual paint pixels under the cursor at DPR 2 in **both Lit and CSS-scaled UV**, small stabilized endpoints, camera zoom/resize, hover/gesture grain and size, mask Fill/Erase/locks/cancellation, atomic history, phone controls and unchanged native glass. Hover tests assert unchanged project/history and evaluated atlas; framebuffer tests verify physical-pixel placement and full state restoration at fractional DPR.

The rebuilt embedded page also passed its **high-DPI offline digital case** in 2.2 minutes: Fill/Erase/undo, exact UV footprint coordinates, black mask tip/painting and save/reload, with **zero unexpected external requests or page/console errors**. This is one selected rebuilt-artifact offline case, not a rerun of the entire historical offline suite. Its point/pixel fixtures account for fractional displayed canvas dimensions instead of treating CSS pixels as logical canvas pixels.

At that earlier checkpoint both builds succeeded (`npm run build`: 12.58 s; `npm run build:githack`: 12.87 s), with bundle-size warnings only. That standalone artifact was **4,007,434 bytes**. The latest rebuilt artifact is recorded below.

```sh
npm run test:digital-paint -- tests/texture-optical-stack-model.spec.js
npx playwright test --config playwright.standalone.config.js -g 'offline digital brush studio'
```

Earlier standalone SHA-256:

```text
67f76ee3a1cf0baee417112588223cf922e674ff0a53b85830543f515be78d08
```

### Pen/touch input follow-up

Four device application regressions now verify **both Lit and UV** with Chromium CDP pen/touch input at DPR 2: pressure-sized real footprints and painted pixels; a newer release location retaining the last nonzero contact pressure; foreign pointer movement/lost-capture isolation; touch lift cleanup; and atomic pinch/lost-capture cancellation. These are **browser-emulated device checks, not physical-tablet certification**. The native input observer runs at document capture because paint intentionally stops propagation at the host; the UV capture fixture first processes a real move to activate pending capture before verifying actual `gotpointercapture` / `lostpointercapture` events. Assertions remain strict.

Pointer ownership fixes stop a different primary device from moving the Lit input marker and stop an unrelated capture event from hiding/cancelling the UV footprint. Cancelled touch input cannot become an idle hover hint.

Latest qualified results on 11 October 2026:

- **77/77 shared model/picking/optical cases passed** in 37.5 s, including 31 additional mask/part-scope models outside the focused painting suite.
- The expanded **66-case digital/optical run passed 64 cases in 19.8 minutes**; two new device fixtures needed the event-observer / pending-capture corrections described above. The corrected **four device cases then all passed in 4.4 minutes**. Thus all 66 selected identities succeeded across the run and targeted rerun—not a claim of a single clean 66-case invocation or of the whole repository suite.
- The **latest standalone high-DPI offline digital case passed** in 2.0 minutes, asserting no external asset requests or page/console errors and retaining layer/mask Paint/Erase/Fill, hover and reload behavior.
- Both latest builds passed: normal **18.75 s**, embedded standalone **18.90 s**, with existing bundle-size warnings only. Native and UV pressure footprints were also visually reviewed.

The latest self-contained `site/index.html` is **4,007,697 bytes**. No project was cleared, and no commit, push or publication was performed. Physical pen/tablet driver behavior and an entire-repository run remain unverified; physical-media simulation/imported-mesh painting/composited PBR export remain outside the digital scope.

Latest standalone SHA-256:

```text
5b80eb95a86eda1dcb42cb5ea32a63a96c11720b2cf934264b1129f9bf953a42
```

### Initial screen-space checkpoint (before filtered-seam cleanup) — 11 October 2026

The later user correction supersedes the UV-space preview design in the older checkpoints above. Default digital Paint/Erase now uses a circular/shape-configured **CSS-pixel footprint at pointer X/Y**, not UV-pixel diameter or a chart-clipped mesh hint. Both deposition and preview changed: frozen Lit/UV projections transfer the shared tip into all visible touched chart/tile/part UVs. Explicit selections and part targets still restrict writes; Fill and asset-drop semantics are unchanged. Legacy UV records remain loadable without migration.

New tests verify actual round Lit paint pixels and an unclipped round preview, a gutter-centered UV click writing **both Body/1001 and Lid/1002**, constant tip size through zoom/CSS scaling, atomic undo/redo, reload/navigation, distorted disconnected charts, near-plane clipping, thin foreground occlusion, mask erase restoring its original source and bounded declarative records. Reload testing exposed a startup atlas replay on the placeholder mesh; digital atlas cache signatures now include final geometry identity, with disposal/rebinding regression coverage.

- The selected integrated run finished **74 passed / 1 failed in 21.9 minutes**. All original 53 digital/cursor/DPI/device cases and 13 optical models passed, including all four browser-emulated pen/touch cases. The sole failure was the new CSS-scaled circle's alpha bounding-box fixture: one physical edge pixel produced a 0.531 CSS-pixel width difference against a strict `< 0.5` assertion. The fixture now allows less than one CSS pixel of raster quantization while separately requiring unchanged analytic radius and roundness.
- The corrected UV case then passed **1/1 in 1.2 minutes** on the unchanged application/build. Thus **all 75 distinct selected identities passed across the integrated run and targeted recheck**, not one clean 75-case invocation and not the entire repository suite. Earlier 70-model and 14-raster/model runs overlap these identities and are not added to the total.
- The rebuilt standalone file passed its **high-DPI offline digital case: 1/1 in 1.9 minutes**. It checks real layer/mask Fill/Paint/Erase, undo and saved vectors/reload, with zero unexpected HTTP assets or page/console errors.
- Both builds passed (`npm run build`: **12.58 s**; `npm run build:githack`: **12.02 s**), with only existing bundle-size warnings. Formatting and `git diff --check` pass; the allowed preview host returns HTTP 200.

That pre-gutter-cleanup screen-space standalone file was **4,019,951 bytes**, SHA-256:

```text
2cafc5e4701963be5c1d982c9bf890f2c1a1be8dc70bcd99973b00b2beb57435
```

The live preview is available. No current commit, push, publication, whole-repository run or physical-tablet/driver verification is claimed.

### Final seamless screen-space checkpoint — 11 October 2026

Final screenshot review revealed a filtered UV-chart hairline even though both charts were authored. Projected layer and mask rasters now pad **one texel into empty gutters only**, preserving full boundary coverage under linear filtering without growing a stroke into another occupied face/chart or selected-out component. Mask replay preserves that padding instead of clipping it away a second time. The real Lit circle was visually reviewed again: the chart hairline is gone. Raster tests also require full filtered alpha on both sides of disconnected chart seams.

The final application and rebuilt artifact were verified after this change:

- **75/75 selected regressions passed in one clean invocation, 22.1 minutes**: all 62 focused digital/cursor/DPI/screen-projection cases plus 13 optical-preservation models. This includes round real Lit paint, actual two-UDIM UV writing, camera/reload/cache, pressure/release alignment, foreign-pointer ownership, touch pinch/lost capture, masks/selection/locks, undo/redo, channels and phone behavior.
- The final standalone artifact passed its **high-DPI offline digital case: 1/1 in 1.9 minutes**, with zero unexpected HTTP assets or page/console errors.
- Both final builds passed (`npm run build`: **12.26 s**; `npm run build:githack`: **12.01 s**) with bundle-size warnings only. Formatting and `git diff --check` pass; the running preview accepts its proxy host (HTTP 200).

These are selected suites, not the whole repository or a physical tablet/driver test. Earlier runs above are historical and are not added as unique tests.

The **final** `site/index.html` is **4,020,340 bytes**, SHA-256:

```text
d764dfe2015e2c32ca916d0899ab566b517062867ecdde48549373583a873dfa
```

No current commit, push or publication was performed. The live preview remains running.
