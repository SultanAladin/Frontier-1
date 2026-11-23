# Texture Studio application plan

## Authorized direction

Texture Studio is the first material-focused standalone application built on Frontier's shared Engine code.
Implement the Material Library first; Frontier embedding follows using the same library and UI, not a second app.
The single progress tracker is `Documentation/Tracker.md`. This document defines scope and acceptance,
not another tracker.

## File format

The current authored material extension is **`.material`**. Its contents are versioned **TOML**, not JSON or a binary
shader archive. Reuse `Engine/Render/SurfaceMaterial.{h,cpp}` and pinned toml++. `Format = 1` describes the existing
opaque/coating contract. Do not silently interpret the reference app's `.texture.json` projects as native materials.
Texture paths remain relative to an explicitly selected `ContentDirectory`. Subfolders do not change that root.
Library indexing must not require a new database format or migrate existing project formats.

## Reference update and ownership

- Inspect and selectively import `Experimental/TextureStudio` from PR #1, without merging unrelated repository work.
- Pin the imported source revision and record local overlays. Preserve both painting apps and their existing projects.
- Keep the React/Three app as an executable interaction reference, including its GPU painting improvements.
- Reference WebGL shaders are not the native Vulkan renderer; unsupported optical/painting features are not ported
  merely because they exist in the reference app.
- `Experimental/TextureStudio`: reference app and initial standalone application composition.
- `Engine`: reusable catalogue, material loading/compilation/saving and application startup/presentation mechanisms.
- `UI/TextureStudio`: reusable native library UI, usable standalone and eventually inside Frontier.
- `Exhibits/TextureStudio`: validation programs, reports and actual captures.
- Existing renderer evidence remains in `Exhibits/Renderer`; remove validation modes from the Project Browser HTML
  and expose them in the separate native/ImGui evidence review instead. Retain tests and original browser samples.

## Delivery sequence

| Sequence | Implementation                              | Acceptance                                |
| -------- | ------------------------------------------- | ----------------------------------------- |
| 1        | Update reference; document native contracts | Pinned source and reproducible checks     |
| 2        | Shared catalogue and edit transactions      | Scan/filter/open/apply/revert/save tests  |
| 3        | Shared startup and standalone window        | Shared GLFW/Vulkan/ImGui initialization   |
| 4        | HTML-matched docked grid and inspector      | Visual parity and real shared controls    |
| 5        | Preview and GPU thumbnail atlas             | Live preview; resident GPU atlas tiles    |
| 6        | Windows delivery and lifecycle coverage     | Runtime, DPI, resize and resource checks  |
| Later    | Embed the same library in Frontier          | No duplicated UI, operations or rendering |

## Native presentation contract — corrected 2026-10-18

The HTML Material workspace is the visual specification, not loose inspiration. The first source/list editor is
only an IO/interaction increment; its default ImGui appearance and fixed two-column layout are not the approved UI.

- Match the reference Material workspace: library card grid, centre material viewport and right property inspector.
- Match typography, colours, spacing, card geometry, selection/hover treatments, icons and property-row proportions
  at the same viewport size, DPI and theme. Reuse shared fonts, themes and semantic icons.
- Reuse `ControlKit::Slider`, `SectionCard`, property pills, dropdowns and other existing shared controls.
  Extend a reusable control only where reference parity requires it; do not create TextureStudio-only copies.
- Host library, viewport and inspector as actual registered dockable panels through the inherited `PanelDockspace`.
  Default docking must reproduce the HTML arrangement, not a permanently fixed `BeginChild` split.
- Reuse the patched tab geometry and existing docking/resize behaviour. Native detached windows remain an explicit
  preference, disabled by default; do not equate internal docking with enabling multiple OS windows.
- Persist layout through existing `.projectspace` workspace conventions, not a new ImGui-owned settings file.
- Keep raw TOML as a secondary source view, not the primary material inspector.
- Material Library remains the current scope. Preserve the reference painting workspaces without adding inert
  painting/baking controls to the native app.

Acceptance requires side-by-side reference/native captures at matched dimensions and DPI, plus real docking,
resizing and shared-control interaction checks. The existing CPU source-editor capture does not satisfy this gate.

## GPU-resident thumbnail atlas

Use one shared GPU-resident colour atlas for resident material thumbnails, not one texture per card and not a
CPU-rendered or read-back/uploaded thumbnail path. This is separate from the material compiler's source-texture
arrays and from the full-size interactive material viewport.

