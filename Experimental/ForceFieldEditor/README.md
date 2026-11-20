# ForceField Studio

An HTML force-field editor for Frontier, built as a standalone page in the Project-Zero editor UI. It authors a bounded force
field — a set of generators, each with an envelope, an attenuation curve and a motion — and shows the resulting field as a
live 3-D volume plus a 2-D cross-section plate.

Open `index.html` in any WebGL2 browser, or run the preview server:

```bash
python3 Experimental/ForceFieldEditor/Serve.py 8094
# → http://localhost:8094/Experimental/ForceFieldEditor/index.html
```

The page has no build step and no dependencies. `Serve.py` binds `0.0.0.0`, serves the repository root (so relative URLs
resolve) and sends `no-store`, so edits show up on reload.

---

## What it does

**Add force generators.** The outliner's *Add source* button (or `A`) opens the full catalogue: nineteen canonical laws plus
the custom formula field, grouped by behaviour. Every generator can be renamed, transformed, re-enveloped, re-curved,
animated, muted, hidden, duplicated and deleted.

**Show the forces.** Each generator has a purpose-drawn diagram of its own law, and the field it contributes is drawn in the
viewport as solid vector arrows coloured by magnitude, with influence envelopes, motion paths and impulse fronts.

**Visualise the flow.** Three complementary readings of the same field:

| Surface | What it shows |
| --- | --- |
| **3-D viewport** | Vector arrows, advected motes, influence envelopes, motion paths, ground grid, probe. Camera-facing streamline ribbons when the flow is on. |
| **Cross-section plate** | Heat map of the evaluated quantity, isolines, in-plane streamlines with travelling beads, in-plane arrows, envelope outlines and the probe marker, on any of the three principal planes. |
| **Telemetry drawer** (`T`) | Anemometer, Beaufort band, 60-second history, spectrum, channel strip — the field read as measured data. |

The viewport renders through WebGL2 and falls back to a CPU rasteriser (with the same geometry, depth sorting and distance
haze) when WebGL2 is unavailable, so the studio always shows the field.

---

## The field model

A document is a `Domain` box, a list of `Source`s and an `Overlay`. Each source contributes through one of three channels:

- **velocity** — the generator injects a medium velocity (wind, streams, swirl, turbulence, formula fields);
- **acceleration** — the generator applies a force (gravity, buoyancy, magnets, torque, lift);
- **damping** — the generator removes energy (drag).

All channels are summed after attenuation (0 at the envelope wall) and the duration gate (continuous, modulated, or an
expanding impulse shell). A body released in the field obeys

```
dv/dt = (V_medium − v) · coupling + a ,   damping applied as exp(−k·dt)
```

which is exactly what the motes integrate (`AdvectMote`).

### Drawing force laws

Force laws carry no velocity of their own, so a streamline through a gravity well would stall and the region would read as
empty space. The studio therefore derives a **flow field** for them (`PrepareFlow` / `FlowSample`): each force law's induced
motion is measured once as the arc its acceleration sweeps over a short drift window, cached as a brush, then painted
radially (pull or push) with a circulation about the anchor's axis. The shape comes from the physics, the placement from the
envelope, and it costs one field sample per probe rather than a nested integration. `Speed` in every view means this drawn
flow, so the 3-D viewport, the slice plate and the legend always agree.

### Impulse fronts

An impulse source drives a shell whose radius grows through the source's own reach over one interval and then repeats, so a
front can never leave the envelope that contains it. The gate is Gaussian in the signed stand-off from that front (σ =
`ImpulseWidth`), with its crest normalised to exactly one; `ImpulseScope` supplies the reach.

### The formula field

```
fx = sin(y + t) * 2
fy = cos(x - t)
fz = 0.2
```

One assignment per line, `fx`/`fy`/`fz`, compiled to a closure (no `eval`). Errors are reported with line and column, missing
channels default to zero, and values clamp to ±1e4.

---

## The twenty generators

