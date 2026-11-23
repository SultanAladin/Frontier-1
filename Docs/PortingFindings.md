# Porting findings — HTML demo → C++ engine (filed 2026-10-22)

Owner instruction: record everything found while porting; attention later. Priority remains
"get it working". This file is the ledger. Severity: 🔴 blocks parity · 🟠 visible/structural
delta · 🟡 cosmetic/process. Cross-refs: plan `Docs/VehicleHmiEnginePlan.md` (§15 risks),
`Exhibits/LogStream/PARITY.md`, `Exhibits/LogStream/CompileProof.md`,
`Docs/TextureWorkspaceComplaintReport.md`, `Docs/MaterialAndFlakesReport.md`.

## A. Parity limits imposed by the copy rule (by design, owner-set)

1. 🔴 **Demo JS/WGSL were never copied or line-read** (HTML only, per owner rule). Every
   renderer "port" so far is the canonical algorithm behind the checked-in unit/shader
   names (Phase 0 contract: 473 files, 80 shaders, 81 entrypoints, 138 UI labels),
   validated against analytic oracles — **not** against the demo's own outputs. Golden-image
   parity (plan Phase 5/6 exit evidence) needs either the demo running somewhere or
   owner-supplied captures at the SDK phase. The demo's `Tools/RestirPathTracer.test.mjs`
   is named in the contract but was not copied.
2. 🟠 The demo **Outliner body is JS-generated** — the copied shell has only the caption
   and an empty `#outliner-body`. Native rows therefore follow the settled Frontier
   language; tree-vs-flat follows record parentage (both supported). Recorded in PARITY.
3. 🟠 **No demo corpus on disk** (`GeometryHierarchy` test meshes etc. live on the e9
   branch). Geometry/GI suites use procedural corpora with exact invariants instead.
4. 🟡 Demo CSS (13 stylesheets) was not copied: UI tokens come from the approved
   `Reference/LogStreamPanel.html` (owner-approved, D8) and TextureWorkspace lineage.

## B. Known product deltas (native vs approved HTML / demo shell)

1. 🟠 Menus render with upstream `DropdownControlSurface` metrics (owner's reuse
   exception) — row height/padding/check style differ from the web prototype's CSS menus.
2. 🟡 Demo "· Session 04" log label has no native counterpart yet; Popout button omitted
   (dockspace owns windowing); export actions are dialog-free placeholders. (PARITY.md)
3. 🟡 Hover/press transitions are instant in native; the only animation is the live-dot
   pulse. ImGui clamps tall menus on-screen where the web prototype would overflow.
4. 🟡 Outliner: with many rows the child **scrollbar overlaps the eye column** by a few
   px (upstream scrollbar metrics). Rows also sit ~38 px pitch vs the 30 px sled height
   (ItemSpacing adds air) — reads fine, noted.
5. 🟡 Outliner is **stacked over the Log Stream** in FrontierMain's default composition —
   this changed the owner-approved 50/50 hero default (columns unchanged, old master
   capture preserved). ⚠️ Awaiting possible veto/restack.
6. 🟡 `PanelDockspace` keeps a stacked slot's height when its partner closes (Outliner
   alone stays 420 px tall in captures) — upstream behaviour, not ours to change here.
7. 🟡 Notch drawer backdrop reads **black under the CPU rasteriser** (ThorVG vector
   surface not composited in the harness) — capture artefact, caption says so; real
   window may differ. Unverified until SDK build.

## C. Bugs found and fixed during the port (ours)

1. Software viewport cube was sliced by a `clip.w <= 0` triangle-drop → replaced with
   homogeneous Sutherland–Hodgman clipping. (NDC-z *is* screen-linear — an early
   "depth fix" theory was wrong and reverted.)
2. ImGui tables wrap text badly in docked cells → upstream `SameLine(150)` row pattern.
3. Outliner rows advanced ~70 px/row (absolutely-positioned hit targets move the ImGui
   cursor) → explicit per-row advance normalisation.
4. Frustum side-plane normals initially `axis + F/tan` (wrong except tan=1) → `axis + F·tan`.
5. First cluster error metric collapsed identically to 0 (parent radius defined by the
   same max term) → sphere-tree form (child error + radial slack). Root errors now real.
