# Phase 0 contract — VehicleHmiPrototype mechanical extraction

Generated 2026-10-22 by `ExtractContract.mjs` from the demo source at upstream
`6afb6f0` (`arena/e9bd7db5-frontier`, `Experimental/VehicleHmiPrototype/`). The
demo source is **not** vendored here (only its HTML is, per owner instruction);
regenerate with:

```sh
git archive 6afb6f00b5f25c45f7cda92c7da65b64845768f3 Experimental/VehicleHmiPrototype | tar -x -C <scratch>
node ExtractContract.mjs <scratch>/Experimental/VehicleHmiPrototype .
```

## Coverage (the Phase 0 exit bar)

| Guarantee | Evidence |
|---|---|
| Every demo file listed | `FileInventory.tsv` has **473 rows** = `git ls-tree` count at `6afb6f0` (Source 137 · Shader 80 · Style 13 · Tools 96 · Html 1 · Other 146) — zero unlisted |
| Every shader family-assigned | `ShaderContract.tsv`: 80/80 in the 12 Slang families (Illumination 9 · Geometry 9 · Effects 9 · Atmosphere 8 · Display 8 · Clouds 7 · Cards 7 · Weather 7 · DistanceField 5 · SharedRecords 5 · Denoise 3 · CabinHmi 3); the extractor **fails** on any unassigned shader |
| Every shader in the load graph | reachability closure over JS string literals + `//! include`: **80/80 reachable** (all are JS-manifest roots; `IlluminationHost.js` alone names 78) |
| Pipeline shape known | creation is centralised — one `createComputePipelineAsync` + one `createRenderPipelineAsync` factory in `ShaderInterchange.js`; the real contract is therefore entrypoints (`ShaderContract.tsv`) × pass/dispatch sites (`PipelineSites.tsv`: 29 compute-pass, 17 render-pass, 42 dispatch sites) |
| UI surface enumerated | `UiControlInventory.tsv` (per-file element/listener tallies) + `UiLabels.tsv` (**138 labels**) — each label row must end the port as *Ported → named C++ file* or *Approved removal → owner decision* (empty columns provided) |

## Limits (honest scope)

Tables are **builder/config evidence**, mechanically derived. They do not prove
runtime encoder order (pass sequencing is decided by JS control flow — Phase 3
captures it from a running build), final DOM composition (labels assembled from
variables appear as `·`-joined template fragments), or WGSL uniform layouts
(bindings are counted, not typed). Include-only shaders (e.g. `RayTraversal`,
`ReservoirRecords`) correctly show no entrypoints.