| Behaviour | Laws |
| --- | --- |
| Vector fields | Linear Wind, Directed Stream, Buoyancy, Radial Burst, Attractor, Repulsor, Axis Attractor, Boundary Adhesion, Containment, Rotational Column, Torque, Drag, Lorentz Field |
| Channel fields | Spline Force, Magnus Force, Lift |
| Turbulence | Curl Noise, Brownian Noise, Magnetic Dipole |
| Author's own | Custom Formula |

---

## Interface

```
┌─ Control Centre notch ───────────────────────────────────────────────┐
│ Outliner            │ Viewport                          │ Inspector │
│  sources, search,   │  3-D stage (WebGL2 or CPU)         │  law,     │
│  collections, stats │  cross-section plate               │  channels,│
│                     │  readout, legend, transport        │  params   │
└─ Telemetry drawer (T) ──────────────────────────────────────────────┘
```

**Shortcuts.** `Space` play/pause · `R` rewind · `F` frame the domain · `A` add source · `Q` cycle quantity · `T` telemetry ·
`/` search · `Delete` remove selection · `Ctrl+Z` / `Ctrl+Shift+Z` undo/redo · `Ctrl+S` export JSON · `Ctrl+Shift+R` reset ·
`Ctrl+K` control sheet.

**Narrow windows.** Below 1000 px the outliner and below 820 px the inspector leave the grid; a floating rail then offers both
as slide-in sheets, so no capability disappears on a laptop-sized window.

**Documents.** Export as JSON (round-trips through `SanitizeDocument`), cascade TOML, or a sources CSV. Imports are sanitised,
not trusted: unknown laws, out-of-range numbers and malformed vectors are repaired.

---

## Code map

| Path | Responsibility |
| --- | --- |
| `Engine/FieldMath.js` | vectors, noise, envelopes, attenuation, motion, duration gates, impulse, streamlines (RK4 + RK2 flow), marching squares, colour ramps, camera fitting |
| `Engine/ForceFieldCatalog.js` | the twenty laws: domains, enums, glyphs, parameter schemas, sanitizers |
| `Engine/VectorFieldLang.js` | formula compiler (no `eval`) |
| `Engine/FieldEvaluator.js` | `EvaluateAt`, `FlowSample`, `SuggestMaximum`, overlay/document sanitizers, serialisation, default scene |
| `Engine/StudioState.js` | document store, selection, clock, undo/redo, persistence |
| `Viewport/FieldViewport.js` | WebGL2 renderer (ribbon streamlines, instanced solid arrows, motes, line work, distance fog) |
| `Viewport/SoftwareViewport.js` | GPU-free renderer with the same geometry and haze |
| `Viewport/SectionRasterizer.js` | DOM-free slice rasteriser (heat map, isolines, streamlines, arrows, envelopes, probe) |
| `Ui/*.js` | Project-Zero panels: outliner, viewport panel, inspector, telemetry, notch, widget kit, law diagrams |
| `Styles/Studio.css` | the Project-Zero design language (tokens, trapezoid tabs, dock grid, breakpoints) |
| `Main.js` | shell assembly, shortcuts, import/export, dock rail |
| `Validation/ForceFieldValidation.mjs` | 63 engine assertions |
| `Validation/ReferenceSheets.mjs` | renders the slice sheets to `/tmp/frontier-forcefield-sheets` |

---

## Verification

```bash
node Experimental/ForceFieldEditor/Validation/ForceFieldValidation.mjs   # 63/63 engine assertions
node Experimental/ForceFieldEditor/Validation/ReferenceSheets.mjs        # slice sheets for visual review
```

The studio shell is exercised headlessly as well (jsdom): boot, every panel, adding all twenty generators, parameter editing
with undo, formula compile errors, hide/mute/delete, telemetry, exports, keyboard handling, persistence and the narrow-window
rail. The WebGL path is verified against a recording GL context that checks every shader's ES 3.00 header, precision
qualifier, attribute locations and syntax balance, plus the geometry/instance layouts the shaders declare.

Performance notes: streamline advection is pinned to a coarse signature (document revision + whole seconds of field time) and
cached, so a full pass costs ~36 ms on a document change rather than every frame; motes, arrows and the camera redraw every
frame.