6. First RIS test had proposal ≡ target, which algebraically hides M-scaling ("temporal
   reuse did nothing") → proposal/target exponents separated (0.3 vs 0.7).
7. SVGF edge-retention oracle compared against the nominal 0.9 step instead of the truth
   buffer's actual windowed step (0.724) → oracle fixed; retention is 97%.
8. **Centre-sampled form factors created energy** across radiosity bounces (124% of
   emitted power "absorbed") → symmetric distance regularisation r² + (Aᵢ+Aⱼ)/π.
   Reciprocity stays exact; accounting converges 83% → 90.5% → 100% under refinement.
9. `ReservoirsSpatial` declared persistent without a resting usage → `BarrierDerivation`
   refused the whole frame (its contract working as designed). Resting = StorageWrite.
10. Portable header placed at an upstream path collided after merge → `ProjectContent`
    rename; exhibit CMake `FRONTIER_ROOT` corrected to `../../Frontier` post-merge.
11. Test-writing bugs (solver was right): "back-facing" cards that faced each other;
    "camera facing away" test with geometry still in front; spawn-menu sweep 25 px low;
    parallel build/run tool calls racing (stale binaries read as fresh results) — banned.

## D. Upstream (SultanAladin) observations — for the other agent / owner

1. TextureWorkspace defects from owner screenshots: `Docs/TextureWorkspaceComplaintReport.md`
   (owned by the other agent; untouched here per instruction).
2. Upstream `EngineContent/FontArchives` has **no DM Sans**; the capture harness stages
   its own OTFs. The product needs a decision: bundle DM Sans or accept fallback metrics.
3. ImGui must be the patched fork (`3bae66c` + `Automation/Patches` A–G applied in
   order); stock ImGui lacks the trapezoidal-tab behaviour. ImGui 1.93 aborts if the
   font atlas is unbuilt at `NewFrame` (harness builds it explicitly).
4. `ImGui::IsPopupOpen` outside a frame **segfaults** in this build — harnesses must
   probe `OpenPopupStack` instead. Worth an upstream note/guard.
5. `ConfigureStackedPanels` must precede `ConfigureColumns` (column count validation
   reads the stacked key) — undocumented ordering constraint, now encoded in FrontierMain.
6. Merge hygiene: upstream reclaimed `Applications/`, `Internal/`, `Executables/` in the
   same window our overlay used `Frontier/Applications/` — merged fine, but path
   conventions should be confirmed before the SDK build restructures anything.

## E. Unproven / deferred (the honest frontier)

1. 🔴 **No OS window, GPU, Vulkan, GLFW, or MSVC build has ever run.** All visual proof
   is CPU-rasterised production draw code + `-Werror` compile proofs vs genuine upstream
   headers. The SDK build (Windows) remains the owner's gate (plan Phase 1/3 exit).
2. 🔴 Renderer bodies are portable cores only: no Slang compiled, no demo golden images,
   no FP32-vs-FP64 parity data (cores run double precision for oracle exactness).
3. 🟠 Spatial-reuse unbiasedness is proven in the discrete domain (Jacobian ≡ 1, factor
   hook separately proven). The solid-angle Jacobian parity test is SDK-phase.
4. 🟠 `ClusterNesting` error metric is the sphere-tree stand-in; the demo's simplification
   DAG (and its measured errors) replaces it at the GPU phase. `ClusterNesting` also
   still carries the plan's 🚩 naming flag (§7) — no rename decided.
5. 🟠 GI/filter cores are single-channel (luminance); RGB = same recurrences per channel.
6. 🟠 Atmosphere/cloud models (Phase 6) are canonical Earth models (Bruneton/Hillaire
   constants), not read from the demo's Atmosphere* shaders — parameter parity unknown.
7. 🟡 Phase 7 physics reference is in hand (Pacejka MF6.1 JS in `SultanAladin/Frontier-`)
   and **gated by the owner**; nothing ported. D6 (wheel-ground model) undecided until then.
8. 🟡 D9 Glinty flake replacement approved but deferred until TextureWorkspace stabilises.
9. 🟡 Cabin HMI may depend on a Slate SDK (`SlateCabinLayout/Fittings.js` vs this repo's
   Slate upstream) — Phase 0 open question, needs owner clarification before Phase 8.
10. 🟡 three.js (1.27 MB in the demo) touchpoints still unknown — Phase 0 import scan was
    name-level only; any hidden math/scene helpers surface when the SDK port reads JS.

## F. Process / environment findings

1. Sandbox resets have twice restored **stale working-tree snapshots** over fresh clones
   (most recently: overlay predating the SultanAladin merge). Defence that worked: push
   every turn; recover via `fetch` + blob-compare + `reset --hard`. One page update
   (Indirect GI section, 4.58 MB) was written but absent from its commit — root cause
   unproven (snapshot timing suspected); regenerated. **Lesson: verify `git show --stat`
   after every commit.**
2. Large pushes from the sandbox can fail (`index-pack` on huge single packs; shallow
   fetches missing unrelated-history roots) → full fetch + normal pushes; the one
   force-push slice **accidentally triggered** the paths-filtered networking workflow
   (run 39823088551, completed, content unchanged; token cannot cancel — HTTP 403).
   No further Actions runs have been triggered since; pushes since are plain fast-forwards.
3. `gh pr view` GraphQL is broken (Projects-classic deprecation) → REST via `gh api`.
4. Network is codeload/github/npm/pypi only; no browser/cmake/display/Vulkan SDK in the
   sandbox: builds are manual `g++` replications of the checked-in CMake lists.
5. `git hash-object` (no `--path`) vs index EOL handling can make byte-identical-content
   files hash differently during recovery audits — compare normalised content.

*Filed for later attention per owner instruction, 2026-10-22. Work continues at Phase 6.*