```text
material / texture / lighting / shader revision changes
    → invalidate affected atlas slots
    → prioritize visible cards, then bounded prefetch
    → shared Vulkan material evaluation renders dirty tiles on GPU
    → image synchronization makes atlas writes available to UI sampling
    → shared card UI samples the atlas using tile UV rectangles
```

- Engine owns atlas allocation, slot allocation/reclamation, revision keys, GPU rendering and synchronization.
  UI owns catalogue visibility/selection and supplies visible-thumbnail requests, not Vulkan resource ownership.
- Bake with shared material rasterization into a GPU scratch target, then GPU-copy only the dirty tile and its
  gutters into the atlas. Preserve unaffected tiles; do not create a renderer or pipeline per material.
- Use one sampled atlas image/descriptor for cards. CPU metadata may contain slot tokens, UV rectangles, revisions
  and scheduling information; thumbnail pixels stay on GPU during normal application operation.
- Bound atlas dimensions and GPU memory against device capabilities. A large library uses a bounded resident cache
  with slot reuse, not an unbounded atlas containing every material at once.
- Recycle slots only after their last GPU use retires. Reject stale slot tokens and preserve valid thumbnails while
  edited materials fail compilation. A failed new material gets a diagnostic, not another material's recycled tile.
- Include material/texture contents and preview configuration revisions in invalidation. Apply refreshes affected
  tiles; Save alone must not rebake an unchanged applied material. Scrolling must not rebake clean resident tiles.
- Provide tile gutters and safe UV filtering; do not generate a whole-atlas mip chain that blends adjacent cards.
  Keep colour encoding correct between material output, atlas sampling and UI presentation.
- Budget dirty-tile work per frame and use explicit write/sample dependencies. Do not block the entire device for
  every thumbnail. Atlas replacement and shutdown must retire in-flight resources safely.
- GPU residency is not disk persistence. Rebuild thumbnails after application/device recreation; any future disk
  cache is a separate explicit feature, not an implicit CPU copy of every atlas update.
- GPU readback is permitted for Exhibits validation/captures, not the normal card-rendering path.

Verification must cover dirty-slot isolation, stale-slot rejection, eviction/reuse, unchanged resident tiles,
filtering at tile edges, correct colour, in-flight resource safety and material/texture invalidation. Record actual
GPU execution separately from CPU scheduling checks, compilation and Windows/hardware execution.

## First standalone workflow

```text
Texture Studio executable
    → shared initialization: window/input, Vulkan, ImGui, content, fonts/themes
    → HTML preset catalogue copied to personal application content, or explicitly supplied library directory
    → scan .material files beneath ContentDirectory
    → select → decode → compile textures/parameters → render preview
    → edit → Apply → Save, or Revert
    → shared shutdown and safe resource retirement
```

Do not launch the CAD Project Browser or insert renderer evidence specimens into user libraries.
The user explicitly requested the HTML library: ship its actual 127 presets, not invented replacements.
Use Frontier's shared lifecycle and settings/preferences conventions. Application integration must not create a
parallel settings format. Persistent last-library restoration needs explicit integration with existing projectspace
preferences; until then accept an explicit directory rather than claiming persistence.

## Material Library scope

- Recursive, deterministic catalogue of `.material` files, with names, relative locations, layer counts and diagnostics.
- Search by name/path; invalid documents remain discoverable, with errors rather than silently disappearing.
- Root-contained paths, no symlink escapes, bounded reads and catalogue traversal.
- Open a document without modifying it. Keep authored source, applied material and disk contents distinct.
- Apply validates material syntax and texture compilation before replacing the last valid applied material.
- Revert restores the last successfully loaded/saved source, not an invented default.
- Create and duplicate refuse overwrites. Save verifies the on-disk source has not changed externally.
- Use the existing `.material` schema; preserve source comments during ordinary source editing/saving.
- Structured layer controls reuse the same edit/apply path; no separate UI-only material meaning.
- Preview sphere/plane/bracket, orbit/zoom and exposure; thumbnails use shared renderer resources and invalidation.
- Library catalogue operations and IO must be usable without ImGui or a window.

## Verification gates

Local CPU checks cover invalid documents, nested directories, Unicode, filters, absent roots, symlink escapes,
texture compilation, apply failure retaining the last valid material, source/disk conflicts, create/duplicate safety,
save/revert and source preservation. Add ASan/UBSan runs and native ImGui draw/interaction tests.

Retain existing material/Vulkan regressions. Browser review, CPU ImGui execution, Windows cross-compilation,
Windows execution and hardware GPU execution are distinct results. Never describe a captured HTML page as a native
window or a cross-built executable as executed on Windows. Do not run GitHub Actions without permission.

