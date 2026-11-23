# Fluid Editor checks

Evidence for the combined fluid editor (`Experimental/FluidEditor/`): the c7egoist Fluid Studio solver with the lighting and
pressure ideas taken from the other Fluid Editor.

## Run
```
python3 Experimental/FluidEditor/Serve.py 8100
cd Experimental/FluidEditor && node FluidValidation.mjs
node Exhibits/FluidEditor/Code/ProjectionCompare.mjs
cd <folder with playwright-core and @sparticuz/chromium>
AWS_EXECUTION_ENV=AWS_Lambda_nodejs20.x node Exhibits/FluidEditor/Code/FluidCapture.cjs http://localhost:8100/ out.png preset-campfire 150 32
```
`FluidCapture.cjs` loads a preset, runs the solver for N fixed 1/60 s steps without drawing, hides overlays and saves the viewport.
The images use a 32-cell grid because the software renderer needs about 4 s per step at full resolution.

## Images
| File | Shows |
|---|---|
| `FluidEditor-1-CampfireBefore` | Campfire, 2.5 s, original Fluid Studio |
| `FluidEditor-2-CampfireCombined` | Same frame, combined: the plume shadows the floor and the flame lights it orange |
| `FluidEditor-3-SmokeBefore` | Smoke Plume, 3 s, original |
| `FluidEditor-4-SmokeCombined` | Same frame, combined: floor shadow, darker underside, same lit-side brightness |
| `FluidEditor-5-InfiniteGrid` | Infinite Grid · Plume (sparse tiles) running the new passes |

## Results (2026-11-16)
- `FluidValidation.mjs`: 349 checks pass, and every changed shader compiles with glslang in both fixed and sparse forms (`ValidationResults.txt`).
- Browser: Campfire, Smoke Plume and Infinite Grid · Plume run with `errors []`.
- Divergence left inside the domain after projection, same 48³ plume (`ProjectionResults.txt`):

| Scheme | Step 1 | Step 10 | Step 30 |
|---|---|---|---|
| Original Fluid Studio (cleared, 36 Jacobi) | 45.2% | 67.2% | 72.2% |
| Other Fluid Editor (warm start, 22 Jacobi) | 57.8% | 50.4% | 59.5% |
| **Combined (warm start, 36 Jacobi)** | **45.2%** | **43.8%** | **59.1%** |

  A fully converged solve leaves 6–9%, so this is still a visual solver, not an incompressible one. The test models the warm start
  without the editor's 0.96 retain factor.
- ⚠️ Headless Chromium with the software renderer (SwiftShader) only; looks are not judged on a real GPU.

## Follow-up 47 — calmer forces, the other editor's presets, flipbook export (2026-11-16)

### Why scenes started clean and then got messy
`Code/FluidMetrics.cjs` reads the velocity back every few seconds and reports the mean speed, the 95th-percentile speed
and the mean vorticity inside the smoke. If a scene is stable those numbers level off. Before this fix they kept rising:

| Preset (24-cell grid) | 1 s | 10 s, before | 10 s, after |
|---|---|---|---|
| Campfire | v 1.79, ω 9.0 | v 3.67, ω 12.4 → with the force limit alone, v 8.33, ω 25.0 | **v 1.76, ω 7.4** |
| Smoke Plume | v 1.55 | v 13.5, ω 26.7 | **v 1.05, ω 2.7** |
| Tornado Funnel | v 5.48 | v 12.1, ω 25.9 | **v 2.02, ω 5.4** |
| Wind Tunnel | v 4.07 | v 17.7, ω 25.3 | **v 3.25, ω 2.4** |

The original Fluid Studio (before Follow-up 46) runs away too: Tornado reaches v 37.5 and ω 156 at 10 s. So the cause is in the base
solver, not in the merged code. Two things caused it:
1. **Forces never stopped pushing.** Wind, vortex, attractor and curl forces add the same acceleration every step, however fast the
   gas already moves. Now each steady force is scaled by `clamp(1 − v·f̂ / (|f|·T), 0, 1)`, so gas stops speeding up once it reaches the
   force's natural speed. `T` is the new Domain setting *Force response*, 1 s by default (0 turns the limit off). Bursts, drag,
   containment and shocks are left as they were. This alone stabilised Smoke Plume and Wind Tunnel.
2. **MacCormack velocity advection was adding energy.** With *Semi-Lagrangian* advection, Campfire and Tornado stayed flat. The
   MacCormack step clamped its overshoots to the neighbouring cells, and every clamped overshoot added a little energy, step after step.
   Now, where the correction overshoots, it uses the plain semi-Lagrangian value instead (Selle et al. 2008), and the correction fades out
   where gas crosses more than one cell per step. Calm regions still get MacCormack's sharper detail.

