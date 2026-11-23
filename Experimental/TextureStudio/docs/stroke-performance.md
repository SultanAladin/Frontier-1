# Stroke performance: why painting lagged, and what changed

## Diagnosis (measured, not guessed)

The brush *preview* lives on the GPU (a textured footprint shader / screen
canvas), but stroke *deposition* was never GPU-resident. Every animation
frame of a drag, the draft document republished the growing stroke and the
surface controller rebuilt the layer's whole channel atlas from scratch:

| Per-frame cost (old path) | Where |
|---|---|
| Re-rasterize **every** operation × **every** channel into fresh 1024×256 canvases (Canvas2D `drawImage` per dab) | `rasterizeDigitalChannels` |
| `getImageData` per channel + per-texel sRGB→linear `Math.pow` loop (262,144 texels × up to 13 channels ≈ 6 ms/channel in Node) | `createDigitalPaintAtlas` |
| Allocate a new `Uint8Array` + `THREE.DataArrayTexture`, upload the full atlas, dispose last frame's texture | `createDigitalPaintAtlas`, `digitalPaintSurface.update` |
| Full-history dab regeneration just to read the last dab for the cursor (plus an HSV hex string per dab) | `digitalBrushCursor` |
| Rebuild triggered even by blend-mode changes (blend was in the atlas JSON signature) | `digitalPaintSurface.update` |

That is O(total painted history) per frame, so strokes got slower the
longer they ran — cursor lag plus whole-viewport FPS drop. Masks had the
same shape: `rasterizeMaskPaint` replayed every mask operation per draft
frame. Screen-space strokes additionally re-projected per channel.

## Update — deposition now runs on the GPU

The incremental session below removed the *history replay*; deposition
itself still ran on Canvas2D. `createDigitalPaintSession`
(`src/digitalPaintGpu.js`) is now the default session the surface
controller builds, and it keeps every stage in render targets:

- Dabs are drawn as **one instanced quad batch per stroke step** into
  per-channel scratch targets. The tip falloff (hardness smoothstep,
  roundness, square/round), grain (noise/lines/checker, including the
  CPU tip's exact seeded noise hash) and the region clip are evaluated
  in GLSL; region clip masks are rasterized once per region from the
  same `Path2D` geometry the CPU path clips with.
- Finished operations fold into persistent per-channel **base targets**;
  the growing operation repairs only the changed rectangle (rect-clear
  plus a full dab-sequence redraw clipped to it — the same sequential
  source-over rule as the CPU session), so per-frame cost stays
  O(new dabs), not O(history).
- Composites are fullscreen passes (stroke opacity still applied
  exactly once; erase is destination-out factor blending), and the
  published atlas is a `WebGLArrayRenderTarget` whose
  `DataArrayTexture` binds directly as the material's `sampler2DArray`.
  No canvas readback, no per-texel packing, no slice re-uploads.
- Fallbacks are structural, not best-effort: no WebGL2 → CPU session;
  any GL failure mid-stroke → CPU session rebuild, painting preserved.
  The live backend is observable as `data-digital-paint-backend`
  (`gpu`/`cpu`) on the viewport canvas.

Deliberate divergences from the CPU reference (which remains the
byte-exact fallback): compositing blends in linear space instead of the
canvas's sRGB space, and the tip is evaluated analytically per fragment
instead of sampling the 64px tip bitmap (the `pixel` preset still snaps
to tip texels). Dab placement, colours, determinism and opacity rules
are unchanged, so documents round-trip identically.

Evidence: `node scripts/verify-gpu-packing.mjs` checks the dab instance
packing, support detection and factory fallback; all session shaders
parse under a GLSL grammar check; `tests/digital-paint-gpu.spec.js`
proves the app reports the `gpu` backend and that paint → erase → undo
changes and restores real viewport pixels in a browser (Playwright runs
in CI; this sandbox has no browser).

## Update — masks, screen projection, thumbnails and field baking on GPU

The remaining CPU stroke paths moved to the GPU through one shared kit
(`src/paintGpuKit.js`: the instanced dab pipeline, region clips and
premultiplied composites extracted from the channel session — no second
implementation of the tip, grain or opacity rules):

- **Mask strokes** (`src/maskPaintGpu.js`): an incremental GPU twin of
  `createMaskPaintSession` with the same fold/takeover/repair rules.
  Legacy radial dabs draw through an instanced gradient-stop shader;
  digital mask dabs use the shared tip; region fills and clipped
  composites mirror the canvas sequence.
- **Mask-field baking**: `rasterizeMaskField`'s per-texel loop (fill,
  Gaussian point-gradient luminance, colour-match, stroke blend, invert,
  strength) is a single fragment shader fed by the baked position table
  as a float texture; owner fields multiply into the coverage target
  with `DstColor × Zero` blending instead of canvas `multiply`.
  `createMaskCoverage(geometry, renderer)` (`src/maskCoverageFactory.js`)
  picks GPU when a WebGL2 renderer exists and delegates the controller's
  whole life to the CPU chain after any GL failure.
- **Screen-projection strokes** (`src/screenPaintGpu.js`): the per-texel
  layout (`texels()` table) uploads once as float textures; a frozen
  projection renders depth + front-face targets once (16-bit face ids);
  each stroke's dabs deposit into a screen-space stencil target through
  the shared dab pipeline, and a transfer pass performs the scope test,
  projection, depth acceptance, bilinear stencil sampling and one-texel
  gutter dilation per stroke. The projected layout is therefore cached
  on the GPU instead of being recomputed per stroke on the CPU.