## Deferred work

Painting, baking, procedural families other than automotive paint, general asset browsing, Frontier embedding, complex-IOR conductors,
transmission/refraction, SSS, multiple scattering, HDR environment transport, GI/RT/ReSTIR and renderer performance
claims are outside the first Material Library milestone. Preserve reference features without displaying inert controls.

## HTML library and presentation correction — 2026-10-18

The native default is now the actual 127-entry HTML library. Complete normalized recipes and control definitions
are retained in typed TOML; native `.material` definitions compile physical coefficients and the executable automotive paint block. This does not
close procedural shader parity. An explicit directory remains untouched by default content installation.

Native styling now uses the HTML panel sizes, 79px header region, dark 30px dock strips, collection/category pills,
125px cards, rounded inspector sections, shared 24px/22px sliders and matching DM Sans source faces. Shader-ball
profile equations and pose follow the HTML. The shader ball now has a rendered pedestal, foot and ring; shared/scoped semantic icons and scrollable inspector cards are implemented. Remaining reference interactions, viewport tools, other procedural shader families, complete reference shading and true Windows captures still block 1:1 visual acceptance. Do not close the
user's 1:1 requirement based only on the improved layout, exported catalogue or portable test results.


## Automotive paint and scoped icon correction — 2026-10-18

- Port the actual HTML cellular flake/binder/orange-peel equations into shared native Slang, not metadata-only recipes.
- Preserve all automotive fields through strict `.material` codec, Apply/Revert/Save/Duplicate and immutable GPU parameters.
- Render the original-dimension tapered stand, lower foot and metallic trim as classified GPU geometry; paint edits must not colour the base.
- Inspector cards use shared surfaces/sliders, wrap descriptions, collapse and scroll without nested opaque rectangles. Correct numeric units/log scales, disabled alpha and tooltip hit regions.
- Shared editor symbols live in `Engine/Interface/Icons/EditorSymbols.inc`; Texture and Fluid have separate scoped catalogues. `IconArt::Catalogue`, `Available`, rasterization and presentation enforce domain access, including cache hits. Editor icons remain reusable everywhere.
- New local automated checks: `VerifyProceduralPaint.py` (strict/ASan/UBSan actual GPU sensitivity and scoped SVG rasterization), plus the existing library/workspace/atlas/renderer regressions. CTest registers icon checks automatically and GPU paint checks when shaders/driver are explicitly provisioned.
- Package a linked Windows x64 executable, fonts, icons, 127 presets, ten compiled shaders, notices and file fingerprints with `Automation/BuildTextureStudioWindows.py`. A linked cross-build is not Windows execution; the user's runtime/hardware test remains open.


Verification completed: 250 strict/sanitized library/UI checks per mode; 58 paint and 90 icon checks per mode;
strict/sanitized atlas and existing renderer regressions; actual linked Windows x64 GUI test ZIP with verified
payloads and fresh source/shader fingerprints. See `Exhibits/TextureStudio/Results/DeliveryChecks.json` and
`WindowsReadme.md`. User Windows launch/hardware testing is still required; other procedural families and
1:1 appearance remain open rather than being closed by the successful automotive-family implementation.


## Standalone applications and optional Frontier workspaces — authorised 2026-10-18

Each application must run independently and expose the same reusable native UI/rendering implementation when
its Frontier extension is enabled. Disabling an extension removes its workspace, commands and specialised icons;
it does not delete authored content. Standalone and embedded execution share initialize/activate/loop/finalize
contracts, formats, diagnostics, theme/fonts and GPU resources. No React wrapper or duplicated engine is planned.

| Application / extension | Standalone scope | Frontier scope when enabled | Delivery order |
|---|---|---|---|
| UV Workspace | UV islands, seams, packing and distortion inspection | UV workspace and Texture-scoped tools | After material acceptance |
| Texture Workspace | Current build: Material Library, Preview and Inspector only | Same docked material UI; painting later | Current delivery |
| Parametric Sketch Workspace | Constraints, dimensions and sketch solving | Drafting/sketch workspace | After shared extension contracts |
| Fluid Editor | Water/smoke authoring and simulation inspection | Fluid workspace; Fluid-only icons | Planned |
| Fracture Editor | Partitioning, fracture constraints and inspection | Fracture workspace | Planned |
| VFX / Particle Workspace | Particle sources, motion and visual-effect inspection | VFX/particle workspace | Planned |

