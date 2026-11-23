# Backlog — deferred migration work

Historical reference only. Current ownership is in `Documentation/FolderStructure.md`;
current progress and findings belong in `Documentation/Tracker.md`. Do not maintain a competing map or tracker here.

Running pick-up list. Append entries; prune when done. Newest on top.

---

## Banned-name audit of `Internal/` — scan report (2026-08-06)

Full scan saved at `Documentation/AUDIT-NamingScan.md`. Ran every banned word from
`SKILL-Naming.md` over all 697 `Internal/` files; the report holds the master banned-word table,
raw occurrence counts, and the identifier-level violations with file locations. Clean: `System`
`Core` `Hierarchy` `Subsystem` `Entity` `Kind` `Manager` `Kernel` (only in "never use X" comments).
Open violations: kinship (`ArenaParent`/`TreeParent`/`FirstChild`… in `GeometryTreeBuild.cpp`,
`InstanceTreeRefit.comp`, `DraughtBoolean.cpp`), `Node` (`ArenaNode`/`TreeNode`/`LeafNode`…),
`Mesh` (`DrawVisibilityMesh`…), `Shell` (`ToolCardShell`/`OpenShell`/`LinkedShell`…), `Element`,
`Object`, `Item`, `Data`, `Base`, unapproved `…Record` composites (`PartitionCullRecord`,
`SurfelRecycleRecord`…), `Frame`, `Stage`, `Helper` (`SurfelSampling.glsl:107`), plus the
user-owned deferred `…Pass`/`…Inscription` renames (§2). No files changed in `Internal/` — report
only.

---

## Precompute surfel distribution for static meshes (2026-08-03)

Surfel GI coverage is currently **view-dependent**: `SurfelSpawnRequest.comp` seeds surfels from visibility
pixels, so a surface only gets surfels once the camera has looked at it, and coverage is clumpy — the direct
cause of the "no surfel → no GI" gaps the debug view (modes 5–7) makes visible. The live Numpad +/- density
control (raises the spawn-throttle multiplier) mitigates but does not solve it.

**Better approach, deferred:** for **static meshes** (floor slab, the Suzanne heads) precompute a
**view-independent** surfel distribution at load — e.g. blue-noise / area-weighted sampling over each mesh's
surface (roughly "one per patch", the user's "1 per vertices on Suzanne" intuition, done properly by surface
area rather than raw vertex count) — and seed those surfels once so coverage is uniform and complete before
the camera moves. Screen-space spawn stays for dynamic/streamed geometry. Keep the two paths coexisting: the
precomputed seed for statics, the visibility-pixel spawn for everything else.

Not scoped into the current GI phases; revisit after Phase 4–6.

---

## Dead thin `WorkspaceHost/Outliner/OutlinerPanel` — registered, never invoked (2026-08-03)

The outliner unification is done: ONE shared panel (`WorkspaceHost/SketchOutliner/SketchOutlinerPanel`,
ns `Frontier::SketchOutlinerUi`) + per-app content profiles (`SketchContentProfile` / `SceneContentProfile`),
content-agnostic via `OutlinerContentProfile`. Both private 1707-line copies (`SceneDirectory/…Panel` and the
stray `SketchOutliner/…Panel`) are deleted; SceneDirectory / SketchOutliner / SceneDirectoryInspector all link
the shared panel from EngineContext.lib. All five consumers build green (EngineContext, the three validation
apps, Editor).

**Left open, on purpose:** the OTHER, thin outliner — `WorkspaceHost/Outliner/OutlinerPanel`
(`ConstructOutlinerPanel(Theme, Config, Model)`, with `OutlinerConfiguration` + `Model/OutlinerModel` +
`Model/OutlinerRow`). It is registered by ~5 workspace layouts but **never invoked at runtime** — a separate,
much smaller panel unrelated to the SketchOutliner tree. Not folded into this pass. Decide later whether to
retire it or route those workspaces onto the shared SketchOutliner panel + a profile.

Also deferred (unchanged this pass): `OutlinerPalette` stays file-local/self-paletted; theme-driving it is a
separate future item.

---

## The eight-corner box rule has NO gate until the traversal (#11) lands (2026-08-02)

**A real coverage gap, deliberately left open rather than faked.** Both `InstanceBoundsReduce.comp` and
`InstanceMortonCode.comp` bound an instance by transforming all **eight** corners of its local box. The cheap
wrong alternative — transform only the min and max corners — is too SMALL under rotation, and a too-small
instance box means a ray **misses geometry it should hit**. That is a silent, view-dependent artefact, not a
crash.

`InstanceBoundsValidation` used to claim it proved the rule via a "foil" case that recomputed the scene the
wrong way and asserted the GPU disagreed. **That claim was false and the case has been deleted.** The two
rules differ only in box EXTENT and provably never in box CENTRE — opposite corners stay opposite under any
affine map, so both rules average to the image of the local centre (measured: 0 of 24 mesh/variant
combinations differ, `_ClaudeScratch/build/CentroidFoilProbe.cpp`). `InstanceBoundsReduce.comp:162` discards
the extent the instant it takes `(WorldMinimum + WorldMaximum) * 0.5` and reduces **centroids**, so nothing
either dispatch outputs can discriminate the rules. No amount of scene design fixes this — it is structural.

🚩 **Task #11 must carry the assertion.** The traversal is the first consumer that tests a ray against an
instance's box extent, so it is the first place the rule is observable. Concretely: build a scene with an
obliquely rotated instance and a ray that enters only the eight-corner box, and assert the hit. A two-corner
regression must fail that test. Without it the engine has no check on the rule at all.

---

## Extended Morton codes — up to 54% BVH quality for open-world scenes (2026-08-02)