`StabilityResults.txt` holds all 28 runs (all 18 Fluid Studio presets that hold smoke and all 18 new ones). Nothing runs away. The two
that drift up between 7 and 13 s (the Fire Tornado and the new Camp Fire) were run on to 20 s, and they settle back down
(Fire Tornado mean speed 1.97 → 3.26 → 2.65; Camp Fire 2.08 → 2.51 → 1.95).

### The other editor's presets
`Experimental/FluidEditor/Engine/PyroScenePresets.js` holds the other editor's 18 parameter blocks, copied unchanged. A translator turned
each block into a Fluid Studio scene (since the layer stack phase 2 the results are `Presets/*.toml` documents and `PyroScenePresets.js`
is no longer loaded; it stays as a 📝 reference of the original values):
- One emitter and/or a keyed blast (charge emitter plus radial burst, with ballistic debris for shrapnel).
- Wind, a single gentle curl-noise force for turbulence, and swirl only for the tornado scenes.
- Colliders: a sphere, or several spheres standing in for the cylinder, slab and tyre-ring obstacles.
- The six fire palettes and the smoke look.

The two editors use different units, so every rule is a fixed ratio calibrated on the camp fire, which both editors have. The other
editor's very strong confinement (7–11) and turbulence (3–5) are deliberately not copied at face value; vorticity is capped at 0.35. In
the scene browser they appear as **Pyro scenes**, next to the 18 Fluid Studio scenes.

### Flipbook
The **Flipbook** toolbar button opens a drawer tab. Choose 4/16/32/64 frames, 64–512 px tiles, 12–60 fps and a 0–3 s warm-up, then
optionally *Start from an empty grid* and *Transparent background*, and press **Bake**. You get a sprite sheet (left-to-right,
top-to-bottom) with a looping preview, plus a `.flipbook.json` with frame UVs, simulation times, the camera and the scene. The settings
and the JSON format are the other editor's.

Differences from the other editor:
- It bakes on the live solver, so the viewport carries on from the last baked frame. The other editor bakes in a second WebGL context.
- Frames include smoke, fire, debris and particles, but not vectors, slices, tracers or gizmos.

`Code/FlipbookCheck.cjs` drives the drawer like a user would: it picks the settings, presses Bake, waits, then saves the sheet and
screenshots.

| File | Shows |
|---|---|
| `FluidEditor-6-Tornado10sBefore` / `-7-Tornado10sAfter` | Tornado Funnel at 10 s on a 32-cell grid: torn and sheared before, a steady column after. At this grid size the dust fills the box rather than forming a funnel; that comes from the preset's settings, not from instability |
| `FluidEditor-8-Campfire10sAfter` | Campfire at 10 s, still one coherent flame (before, it had blown up by this time) |
| `FluidEditor-9-FlipbookSheet` | A baked 16-frame, 128 px Campfire sheet (4 × 4) |
| `FluidEditor-10-FlipbookDrawer` / `-11-PresetBrowser` | The Flipbook drawer after a bake, and the scene browser with both preset groups |

Results: `FluidValidation.mjs` passes 496 checks, which includes loading all 36 presets (`ValidationResults.txt`). The browser runs had
`errors []`. ⚠️ All of this ran on the software renderer (SwiftShader) only. The flames look over-bright at 32 cells (a white core
running up the column), in both the old and new Campfire. That is a look and resolution issue that was not changed here.

## Follow-up 51 — Layer stack phase 4: timeline, keys, scrubbing (2026-11-16)

| File | Shows |
|---|---|
| `Images/TimelineDopeSheet` | Demolition Blast with keyed Thermal buoyancy (Smooth, one Bezier key) and a keyed Vorticity weight (Ping-pong); the timing of the charges shown as derived Weight tracks (hollow keys); one key selected |
| `Images/TimelineCurves` | The curves view of the Buoyancy track: monotone Smooth segments, no overshoot past the keys |
| `Images/TimelineInspectorKeys` | The Buoyancy layer card: ◆ beside each keyable control, the keyed one in amber showing its amount at the playhead |

- `Code/ScrubDeterminism.cjs` hashes every checkpointed texture after five ways of reaching the same time; results in
  `Results/ScrubDeterminism.md`. `Code/TimelineCapture.cjs` made the dope sheet and curves captures.
- `FluidValidation.mjs` §9 holds the envelope conversion test and the key, file and checkpoint checks (`ValidationResults.txt`).
- ⚠️ SwiftShader only, at 40 cells.