**Frontier test executable opens only the Project Browser.** Opening/selecting a project is validated and logged,
but this test executable must not silently enter the general editor or a CAD viewport. Texture Workspace exposes
only the three material panels, not Pattern/Texture/Stamp/Baking tabs. Existing painting prototypes remain intact.

### Current implementation and CI sequence

1. Add interactive zoom out/reset/in pill, wheel zoom, preview geometry selection and environment/exposure controls.
2. Keep thumbnail geometry/lighting fixed; preview changes dirty only the full-size GPU target. Reuse programs,
   uploaded geometry, resident material textures and stable preview/atlas descriptors.
3. Supply Studio Softbox, Warm Courtyard, Cool Rim and Overcast lighting presets with real GPU parameter changes.
4. Add bounded per-run diagnostic files: startup/content roots, fonts/icons, Vulkan device/driver, shaders,
   selected material/revisions, preview settings, atlas work, persistence and severe exceptions. Never log source
   documents, credentials or every pointer movement. Flush actionable events; retain eight runs, 8 MiB per run.
5. Publish the required sources on this session branch (user explicitly approved), dispatch Windows GitHub Actions,
   compile shaders from `Engine/Render/Shader` and place runtime SPIR-V in `EngineContent/ShaderArchives/Surface`.
6. Produce one test artifact with Frontier.exe, TextureWorkspace.exe, shared content, licences, SHA manifest,
   launch instructions and diagnostic collection instructions. CI logs are uploaded even if compilation fails.
7. User acceptance: Windows startup, browser-only Frontier, material-only Texture Workspace, all four shapes,
   zoom/reset, all four environments/exposure, green paint and Apply/Revert/Save; collect logs on any failure.

### Next after this build

- Fix defects from user Windows/GPU-driver logs before adding more tools.
- Close remaining HTML/native lighting, viewport and inspector differences; port the remaining procedural recipes.
- Define extension availability and `.projectspace` persistence/missing-extension diagnostics, then embed the same
  Texture UI in Frontier behind an explicit enable toggle.
- Implement UV standalone/embedded parity, then parametric sketching; begin Fluid/Fracture/VFX as independently
  tested applications with domain-specific icons, content and GPU budgets. These future applications are planned,
  not represented as implemented by the current CI build.


### Windows Actions acceptance record — 2026-10-18

