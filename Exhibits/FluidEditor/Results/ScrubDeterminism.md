# Scrub determinism (layer stack phase 4)

`Code/ScrubDeterminism.cjs` loads a preset at 40 cells. For each run it reads back every texture the checkpoint ring stores and
hashes the raw bits (FNV-1a over the float words). It then compares five ways of reaching the same time T:

| Run | How the playhead reaches T |
|---|---|
| Straight | Cold start, step to T (checkpoints at 0.5 s, 1.0 s are captured on the way) |
| ForwardThenBack | Step on to T + 0.8 s, scrub back to T: restore the newest checkpoint ≤ T, re-step |
| BackThenForward | Scrub to 0.1 s (restart at 0), then forward to T |
| FiveStepsBack | Scrub to T − 0.04 s, then forward to T |
| Cold | Ring cleared, reset, step to T |

`+keyed` adds a Smooth Buoyancy track (keys at 0 s and 0.9 s, the second written by an edit, which re-simulates) before the runs.

| Preset | Grid | Textures | T | Hash (all five runs) | Result |
|---|---|---|---|---|---|
| Campfire | 24×40×24 | 9 (velocity, state, pressure, curl, tracers ×2, debris ×3) | 1.3 s | `876a571d` · step 78 | 🟢 identical |
| Campfire + keyed | 24×40×24 | 9 | 1.3 s | `c03e404d` · step 78 | 🟢 identical (and different from the un-keyed run, so the keys act) |
| Form (two particle clouds) | 40×40×40 | 15 (+ positions, velocities, previous per cloud) | 1.3 s | `8d124b58` · step 78 | 🟢 identical |
| Infinite Grid · Plume (sparse tiles) | 48×24×48 tiles | 9 | 0.8 s | — | ⚠️ not finished: SwiftShader slows to many seconds per step once the plume fills tiles, and the run hit its 25 min limit. FluidValidation §9 checks the sparse restore on the mock GPU (tile pool, grid origin, step count) |

Browser: `errors []` on every run. ⚠️ SwiftShader only.
