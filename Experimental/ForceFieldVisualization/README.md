# Force field visualization

One renderer for a horizontal slice of a force field, used by the Force Field editor and the Particle editor (and,
later, the Frontier editor). The look is the eos Force Field Debugger's vector field; the range is 100 m to 100 km.

![Map at 100 km with cascades](../../Exhibits/ForceFieldEditor/Images/ForceFieldMap100km.jpg)

## Projections

| Mode    | Use                                                         | Heatmap drawing                         |
|---------|-------------------------------------------------------------|-----------------------------------------|
| Map     | Top-down, log zoom about the cursor, pan, double-click to centre | One image, affine transform         |
| Oblique | The slice as a tilted plate; source columns stand on it    | One image, affine transform             |
| Camera  | Any view-projection (column-major, WebGPU depth 0–1)        | Projected cells on an offscreen layer   |

## What it draws

- Turbo heatmap (the eos colour stops), smooth or eos cells; quantities: horizontal speed, east, south, vertical,
  vorticity ω, divergence ∇·v; automatic scale at the 98th percentile, eased between refreshes, or fixed.
- White arrows with 5 px heads at ±0.55 rad (eos); ⊙ / ⊗ where vertical flow dominates, measured against the view's
  median so uniform gravity marks nothing.
- Tracers riding the sampled lattice at a display rate (direction and structure, not real-time travel).
- Source outlines by contribution (flow teal, accelerate amber, damp violet): circles, boxes, columns, tracks,
  bearing arrows, labels that avoid each other; small sources keep their dot and drop their label at wide spans.
- Scale bar (m / km), legend, probe crosshair, cascade overlay, and a minimap of the authored extent with the view's
  outline (or the camera's ground trace).

## Level of detail

The lattice cell is the power of two nearest `CellPixels` on screen, snapped to the world: panning never shimmers
and each halving of zoom is one level. The caller's sampler receives the cell size (`CellSpan`) so terms shorter
than two cells can fade, as a coarse cascade would average them. A sample costs about 1 µs in the Force Field
editor's field, so a 150 × 95 lattice refreshes in about 14 ms; it is re-sampled only when the view, the slice or the
field changes, or every `RefreshInterval` while time runs.

## Use

```javascript
const View = ForceFieldVisualization.Create(Canvas, {
    Sample: (Contribution, X, Y, Z, Time, CellSpan, Out) => { /* add m/s, m/s² or 1/s into Out */ return true; },
    OnSourcePick: Token => {}, OnSourceMove: (Token, X, Z) => {}, OnHover: Point => {}
});
View.Configure({ Quantity: 'Speed', Contribution: 'Flow', SliceHeight: 10, Sources: [/* outlines */] });
Object.assign(View.View, { Mode: 'Map', CentreX: 0, CentreZ: 0, Span: 6000 });
View.AttachNavigation();
requestAnimationFrame(function Tick() { View.Render(Seconds()); requestAnimationFrame(Tick); });
```

Returning `false` from `Sample` marks a point outside the field (drawn transparent). The C++ port keeps the same
split: the field supplies samples, this renderer owns sampling density, colour, arrows and overlays.