- **Thumbnails**: `digitalStrokeThumbnailGpu` deposits stroke operations
  into render targets on a small shared renderer and reads back exactly
  once per finished stroke to crop the 256×128 history image (a data URL
  is inherently a CPU artefact; rasterization no longer is). The
  commit-time coverage probe uses the same stack's cached marks.

Deliberate divergences from the CPU projection reference (which remains
the fallback and the pixel reference): the GPU layout keeps one mapping
per atlas texel (the last in table order) where the CPU retains every
overlapping mapping and lets the highest stencil alpha win; depth
acceptance compares the texel's own projected depth against
hardware-rasterized depths at neighbouring pixel centres instead of
analytic per-face depths at the exact XY. Both matter only for genuinely
overlapping charts and near-parallel touching faces.

Evidence: `node scripts/verify-mask-gpu.mjs` proves the legacy dab loop,
the gradient uniform packing (vs `compilePointGradient`) and the stencil
sizing arithmetic; `node scripts/verify-glsl.mjs` parses all 17 shader
blocks; `tests/mask-paint-gpu.spec.js` paints projected and gradient
mask strokes in a browser; `tests/texture-mask-paint-studio.spec.js` and
`tests/screen-paint-studio.spec.js` now exercise the GPU paths
end-to-end in CI.

## Fix

Deposition is unchanged mathematically (same dab stream, same Canvas2D
compositing, stroke opacity still applied exactly once per operation), but
it is now incremental:

- `digitalBrushDabsFrom` / `digitalBrushLastDab` (`src/digitalBrush.js`)
  resume the deterministic dab stream at any index. Appending samples only
  appends dabs (spacing is arc length from the stroke origin); the seeded
  RNG consumes exactly 8 draws per dab, so resumed dabs are bit-identical.
  The cursor now computes only the final dab instead of the whole stream.
- `createDigitalPaintAtlasSession` (`src/digitalPaintRaster.js`) keeps
  per-layer channel canvases across frames: finished operations fold into
  persistent base canvases once; the growing operation deposits only its
  new dab tail into a scratch canvas; the atlas byte packing runs over
  the changed rectangle only; one `THREE.DataArrayTexture` stays allocated
  and only dirty slices re-upload via `addLayerUpdate`. History edits that
  break the append-only prefix (undo, clear, channel-set change) fall back
  to the one-shot builder. ⚠️ The final dab is clamped to the stroke
  endpoint, so appending samples *moves* it — the tail from that dab on is
  repaired (rectangle clear + full dab-sequence redraw), never appended
  over.
- `createMaskPaintSession` (`src/maskPaintRaster.js`) applies the same
  pattern to mask strokes: digital ops keep their mark-scratch + clipped
  composite; region/legacy ops deposit directly on the presented canvas /
  fold directly, exactly as the one-shot path draws them (an intermediate
  scratch would blend twice and drift soft edges by a byte or two).
- Blend, normal and height-strength are uniforms again: changing them no
  longer rebuilds any atlas.

## Evidence

`node scripts/verify-stroke-incremental.mjs` (needs
`npm i --no-save @napi-rs/canvas`) drives a 60-sample stroke growing
sample-by-sample — plus endpoint replacement, interior smoothing, tapers,
erase, a second operation and undo — through both paths:

- Digital atlas: **38/38 frames byte-identical** to the one-shot replay
  (4,194,304 bytes compared per frame).
- Mask sessions: byte-identical except one destination-out edge case
  (4 bytes within ±1 — Canvas2D does not guarantee bit-identical
  premultiplied rounding across equivalent draw sequences at dab
  falloffs).
- Timing (software canvas, 3 channels, 30 growth frames):
  **~4–5 ms/frame incremental vs ~41–45 ms/frame full replay (≈8–11×)**.
  In a browser the gap is wider: canvases are GPU-accelerated and only
  dirty texture slices cross to the GPU.
- `tests/digital-paint-incremental.spec.js` repeats the equivalence check
  against a real browser canvas.

## Remaining known costs (not yet addressed)

| Item | Note |
|---|---|
| `normalizeDigitalPaint` per rAF | Draft publishing still revalidates all operations (O(total samples), no rasterization). Next lever if very long documents profile hot. |
| `paintingPointer` allocations | A camera `Matrix4` + projection object is built per pointermove even though a stroke's projection is frozen at pointer-down; the cursor controller also rebuilds one per frame. Cheap individually, worth memoizing. |
| Coalesced pointer events | Input sampling is still one sample per DOM pointermove; `getCoalescedEvents()` would smooth fast strokes now that deposition is cheap. |
| Sparse underlay captures | `digitalPaintSurface.prepare()` still keys 8-slice underlay captures on JSON signatures of neighbouring layers; stable during a stroke, but large documents pay the stringify on dirty frames. |
| CPU reference paths | The CPU sessions/projection/field in `digitalPaintRaster.js`, `maskPaintRaster.js` and `screenPaintProjection.js` remain as fallbacks and pixel references; they still run wherever no WebGL2 renderer exists (and `screenOperationHasCoverage` remains the probe of last resort at stroke commit). |