- **Passed:** [Windows Actions run 39142811899](https://github.com/SultanAladin/Frontier/actions/runs/39142811899).
- Built revision: `c586fd707283afea78462f8e467d6c85ba4c44e5` on `arena/73e163a3-frontier`.
- [Download Windows test applications](https://github.com/SultanAladin/Frontier/actions/runs/39142811899/artifacts/12142541200).
- [Build diagnostics](https://github.com/SultanAladin/Frontier/actions/runs/39142811899/artifacts/12141379773).
- Both native Windows x64 executables linked; all ten Slang entries compiled and packaged in the runtime directory.
- Five Windows CPU/native ImGui acceptance tests passed. Local GPU shape/environment/zoom/exposure, stable image
  views and thumbnail isolation passed; strict/sanitized material transactions each passed 259 checks, and actual
  preview pointer input passed both strict and sanitized execution.
- CI exposed and corrected Windows checkout line endings, native path separators after lexical normalization,
  and a binary-preservation test fixture. Authored material comments, conflict refusal and last-valid Apply remain tested.
- Runtime logs: `%LOCALAPPDATA%/Frontier/Logs`. `CollectDiagnostics.cmd` copies logs beside the test executables.
- Interactive Windows Vulkan startup and hardware-driver execution are **not** certified by the CPU CI tests.
  Use the packaged README checklist and send logs on failure. GitHub artifacts are retained for fourteen days.
- Evidence-only documentation is recorded after the compiled revision; no application code is changed by that record.


### Correction and distribution hold — 2026-10-18

The application names are **Frontier** and **TextureWorkspace**. The browser-only entry point is now under
`Experimental/Frontier/Native`; the incorrectly named application target/directory has been removed.
Prior binary downloads linked above are superseded and are not recommended for testing.

Before another binary is offered: verify pointer zoom/pan/orbit, wheel/keyboard input, reset/fit, pointer release,
widget/popup isolation, shared sensitivity/invert-pitch and GPU camera/normal transforms. Atlas thumbnails must
remain unaffected. Remove archive command scripts. Require valid publisher signatures, current clean Defender
results and executable hashes matching the security report. Unavailable signing/scanning means **hold distribution**,
not a fabricated safe result. Chrome reputation itself cannot be guaranteed or bypassed by this work.


### Navigation correction acceptance

- [Windows build and test run 39146539736](https://github.com/SultanAladin/Frontier/actions/runs/39146539736)
  passed for `dcfe53f`: Frontier and TextureWorkspace compiled, including Windows version resources and all ten
  Slang shader entries; all five Windows CPU/native ImGui tests and four fail-closed packaging tests passed.
- Local strict and address/undefined-sanitizer navigation tests passed with actual button, drag, wheel and keyboard
  events. Software-Vulkan readbacks verified pan and orbit alter the preview while keeping thumbnails and views stable.
- The native host delegates pointer sensitivity and pitch inversion to the shared applied input preferences.
- Updated visible evidence: `Exhibits/TextureStudio/Images/MaterialInspectionControls.png`.
- **No new executable download offered:** CI withheld packaging/upload because distribution trust requirements
  were not satisfied. Chrome warning resolution is not claimed. Next: obtain exact Chrome warning, configure
  legitimate trusted CI signing, obtain a current clean antivirus result, then reconsider distribution. Never request
  signing private keys in chat or recommend disabling protection. Interactive Windows/GPU startup remains unverified.


### Reference distribution investigation

- Inspected `SultanAladin/Frontier-`, not this repository, at historical revisions
  `7dd674484d7387cd360cc892b5a113414b37f78f` and `61c5be6de9df9c3a8a0cd28bece730d8693a3bd4`.
  The original UI import revision predates these workflows. No new Actions run was started.
- The desktop packager also used Python ZIP compression and included a diagnostic `.cmd`; its inspected
  workflow/toolchain/packager contains no application signing step. Neither scripts nor missing signing
  has been established as the cause of Chrome blocking this delivery.
- Standalone binaries used MSVC Release/static CRT on Windows 2022 and GitHub artifact/prerelease delivery.
- Networking used Epic's official configured bootstrapper, checked against the vendor binary, and direct
  GitHub releases with checksums and extracted GUI tests. This does not sign its custom application and
  is not an appropriate substitute launcher for Frontier/TextureWorkspace. Certificates were not inspected.
- User screenshot identifies arena.ai as the blocked download origin. Direct GitHub delivery is a meaningful
  provenance difference, not evidence of the cause or a reason to rehost a blocked archive unchanged.
- Evidence: `Exhibits/TextureStudio/Results/ReferenceDistributionComparison.json`. Next: identify the
  user's exact previously accepted reference artifact, compare actual archive contents/PE signatures and
  antivirus results against the blocked archive, then use traceable GitHub distribution for a verified build.
  Existing release hold remains. No runtime changes made for this investigation.


### Direct GitHub distribution correction

- Google documents separate dangerous, suspicious, unverified and insecure download categories:
  <https://support.google.com/chrome/answer/6261569?hl=en>. Enterprise documentation distinguishes
  file, URL and host verdicts: <https://support.google.com/chrome/a/answer/7579271?hl=en>.
  The screenshot alone does not expose the exact Safe Browsing verdict or detection reason.
- Added an opt-in GitHub prerelease job following the reference's direct-download provenance. It needs
  successful Windows trust verification, validates the original archive SHA-256 again, and publishes
  the original ZIP/checksum without Arena rehosting. Normal pushes do not create releases.
- Existing signature/antivirus hold remains; no self-signing, vendor launcher substitution, renamed
  blocked archive or protection bypass. A direct GitHub URL is not evidence that Chrome will accept it.
- Added per-application signature statuses and Defender readiness to check annotations and the Actions
  summary. Full scanner details remain in DistributionChecks.json.
- Local checks: workflow YAML parses; publication is opt-in, trust-dependent and least-privilege;
  checksum mismatch refusal is configured; four packaging refusal cases pass. Windows execution of
  the new summary/publication steps still requires CI; publication remains untested until trust is ready.

- Windows run <https://github.com/SultanAladin/Frontier/actions/runs/39148623800> passed at `9671944`.
  Its verification annotation reports `Frontier=NotSigned; TextureWorkspace=NotSigned; Defender=passed; ready=False`.
  This is a clean scan of the rebuilt output, not the original blocked ZIP. Packaging/upload remain skipped
  under the newly added signed-only policy; optional release publication was not invoked. Evidence:
  `Exhibits/TextureStudio/Results/DownloadTrustChecks.json`. Chrome verdict is still unverified.


### Authorized unsigned GitHub preview

- User explicitly selected **Scanned unsigned preview** after comparing it with the signed-only hold.
  This supersedes the earlier unconditional signed-only distribution rule for explicitly authorized previews.
- Both the CI report and packager must explicitly authorize unsigned output; default remains signed-only.
  Only `Valid` or explicitly authorized `NotSigned` statuses are accepted; invalid/untrusted signatures
  remain blocked. A fresh current clean Defender scan and executable hash match remain mandatory.
- Unsigned output is disclosed in the GitHub release title, `UNSIGNED-PREVIEW.txt`, signature report and
  package manifest. No browser block override is recommended. Chrome acceptance remains unverified.
- Publication can be explicitly requested by workflow inputs or the approved push marker
  `[publish-windows-unsigned-preview]`; ordinary pushes do not create releases. Only this session branch
  is used. No master push/merge or Linux package is involved.

- First authorized run `39149413532`: Windows build, five acceptance tests, 72 signature/scan policy
  combinations and Defender scan passed; unsigned readiness was true. Packaging refused a PowerShell
  single-string splat that expanded the option into characters. No binary was published. Replaced it
  with an explicitly typed string array and added an actual Python argument-boundary assertion in CI.

- Final run <https://github.com/SultanAladin/Frontier/actions/runs/39149823906> passed at `e412b34`:
  both applications, ten shader entries, five acceptance tests, four packaging refusals, 72 signature/scan
  policy combinations, fresh Defender scan, packaging, archive checksum verification and GitHub publication.
- Published the explicitly authorized **UNSIGNED** Windows preview directly on GitHub:
  <https://github.com/SultanAladin/Frontier/releases/tag/frontier-windows-39149823906>.
  ZIP and SHA-256 asset are available. No Arena-rehosted binary is offered. No signing is claimed.
- Remaining: user verifies Chrome accepts the direct GitHub download without overriding protection;
  only then perform the documented interactive Windows/GPU/input checklist. If blocked, stop distribution
  handoff and investigate/report the precise verdict instead of changing names/hosts repeatedly.
- Defender command uses a custom scan with `-DisableRemediation`; Microsoft documents that this ignores
  file exclusions and scans archives, while leaving detections unremediated for the gate to reject:
  <https://learn.microsoft.com/en-us/defender-endpoint/command-line-arguments-microsoft-defender-antivirus>.

#### Native Float32 vector surface map recovery (2026-10-19)

Scope remains approximately 68%. A dedicated compute shader now creates typed RGBA32F colour/height/normal/roughness/
metalness/opacity/occlusion maps and per-channel alpha masks from ordered vector layers. The Vulkan format is parsed
from SPIR-V and unsupported Float32 storage/filter/readback is refused without an RGBA8 fallback. Layer-local channel
contributions preserve a separate gold-thread metalness response over fabric.

The shared PFM encoder writes unmodified little-endian Float32 samples bottom-first. Seven Vulkan readback checks pass
strict and ASan/UBSan using SwiftShader. The deterministic sample ZIP contains 14 maps/masks from the 32x32 validation
fixture; it is not a user document export. Actual GPU captures and existing native GIFs are listed in
`Exhibits/TextureStudio/NativeSurfaceEvidence.html`. The GIF itself displays 8-bit pixels and does not prove precision.

The native Texture Workspace host now switches to the Pattern panel, persists a separate editable vector TOML source,
and records its GPU coverage and selection pipelines. CMake builds `VectorSurfaceValidation`, `VectorCoverageValidation`,
`PatternWorkspaceValidation` and `FrontierTextureStudioUI`; all three relevant CTests pass. The Pattern runner reports
606 checks (340 coverage dispatches, 63 selection dispatches) and records the ImGui/Vulkan evidence linked in
`Exhibits/TextureStudio/NativeSurfaceEvidence.html`. The runner reports `windows_executed: false`; a complete native
Windows application build/runtime and GPU capture remain unverified.

Remaining: connect Pattern-authored Float32 channels to the existing PBR material renderer and validate integrated GPU
submission/sampling; keep 3D application disabled until then. Height/tangent calibration, 512x512 performance, and
stitch/weave presets remain open. Full Pattern parity is open; Stamp is deferred.

## Crash, catalogue and Pattern correction — 2026-10-22

User-reported failures on the Windows build (severe-failure dialog while editing sliders, duplicated
preview specimens, broken orbit feel, perpetually re-rendering library, locked procedural sliders,
flat botanical/leather materials, slow scratches, blurry wrong-scale pattern thumbnails) were audited
end-to-end against the HTML Alloy source and fixed:

- **Crash (`VK_ERROR_OUT_OF_POOL_MEMORY`)**: unbounded descriptor churn from per-edit preview texture
  re-creation. Preview residence is now ping-pong double-buffered, descriptor reclamation is lagged
  behind retirement, the ImGui descriptor pool is sized for the workspace, and GPU faults are isolated
  per panel instead of aborting the host. The material atlas re-renders only when a material is saved
  (source-digest gated), never continuously.
- **Viewport**: all five preview specimens were drawn at once; draws are now per-shape index ranges.
  Orbit/pivot maths already matched OrbitControls and the measured specimen bounds — unchanged.
- **Scratches**: the CPU bake re-evaluated every instance per pixel. Instances are memoized by packed
  key with exact early-outs; output is bit-identical (validation-pinned) at ~7.5x speed.
- **Catalogue audit** (`Exhibits/TextureStudio/Code/AuditMaterialCatalogue.py`, results in
  `Results/MaterialCatalogueAudit.json`): before, 86/131 materials rendered flat and 355/653 recipe
  controls resolved. Every remaining Alloy surface family is now a native procedural op (leaf, grass,
  petal, stem, cactus, leather, crocodile, wax, skin, paper, rust, ceramic, rubber, tiles, granite,
  marble, concrete, plus metal/polymer grain and the fruit pore-roughness term); 123/131 materials are
  textured (the rest are GPU paint and glass, which have no HTML surface kernel) and 613/653 controls
  resolve and write through. LED emission strength and corrugated rib frequency resolve. Committed
  512px bakes are pinned byte-exact for all 265 maps.
- **Pattern studio**: gallery thumbnails are SVG documents rasterized by ThorVG (the home-grown
  scanline rasterizer is deleted). The SVG encoder uses the coverage shader's half-extent geometry —
  the old tessellation drew every primitive at 2x. Presets are one motif expanded by the array
  generator with per-copy ink, not hand-stamped duplicates. The canvas coverage ladder gains a 1024
  step for light documents; the material bridge keeps 512.

Honestly deferred (locked controls remain visibly locked, audit-counted at 40/653): thin-film
iridescence terms on three paints, glass transmission depth, sand/layered-sand recipe binding,
woodland-camouflage weave underlay and ballistic-polymer grain under their coded-field albedos.
Windows GPU execution of these fixes is still unverified in this environment (no Vulkan ICD);
headless Release and Debug+ASan suites pass 26/26.

## Orbit staging and LED radiance correction — 2026-10-22 (Windows test round)

User testing of the published build (5ef3c89) confirmed the catalogue but flagged two defects:

- **Orbit felt like the specimen tumbling.** The earlier note verified the camera only; the staging
  was at fault. `ConstructMaterialPreviewSpecimen` baked a 0.18 rad group pitch into ball+pedestal,
  so the pedestal plane was tilted in world space and rolled diagonally under yaw (rim heights
  spread ~0.34). In the HTML scene the stand/foot/ring are unrotated siblings of the posed ball.
  The group pitch is removed (ball keeps its intrinsic HTML pose), Pivot/Radius re-measured
  ({0,-.002,0} / 1.085), and validation now pins pedestal ring level plus rigid light carry.
  The preview rig was also view-locked (constant view-space key light, sky up = view up, softbox
  directions in view space), gluing highlights to the screen during orbit. The key light and sky
  up are now carried through the world frame per inspection update and the softbox panels are
  world-frame directions rotated into view in the shader; default-pose framing is bit-identical.
- **LED emission blown out at the wrong scale.** `MaterialSurface.slang` sampled the radiance
  texture with raw coordinates while every other channel applies the stratum UV scale/offset
  (9×4.5 on the LED strata), stretching the emitter mask across the specimen. The mask bake and
  authored strength were already correct (single gain via RadianceNormal); the sample now uses
  the first stratum's UV transform, matching the packing contract of one radiance slice.

## Environment archives and per-layer inspector — 2026-10-22 (Windows test round)

User verdict on build `39817580609`: orbit and LED fixed; the analytic environment presets read
as tinted and cheap, and the inspector mixed every stratum's properties into shared cards.

- **Baked studio environments replace the analytic rig.** `Exhibits/TextureStudio/Code/EnvironmentBake.cpp`
  evaluates five colour-neutral area-light rigs (gnomonic softbox panels over vertical gradients,
  2x2 supersampled) into 1024x512 equirect PNGs under `EngineContent/EnvironmentArchives/`
  (StudioSoftbox, StudioStrip, WindowLight, Overcast, DarkStudio). `MaterialLibraryRendering::
  ConfigureEnvironments` scans that folder (studio first, then filename order, cap 8 layers), decodes
  each archive to a shared 512x256 scene-linear field, derives strength (hemispheric-mean
  normalization to the tuned reference level) and a key light (brightest-region centroid, so
  imported maps light from their own panels), and uploads one `Texture2DArray` layer per entry.
  The shader samples it at set 0 binding 3 in the fixed world frame (`SampleEnvironmentMapRadiance`
  behind `FRONTIER_ENVIRONMENT_MAP`; the lightweight preview program keeps the analytic gradient),
  replacing `IntegrateStudioSoftboxes` and the Horizon/Zenith gradient in the authored-material
  path. Thumbnails use layer 0. Preview layer indices clamp to both the catalogue and the layers
  a residence actually uploaded, so a rescan can never address a missing layer; rescans clear
  tile source digests so resident tiles rebake under the new rig.
- **Viewport environment card.** The tinted preset dropdown and the standalone exposure card are
  retired. One card (dropdown surface) shows a sphere preview per archive — CPU-integrated from
  the same linear pixels the shader samples, Blender-style — with Exposure and a new Light
  intensity (scales map strength and key light together) at the bottom, plus an
  "Import environment..." rescan: drop any equirect PNG into `EnvironmentArchives/` and it joins
  the catalogue with a derived preview/key light. `MaterialPreviewConfiguration::Environment` is
  now a catalogue index (0..63 validated, host-clamped).
- **Inspector categorized per layer.** Every recipe control is projected onto the stratum its
  coefficient edits (shared procedural pool first, then the automotive-paint block, then stratum
  scalars via the `coat*` rule). Cards render under LAYER 1..N headers (Base / Interlayer /
  Coat), preserving each material's recipe groups as the cards within a layer — metallic paint
  now shows flakes and base finish on layer 1, coat gloss/roughness/IOR on layer 2. Colour
  swatches follow the stratum owning their key. Stratum scalars no recipe control covers render
  on a per-layer "Layer properties" card (Roughness, Metallic, IOR, Coverage, dielectric
  Thickness — newly a structured coefficient), and profile-less materials get the generic scalar
  set on every layer instead of only stratum 0.

Verification: shaders compile through the pinned Slang WASM toolchain; baker radiometry printed
per rig (effective ambient/peak after runtime normalization); `MaterialPreviewConfigurationValidation`
(645,546 checks) drives the new card by pointer (open, exposure drag, tile pick, dismiss);
`MaterialLibraryValidation` scrolls to the coat-layer gloss slider and asserts live apply; full
host suite 26/26. Windows GPU confirmation (map sampling on device, sphere-preview staging)
remains with the user's hardware test of the published build.

Still honestly deferred (unchanged from the audit's 40/653 locked controls): sand/layered-sand
recipe binding (needs a `sand` procedural op over the existing `BakeSandMaps` field so the bed
sliders gain a pool to edit), thin-film iridescence, glass transmission depth, and the
camouflage/ballistic coded-field underlays — each is a renderer/op feature, not an inspector
arrangement, and stays visibly locked with its tooltip rather than fake-enabled.

### Correction — environment switching froze the preview (Windows test of 39829213006)

User log `TextureWorkspace-1792696201963752.log` pinned it: `[GpuFaultIsolated] PreviewConfigure:
Invalid material parameters or dormant shader contract` on the first non-default environment,
then the same on material switches — viewport frozen on the last good frame while the UI stayed
at 120 FPS. Cause: `ValidateMaterialScattering` still required `PreviewConfiguration[2] == 0`
(the old dormant slot) while the environment change made that slot carry the archive layer.
The contract now bounds the layer to catalogue capacity (63); `MaterialPreviewConfigurationValidation`
pins the acceptance window so CPU CI catches this class without a GPU. Also fixed in the same
pass: `LightIntensity` missed the discrete copy in `SynchronizePreviewCamera` (only reached the
renderer on the orbit-settle snap); environment sampling moves to mip 2 (the 8-sample quadrature
turned bright panels into per-pixel fireflies at base level — the visible stipple); the strength
normalization target drops 0.3 -> 0.2 and the key weight 0.4 -> 0.32 (big-panel archives were
flooding metallic paints desaturated toward white); the catalogue caps at 8 entries to honour
the shared texture-array pyramid contract. Environment switching itself never rebaked anything:
the log shows 28 tile bakes across the whole 168 s session (per-session catalogue warm-up); an
environment change remains one uniform update — preview textures rebake on material edits and
tiles on save, exactly as before.