**Deferred, not rejected.** `VolumeBoundsReduce.comp` currently reduces only the CENTROID box, which is the
correct input for plain 30-bit Morton coding and is what AMD's `gpurt` uses too. Vinkler et al., *Extended
Morton Codes for High Performance Bounding Volume Hierarchy Construction* (HPG 2017,
https://dcgi.fel.cvut.cz/projects/emc/emc2017.pdf) measure **up to 54% BVH quality improvement** — and they
attribute it specifically to *"scenes with a non-uniform spatial extent and varying object sizes"*, which is
exactly the open-world many-mesh case this GI work targets. Extended codes interleave size/extent bits with
position bits, so they need the **union of triangle AABBs** in addition to the centroid box.

Cost to collect: **6 more atomics and 24 more bytes** in a dispatch that is already DRAM-bound at ~140 MB of
scattered vertex reads — i.e. free. `gpurt` keeps both boxes for the same reason (`IsCentroidMortonBoundsEnabled`).

🚩 Not done now because it changes the Morton pass, the tree build, and the bounds contract at once, and the
reduce is green and gate-verified. The reduce is the natural place to gather the data when the tree build is
proven. Revisit after task #8.

---

## GPU scene AABB reduce — optimisation researched, KEEP AS IS (2026-08-02)

Closed, recorded so it is not re-litigated. Asked whether `VolumeBoundsReduce.comp` could be made more
accurate or cheaper. Answer: **no on both counts, and the current shape matches shipping production code.**

- **Accuracy**: already bit-exact vs a CPU float reduction over 278 GPU cases. Min/max are selections, not
  arithmetic, so there is nothing to improve.
- **Cost**: the dispatch is **DRAM-bound** — ~140 MB of scattered, index-indirected vertex reads at 3M
  triangles, a ~1–2 ms floor on a ~192 GB/s bus. The shared-memory reduction hides entirely behind that.
- **Subgroup `subgroupMin`/`subgroupMax`** (device confirms `ARITHMETIC`, size 32): removes 8 barriers and
  drops shared memory 6 KiB → 192 B, but published benchmarks put the reduce saving at ~zero (one Vulkan
  reduce/scan study calls it *"rather disappointing"*; a re-test measured **identical kernel duration**, both
  >96% memory throughput). Would also change NaN behaviour — the subgroup spec DROPS NaN, scalar `min()`
  leaves it undefined — and adds an unpinned signed-zero corner, a runtime feature gate, subgroup-size
  specialization, and reconvergence fragility. Not worth it against a gate-verified path.
- **Rejected too**: int64 packing (halves ~70K atomics over 6 L2-resolved addresses — noise), two-pass
  scratch reduce (extra dispatch for contention that isn't hurting), persistent threads (loses the trivially
  correct `GlobalInvocationID == triangle` mapping).
- **Corroboration**: AMD's `gpurt` ships this exact design — Herf's 2001 float-flip unchanged in `Bits.hlsli`,
  wave-reduce then one atomic per wave.

🔴 Device fact worth keeping: `VK_EXT_shader_atomic_float` IS present here (add/exchange), but float atomic
MIN/MAX needs `VK_EXT_shader_atomic_float2`, which is **absent** — Turing has no `FMIN`/`FMAX` buffer atomic
(RDNA2 does). So the ordered-int form is not a workaround for a disabled feature; enabling the float-atomic
feature `VulkanHost` omits would NOT unlock `atomicMin` on a float. Comments in `VolumeBoundsSubmission.h`
and `VolumeBoundsReduce.comp` corrected accordingly.

---

## VolumetricFlowSolver — multiple scattering shipped, two follow-ups (2026-07-31)

Deep-scatter approximation (Wrenninge et al.) is in: `EvaluateDeepScatter` in `RaymarchShaderSource`,
three bands, with a `MultipleScattering` graph unit. Gated by `_ClaudeScratch/tmp/DeepScatterProbe.py`.
Measured gain over single scatter by optical depth: `0.05 → 1.04×` · `1.0 → 1.42×` · `3.0 → 3.51×` ·
`8.0 → 77.9×`. Band count 1 reduces to the previous single-scatter form to **0.000e+00**, so the
bypass state is exactly the old look.

⚠️ **Unmeasured on hardware.** The dials (`ExtinctionDecay 0.52`, `ContributionDecay 0.58`,
`AnisotropyDecay 0.45`, `MultiScatterAmbient 0.85`) are physically-motivated but **not** art-directed
against a render, and the ambient lift interacts with the irradiance cache — both add to the ambient
term, so the plume may now read too bright. First thing to check when the render is next observed.

📝 Deferred, both cheap and both real:
- **Per-channel deep scatter.** `Scatter.Weighting` is scalar, so all three bands share one extinction.
  Soot's blue absorption is already spectral in the main march (`AbsorptionTint`), so multi-scattered
  light *should* redden with bounce order and currently does not. Making `ScatterResponse.Weighting` a
  `vec3` is a few lines; it was left scalar to keep the first version's cost provable.
- **Presets do not set the scatter dials.** All five presets predate the unit, so they inherit the
  defaults. `Smouldering` and `Explosion` in particular want different bounce counts — thick cold smoke
  is where deep scatter earns the most, a thin fast fireball where it earns least.

## VolumetricFlowSolver — projection stencil inconsistency ✔️ FIXED 2026-07-31

`Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html`. The three stages that touch pressure disagreed
on their stencil, so the operator being **solved** was not the operator being **applied**:

| Stage | Stencil (was) | Stencil (now) | Span |
|---|---|---|---|
| Divergence of velocity | central difference, ×0.5 | **backward difference**, no halving | ±1 |
| Pressure Laplacian (V-cycle smoother) | 7-point | 7-point, untouched | ±1 |
| Gradient subtracted from velocity | central difference, ×0.5 | **forward difference**, no halving | ±1 |

✔️ **Shipped fix: (a), the forward/backward MAC pair.** Both sites changed in one edit as required.
The `0.5` factors went with the stencils — a one-sided difference over a unit grid carries no halving,
and keeping it would have halved every correction. Gate result, `ShippedKernelProbe.py` interior
divergence removal on 32³: **1.5× → 612×**. Exactness against a dense solve holds at 1.2e-12 and grid
independence is unchanged (factor 0.409 / 0.423 across an 8× voxel range), confirming the pressure
solve was never the problem.

⚠️ **The wall ring keeps a 0.1949 composition error** (interior is 0.0000). That residue is inherent
to the discrete Neumann condition, is what `BoundaryDamping` absorbs, and is **not** reduced by more
cycles. `BoundaryDamping` was tuned against a projection that removed 1.5×; now that it removes 612×,
it may want retuning. Not yet inspected in the render.

📝 The probe itself had to be corrected in the same pass: its `ClampedDifference`/`MeasureDivergence`
helpers transcribed the **old** central form, so left alone it would have measured the new solver
against the retired operator. Replaced with `BackwardDifference`/`ForwardDifference`. The shader and
the probe transcription are now a matched set of three sites, all carrying 🔴 comments saying so.

⚠️ Central div ∘ central grad does **not** compose into the 7-point Laplacian — it reaches ±2 and
skips the immediate neighbours, i.e. the Laplacian on every *other* cell. Consequences: residual
divergence plateaus instead of converging regardless of V-cycle count, and the odd/even sub-lattices
decouple so a **checkerboard pressure mode** is nearly invisible to the operator and the smoother
cannot damp it (reads as high-frequency speckle in velocity).

✔️ **MEASURED 2026-07-31** — the reasoning above was analytic; these are numbers. Relative deviation
of each candidate pair from the compact 6-point Laplacian the V-cycle actually inverts, 32³ random
field, interior only: **central div ∘ central grad = 0.7965** · **backward div ∘ forward grad =
0.0000**. Fix (a) is exact, not merely better.

🔴 The cost is now quantified too, and it is larger than "convergence plateaus": `ShippedKernelProbe.py`
reports **4 V-cycles remove only 1.5× of interior divergence**, while the *same* cycles reach the exact
pressure to **1.2e-12** against a dense solve on 16³ and are grid independent. Both are true at once —
the pressure solve is correct in isolation and the projection then applies it through an operator the
solve never inverted. This is why the solver's own convergence readouts look healthy: the V-cycle
genuinely converges, just for the wrong operator. Visible as mass gain/loss over time, smeared plumes
and weak vortex structure.

📝 Repro: `_ClaudeScratch/tmp/StencilPairDiag.py` (diagnostic, not a gate) composes each stencil pair
and prints the deviations. `ShippedKernelProbe.py` FAIL(1) is this defect, **not** a probe bug — worth
stating because that probe's divergence check gave a *false* 1.5× once before from periodic wrap, was
corrected to clamped differences, and still reports 1.5×. The second 1.5× is real.

**Decision history (user):** first ruled *"fix (a), deferred — document only, don't hook it up"*, then
reversed the same day to *"#15 then multiscatter"* — applied. Both sites
(`EvaluateDivergence` + `SubtractPressureGradient`) **had to change in one edit**, because correcting
either alone breaks the self-consistent pairing while leaving the mismatch. Anyone reverting one half
reintroduces the defect in a form the probes will not attribute to them.

- **(a) forward/backward pair** — divergence backward, gradient forward. Composes *exactly* into the
  7-point Laplacian already solved; all three stages become consistent and the checkerboard mode is
  fully seen and damped. What staggered/MAC grids do. Few lines in the div + grad shaders, smoother
  untouched. Cost: half-cell shift in where pressure lives (cosmetically invisible here).
- **(b) widen the solved operator** to the ±2 stencil central-central implies — also consistent, but
  *keeps* the odd/even decoupling, so it legitimises the checkerboard and then needs extra damping.
  More work, worse result. Rejected.

📝 Scope: this is a convergence/speckle correctness fix only. It does **not** affect the plume
sinking (`AmbientTemperature` 0.0 with τ decaying faster than ρ, so `-DensityBuoyancy·ρ` outlives the
lift) — that behaviour was inspected and deliberately **left as is** by the user the same day.

---

## ChainAuthoring — native-speed operation chains (2026-07-30)

Plan: `Documentation/Research/PLAN-ChainAuthoring.md`. Visual authoring surface where each unit is a
preexisting native C++ function; the wired chain is **transpiled to C++ and compiled to a DLL**, never
interpreted. Measured **−2.8 % vs hand-written C++**, **~312 ms** rebuild, hot swap with live records
intact. Probes in `_ClaudeScratch/build/CompileProbe/` (git-ignored — reproduce, do not cite).

| Item | State | Notes |
| --- | --- | --- |
| 🔴 **The batch-size gate decides whether this is worth building at all** | ◻ **OPEN — blocks every other step** | Crossover sits between **1 K and 10 K elements** per evaluation: at 1 K a 312 ms compile needs **16,596 evaluations** to repay itself, at 10 K it needs **1,098**, at 1 M it needs **10**. ⚠️ Speedup is **flat (~1.6–2.0×) at every batch size** — what scales is the *absolute* time saved per evaluation, and that is what must repay the compile. So a chain over a handful of parametric values gains nothing no matter how hot it is, and the correct answer there is a direct evaluator, not codegen. Step 0 is to measure real `Authoring/Geometry` functions at realistic element counts. SideFX documents the same threshold for Houdini compiled blocks. |
| 🔴 **Operations must be `inline` in headers, or the whole design collapses** | 📝 the core constraint | Fusion is the entire mechanism: MSVC inlines the operations into one body, allocates registers across their boundaries, and **vectorizes** — measured 0 `call` instructions and a 4× unrolled loop in `FusedGraph.asm`. 💡 The win is unlocking SIMD, **not** saving call overhead, which is why an opaque call per operation costs 9× on cheap-arithmetic chains rather than a few percent. Expose the operations only as DLL exports and nothing inlines: the penalty returns in full. |
| **Reload granularity is the whole chain, never the operation** | 📝 resolves inlining vs hot swap | Inlining wants static linkage; hot reload wants an ABI boundary. Putting the boundary at the *chain* satisfies both — one indirect call per evaluation amortizes to nothing, while everything inside the DLL still inlines. |
| 🔴 **Writing over a mapped DLL fails outright; shadow-copy before `LoadLibrary`** | ✔️ verified fix | Reproduced both directions in `PdbLockProbe.cpp`: naive recompile over the loaded path → `FAILED — path locked`; shadow-copy → `SUCCEEDED`, new revision live. 📝 Case B reused the **same PDB path** and still worked, so the **DLL mapping is what locks, not the PDB** — shadow-copying alone is sufficient. |
| **Rebuild latency is dominated by header parsing, not optimization** | 📝 measured | ~312 ms with a PCH'd thin interface header · ~474 ms thin · **~2258 ms** once four common STL headers are included. 💡 `/Od` is **not** faster than `/O2` (~1.0 s each) — optimization is free, so never trade it for latency; the only lever that matters is what the generated code `#include`s. ⚠️ Spawning `vcvars64.bat` per compile costs **7–16 s**; capture the environment once at startup. |
| **Resolve `RecordToken` once in a prologue, never per element** | 📝 hard budget constraint | `MicroUtils/RecordToken.h` is already the index + generation slotmap this needs. Hoisted resolve **1.06×**; per-access **1.43×**; per-access with the generation check **1.91×**. ⚠️ Only the hoisted form is inside the 1–5 % budget, and 1.06× is already a cache-friendly best case. The generation counter is what keeps a token safe across a reload — a stale token fails a comparison instead of dereferencing freed memory. |
| **Signature metadata cannot silently drift from the function** | ✔️ verified in `DriftProbe.cpp` | Registration is `Register("Derive", &DeriveEquation).In("x").In("y").Out(…)`; the deduced arity is baked into the type, so a 4-pin chain on a 5-parameter function is `error C2338: 'pin count does not match the function signature'`. 📝 Templates **cannot** recover parameter names, nor separate write-only `out` from read-write `in-out` — those two facts are the only hand-supplied metadata. They *can* reject a by-value parameter as an output, which is cross-checked. |
| 🚩 **Permit non-compilable operations, and mark them** | 📝 design rule, from the survivors | Houdini ships a badge for non-compilable SOPs; `torch.compile` falls back with a graph break. Blueprint nativization instead promised near-total coverage of a legacy set and **its worst failures were exactly at the seams** where that promise broke. ⚠️ Never translate a chain partially — compile the whole chain behind a C ABI so no nativized/non-nativized boundary can exist. |
| **Reload cycle after RuntimeCompiledCPlusPlus** | 📝 noted | Stable identity preserved across the swap, then `serialize → recreate → deserialize → notify`. Live++ and Unreal Live Coding converge on the same sequence independently. ⚠️ RCC++ carries an `_isRuntimeDelete` flag because destructors that cascade deletes **destroy the surrounding record set on reload** — design that out rather than debug it. Keep `DllMain` an empty stub; it runs under the loader lock. |
| 🔴 **`Node`, `Graph`, `Kernel`, `Module`, `Frame`, `Data` are all banned** | 📝 naming settled | Per `SKILL-Naming.md` §1, so this subsystem cannot be called a node editor in code. Constructed via §0: `Authoring/ChainAuthoring/` · `OperationEntry` · `OperationCatalogue` · `AdjacencyTable` · `ChainTranslation` · `ChainCompilation` · `RevisionActivation` · `ChainAuthoringPanel`. 📝 `imgui-node-editor` is **already vendored** in `ExternalPackages/`, so the canvas is not the work. |
| **Epic removed Blueprint Nativization in UE5 and never said why** | 📝 the precedent, stated honestly | The UE5 Migration Guide lists it under Gameplay Framework → Removals with **no rationale**, and **no credible speedup figure for the feature exists publicly**. ⚠️ Of 14 identified failure modes ~11 stem from reproducing full UObject semantics under *partial* translation (reflection, GC, replication, delegates, latent nodes, cook targets, bug-for-bug parity) — none of which exist for pure functions over plain records. The three that do carry over: debuggability of generated code, preview/compiled divergence, and maintenance burden. Unverified and not to be repeated as fact: the UE-xxxxx issue IDs (tracker 403s), the *Mortal Shell* / *Valorant* anecdotes, Nuke Blink's LLVM backend, Bifrost internals, Blender's multi-function architecture. |

---

## P6.4 S7 caster depth raster (2026-07-30)

`ShadowDepthRasterSubmission.{h,cpp}` + `ShadowDepthRaster.vert/.frag` rasterize caster depth into the pages
S6 primed, resolving occlusion with `imageAtomicMin`. One draw per seeded clipmap level. Builds clean, both
modules ship as `vulkan1.0`; push layout verified against the compiled SPIR-V of BOTH stages by
`_ClaudeScratch/tmp/DepthRasterLayoutProbe.cpp` (static_asserts, so it re-checks every build).
Now WIRED into `RenderExtension`: initialized after S6 (gated on `fragmentStoresAndAtomics`), recorded per image
after `RecordShadowPageClear` with the atlas in `GENERAL`, finalized before the atlas. Both caster meshes draw.
✔️ **The cache is now LIVE**: `MarkShadowDepthPagesRendered` runs after the last caster mesh, so
`ContentStale` and the tile `Update` bit are both cleared on the device path. Measured decay: **51 pages on image 0, then 0 every image after**.

| Item | State | Notes |
| --- | --- | --- |
| **S7 is a RENDER-TARGET-LESS raster — zero colour, zero depth attachments** | ✔️ **LANDED 2026-07-30** | The atlas is `R32_UINT` with `STORAGE|SAMPLED` usage and so **cannot be an attachment of either kind**, which is why there is no hardware depth test: occlusion is `imageAtomicMin` alone (commutative, so any fragment interleaving converges on the nearest caster; a plain `imageStore` would keep whichever fragment landed last and read as shadows flickering between casters). The rendering scope is opened purely to establish a **viewport**, sized to one level's 32x32 tile window. 📝 Needs dynamic rendering — a zero-attachment `VkRenderPass` + matching framebuffer is rejected by several drivers. |
| 🔴 **`gl_FragCoord` CANNOT be the atlas write address** | 📝 the core design constraint | One draw covers tiles whose pages sit anywhere in the 16x16 grid **in whatever order the allocator handed them out**, so no affine map takes a clip position to an atlas texel. The fragment stage resolves its own texel per fragment: light position → light tile → toroidal slot → page index (the S5 mapping SSBO) → page origin → texel within page. This is why the S5 mapping upload is a hard prerequisite rather than an optimization. ⚠️ An unmapped tile must **discard, not clamp** — writing it anywhere corrupts a page belonging to a different region, which reads as a shadow floating in empty space. |
| 🔴 **`float(ClearValue - 1u)` as the depth scale is UNDEFINED, and fails toward "occludes everything"** | ✔️ **FIXED 2026-07-30** before first execution | `float` has a 24-bit mantissa, so `0xFFFFFFFE` is **not representable and rounds UP to exactly 2^32**; a fragment at depth 1.0 then converts a value one past the uint32 max — UB in both C++ and GLSL. Measured on the reference compiler it lands on **`0x00000000`, the NEAREST depth**, so the farthest caster in the slab would occlude everything in its page: the worst possible failure direction, visible only on distant casters. Fixed by scaling with **`0xFFFFFF00` (4294967040.0)** — eight low zero bits, so exact in `float`, and safely below the identity so "nothing here" stays unreachable. 24 bits is far more than a 256-texel page resolves. Caught by a `C4309` truncation warning on the probe's own copy of the expression; proven in `_ClaudeScratch/tmp/EncodeRangeProbe.cpp` (exact, in range, monotonic across 4097 samples). |
| 🔴 **A GLSL descriptor collision that COMPILES CLEAN** | ⚠️ fixed; the detection method is the lesson | Both S7 stages first `#include`d `ShadowTileStore.glsl`, which declares the tile table at **set 0 binding 0** — where S7 binds the atlas storage **image**. `glslc` returned **0** because the unused block is stripped from the name table, but `spirv-dis` showed `OpDecorate %ShadowTileTable Binding 0` **still emitted** next to `OpDecorate %ShadowAtlas Binding 0`: two descriptor types at one binding, UB no compiler reports. 📝 Same collision as S6, but S6's was caught by a missing symbol while this one was silent — so **exit code 0 from glslc does not mean the bindings are sound; disassemble.** Fixed by dropping the include and mirroring the three needed helpers/constants locally (flagged as unchecked duplicates in both files). |
| **Depth encoding direction is the whole correctness of the min** | 📝 noted | `ForwardAxis` points **along** the light's travel (`SunShadowClipmap.h:57`), so larger light-space z is farther from the sun and `atomicMin` keeps the occluder. ⚠️ Any sign flip silently inverts the test into a max — keeping the *farthest* surface — and the image comes out **inside-out (objects lit through themselves), not blank**, which is why this is stated rather than left to be noticed. Depth travels down an interpolator, **not** `gl_Position.z`, which would clip every caster outside [0,1] and delete exactly the nearest-to-sun casters that matter most. |
| **Face culling is OFF, deliberately** | 📝 noted | Back-face culling from the light's view discards the far side of a closed caster, and for an **open or single-sided mesh (the floor slab) it can discard the only face**, so the object stops casting entirely. Depth-only passes elsewhere cull front faces to fight acne, but that trades an artifact for a missing caster; with atomic-min resolve the near surface wins regardless. |
| 🔴 **S7→(next image)S6 is a second WAW hazard, in the reverse direction** | ⚠️ handled, very easy to miss | `RecordShadowDepthRaster` ends with a `GENERAL`→`GENERAL` barrier (`SHADER_WRITE` → `SHADER_READ|SHADER_WRITE`, `FRAGMENT` → `COMPUTE|FRAGMENT`). The tracer-read direction is obvious; the one that is not is that **next image's clear compute could begin writing the identity while this image's fragment atomics are still landing**, so a freshly cleared page silently reacquires last image's depth. Both hazards live inside one layout, so no transition covers either. |
| **The window centre is derived from the NEGATED origin** | ⚠️ unverified on device | `ToroidalOrigin` is stored in ADD form, so the window corner is `-Origin` and the centre is `-Origin + Resolution/2`. 📝 Getting this wrong offsets every caster by half a window, reading as **shadows detached from their objects**. The CPU probe cannot catch it (it never rasterizes); it needs the S7-on-device check once the pass is wired. |
| 🔴 **`ContentStale` was never cleared — S7 drew every page every image** | ✔️ **FIXED 2026-07-30 (#19)** | S7 deliberately does **not** call `MarkShadowPageRendered` (a separate explicit step, so only a caller that knows the draw was submitted may clear the flag). Until that wiring exists nothing clears staleness on the device path, so S6's measured "static scene clears ZERO pages" holds for the *predicate* but **not for the shipped pipeline** — a live run re-clears and redraws the same pages forever. `PageClearProbe.cpp`'s `SimulateRaster` already models the intended behaviour. |
| **Instance buffers are BORROWED from the visibility rasters** | 📝 noted | Each caster's set binds its existing instance buffer at set 0 binding 2, so a caster cannot be placed differently for its shadow than for its shading — one transform source, two consumers. `ReadyCondition` does **not** imply a set was acquired; the record path validates the set it is handed and draws nothing otherwise. |
| **Probe build needs two extra include roots** | 📝 noted | `BufferAllocation.h` reaches into Authoring's `PolygonCluster.h`, which bare-includes `LinearAlgebra_Float64.h` and its `Modeling` siblings, so a scratch probe needs `/I Internal\EngineContext\Math` and `/I Internal\Authoring\Geometry\Modeling` on top of `/I Internal`. Already documented for the real build in `Internal/Graphics/Build.bat:50-57`. |
| 🔴 **The scene has TWO caster meshes, and a single shared descriptor set could only ever draw ONE** | ✔️ **FIXED 2026-07-30** during the wiring | S7 shipped with `maxSets = 1` and a `BindShadowCasterInstances` that rewrote binding 2 in place. But the casters are the heads (`SceneGeometry` / `VisibilityRaster.InstanceBuffer`) **and** the floor slab (`FloorGeometry` / `FloorRaster.InstanceBuffer`) — two meshes, two instance buffers. Re-pointing one set between two draws in a single command buffer mutates a descriptor a queued draw still references, which is undefined and reads as **one mesh casting with the other's transforms**: a shadow of the wrong shape, far harder to notice than a missing one. 📝 Fixed by mirroring what `VisibilityRasterization::DrawVisibilityMesh` already does — a set per mesh through one shared pipeline layout: `ShadowCasterSetCapacity` sets allocated up front, `AcquireShadowCasterSet` returning a cached set per buffer, and `CasterSet` as a record parameter. |
| **The floor slab must CAST, not only receive** | 📝 noted | Easy to treat the slab as the receiver alone, but a floor that receives without casting loses its own contact shadow. It gets its own caster set for that reason. ⚠️ It is also **single-sided**, which is the concrete reason S7's pipeline sets `cullMode = VK_CULL_MODE_NONE`: culling from the light's view could discard its only face and the slab would stop casting entirely. |
| **The record path validates the set it is HANDED, not merely non-null** | 📝 noted | A set from anywhere else would have binding 2 pointing at something that is not an instance buffer, and the vertex stage would read it as transforms. `RecordShadowDepthRaster` scans the sets it handed out and draws nothing otherwise. ⚠️ Capacity exhaustion is *reported* rather than silently aliasing onto an existing set, for the same wrong-shape reason as the row above. |
| **S7's trailing barrier records once per caster mesh — redundant, NOT wrong** | 📝 noted, do not "optimize" | Two S7 draws contend for the same texels, and `imageAtomicMin` needs no ordering between them (min is commutative, which is exactly what lets the meshes be recorded in any order). The barrier's real job is the boundary with the compute passes on either side — the tracer's reads ahead, and the NEXT image's S6 clear behind. Hoisting it out of the per-mesh path and getting that boundary wrong is a race that surfaces on one driver. |
| **The `MarkShadowPageRendered` step must run after the LAST caster mesh** | ◻ **OPEN, folded into #19** | With more than one mesh the "pages are drawn" claim only becomes true once every mesh has been submitted. Clearing `ContentStale` after the first would cache a **half-drawn page** — heads shadowing, floor not — and the cache would never redraw it. |
| 🔴 **The mark step must run after the LAST caster mesh, and only if a draw was actually recorded** | ✔️ **LANDED 2026-07-30** | Two constraints, both silent when broken. Marking after the FIRST of two meshes caches a **half-drawn page** — heads shadowing, floor not — that the cache then never redraws. And marking when *neither* mesh drew (no scene, no sets, census already zero) declares depth valid for pages still holding the clear identity, so every receiver over them reads as **fully lit**. Hence the call sits outside both record calls, gated on `SceneLevels > 0 || FloorLevels > 0`. |
| 🔴 **Two flags gate two passes — lowering only `ContentStale` looks cached from one side and stays dirty forever on the other** | 📝 noted | `ContentStale` (per page) drives S6/S7 through `ShadowPageNeedsRender`; `ShadowTileUpdateBit` (per tile) drives `ShadowTileNeedsRender`, which masking and the tracer consult. `MarkShadowDepthPagesRendered` lowers **both**, keyed off the page record's `OwnerLevel`/`OwnerTile` so the reverse lookup needs no search. ⚠️ `PageDriveProbe.cpp` §4 checks the tile side independently rather than inferring it from the page count, because the page count alone passes under a half-fix. |
| **The mark step re-tests `ShadowPageNeedsRender` rather than taking a list from S6** | 📝 noted, deliberate | S7 draws whole tile *windows*, but only pages whose tiles were both wanted and stale received depth. Re-running S6's own predicate keeps the two selections in step **by construction** instead of by a list that could drift. 📝 Collected then lowered in two phases, because `MarkShadowPageRendered` mutates the very predicate the walk tests — a single fused loop is correct only by accident of iteration order. |
| **Marking after RECORDING, not after execution, is sound** | 📝 noted | These are CPU-side flags describing what the *submission* will contain, and the submission is ordered, so the depth exists by the time anything samples it. Unsound only if the recording is later abandoned. ⚠️ Related ordering that IS fine: `UploadShadowTileStore` runs at `RenderExtension.cpp:1234`, *before* S6/S7 record, so lowering `Update` afterwards does not reach the device this image — harmless because `ResetShadowTileDemand` preserves `ShadowTileUpdateBit` and the mirror uploads next image. |
| **A steady page count does NOT prove the cache works** | 📝 noted, the probe lesson | `PageDriveProbe.cpp` §3 already showed a static scene holding 51 pages with zero evictions — and that passed the whole time `ContentStale` was never cleared, because it measures page CLAIMS converging, not S6/S7 WORK converging. §4 measures the render selection instead (`needing 51 → 0`) and is the section that fails if the #19 call site is removed. It also checks the cache is still **invalidatable** (a sphere re-dirties 13 pages), since a flag nothing can raise again would pass every decay check and be worse than no cache at all. |

---

## P6.4 S5 page drive + S6 page clear (2026-07-30)

`DriveShadowPageAllocation` + `UploadShadowPageMapping` consume marking demand into real page claims and
publish `TilePageMapping` to the GPU as an SSBO; `ShadowPageClearSubmission` then primes exactly the pages
that need redrawing. Verified by `_ClaudeScratch/tmp/PageDriveProbe.cpp` (10 checks) and
`PageClearProbe.cpp` (14 checks), 0 failures, each with a live wrong-alternative control.
🔴 Still missing for visible shadows: **S7's depth raster** and the P6.5 tracer that samples it.

| Item | State | Notes |
|---|---|---|
| **Nothing allocated pages before this — the atlas was 6144 sentinels** | ✔️ **FIXED 2026-07-30** | 🔴 `RequestShadowPage` had **no caller anywhere in `RenderExtension.cpp`** (grep: *No matches found*). The atlas was initialized and `OpenShadowPageImage` + the census ran every image, so every diagnostic looked alive, but `TilePageMapping` held 6144 × `ShadowPageUnmapped` forever. Any S6/S7 built on top would have addressed an empty mapping and read garbage — and the census would have kept reporting a healthy 256-page free pool while doing it. 📝 Lesson, same shape as the double-tone-map row: **an initialized subsystem with live counters is not a driven subsystem — grep for the caller.** This is why P6.4's scope was re-cut from "upload the mapping" to "drive S5 at all" mid-phase. |
| **Coarsest-LOD-first allocation order is load-bearing — now MEASURED, not asserted** | ✔️ verified by control | 🔴 `RequestShadowPage`'s reclaim only ever takes a victim from a level **coarser** than the requester, so a page is evictable only once the coarse level has already been served. Fine-first therefore lets LOD 0 drain the free list and then find nothing evictable (coarse pages are still `Free`, never `Cached`). ⚠️ **This is the exact OPPOSITE of S3's fine→coarse demand propagation** — the two orders are easy to conflate, and getting the drive backwards is invisible to any "did we allocate something" test: both orders allocate, both report plausible totals. The probe runs the same demand both ways (900 receivers, 256-page pool, 775 starved either way): coarsest-first → `L0=0 L1=151 L2=72 L3=20 L4=9 L5=4`, coarse tail (L4+L5) = **13**; fine-first → `L0=256`, every other level **0**, coarse tail = **0**. The tracer falls back to a coarser page when a fine one is missing, so a zero coarse tail is the one outcome that yields visibly *wrong* shadows rather than merely blurry ones. |
| **S5's demand is ONE IMAGE STALE by construction on the CPU path** | ⚠️ accepted, inherent | `Store.TileWords` holds what the *previous* submission's download produced, so a newly-visible tile gets its page on the next image — the shadow appears one frame late rather than wrong. Reading fresh demand would need a device stall. 📝 This dissolves when S5 is ported to compute (deferred deliberately: a GPU allocator underneath an unverified raster stacks two new failure surfaces with no oracle to separate them; CPU allocation keeps the 69-check store probe live, so any wrong depth is unambiguously S7's). 🔴 The drive must run **strictly after `OpenShadowPageImage`** — the open demotes every `Used`→`Cached`, so allocating first would have fresh claims immediately demoted and the pool would hand the same page out twice. |
| **The probe's own false alarm: measure only after the clipmap settles** | 📝 noted, probe fixed | The first run reported `[FAIL] a static scene holds a steady page count` (46 → 51). The **probe** was wrong, not the code: `RefreshSunShadowClipmap` seeds every level's origin from zero on its first call (`SunShadowClipmap.cpp:184` — `if (!Window.OriginSeeded) { WholeWindowDirtyCondition = true; }`, with `OriginSeeded = true` set only in `Integrate`), so image 0 addresses a different lattice than every image after it and legitimately claims a different count. Two settling refreshes before measuring → 51 → 51 across 12 images, 0 evicted. ⚠️ Third instance of this class (post-S3 count assertion, `1x1` masking) — **an assertion that over-reaches, caught by instrumenting rather than arguing.** Any future clipmap-based probe must settle first. |
| **S6 page clear shipped — and "clear the atlas" is WRONG, not merely slow** | ✔️ **LANDED 2026-07-30**, 14 checks / 0 failures | `ShadowPageClear.comp` + `ShadowPageClearSubmission.{h,cpp}` clear only the pages `ShadowPageNeedsRender` selects (`Used && ContentStale`), via a dense page-index SSBO uploaded per image, dispatched `8 x 8 x PageCount`. 🔴 **A whole-atlas `vkCmdClearColorImage` would produce a PIXEL-IDENTICAL image while destroying the cache** — it wipes every `Cached` page, so all 256 come back stale and S7 redraws everything forever. No image comparison can catch this; only the count can, which is why the gate is *"a static scene clears ZERO pages"*. Measured: image 0 = **51**, image 1 = **0**, images 2-7 = **0**. The control quantifies the wrong answers on image 1: conjunction **0** vs **Used-alone 51** (a cache that never hits) vs Stale-alone 0. ⚠️ Clearing to **0** would be the worst possible value — 0 is the NEAREST depth, so every unwritten texel becomes a caster pressed against the light and receivers fall into full shadow; the clear value is pushed from C++ as `ShadowPageClearIdentity` so it cannot drift from S7's `imageAtomicMin` identity. |
| **S6 deliberately includes NO GLSL header** | 📝 noted | `ShadowTileStore.glsl` declares the tile table as a storage buffer at **set 0 binding 0**, which is where S6 binds the atlas storage image — including it collides. First draft did include it and also referenced `ShadowPageCapacity`, which that header **does not define** (it lives in `ShadowPageAtlas.h`, C++ only); caught by compiling the shader before writing any C++ against it. S6 addresses pages by index and needs no tile flag, origin, or word layout, so every quantity arrives by push constant. ⚠️ Compiles as **`vulkan1.0`** (the `ShaderPlan.ps1` default) — correct, since it uses only core `imageStore` on `r32ui`; no `$TargetEnvForShader` entry needed. |
| **The S6→S7 hazard is WRITE-after-WRITE inside one layout** | ⚠️ handled, easy to lose | `RecordShadowPageClear` ends with a `GENERAL`→`GENERAL` image barrier (`SHADER_WRITE` → `SHADER_READ|SHADER_WRITE`). 🔴 Because the layout does not change, **no layout transition will ever cover this** — and without the barrier the clear may land *after* a caster's depth and overwrite it with the identity, which reads as casters flickering rather than as a missing barrier. 📝 `TransitionShadowPageAtlas(..., GENERAL)` is called in the preamble rather than inside the pass, so a future S7 in the same image does not transition twice. |

---

## P5.9b two-scope frame — consequences left open (2026-07-29)

The linear-HDR split shipped (radiance target + one resolve; sky and shade moved into a radiance
scope in the preamble; overlays draw onto the `_SRGB` swapchain after the resolve). Four things it
deliberately changed or left unresolved — none are defects in the tone mapping.

| Item | State | Notes |
|---|---|---|
| **G16 — the P5.9b gate cannot be phrased as identity against the OLD pipeline** | ✔️ **RULED (2026-07-30)** — wording below is authoritative | 🔴 **"Identical for opaque" is FALSE and was withdrawn**; an earlier revision of this row asserted "Opaque pixels are unchanged", which is wrong three times over. (1) **The operator is not identity for in-range values** — `RadianceResolve.frag:45-51` subtracts `Offset` *before* the `Peak < StartCompression` early-out, so every non-black pixel is darkened by up to 0.04 linear even when it never reaches the knee (linear 0.5 → 0.46, ≈ −5.5 codes; linear 1.0 → −15 codes). Khronos' "1:1" claim means baseColor round-trips through a render *including ~4% Fresnel*, not that the curve is a pass-through. (2) **P5.9a's swapchain change was itself an intended shift** — the removed `pow(x, 1/2.2)` differs from the piecewise sRGB OETF Vulkan `_SRGB` formats actually apply by up to ~8.5 codes, worst in deep shadow. (3) fp16 round-trip is the *smallest* term at ±1 code. Combined old-vs-new opaque delta reaches **~35 codes** (linear 0.02: 43 → 8) — a visible global darkening, not rounding. 📝 **Correction to the maths as first argued:** the right statement is **non-commutativity of a non-affine map with a convex combination**, *not* Jensen's inequality — Jensen needs convexity and gives a directional bound, but a sigmoid tone curve is convex in the toe and concave in the shoulder, so it does not apply globally. Equality holds iff the operator is affine over the spanned interval, or `a ∈ {0,1}`, or both operands are equal. ⚠️ Consequence specific to PBR Neutral: it *is* exactly affine (a flat −0.04) through the mid-range, so two well-exposed mid-tones blend **bit-identically** in either order — a gate claiming *all* transparent pixels change would also be false. Divergence is confined to the sub-0.08 toe and the >0.76 knee, peaking at ~+102 codes for a bright HDR background seen through glass. 🔴 **The gate must therefore be stated against newly-blessed goldens, never against the old pipeline** — any identity-versus-old-output phrasing fails on the first opaque mid-grey. Precedent: Godot ships this same consequence as `documentation`, not a bug (godotengine/godot#80868, clayjohn: *"an unavoidable consequence of using HDR"*), with a per-viewport opt-out so UI keeps sRGB blending. Unreal's colour-pipeline notes warn of "differences in appearance to existing content" and never claim identity. Blending in linear before the tone map is the physically correct order (GPU Gems 3 Ch. 24: gamma must be *the last step before display*). |
| **No golden-image harness exists — and the tone map does not need one** | ✔️ **RULED 2026-07-30**, researched against upstream practice | 🔴 First, a fact that invalidates any plan phrased around *"re-blessing the goldens"*: **there are no goldens, and no way to make one.** The only readback in the tree is `ObjectPickReadback` (**4 bytes, one pixel**, for object picking); there is no `vkCmdCopyImageToBuffer` of a colour target, no PNG writer, no reference images, and no comparison tooling anywhere in `Internal/` or `Executables/`. Any gate written as "compare against blessed goldens" was aspirational. ✔️ **Ruling: for the tone map, do not build one — the analytic gate is strictly stronger.** A golden image can only report *"different"*; because the tone map is a **pure function of one pixel**, `ToneMapProbe.cpp` reports *"wrong, and here is the input that breaks it"* — 27/27 checks, zero pixels rendered (see the P5.9b row). The decisive test is **idempotence** (`f(f(x)) ≠ f(x)`, 298/299 samples, always darker), which would have caught the double map on its first run; an image diff would only have shown a slightly darker picture that a human might have accepted. Khronos publishes **no numeric test vectors** for PBR Neutral, but it publishes the closed-form spec (`F90 = 0.04`, `Ks = 0.8 − F90`, `Kd = 0.15`), from which vectors are derivable to arbitrary precision — better than a fixed table. ⚠️ The shipped `.cube` LUT in KhronosGroup/ToneMapping is explicitly an **approximation** for DCC tools; it is **not** a precision reference and must not be used as one. 📝 **When an image gate is eventually needed** (geometry, shadows, anything with a neighbourhood, where no analytic reference exists), the researched answer is **NVIDIA FLIP**, not per-pixel diffing: NVlabs/flip is BSD-3 and **a single header since v1.3** with a pure-C++ backend, and `wgpu` — the closest analogue to this engine, one renderer over many backends — gates on FLIP **mean** with `Mean(0.018)` in practice and a documented starting range of `[0.01, 0.1]`. Blender's two-knob fallback (`fail_threshold = 0.016` + `fail_percent = 1`) is the cheap alternative, with its own bump policy worth copying: **widen the failing-pixel percentage first, and the per-pixel magnitude only above 0.5% failing**, because many pixels off slightly is hardware variance while few pixels off badly is a real bug. 🔴 Do **not** gate on bit-equality: the Vulkan spec makes cross-vendor output implementation-dependent *by design* (`subPixelPrecisionBits` and `subTexelPrecisionBits` are queryable limits, and `standardSampleLocations == VK_FALSE` permits different sample positions), so bit-exactness would mandate one rasterizer design. Mesa only gets away with checksums by pinning each one to a single device+driver, and pays in constant churn. ⚠️ Compare the **final sRGB output**, not the linear HDR target — LDR-FLIP assumes sRGB in `[0,1]`, dEQP's fuzzy compare accepts only 8-bit unorm, and HDR comparison needs exposure-sweep machinery. Capture the HDR buffer for *diagnosis* on failure, never as the gate. 📝 Blessing protocol when it exists: **never bless and fix in the same commit** (they are mutually exclusive — Filament's CI-accept path literally disables the gate), refresh only tests that actually failed (Blender copies only new/failing), record the metric delta in the commit message, and treat a missing reference as `VERIFY` rather than a pass. |
| **✔️ Double tone map — CLOSED 2026-07-30** | ✔️ fixed + device-verified | P5.9b was marked 🟢 while **both** upstream shaders still tone-mapped into the linear target the resolve maps again, so every scene pixel was mapped twice. `SurfaceShade.frag`'s `TonemapPbrNeutral` and `SkyDome.frag`'s ACES-ish curve are both removed; `RadianceResolve.frag:79` is now the only tone map in the shader tree. 🔴 The sky curve was doing second, worse damage: it ended in `clamp(Mapped, 0, 1)`, so **all >1.0 sky radiance was destroyed before reaching the float target** — sun disc and horizon headroom gone, unrecoverable downstream. 📝 Lesson worth keeping: all three files' comments *claimed the defect was already fixed* (`SkyDome.frag` described the double-tonemap defect as "fixed in P5.9b" on line 56 while the offending curve ran on line 97). **A header comment is not evidence a phase landed — grep the call sites.** ⚠️ `SkyDome`'s `Push.Exposure` is now intentionally unused; it stays in the block for layout compatibility. Do not "restore" it. |
| **Matcap is SCENE-REFERRED; the tone-map inverse was deleted as broken** | ✔️ **FIXED 2026-07-30 — the previous entry here was WRONG and is corrected below** | This row previously recorded the matcap as *"⚠️ accepted, has a caveat"*, writing through `InvertPbrNeutral` so the resolve's forward curve would cancel. 🔴 **That inverse did not have a caveat — it had a catastrophic bug, found by measuring the round-trip instead of asserting it.** PBR Neutral's display peak approaches 1.0 **asymptotically and never attains it** (`Peak=1` → 0.880, `Peak=5` → 0.987, `Peak=1e6` → 0.99999994), so a display value near 1.0 has **no finite linear pre-image** and the inverse's `D²/(1-NewPeak)` term explodes — display 0.999 demands linear 58, display 1.0 demands ~5760. The `max(1.0 - NewPeak, 1e-5)` guard bounded the *divisor* but not the *result*, so every channel saturated. Measured over a 9261-sample sweep: display **(1.0, 0.5, 0.0) round-tripped to (1.0, 1.0, 1.0) — PURE WHITE, 255 codes of error.** A saturated matcap rendered white. Round-trip error by display peak: `0.04–0.75 → 0.000` (exact, the affine region) · `0.775 → 2.6` · `0.90 → 7.6` · `0.95 → 45.8` · `1.00 → 255.0`. ⚠️ Worse, `Studio` reaches ~1.42 before clamping, so the ramp sat **squarely in the blow-up zone** — this was not a corner case. **Two alternative fixes were measured and rejected:** clamping the input to the knee caps a matcap at 0.76 display (white reads ~61 codes dark) and still drifts hue by 0.167; a per-pixel display-referred flag is structurally correct but `OutColour.a` is **NOT free** (verified: the shade pass runs `blendEnable = VK_TRUE` with `SRC_ALPHA` and the glass path writes a real alpha), so it needs a second attachment or a stencil bit. ✔️ **Shipped fix: the matcap is authored as LINEAR RADIANCE and tone-mapped by the resolve like every other surface.** `InvertPbrNeutral` is deleted (~26 lines), the 0..1 clamp is gone (it existed only to keep the inverse in its narrow domain), zero round-trip error because there is no round trip, no F5-bypass caveat, and ~15 ops cheaper. The trade is a **look change** — a matcap reads as a lit material rather than an exactly-preserved authored image — accepted deliberately (user ruling, 2026-07-30) as obviously correct against rendering saturated colour as white. 📝 Lesson: **a round-trip claim is not evidence until the error is measured.** The 🔴 block where the inverse used to live carries the numbers so it is not re-proposed. |
| **`RenderSchedule` Phase-0 premise is void** | 🚩 needs re-statement | The schedule's gate was "schedule path and inline path produce identical pixels", built on a one-scope sky→grid spine. Sky no longer records in the swapchain scope, so the schedule now records only the grid. Default-OFF and inert today, but **do not flip `EnabledCondition` on** until its gate is redefined against the two-scope frame. |
| **Grid and id-hash resolve now draw OVER the shade** | ✔️ grid FIXED · ⚠️ F2 intended | 📝 Correction to this row as first written: it claimed the grid drawing over the objects was acceptable because it "owns no depth attachment". It was not acceptable — an infinite ground plane painted over the objects standing on it. Fixed by giving the grid a manual depth test (samples the raster's scene depth, rejects occluded hits) instead of a depth attachment, so it stays a pure colour composite that skips the tone map yet still reads correctly behind geometry. The **F2** inversion stands as intended: the debug hash should win over the shading it is compared against. |
| **Exposure has no UI and no auto-metering** | 🚧 unresolved | `SceneExposure` is a live push constant fixed at 1.0 with no control bound. F5 flips the operator (PBR Neutral ↔ linear-clamp bypass) but nothing drives exposure. Needs either a key/slider or eye-adaptation; the latter wants a luminance reduction over the radiance target. |
| **Grid depth test is perspective-only** | 🚩 known gap | The shader derives its hit distance as `RayParameter × DepthNearPlane`, an identity that holds only because the perspective vertex path builds the ray as eye → near-plane point. Under a PARALLEL projection the vertex stage spans near → far, so the scale is wrong and `AssembleGridConstants` sets `DepthTestEnabled = 0` — the grid draws over objects again in ortho views. Fixing it needs the ray expressed in view metres in both lenses (push the view-axis span, or normalize the direction and carry a separate scale). Only 12 bytes of push budget remain, so this likely wants a small uniform block rather than another push float. |

## P6 sun-shadow deferred items (2026-07-29)

From `Documentation/PLAN-SunShadowClipmap.md` §6.2 / §8. These are the gaps P6 ships **with**, plus
one route deliberately rejected — not defects to fix in the shadow math.

| Item | State | Notes |
|---|---|---|
| **Merge floor + head geometry into one buffer pair** | ✖️ rejected for P6 | Was route ③ for getting the plane shaded. Touches `WorkspaceDocumentDecoder`, the partition-base scheme, and `InstanceCullSubmission`, and breaks the disjoint-range identity contract. P6.3a instead binds the floor as a second mesh channel — **b5/b6/b7** as shipped, not the b9/b10/b11 the plan sketched: bindings 5-7 were free, so the higher ordinals bought nothing. Revisit only if a third mesh channel appears. |
| **Shade's floor bindings alias the head buffers when no floor document loads** | ⚠️ inherent | 🔴 An unwritten descriptor is undefined memory, not an empty buffer, so b5-b7 are always written — aliased onto b1-b3 when `CheckerFloor.wsdoc` is absent. They then report a healthy non-zero length while holding HEAD data. `FloorShadeEnabled` (from `SurfaceShadeInscription::FloorGeometryBound`) is the ONLY safe discriminator; a length test would silently reconstruct floor pixels from head triangles. Same trap `ComponentOverlayInscription` documents for its authored tables. |
| **A page's mapping slot must be STORED, never re-derived** | ⚠️ inherent | 🔴 The slot a shadow tile occupies depends on the clipmap's toroidal origin, which **scrolls between the image that files a page and the image that evicts it**. `ShadowPageRecord::OwnerSlot` is therefore authoritative over `OwnerTile`; re-deriving the slot at reclaim time clears an innocent mapping and leaves the real one dangling, so two tiles point at one page and one reads another region's depth. Caught only by the 40-image churn probe (the sole test that scrolls *between* allocation and eviction) — it also leaked pages into a permanently-`Used` state, `used=90` vs the correct `used=16`. **The S4/S5 GPU port must carry the same stored-slot rule.** |
| **P6.2 ships S4/S5 as a CPU mirror; S6 clear is not written** | 🚧 unresolved | The pool logic, atlas image, views, and layout transitions are live, but `ShadowPageFree`/`ShadowPageAllocate` exist as CPU functions rather than the planned `.comp` dispatches, and `ShadowPageClear` (S6, atlas ← `0xFFFFFFFF`) has no shader yet. Deliberate: the depth raster (P6.4) is the first consumer that needs cleared pages, and writing the clear before there is anything to clear could not be verified. Census is CPU-side too — the host-visible readback buffer (§4 C6, one image stale) is not built. |
| **`SceneInstance.Tint` is never read by the shade pass** | 🚧 unresolved | Found while investigating why the floor renders white. `SurfaceShade.frag` declares `Tint` in `SceneInstance` but never multiplies by it, so authored per-instance tint is dropped for **every** object, not just the floor — `CheckerFloor.wsdoc` carries `tint = [0.34, 0.34, 0.34]` and shows near-white. Not a shadow defect; wiring it would raise floor/shadow contrast. Left as-is at the user's direction ("leave plane white"). |
| **🔴 No page-staleness flag — a moved caster's shadow never rebuilds** | ✔️ **RESOLVED 2026-07-30** — `ShadowPageRecord::ContentStale` added as a bit **orthogonal** to `Ownership`, with `InvalidateShadowPagesInSphere` (tag pass, call **twice** — previous **and** current bounds), `InvalidateShadowTileContent` (single tile), `MarkShadowPageRendered` (the **only** clear route, S7-owned) and `ShadowPageNeedsRender` = `Used && ContentStale`. Census gains `PageStaleCount` + `PageRenderCount`. **69/69 probe checks pass** (45 pre-existing + 24 new). 🔴 **The lifetime mismatch is what forbids merging it into `Ownership`, and the probe pins it:** `OpenShadowPageImage` demotes `Used`→`Cached` every image, so a merged encoding would be **wiped by that demotion** — a page tagged stale in image N would silently read clean in image N+1 with nothing having redrawn it, reintroducing the very defect by encoding. Staleness therefore persists until a **render** clears it. 🔴 Three paths set it that are easy to miss: `DetachShadowPage` (a recycled page holds the *previous owner's* depth — the most dangerous page in the pool), the free-list grant, and the eviction grant. The **cache-hit path deliberately does not touch it** — re-requesting a tile means "I still want this page", not "its depth is fine". ⚠️ Sphere bounds **over-mark** (corner tiles tagged without contact); accepted knowingly since the bounds already exist world-space per instance, and an OBB upgrade only ever *removes* tags so it cannot turn a correct image incorrect. 📝 Measured: static scene → `PageRenderCount == 0` (cache fully intact); moved caster → `prev=4 cur=1` tagged of 25 held, i.e. the **vacated** tiles dominate, which is the deletion argument showing up in the numbers. Original diagnosis below, kept because the *why* is the load-bearing part. — `ShadowPageOwnership` has `Free`/`Cached`/`Used`, which encodes *"should this page exist"* but **not** *"is this page's content still correct"*. A caster moves → its page stays `Cached` → the next request takes the cache hit at `ShadowPageAtlas.cpp:240-250` → the tile serves depth from the object's **old position, permanently**. The only invalidation that exists is `InvalidateShadowLevelPages` (a whole level, for the C4 rotated-sun path); there is no per-tile route. 🔴 **This is what Fork C's "×2 previous + current" bounds is actually for**, and it was nearly dropped as redundant cost: EEVEE's pair is **cache invalidation, not motion blur** — `eevee_shadow_tag_update.bsl.hh:8-11` *"needs to tag the shadow map tiles it was in and is now into… 2 pass of this same shader"*, dispatched over `bounds_buf.previous()` then `bounds_buf.current()` (`eevee_shadow.cc:940-951`), both setting `SHADOW_DO_UPDATE`. Proof it cannot be motion blur: **deleted** objects are pushed to `past_casters_updated_` only (`eevee_shadow.cc:829-832`) — no current bounds, no motion, yet the vacated tiles still must be invalidated. **Fix = add a `do_update`-equivalent flag distinct from `Used`**, tagged from both the previous and current caster bounds; a page renders only when used **and** stale (`eevee_shadow_tilemap_finalize_comp.glsl:66`: `tile.is_used && tile.do_update`). ⚠️ Do **not** collapse the two into one flag — allocation and staleness are different questions, and conflating them is exactly this defect. 📝 The previous-bounds pass is cheap to gate: EEVEE requires `recalc && is_initialized`, so static and first-frame objects skip it entirely and the dispatch is skipped outright when the changed-caster list is empty. |
| **The S1/S2/S3 marking chain is dispatched and running** | ✔️ **RESOLVED 2026-07-30** | ✔️ The virtual tile table + `Shadow/Shaders/ShadowTileStore.glsl` are live, **62/62 CPU checks**, device log **byte-identical** to the P6.3c baseline (correct — nothing dispatches it yet, so this proves the unit is inert rather than quietly perturbing the renderer). One 32-bit word per tile (6144 = 24 KiB): bits **0..7 marking-only** (`atomicOr`), bits **8..31** the allocator's packed page index, masks asserted **disjoint** so a marking pass cannot corrupt a live mapping. 🔴 The per-image reset **clears demand but preserves `Update`** — the same lifetime asymmetry that keeps `ContentStale` out of `Ownership`, one level up; a reset that wiped `Update` would let a moved caster's tile read clean next image with nothing redrawn. ⚠️ **Externally corroborated:** Blender's developer docs independently describe the same pass order and the same three-state O(1) allocator, and name a mechanism this plan omitted — **Tile Masking** (*"untag the lower LOD tiles that are completely overlapped by higher LOD tiles"*), now `MaskRedundantShadowTiles`. 🔴 **Two real bugs, both caught by probing and both invisible to structural tests — worth remembering as bug *classes*.** ① A level's window enumerated as `Origin + Slot` instead of **`Slot - Origin`** (`ToroidalOrigin` is stored in **ADD form**, the *negated* corner, `SunShadowClipmap.h:22`): the wrong form still covers all 32 physical slots bijectively, so in-range, bijection **and** store-vs-atlas agreement all pass, while naming a different set of light tiles (`[16..47]` vs `[-16..15]`) whose halved ancestors fall outside the coarse window — propagation reached **0/32** coarse tiles instead of 32/32. **Lesson: assert RESIDENCY, not addressing.** The GLSL had the mirror defect (subtracting where the CPU adds), so the validator now asserts that operator's **sign as text** — no compiler checks a constant duplicated across a C++/GLSL boundary. ② Masking used the `Coarse` bit alone to mean "inherited", but **`Direct` and `Coarse` legitimately coexist** (a distant receiver samples a coarse tile itself while nearer geometry propagates into it), so it masked a page a receiver was actively reading — a hole at exactly the distance coarse LODs serve, which reads as a shadow-*distance* bug. Fixed with a separate `Direct` bit propagation never raises; masking requires `Coarse && !Direct`. **Lesson: a bit set by two producers cannot answer "who asked for this".** ✔️ **The three shader bodies now exist and compile** (48 modules): `MarkVisibleShadowPages.comp` (S1, 8×8 over the **id buffer** — not depth, whose clear reconstructs a far-plane point and over-marks), `ShadowTileTagInscription.{vert,frag}` (S2, raster, `Update` only), `ShadowTileLevelPropagate.comp` (S3, local 32×32). ✔️ **All three now execute per image**, via the new `ShadowTileMarkingSubmission` unit (3 pipelines, ONE shared 5-binding set the shaders deliberately agree on, plus the binding-3 per-level origin UBO none of them had a producer for). Recorded in `RecordPreamble` at the single point both of S1's inputs are legal to sample — right after the id buffer reaches `SHADER_READ_ONLY` and before the radiance scope opens (Vulkan forbids nesting S2's own dynamic-rendering scope inside it). 📝 Measured steady state: **~122 direct + ~8-16 coarse-only of 6144**, with the fine→coarse 4× shrink visible in the ratio, `update` accumulating across images (staleness outliving the reset, as designed) and **zero validation errors**. 🔴 The chain writes **demand only** — nothing allocates a page or rasterizes shadow depth from these bits yet (S4/S5 are CPU mirrors, S7 is P6.4) — so the presented image is unchanged and the Phase-0 pixel-identity gate still holds. 🔴 **Three real defects surfaced only by running it, all invisible to the compile-verified state and all worth remembering as classes:** ① S3's pipeline lacked `VK_PIPELINE_CREATE_DISPATCH_BASE_BIT`, which a non-zero `baseGroupZ` requires at **creation** time — caught by the validation layer, not by any count. ② **`gl_WorkGroupID.z` excludes the dispatch base**, so all six per-level dispatches processed level 0 and demand reached LOD 1 and stopped: `coarse-only 0` on every image with `TileUsedCount == TileDirectCount` **exactly**, which reads as "propagation is broken" rather than "the level index is always zero". Fixed by reading `gl_GlobalInvocationID.z`, which is base-inclusive. **Lesson: a builtin that silently reads 0 is indistinguishable from a logic bug — check the count identity, not just the count.** ③ Upload and readback shared **one** staging buffer, so `UploadShadowTileStore`'s CPU `memcpy` clobbered the bytes the previous frame's download copy was still writing; with `FramesInFlight = 2` the tally alternated real / all-zero every image. Fixed with a **3-slot readback ring** (≥ `FramesInFlight`, the same argument `PickReadbackSlots` documents) and by resolving **before** the reset+upload, not after. ⚠️ The tally still **cannot** see an origin-sign error: `Slot - Origin` and `Origin + Slot` both enumerate all 32 slots bijectively, so every count is byte-identical either way — the CPU-mirror cross-check is tracked as its own row. 📝 Two plan rows were **wrong and are now corrected**: S2's "instanced OBB" had **no data source** (the only bounds producer, `PartitionCullRecord`, carries a **sphere**; the sphere proxy never under-covers, since a sphere's silhouette is a circle of the same radius under every sun angle — an OBB path needs a new producer), and S3's `1×1×LevelCount` **cannot order the levels** (no inter-workgroup ordering exists, so one dispatch propagates one level deep nondeterministically — the recorder must issue one dispatch per level, ascending, with a barrier between). |
| **The origin-sign convention is GPU-verified word for word** | ✔️ **RESOLVED 2026-07-30** | `_ClaudeScratch/tmp/OriginSignProbe.cpp` drives the **shipped `.spv`** headless against a CPU mirror written independently of the camera code (borrowing `BuildInverseViewProjection` would have made the comparison circular) and compares **all 6144 words elementwise**: **0 mismatches** against the ADD-form mirror. 🔴 **The control is what makes it load-bearing:** the sign is a *parameter* of the same mirror, so the probe also builds the wrong-sign form and asserts it **disagrees** — **1704 mismatches**. Without that, a passing run at a zero origin would prove nothing, which is why `Check(AnyOriginNonZero)` gates the comparison (measured L0 `(-11, 59)` … L5 `(16, 18)`). 📝 **The count-blindness claim was over-stated and the run pinned where it actually holds.** Bijectivity covers **S1 marking only**: both signs enumerate all 32 slots per axis, so the post-S1 used count is identical (**646 == 646**, measured) and no tally can tell them apart. **S3 then aggregates fine tiles into shared parents via `ResolveCoarserTile`, and which receivers share a parent DOES depend on the slot they landed in** — so post-S3 totals legitimately **diverge** (**964 vs 1007**). ⚠️ Consequence for future work: the runtime tally is sampled *after* S3, so it is **weakly** sign-sensitive — not a usable check, but do not describe it as fully blind. One probe assertion was wrong, not the code: asserting count equality after propagation was my error, corrected to assert it at S1 and to print the S3 divergence as a recorded property. |
| **Headless device enables `VK_KHR_swapchain` with no `VK_KHR_surface`** | 🚧 unresolved | Surfaced by the validation layer while running the origin-sign probe: `vkCreateDevice(): pCreateInfo->ppEnabledExtensionNames[0] Missing extension required by the device extension VK_KHR_swapchain: VK_KHR_surface`. `InitializeVulkanHost` requests the swapchain **device** extension unconditionally, even when the caller enabled **zero instance extensions** — a presentless configuration where its instance-level prerequisite cannot be satisfied. ⚠️ **Latent, not fatal:** the device still comes up with `dynamicRendering=1 fragmentStoresAndAtomics=1`, so every offscreen probe works; the cost is a FAULT line that trains the eye to ignore validation output, and a driver that enforced the dependency strictly would fail device creation outright. Fix is to gate the swapchain request on presentation actually being requested, which also gives headless tools a clean log to diff against. |
| **Light-tile addressing saturates past ~1 Gm from the origin** | ⚠️ inherent | `FloorToLatticeCell` casts `metres / tileMetres` to `int32`, so a level-0 observer beyond **1.07e9 m** (2.1e9 m at LOD 1, 3.4e10 m at LOD 5) produces `INT32_MIN` for every tile and the window stops being self-consistent: **a receiver 3 m from the observer resolves outside its own 32-tile window**, and demand collapses from 86 marked tiles to 6 — measured, not inferred. 🔴 The failure is **silent**: no count goes to zero, no bounds check fires (the toroidal wrap folds `INT32_MIN` to a valid-looking slot), and the shadow simply detaches from the geometry. 📝 **Not worth fixing as an int32 problem** — `float32` positions lose the tile lattice **two orders of magnitude earlier**: at 1e7 m the float spacing is 1.0 m against a 0.5 m tile, so neighbouring receivers already alias onto one tile. Any real planetary-scale fix is camera-relative or double-precision world positions, not a wider tile integer. ⚠️ The int32 `LightTile + ToroidalOrigin` addition itself is **safe** — the origin stays within ±{res/2}, so it never overflows; verified agreeing with int64 arithmetic at `INT32_MAX` and `INT32_MIN`. |
| **CPU marking chain stress-probed: 19,936 checks, 0 failures** | ✔️ verified — **2026-07-30** | `_ClaudeScratch/tmp/ShadowStressProbe.cpp`, run under a **32 MiB counting `operator new` ceiling** so a runaway allocation fails the probe instead of the host. Covers: degenerate stores (uninitialized, 1 level, 1×1, resolution 17, store/clipmap resolution **mismatch**, out-of-range levels); int32 extremes on every entry point; the bit contract (🔴 the packed page index survives all marking **and** the reset; the reset clears demand and preserves `Update`); idempotence of marking, propagation, **and convergence of masking** under 50 re-runs; the two documented masking defect classes (a `Direct` coarse tile is never masked, a **partially** covered one is never masked) swept over 60 pseudo-random demand fields; **4000 images** of scroll churn with teleports and sun rotation (zero allocation growth, `Update` converges rather than saturating); and the addressing **bijection** across 400 random origins × 6 levels. ⚠️ Scope is the **CPU** logic — the readback ring and the barriers are GPU concerns already exercised at runtime. 📝 One probe assertion was wrong, not the code: a 1×1 store still strides words by level, so masking the inherited chain above the `Direct` tile is correct behaviour. |
| **Masking is CPU-only — the runtime `masked 0` is structural** | 🚧 unresolved | `MaskRedundantShadowTiles` is a **CPU mirror with no dispatched counterpart**: the live chain is S1→S2→S3 and nothing lowers redundant `Coarse` demand on the GPU. ⚠️ So the per-image `masked 0` in the tally is **by construction, not a measurement** — do not read it as "no tile needed masking", and do not treat a later non-zero value as a regression. 🔴 The port is **not** a transcription of the CPU loop: masking walks **coarse→fine** (the opposite of S3), so it needs its own per-level dispatch chain with a barrier between levels, and it reads each tile's **four children** — a tile whose children are being masked concurrently would be judged against a half-updated level, which is exactly the ordering hazard S3's `gl_WorkGroupID.z` bug already cost an image of debugging. 📝 The invariants it must preserve are pinned by the stress probe (a `Direct` coarse tile is never masked; a **partially** covered one is never masked; repeated runs **converge**), so the GPU pass has a ready-made differential oracle. Sequencing: masking only pays off once **S5 allocates** and the page pool can actually run short — until then it would drop demand nothing was competing for. |
| **S2's "previous bounds" sub-draw has no data source** | 🚧 unresolved | The ×2 previous+current contract exists to dirty the tiles a caster **vacated** as well as the ones it now occupies, but nothing can supply the previous bounds: `SuzanneSceneInstance` carries no previous transform and the cull upload runs **only in the scene-load branch**. Consequence: with only the current sub-draw issuable, **a caster that moves does not invalidate the page it left behind** — its shadow stays frozen at the old position. Not a shader defect (`ShadowTileTagInscription` already handles both sub-draws unchanged); it needs a previous-transform mirror plus a per-image cull upload. |
| **Shader include-staleness gate** | ✔️ fixed — **2026-07-30** | The Shadow shaders are the tree's first to `#include` anything, which silently invalidated `ShaderPlan.ps1`'s gate: it compared **source** mtime to output mtime, so editing `ShadowTileStore.glsl` left every dependent `.spv` looking up to date and the **old binary shipped**, carrying stale bit values against the new C++ ones — a mismatch that reads as a marking bug, not a build bug. Fixed with `glslc -MD` depfiles (`<output>.spv.d`, now git-ignored; `*.spv` did **not** cover them, so 48 artifacts would have been committed). ⚠️ The script's own comment had predicted this exact failure and named the fix. Verified in both directions: touching only the include recompiles **exactly the 4 dependents** and skips the other 44; deleting it fails all 4 loudly with **exit 1** instead of shipping the stale module. |
| **A level must be able to reclaim its OWN stale pages** | ⚠️ inherent | 🔴 The coarsest-first scan stops at the requesting level so a coarse LOD can never take a finer one's page — but that bound also means a level can never reclaim its own abandoned pages, which **starves a walking camera permanently**: it drops the tiles behind it every image, those pages stay `Cached` and unreachable forever, and the pool settles at `used=72 cached=184 free=0` with every later request returning nothing. Fixed by a same-level last-resort fallback. ⚠️ It does **not** reintroduce the LOD 4/5 thrash the bound prevents, and the reason is the whole safety argument: the fallback requires `LastUsedImage < ImageOrdinal`, so a page is only taken when its tile was *not* requested this image — two tiles contested within one image can never evict each other, because a page claimed this image is `Used`, not `Cached`. Found by the P6.3b motion probe; the P6.2 churn probe **could not** find it because it spread requests across all levels, so its fine-level requests always found a coarse victim. **Any GPU port of the reclaim must carry this fallback and its guard.** |
| **Page residency has no producer until S7** | 📝 by construction | The atlas census reports vacancy at **6144/6144** and will keep doing so until P6.4 lands — the only thing that marks a tile valid is the S7 depth raster, which is not written. Consequence: the dev-profile instrumentation traces the **toroidal origin**, not residency, because residency carries no signal yet. Do not read the vacancy figure as a defect or "fix" it before P6.4. |
| **`CasterBoundsStore` producer** | ✔️ **RULED — reuse, no new store** (2026-07-30) | Fork C is closed: S2 reads `InstanceCull.RecordBuffer` rather than an authored `CasterBoundsStore`. `UploadInstanceCullRecords` already fits one `PartitionCullRecord` per instance as the mesh-local sphere **transformed by that instance's `Model`** (`RenderExtension.cpp:919-929`), so the world-space per-instance sphere is already device-local; capacity becomes `RecordCapacity` = 1024 (`RenderExtension.cpp:755`), retiring the unbacked 128-caster estimate. 📝 The record lives at **`Visibility/SurfacePartition.h:52`** — an earlier revision of this row cited `Render/Surface/SurfacePartition.h`, a path that **does not exist**. ⚠️ Still open, and NOT closed by this ruling: the "×2 previous + current" half of the contract has no data source (`SuzanneSceneInstance` carries no previous transform, and the upload runs only in the scene-load branch), so nothing today can invalidate a page whose caster **moved**. Tracked as its own row below. |
| **Blue-noise tile source** | 🚧 unresolved | `ShadowBlueNoiseTile` (64×64 R8, 4 KiB) is bound but has no origin — generated at init (which algorithm?), baked header array, or loaded asset (then it needs a `Build.bat` copy line; only `.spv` lines exist today). Blocks the P6.6 bias/acne gate. |
| **Single-layer depth leak** | ⚠️ inherent | A page stores one depth per texel, so the tracer sees only the nearest occluder. Thin double-walled geometry can leak. Structural to SMRT — do not "fix" in shading. |
| **Ray/step counts without TAA** | 🚩 tuning | Shipping 4 rays × 8 steps (36 taps) because Frontier has **no TAA** and no denoiser; EEVEE's own default is 1 × 6. Revisit when TAA lands (see the SSS/TAA entry below — same blocker). |

## P5 clustered lights — plan authored; stochastic tier blocked on TAA (2026-07-29)

`Documentation/PLAN-ClusteredLightCulling.md` is the authoritative plan. P5 was verified 🔴 **absent**:
`SurfaceShade.frag:126` carries one `LightDirection` push-constant plus compile-time `LightColour` /
`LightIntensity` / `AmbientColour` (`:274-276`) — no light list, buffer, or culling anywhere.

Decision: **tiles + Z-bins, deferred, scalarized** (Drobot 2017 / EEVEE Next), **not** the UE5/Godot 3D
froxel grid — same memory, ~450× the Z resolution (8096 bins vs 18 at ~1 MB). It needs **no depth
prepass**: the shade unit already reconstructs exact world position at `:324`, and Olsson 2012's cluster
lookup is independent of forward vs deferred. 277 KB at 1080p / 32×32 tiles / 1024 lights.

⚠️ Two corrections worth not re-deriving: **Z-binning is Drobot, not Aaltonen** (the analysing blog is
Sebastian *Sylvan* — name collision), and **UE5 is not clustered-deferred for opaque** (the light grid
serves translucency / volumetric fog / reflections / Lumen only; opaque shades per-light the classic way).

🏴 **The stochastic tier (MegaLights / ReSTIR / HypeHype) is blocked on TAA** — same missing dependency
the SSS entry below already records for Burley. One gap, two blocked features. The exception is
**ReSTIR-Sampled Shadow Maps** (CGF 44 / EG 2025), the one stochastic path that works without ray
tracing.

📝 An earlier revision of this entry also claimed the work was "blocked in-app by the `InstanceOrigins`
compile break". **That was wrong** — `InstanceOrigins` does not appear anywhere in `Internal/Graphics`,
and `Graphics.lib` + `RenderExtensionValidation.exe` both build and run. See the RESOLVED entry at the
bottom. Nothing about clustered lights is blocked on a compile break.

Verified negatives, so they are not re-researched: **no** peer-reviewed 2024–2026 paper on tiled/clustered
binning, and **no** paper at all on visibility-buffer + many-lights — that combination exists only in
engine talks. Note UE5's Nanite resolves its visibility buffer *into a G-buffer* before lighting it.

## Hair / fur shading — deliberately excluded from the material plan (2026-07-28)

`Documentation/PLAN-UnifiedMaterialModels.md` covers 9 shading models + emissive; **hair/fur is out of
scope on purpose**, not overlooked. It is not a channel slot — it needs its own **geometry pipeline**:
strands or cards, sorted transparency, and deep opacity maps. Unreal spends a whole shading model on it
(`SHADINGMODELID_HAIR`) whose graph inputs **replace** Normal/Metallic with **Tangent, Scatter, Backlit**.

Math when picked up: **Marschner 2003** R / TT / TRT single-fibre lobes + **Zinke 2008 dual scattering**
for multiple scattering (global + local, accelerated by a deep opacity map; ~2 orders of magnitude faster
than unbiased path tracing). Industry practice is still hair cards with dual specular highlights
(primary + tinted secondary) plus a transmission term. Research and full citations →
`Documentation/RESEARCH-MaterialModels2026.md` §6.

⚠️ Do not budget a material channel for this now. Revisit only if a character-authoring consumer lands.

## SSS needs TAA before Burley can be the default integrator (2026-07-28)

`RESEARCH-MaterialModels2026.md` §7 verified that all three major engines use **Burley normalized
diffusion** for subsurface, and that Unreal states Burley **"requires Temporal Anti-Aliasing to display
properly"** — it importance-samples the *profile*, not the *lighting*, so undersampled lighting shows as
stochastic noise. Frontier has **no TAA**.

Consequence for the M6 milestone: ship **Jimenez separable SSSS** (deterministic 7-tap) as the default,
with Burley behind a toggle, until TAA exists. Keep **Penner pre-integrated** as the zero-dispatch
forward/low-end tier. Revisit the default once TAA lands.

## FlowNetwork — deferred; blocks Terrain/TerrainFieldProgram (2026-07-27)

During the Authoring migration, `Graphics/Terrain/TerrainFieldProgram.{h,cpp}` was found to include
`FlowNetwork.h` — an authored-graph type that in the retired tree is a **UI component**, not a
geometry type: `RetiredProject/Engine/Internal/Interaction/Interface/Components/WorkspaceHost/FlowNetwork.{h,cpp}`
(with a partner `FlowNetworkPanel.{h,cpp}`). Its golden home is **`Interface/WorkspaceHost/`** (pillar 6).

Deferred by user direction — **not ported this session.** Consequence: **Terrain does not fully
compile** until FlowNetwork lands (TerrainFieldProgram's `RetrieveFlowEntry` / `FlowEntry` / `FlowLink`
/ `FlowPort` references are unresolved). Terrain source itself is ported and rehooked; only this one
cross-pillar include is dangling.

| Target | Source | Action when picked up |
|---|---|---|
| `FlowNetwork.{h,cpp}` | retired `…/WorkspaceHost/FlowNetwork.{h,cpp}` | Port to `Internal/Interface/WorkspaceHost/`; resolve its own includes; then unblock Terrain |
| `FlowNetworkPanel.{h,cpp}` | retired `…/WorkspaceHost/FlowNetworkPanel.{h,cpp}` | Optional — only if the panel UI is wanted (not needed to compile Terrain) |

## Audio — no source for the 8 spec components; design plan authored (Batch K, 2026-07-26)

Spec (`FolderStructure.md`) lists an 8-component audio service (AudioExtension, AudioConfiguration,
AudioDevice, SoundSource, SpatialEmitter, AudioListener, MixOrder, AudioSampleStore). Survey of both
trees found **none of the 8 exist as source**. Per zero-fabrication, **nothing ported, nothing invented**.
Instead the authoritative design is captured in `Internal/EngineContext/Audio/PLAN-Audio.md` (component
map, RT-thread contract, miniaudio seam, build order, house-style conformance). Author each component
from that plan when a real audio consumer lands.

The only real audio code anywhere is a GEngine engine-synth playback path
(`RetiredProject/.retired/GEngine/Internal/Runtime/Audio/` — `AudioOutput.{h,cpp}` + `EngineToneSynth.{h,cpp}`,
~356 LOC). It is **out-of-architecture** for the editor (vehicle-sound feature, depends on GEngine's
`DiagnosticLedger`, guards `GENGINE_RUNTIME_*`). Kept as a **pattern reference** in PLAN-Audio.md §7 — do
NOT port as-is; resurrect under a game runtime only if procedural audio ever returns.

| Target component | Source | Action when needed |
|---|---|---|
| `AudioDevice.{h,cpp}` | reference only (retired `AudioOutput`) | Author miniaudio seam per plan §4 |
| other 7 components | ❌ no source | Author from PLAN-Audio.md §2 build order |

## macOS windowing backend — build integration (Objective-C++ + frameworks) (2026-07-26)

`Internal/Platform/Windowing/PlatformWindowMac.cpp` is written and links the full `PlatformWindow`
seam (guarded on `__APPLE__`; the Stub already excludes `__APPLE__`, so exactly one backend compiles
per OS). Nothing to download — it uses only the macOS SDK frameworks that ship with Xcode's Command
Line Tools and the `VK_EXT_metal_surface` header already vendored in `ExternalPackages`. Two
build-infra follow-ups are needed before it does anything on a real Mac:

- **Compile this ONE unit as Objective-C++ (`.mm`) with `-DFRONTIER_APPLE_COCOA`.** All Cocoa /
  `CAMetalLayer` code sits behind `#if defined(FRONTIER_APPLE_COCOA)` (same seam technique as
  Wayland's `FRONTIER_WAYLAND_PROTOCOLS`). Compiled as plain C++ (the only mode on the Windows box)
  it builds against core `<vulkan/vulkan.h>` alone: the target still links, but every window reports
  failure. The real layer switches on only when the unit is built `.mm` on a Mac with the define set.
- **Link `-framework Cocoa -framework QuartzCore`** on Apple targets (QuartzCore supplies
  `CAMetalLayer`; Cocoa supplies `NSWindow` / `NSApplication` / `NSView`). MoltenVK provides
  `vkCreateMetalSurfaceEXT`.

Status: **UNVERIFIED until built on macOS** (no Mac available here; the enum / signature references
were checked against `InputPacket.h` by hand and match). When the target build infra is migrated, add
the `.mm` compile rule + framework link flags for the Apple target, then compile-verify.

## Navigation ViewCube — second half not yet ported (Batch J, 2026-07-26)

`Navigation/Camera/` (12 files) is ported + build-verified. The second half of the Navigation pillar,
`ViewportOrientationManipulator/` (the live ViewCube — 17 files under Configuration/, Intersection/,
Projection/, Topology/, AuxiliaryTriggers/ + the two roots), is **not yet ported**. Source is in the
reorg tree at `Internal/Interface/ViewportOrientationManipulator/`. It depends on ImGui
(`Context.DrawList`), reads/writes the now-ported `ViewportCamera`, and its source guards are
`FRONTIER_INTERFACE_*` while the target re-homes it under `Navigation/` (guards → `FRONTIER_ENGINECONTEXT_NAVIGATION_*`).
Note the source already carries a `Projection/CoordinateProjection.h` that collides by name with
MetricSpace's `CoordinateProjection.h` — confirm which is canonical before porting. Port as its own batch.

## Configuration — no real source, not ported (Batch G, 2026-07-26)

Spec (`FolderStructure.md` lines 39–41) lists `Configuration/` = `EngineConfiguration.{h,cpp}` +
`RenderConfiguration.{h,cpp}`. Survey: `EngineConfiguration` is an empty scaffold stub
(`RetiredProject/.retired/Attic-Geometry/.../Root/`); `RenderConfiguration` has **no source anywhere**.
Per zero-fabrication, **deferred, not invented**. (`CameraConfiguration.{h,cpp}` exists in several
trees but is Navigation-pillar, rank 8 — not this folder.)

| Target component | Source | Action when needed |
|---|---|---|
| `EngineConfiguration.{h,cpp}` | 🔴 stub | Author when engine-wide settings land |
| `RenderConfiguration.{h,cpp}` | ❌ absent | Author when render settings consumer appears |

## EngineHost — empty scaffold, not ported (Batch F, 2026-07-26)

Spec (`FolderStructure.md` lines 33–36) lists `EngineHost/` = `EngineHost.{h,cpp}` (was
`RootExtension`, "initialize → loop → finalize"), `ExtensionRegistry.{h,cpp}` (was
`InternalRegistry`), `TimingExtension.{h,cpp}` (delta accumulation). Survey of the source
(`RetiredProject/.retired/Attic-Geometry/Engine/Internal/EngineInfrastructure/Root/`) found all
three are **empty scaffold placeholders** — bare `struct X {};` with "implementation pending"
banners, no logic. Per zero-fabrication, **deferred, not invented**.

| Target component | Source | Action when needed |
|---|---|---|
| `EngineHost.{h,cpp}` | 🔴 `RootExtension` stub | Author the Initialize→loop→Finalize lifecycle when the runtime entry point lands |
| `ExtensionRegistry.{h,cpp}` | 🔴 `InternalRegistry` stub | Author the active-extension registry alongside EngineHost |
| `TimingExtension.{h,cpp}` | 🔴 `TimingExtension` stub | Author delta accumulation when the frame loop needs it |

Also present in the same source folder (not spec'd for EngineHost): `EngineConfiguration.{h,cpp}`
(→ Configuration pillar, rank 5) and `MemorySegment.{h,cpp}` — verify state before porting.

## SpatialAcceleration — placeholder home only (Batch E, 2026-07-26)

No source exists. `Internal/EngineContext/SpatialAcceleration/README.md` marks the reserved home for
future `SpatialPartition.{h,cpp}` (octree/BVH) + toroidal structures. Nothing ported; nothing invented.

## MicroUtils — canonical utilities not yet authored (Batch C, 2026-07-26)

The spec (`FolderStructure.md` lines 56–61) lists 8 MicroUtils components. Survey of the source
trees found only **one** with real logic; the rest are stubs or absent. Per the zero-fabrication
rule these were **deferred, not invented**. Author each when its first real consumer needs it.

| Component | Source state | Action when needed |
|---|---|---|
| `VacancyTable` ★ | ✅ ported this batch (de-forked from `InstrumentVacancyTable`) | — done |
| `TokenIssuer` ★ | ❌ no source anywhere | Author when Scene/Revision identity lands (design: monotonic issue + generation) |
| `TokenAuthentication` ★ | ❌ no source anywhere | Author alongside `TokenIssuer` (stale-token check) |
| `IdentifierAllocation` | 🔴 stub (`struct {};`, "implementation pending") in Attic-Geometry | Author when a real ID allocator is required |
| `ColorConversion` | 🔴 stub | Author when Theme/ColorEntry needs it |
| `StringFormatting` | 🔴 stub | Author when a real formatter consumer appears |
| `HashComputation` | 🔴 stub | Author when hashing consumer appears (xxHash is vendored) |
| `RangeArithmetic` | 🔴 stub | Author when a real range util consumer appears |

Stub sources (do **not** port as-is — no logic):
`RetiredProject/.retired/Attic-Geometry/Engine/Internal/EngineInfrastructure/MicroUtils/*.{h,cpp}`

## Instrumentation VacancyTable — rewire to canonical (deferred, needs confirmation)

`Internal/Interface/Instrumentation/Registry/InstrumentVacancyTable.{h,cpp}` (in the SOURCE tree)
is a prefixed fork of the now-canonical `EngineContext/MicroUtils/VacancyTable`. When the
Instrumentation pillar is ported, rewire it to `VacancyTable<InstrumentCapacity>` (64) and drop the
fork. **Copy-first**: the original stays until the rewire is confirmed to build.

## MicroUtils extras found but not spec'd (flagged, not ported)

Real code in `RetiredProject/.retired/Engine-Deprecated/Internal/MicroUtils/`:
`SparseSolver.{h,cpp}` (~10 KB) and `PortableCRuntime.h`. Not named in the FolderStructure.md
MicroUtils row. Decide placement before porting (SparseSolver likely belongs to a solver/math home,
not MicroUtils).

## Motion — stub components not ported (Batch B, 2026-07-26)

Spec lists MotionExtension, Interpolation/ (Scalar/Vector/Color), BezierEasing, EaseTable,
InertiaDynamics, DampingDynamics, SequenceAnimator, TimelineAnimator. All are empty stubs in the
source. Only the 4 real files were ported (EasingProfile, SpringDynamics, TransitionDescriptor,
MotionEvaluator). Author the rest when a real consumer needs them.

## Normal derivation — smooth / hard toggle + authored sharp edges (2026-07-29)

`SurfaceShade.frag` currently **hardcodes** the geometric (flat) normal:
`normalize(cross(World1 - World0, World2 - World0))`. That was the correct fix for the shade pass
reading an all-zero normal (which normalized to NaN and left every head unlit), but it hardcodes
one shading policy for all geometry.

Root cause chain, so it is not re-diagnosed later:

| Layer | State | Consequence |
|---|---|---|
| `Suzzane.obj` | `s 0`, 499 `vn` for 500 faces — one normal per FACE | authored flat; no per-vertex normal exists |
| OBJ `v/vt/vn` corners | normals are **face-varying** | needs a corner→normal indirection |
| `VertexField.Normal` | indexed **per-vertex** only | face-varying normals have nowhere to live |
| `.wsdoc` bake | writes `normals = []` | nothing reaches the GPU |
| stride-32 `RenderVertex` | position @0, normal @12, texcoord @24 | normal slot present but always zero |

Work when a consumer needs smooth shading (a subdivision surface, or an imported smooth asset):

- Add a **normal-derivation toggle** — flat (per-face plane) · smooth (area-weighted vertex
  accumulation) · authored (use the asset's own normals when present). Per-object, not global; the
  shade pass reads it rather than hardcoding one branch.
- Honour **authored sharp edges / crease tags** — angle-threshold split so hard edges survive a
  smooth derivation. Needs a corner-normal (face-varying) channel, i.e. a widened vertex contract
  through `VertexField` → encoder → decoder → the stride-32 stream, plus a re-bake.
- Until then: smooth-highlight presets (Chrome, Clearcoat, Metal) read as **per-facet** lobes. That
  is a data limit, not a BRDF defect — do not "fix" it in the shading math.

## Material presets — deferred channels + unresolved decisions (2026-07-29)

From `Documentation/PLAN-MaterialPresets.md` §7/§8. The shade pass is live (13 presets over
`SurfaceShade.frag`, one 4-bit model id + a feature mask), so these are the gaps it ships WITH — not
defects in the BRDF. Do not "fix" any of them in the shading math.

| Item | Blocked on | Notes |
|---|---|---|
| **Anisotropic model** (brushed / machined metal) | stride-32 vertex format carries **no tangent** | An aniso lobe needs a per-pixel tangent frame to point along. Deliberately ABSENT from `SurfacePresetTable.h` — not an oversight. Widening the vertex contract is the prerequisite. Also covers §7's "reflection anisotropy", which the plan credits to this model. |
| **Flakes** (metallic-flake car paint, glitter) | needs a flake normal map or procedural node | Not a channel — a normal-perturbation variation on Clearcoat. Post-1.0. |
| **Refraction anisotropy** (stretched refraction, brushed glass) | needs an anisotropic **BTDF** | The Glass tail ships isotropic first. |
| **Matcap sphere texture** | no texture bound yet | `SurfaceShade.frag` synthesizes a studio ramp instead. ⚠️ When the texture lands the normal must be taken to **VIEW space** first — a matcap is indexed by the view-space normal, which is what sticks the lighting to the camera; world space rotates the highlight. |

Open decisions (owner: user; both still 🚧 in the plan):

- **Preset resolution timing** (§5) — load-time flatten · runtime resolve · **both**. Both are
  wanted: runtime for live authoring (TexturePainting edits a preset and re-shades), load-time
  flatten for baked content. Same `SurfacePreset` record, two consumers — exact split decided at M7.
  The shipped path today is the load-time flatten half (`SurfacePresetTable`).
- **GI-hook scope** (§6) — contract-only · also draft the emissive-injection compute unit.
  Recommendation is contract-only: all GI is 🔴 not started, so there is no probe field to inject
  into yet. The 4 `ContributionRole`s need **zero** new channels — emissive routing is already
  carried by `EmissiveStrength > 1.0`.

## ✔️ RESOLVED — `InstanceOrigins` compile break (closed 2026-07-29)

**No longer a defect.** Re-verified on disk: `InstanceOrigins` has **zero matches anywhere in
`Internal/Graphics`** — the `RenderExtension.cpp` reference this entry described was *removed*, not
fixed by declaring the member. `RenderExtension.h` correctly declares no such member, so there is no
`C2039` and nothing blocks `Graphics.lib`.

📝 What it *was*, recorded so the concept is not re-invented: a CPU-side `std::vector<Vector3f>` of
per-instance world origins — one entry per **placed object**, never per-triangle/per-face. It was the
intermediate feeding the prebaked voxelization: capture each instance origin → `TriangleCellOverlap`
→ fill `OccupiedWorldCells`. The surviving pair in `RenderExtension.h` is the real contract:

| Member | Role |
|---|---|
| `OccupiedWorldCells` | truth — cells scene surfaces occupy; GI / shadow consumers read this |
| `DisplayedOccupancyCells` | outer-shell subset, debug overlay only (a viewing decision, not truth) |

⚠️ The vestigial header comment near the clipmap-inspection block still describes deriving occupied
cells "from captured instance origins" — harmless, but it is the only remaining trace.

## Build-script stale-artifact traps (2026-07-29)

Two traps hit while building, both still live and worth knowing before the next build:
`Build/obj/EngineContext/`
held a `LinearAlgebra_Float64.json` dep-stamp with **no** matching `.obj`, and the Build.bat trusts
the stamp — so it skipped the unit and then failed at archive time with `LNK1181`. Deleting the
orphaned `.json` recompiles it. The same scripts also do not treat a HEADER edit as a reason to
rebuild a `.cpp`, so a push-constant / struct change needs the dependent `.obj` deleted by hand.

## Rock formation prototype — deferred (2026-07-31)

`Documentation/Plans/References/InheritedEngine/Prototypes/RockFormation/` (WebGPU, SDF, node-based). M1 renders an arch; these were
deliberately deferred rather than forgotten.

📝 **The prototype's own running notes live at `RockFormation/RockFormationNotes.md`** — confirmed-working
species, the full deferred list with reasoning, hazards, and the verified Blender-addon findings. Kept
beside the code because it is prototype-local detail; this section holds only what outlives the prototype.

**Deferred as of 2026-07-31** (detail in that document): strata/colour warping — the banding is too
uniform, and `DomainWarp` is the wrong tool because it smears bed thickness incoherently, so bedding needs
an anisotropic warp linked to **both** `StratumBand` and `StratumTint`; `TunnelMass` needs more shape
control plus a **carver visualisation** in the editor (an invisible carver is why a 2.4× body-scale error
survived a whole session of clean compiles); surface cracks (aperture is sub-voxel at 256³, so cracks must
enter as a resistance field, not subtracted geometry); rock colouring research (deferred by the author);
erosion rate UI; and wiring the finished `ErosionTimeline.js` into the host.

**Detached pieces need voxel connectivity (M2).** A Voronoi joint can cut clean through a ligament and
leave a chunk floating in midair. This is correct SDF behaviour — a distance field has no notion of "a
piece", so nothing in the field can tell attached rock from a floating fragment. Detection needs the M2
voxel bake plus a flood fill from the ground; only then can a detached part be dropped, or settled onto
the floor and kept eroding there. A field-side heuristic was considered and rejected: it guesses wrong
near overhangs and erases legitimately attached rock.

**Surface reads too smooth and colour too flat.** Ridged relief runs too few octaves for fractured rock,
and there is no fine surface grain, so faces read as clay. Tint varies only over broad strata bands; the
realistic drivers not yet wired in are joint proximity (iron staining on fracture faces), cavity depth
(desert varnish) and ledge dust. Both were deferred on purpose — the render was too slow to judge, and
tuning detail against a render you cannot evaluate is how self-tuned numbers get baked in as if measured.

⚠️ **The prototype must be served over HTTP.** It is split into ES modules, which the browser fetches
under CORS rules; a `file://` page is an opaque origin, so every import is blocked and the page hangs on
"provisioning WebGPU" having run no code at all. Use `RockFormation/LaunchRockFormation.bat`.

🔴 **CPU/GPU uniform layout is a verification blind spot.** `ViewProfileVectors` was 5, derived from a
scalar count, where the declared struct needs 7 lanes — a `vec3f` is 16-byte aligned and claims a whole
lane. It undersized the buffer (848 B vs 880 B) *and* wrote entry dials over the tail of the head. The
shader compiled and the pipeline validated throughout, because the mismatch lives between the shader and
a JS-side byte count that no compile probe touches. `_ClaudeScratch/tmp/CheckUniformLanes.py` now derives
the lane count from WGSL alignment rules and cross-checks the constant, the struct and the writer.
