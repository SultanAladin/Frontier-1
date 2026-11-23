# Space Formats — Dependency and Editable-Content Specification Plan

**Status: scoped editable projectspace persistence implemented; other format work remains specification-only.**
Encoding B remains approved; unrelated/binary format implementation still requires plan approval.
No wholesale format migration, commit, push, or branch change. The separately requested Control Centre UI fix is
authorised.
Read alongside the [preserved original brief](../References/SpaceFormatsOriginalBrief.txt).
Baseline: `SultanAladin/Frontier-` at `338293863d7df4e585dec0c4acae45fe9c524ed5`, not this checkout's repository.
Source pins, line-linked findings, and review checks: `Exhibits/SpaceFormats/Results/SourceAudit.json`.
The original brief and every inspected baseline file have retained SHA-256 and Git blob identities.

## 1. Review outcome and corrections

| Classification       | Finding / correction                                                   |
|----------------------|------------------------------------------------------------------------|
| Confirmed            | FSPC v1 has 13 types, a 20-byte header, 112-byte INST and 24-byte MSLT.   |
| Confirmed            | Existing codecs/export/pack/explode are reusable starting points.       |
| Confirmed            | External reference loading does not verify its recorded content hash.  |
| Confirmed            | Pack embeds direct REFS; it does not recursively package their closure. |
| Confirmed            | Parser collects checksum failures; Open success alone is not validity. |
| Confirmed            | CAD already saves/opens a `.arc` construction journal. Preserve it.     |
| Confirmed            | CAD recipes and snapshot undo already exist; do not replace blindly.   |
| Suspected            | Shell-quoted extraction paths may permit command injection. Not tested.|
| Proposed requirement | SHA-256 pins, typed DAGs, durable topology references and bounded load. |
| Proposed requirement | `.fontspace`: typed font document, not another name for an OTF file.    |

The `.arc` discovery is a material correction to an incomplete format-only audit: there is an existing editable
CAD save path outside SpaceExport. ConsoleHost opens it by replaying in a candidate document before replacing the
live document. That protects live edit state from refusal; it is **not** a demonstrated security sandbox.
The future importer must restrict document operations and resources, never execute general embedded scripts on open.
Source references and narrower descriptions for every confirmed/suspected observation are in the audit report.
Historical gate successes and UI captures do not validate this proposed format revision.

## 2. Terminology and reusable expansion control

**Selected name: `PanelExpansionTab`.** A compact edge tab changes the visible extent of a panel; it is not
bracket-shaped. Reuse the same geometry/input implementation for the Control Centre and Content Browser.
Motion calculations can be named `PanelExtentMotion`; the hosting panel supplies content and styling.
Specify expansion direction, collapsed/expanded limits, viewport bounds, capture/release, and scaled hit geometry.
A Content Browser may expand upward; the Control Centre expands downward. Do not hard-code either into the control.
Keep lifecycle ownership shared with other interface/scene components, rather than inventing a second lifecycle.
The production control extraction/rename and read-only Content Browser were separately approved and implemented.
Top and bottom drawers share the geometry, drawing, capture/release and motion implementation.
This does not approve format codec implementation. Historical reference paths and capture filenames remain truthful.

## 3. Approved B — editable authoring, compiled binary runtime

**User decision, 2026-10-17: B approved.** This is an explicit binary runtime-packaging exception to the
TOML policy, not permission to replace editable Editor/Authoring source files with cooked runtime output.

| Layer             | Authoritative representation                                      | Consumer                    |
|-------------------|-------------------------------------------------------------------|-----------------------------|
| Editor/Authoring  | Editable TOML/toml++ descriptors and original editable payloads.   | Editors and build tools.    |
| Runtime package   | Compiled binary envelope with versioned typed runtime records.    | Runtime loaders.            |
| Embedded content  | Textures, SVGs, images, fonts and other supported payload files.   | Typed runtime decoders.     |
| Derived caches    | Disposable outputs keyed by source revision and compiler options. | Rebuildable accelerators.   |

The runtime representation is binary, not a requirement to parse authoring TOML during execution. Compilation may
retain descriptor/source bytes for inspection or round-trip tooling, but runtime records have an explicit binary schema.
Embedding SVG/image/font files does not make them editable native documents; retain the originals in authoring storage.
A runtime package must not become the sole surviving CAD/UV/paint/font authoring authority.

- Authoring save preserves editable journals, operations, constraints, layers, dependencies and source payload files.
- Build/cook resolves exact dependency revisions, validates closure/licences, and produces deterministic runtime bytes.
- Persist the source-to-runtime mapping, compiler/schema versions and source digests; never confuse identity and digest.
- Standalone, external-reference and embedded packaging must resolve equivalent typed content under the same pins.
- Missing editable sources are an explicit recovery error; do not promise lossless decompilation of compiled output.
- Existing `.arc` documents remain supported authoring inputs until their reviewed importer/migration is implemented.

The original brief remains immutable. B approves the layer split and binary packaging policy only; no new byte layout,
reserved-field reuse, codec implementation or wholesale migration is approved by this decision.

### Projectspace and workspace preferences — implemented persistence slice

The user corrected and approved a single editable/saveable `.projectspace` container, not separate Appearance,
Input, Notifications or Engine TOML files. This supersedes the earlier Project.toml-to-derived-projectspace proposal
for project configuration. The original brief and binary runtime-package policy remain intact; the new editor
container is not claimed to be an FSPC runtime file or a completed binary codec migration.

| Storage                         | Authority and implementation                                        |
|---------------------------------|---------------------------------------------------------------------|
| Editable `<Name>.projectspace`   | UTF-8 TOML internally; explicit editor discriminator and version 1.   |
| workspace.appearance            | WorkspaceAppearanceProfile: theme, typography, colours, family name. |
| workspace.input                 | Stable command/key tokens and pointer preferences.                   |
| workspace.notifications         | Persisted overlay/alert preferences and hold duration.               |
| workspace.hosting               | Personal browser hosting / detached-window choices; default false.  |
| project / engine                | Identity/name/template revision and engine configuration.            |
| roots / levels / catalogue/pins | Metadata retained; no scene loader or dependency resolver claimed.   |
| Personal.projectspace           | Optional explicitly saved personal override sections.               |
| Binary FSPC/runtime packages    | Separate future runtime contract; not implemented by this slice.     |

```text
Engine/ContentInterchange/ProjectSpaceCodec.{h,cpp}
EngineContent/
├── Defaults/BlankProject/Blank.projectspace
├── FontArchives/                 # Font payloads/licences remain here
└── Icons/                        # Existing shared SVG artwork

<ProjectFolder>/
├── <ProjectName>.projectspace     # Editable project and all project settings in ONE file
└── Content/                      # Default project content root
```

Implemented:

- Creation validates and copies a snapshot of the template, issues a fresh project identity and refuses overwrites.
- Versioned, size-bounded container parsing; binary FSPC/newer versions and unsupported required features refused.
- Content-root and entry locators are relative; reject absolute and `..` paths, duplicate entry identities, malformed
  exact-pin metadata and unresolved startup-level references. Payload digest verification remains future work.
- Shared AppearanceCodec/InputPreferencesCodec table operations prevent independent embedded preference schemas.
- Optional personal overrides remain separate; explicit Personal/Project controls choose where a save goes.
- Scope-specific saves reread the destination and preserve unrelated catalogue, engine and optional extension sections.
- Save applied settings only after successful replacement; failed saves retain the previous file and applied settings.
- Notification preferences persist for projectspace saves; transient notification contents are not serialized.
- Missing profile sections inherit engine defaults. Missing fonts use fallback without erasing the requested family.
- A built-in emergency face permits a minimal UI when packaged fonts are unavailable; not full Unicode coverage.
- Installed engine defaults remain read-only to preference saves. Engine upgrades do not silently rewrite projects.
- Dirty drafts block reload/override switching. There is no automatic project-switching UI in this slice.
- Windows arguments: --create-project <file.projectspace> and --project <file.projectspace>.

Implementation evidence and limitations: `Exhibits/ProjectSpace/README.md`, four actual native captures,
and the production-code tests in its Code folder. No standalone project-setting TOML files are created.
Existing legacy TOML APIs remain compatible, but the new Windows host saves Personal.projectspace instead.
No automatic migration/deletion of old personal preference files is claimed.

Creation requires filesystem hard-link support to publish without replacement and can leave empty created directories
on failure. There is no multi-writer lock or hostile-filesystem/power-loss guarantee. TOML comments, original text
formatting and unknown keys within normalized shortcut-array rows are not retained.

The later authorised notification slice implements top-right semantic cards and Quick Controls Notification Stream.
Shared `NotificationSequence` retains 64 history entries, 16 queued alerts and up to three visible cards. Expiry,
hover pause, authenticated dismissal, suppression, native history clearing and real preference save/error events
are checked by `Exhibits/Notifications/Code/NotificationValidation.cpp`. The supplied aa658ed8 card geometry is reused
in light/dark themes; native evidence and limitations are in `Exhibits/Notifications/README.md`.
The responsive stream sits beside quick tiles on wide screens and below on narrow screens, not in Telemetry &
Notifications. Shared trapezoidal tabs are 200 × 18 logical pixels, half their former dimensions. Invert Y-Axis uses
one unboxed shared switch. Shared `ConfirmationDialogue` implements the supplied rounded and semantic-header variants;
the rounded Apply/Cancel popup is wired to preference saves. Cancel/Escape retain drafts and saved bytes; modal input
cannot operate the controls beneath it. The semantic-header variant has a labelled native fixture, not an invented
operation. Genuine notification action variants and independent severe-error presentation remain pending.
No render/delete operation is invented.

Presentation references are pinned separately in `Exhibits/SpaceFormats/Results/ConfirmationReferences.json`.
They come from this repository's SourceImages, not the older Frontier- format baseline.

| Uploaded reference prefix | Presentation contract                                                    |
|---------------------------|--------------------------------------------------------------------------|
| b79e438e                  | Full red fatal-error surface; readable headline, details and safe actions.|
| aa658ed8                  | Light/dark notification tile with severity icon, title, text and dismiss. |
| 04223734                  | Compact strip with optional genuine action and dismiss.                  |
| 69ca33c1                  | Notification tile with separate right-side actions.                      |
| 71a8613a                  | Rounded modal confirmation with cancel/primary actions and severity icon.|
| 763e0508                  | Semantic-colour header/details/footer dialogue variant.                  |

- Reuse current fonts, semantic theme colours, shared controls and inherited SVG artwork; no replacement visual design.
- Information, success, warning, error and confirmation share reusable presentations with appropriate icon/colour.
- Notifications appear top-right outside PanelExpansionTab and are retained in Quick Controls' Notification
  Stream.
- Modal confirmations capture pointer/keyboard focus; cancel never performs the proposed destructive operation.
- Actions such as Undo, Retry, Render or Delete appear only when a real operation supplies that action.
- There is no render/delete implementation in this host; presentation tests must not pretend those operations exist.
- Bound notification history/queue, support dismissal/expiry, and keep fatal errors outside suppressible toast
  preferences.
- The red screenshot is a visual reference, not browser-security text to copy into an engine error screen.

General severe/fatal-error boundary (not Vulkan-specific):

- Cover severe rendering, platform, resource, I/O and other detected failures, not only Vulkan/device loss.
- Classify recoverable errors separately; do not turn every warning or failed preference write into a fatal screen.
- The red presentation occupies the application client area; it is not an exclusive-display takeover or an OS lock.
- The current UI host is GLFW/DX11, not the Vulkan engine. Do not claim Vulkan resize/device-loss handling is
  integrated.
- A normal modal can use the active UI renderer. Device loss requires an independent native/software presentation path;
  drawing another ImGui frame through the failed device is not a fallback.
- Preserve the actual error code/context and a local diagnostic file; show plain-language text instead of only a raw API
  error.
- An in-process error screen cannot prevent/recover an OS crash, power loss, hard driver hang, or process termination.
  Do not promise that this presentation makes the PC crash-proof or guarantees display after a severe failure.
- Avoid re-entering the failed operation or relying on a fresh large allocation. Keep a minimal independent fallback;
  rich SVG/theme rendering is optional in this emergency path, not a requirement for reporting the failure.
- Do not autosave possibly corrupted document memory. Preserve last-known-good files and available diagnostics;
  any recovery save requires a separate validated recovery contract.
- Never promise Retry/Continue until recovery and document safety for the reported failure are implemented and tested.
- Windows-native fallback is platform-specific; keep its interface reusable and other-platform support explicitly
  unimplemented.

Acceptance for the approved slice: standalone/embedded profile equivalence; personal/project precedence and scope;
reload; corrupt/unknown version refusal; Unicode and moved project paths; unavailable fonts; failed-write preservation;
unchanged catalogue on preference saves; notifications/dialogues with real input and native captures;
fault-injected renderer-unavailable fallback without issuing commands on the failed device.
Windows runtime/GPU/device-loss tests remain required and cannot be replaced by Linux CPU captures or a cross-build.

## 4. Separate contracts

| Model                | Stored responsibility                                    | Cycle policy                  |
|----------------------|----------------------------------------------------------|-------------------------------|
| Content dependencies | Exact required document revisions and typed outputs.     | Reject evaluation cycles.     |
| Construction DAG     | Typed operations, parameters, connections and outputs.   | Reject evaluation cycles.     |
| Revision ancestry    | Immutable versions, branching and explicit merge bases.  | Reject ancestry cycles.       |
| Placement directory  | Placement/nesting and geometry/UV/material assignments.  | Validate nesting separately.  |
| Storage containment  | Physical standalone/embedded/archive payload locations.  | Bound depth and total bytes.  |
| Derived cache        | Disposable geometry, atlases, bakes and acceleration.    | Never authoring authority.    |

Undo/redo navigates existing changes/snapshots; it is not automatically a construction or revision DAG.
Classify dependency edges as required-evaluation, optional-enhancement, or non-evaluative annotation.
A named output connects only to a compatible input; connections carry schema versions and interpretation rules.
Derive reverse usage indexes instead of persisting reverse evaluation edges.
Cycle errors include document identity, revision, selected output and the complete offending edge chain.
Evaluate dependencies in a deterministic dependency-first order; never assume stored row order is evaluation order.

## 5. File contracts — retain 13; propose one justified addition

| Extension       | Contract                                                                 |
|-----------------|--------------------------------------------------------------------------|
| `.projectspace` | Levels, catalogue, content roots and exact dependency pins.               |
| `.solution`     | Project collection and engine/toolchain requirements.                     |
| `.geometry`     | Editable CAD/polygon definitions, constraints, units and topology refs.   |
| `.uvspace`      | Seams, islands, packing and operations tied to geometry output/revision.  |
| `.material`     | Parameters, typed inputs, shaders and pigment/texture dependencies.       |
| `.pigment`      | Ordered layers, channels, masks, strokes, brushes and raster payload refs.|
| `.instance`     | Placement and geometry/material/UV assignments, not duplicate geometry.  |
| `.environment`  | Terrain/world authoring inputs and derived outputs.                       |
| `.script`       | Source, language/version, capabilities and declared dependencies.        |
| `.workflow`     | Typed execution DAG, script pins, inputs and outputs; opt-in execution.   |
| `.archive`      | Immutable dependency closure, deduplicated payloads and streaming index. |
| `.runtime`      | Resolved launch configuration and exact content revisions.                |
| `.state`        | Separate versioned session/recovery/save/capture domains; defer payloads.|
| `.fontspace`    | Proposed typed font document; details below.                              |

The baseline still has **13**, not 14. Do not assign a new type tag or extension reader until approval.
Retain `.arc` as an existing legacy CAD input; do not add it to the FSPC type count or remove its reader.
No additional terrain/brush/theme extensions are proposed without a separate semantic justification.

### Fontspace contract — proposed requirement

- Stable font-document identity/revision; metadata for face/style, weight, width, slant and named variable axes.
- Source OTF/TTF payload references or embeddings, media type, SHA-256, face/collection index and decoder version.
- Coverage, named instances, feature defaults and ordered, pinned fallback references; reject fallback cycles.
- Retain original licence text, attribution, embedding/redistribution permissions and subset provenance.
  Unsupported or uncertain licensing blocks redistribution rather than silently embedding bytes.
- Deterministic face selection and diagnostics for missing/unsupported faces; no filename-only classification.
- Subsets are distinct derived outputs with a source revision and coverage digest; never overwrite the original face.
- Raster/SDF atlases remain disposable caches keyed by source/axes/size/rasterizer settings, not durable authority.
- Adapt existing `FontCodec`, `TypefaceRegistry` and TOML catalogue conventions before creating another font path.
  Existing four-family static-font tests do not demonstrate variable axes, shaping, collections or fallback semantics.
- Acceptance: standalone/embedded selection equivalence, Unicode launch paths, missing/corrupt faces, fallback cycle,
  variable-instance metadata round-trip, licence retention and denied-embedding diagnostics.

## 6. Identity, revisions and canonical serialization

Keep logical content identity, immutable revision identity, content digest, output identity and locator separate.
Define document-scoped stable tokens and qualified cross-document references; paths are relocatable hints only.
Each dependency pins identity, revision, expected type/schema, selected output, digest and allowed locator candidates.
Resolve any “follow latest” policy before save/pack; write the resolved pins into the reproducible package.
Identical payload bytes may share storage without merging distinct mutable identities or copy-on-write branches.
A material copy-on-write edit creates an explicit intended identity/revision fork; untouched assignments stay pinned.

Specify canonical authoring UTF-8 TOML emission independent of parser insertion order: key/table ordering, ordered
arrays,
float round-tripping, finite-number rules, negative zero, timestamps, newline policy and unknown metadata handling.
Specify canonical compiled binary records separately. Do not claim arbitrary handwritten TOML text is canonical.
Separate stored-byte digests from semantic digests;
name the algorithm/domain/schema for each. Do not normalize user strings or source text without a defined rule.
Use SHA-256 for persistent integrity/content addressing; verify external and embedded bytes identically.
Retain FNV only as a legacy fast checksum, not identity, authenticity, or a substitute for digest verification.
Digests alone do not authenticate a publisher; trust/signing is a separate future policy.

Under approved B, specify fixed integer widths, little-endian encoding, explicit alignment/padding and offset limits;
version the envelope, document schemas and operation schemas independently. Never serialize compiler ABI/GPU memory.
Reject duplicate singleton sections, overlaps and unsupported required features before exposing typed content.
Unknown optional sections may be retained opaquely only when relocation is safe; unknown required operations produce
an honest unsupported/read-only result, not successful editing or destructive rewriting.

## 7. Worked authoring and storage examples

```text
Assembly.projectspace → Placement A ─┐
                                   ├→ Bracket.geometry @ R7
Assembly.projectspace → Placement B ─┘    Sketch + constraints → Extrude → Fillet → output SurfaceSet
Placement A → UV @ U4 → geometry R7/output SurfaceSet
Placement A → Gold.material @ M2 → Coat.pigment @ P3 → UV U4
                                      layers → masks/channels → batched stroke segments + raster tiles
```

**CAD:** adapt existing FigureRecipe and SceneDocument types, preserving supported operations, analytic curves,
surfaces, topological adjacency, sketch constraints, workplanes, units, tolerances and operation/schema versions.
Map ConsoleHost-owned dimensions, constraints and named workplanes as well as SceneDocument.
Persist authored facts, not just console interaction sequences or tessellation. Keep legacy journal import auditable.
Assign durable operation-output/topology tokens with provenance and explicit split/merge mappings.
Ambiguous face/edge rebinding reports a broken selection; do not silently choose an equal-looking array index.
Resolve the same CAD revision identically after load, and regenerate disposable tessellation from the saved inputs.
Replace the four-round regeneration assumption only after the existing consumers are mapped and DAG ordering verified.

**Paint:** ordered layer/blend semantics remain ordered, not an arbitrary unordered DAG. Batch chronological stroke
samples into bounded editable segments where appropriate. Masks/channel connections form typed dependencies;
brush revisions, raster tiles and imported images are pinned. A bake records geometry/UV/source revisions, settings,
evaluator revision and output digest. Geometry R8 does not silently validate UV U4 or paint authored for R7.
The material consumes pigment outputs; the pigment does not depend back on that same material.

**Storage:** the same Bracket identity/R7 may live at `Content/Bracket.geometry`, inside Assembly.projectspace,
or inside Release.archive. The locator changes; its semantic identity, revision and interpreted output do not.
Pack walks the complete required dependency closure, including font licences and foreign decoder requirements.
Deduplicate payloads by verified digests and byte comparison when collision ambiguity exists; preserve logical IDs.
Explode checks containment, Windows/POSIX path rules, symlinks, reserved names and overwrite policy before writing.
Stage complete results, verify them, then atomically publish; no shell command interpolation or partial success.
Pin resource budgets for bytes, decoded bytes, counts, nesting, dependency depth, work and expansion ratio.
Numeric default limits require fixture/scale review; do not pretend that unspecified limits are already enforced.

## 8. Implementation gates — all future work requires approval

| Phase | Deliverable / exit gate                                                     |
|-------|----------------------------------------------------------------------------|
| 1     | B approved; approve remaining audit, fontspace scope and CAD migration map.  |
| 2     | Shared codec/resolver + typed dependencies + bounded, deterministic loader. |
| 3a    | Two instances/one geometry; external/embedded/offline package equivalence.  |
| 3b    | CAD import/save/load + stable topology; UV/pigment/material compatibility.   |
| 3c    | Fontspace selection/fallback/licences; opt-in workflow execution.           |
| 4     | Non-destructive legacy readers/converter; reconnect CLI/export/pack/explode.|
| Later | Versioned `.state` domain payloads and recovery, after the foundation.      |

Keep the original files and supported v1/.arc readers. Migrate into a new destination, verify interpreted content,
report omissions and preserve recoverable unsupported records. Retire writers only after demonstrated migration.
This checkout differs from the baseline: propose a selective mapping into Engine/ before importing any source.
Do not bring in the scene renderer, CAD application, or CLI wholesale under this planning permission.

## 9. Acceptance and evidence gate

| Case                              | Future required result                                      |
|-----------------------------------|-------------------------------------------------------------|
| Shared geometry / copy-on-write   | One payload; correct targeted edit; other placements intact. |
| Standalone ↔ embedded ↔ archive   | Same identities, pins, outputs and interpreted semantics.    |
| Offline closure / moved paths     | Required content resolves without original source locations.|
| External or embedded cycles       | Reject with full typed dependency chain.                    |
| Geometry revision under UV/paint  | Detect stale compatibility; no silent reassignment.          |
| CAD / paint save-load             | Construction and editing semantics survive; caches regenerate.|
| Unknown required operation        | Recoverable unsupported result; no destructive rewrite.      |
| Malformed / interrupted save      | Bounded refusal/recovery; originals remain intact.           |
| Canonical export / old migration  | Defined deterministic output; preserved original and report. |
| Fontspace                         | Selection, fallback, licence and payload contracts above.    |

All proposed-format acceptance cases are **not run**: no proposed codec exists yet.
The separately authorised UI correction restores Quick controls → Settings → category list → Appearance, then
Display / Typography / Theme. It reuses pinned Control Centre geometry, SVGs, Input and Notification layouts;
GI/Render/Materials categories are excluded. The separately authorised Input port now provides working UI command
shortcuts, chord capture/conflict checks, persistent editable TOML and a pointer-response preview. Scene camera/tools
remain outside this host; no viewport integration is claimed. Notification preferences persist in projectspace;
notification contents/history remain session-only.

The review embeds newly captured production ImGui CPU output from the uncommitted UI correction at `e016a0b`.
Native build, pointer/navigation, sanitizer and Windows cross-build evidence is recorded separately under
`Exhibits/ControlCentre/Results/NavigationChecks.json`. These checks do not prove Windows runtime/GPU behaviour
or any proposed-format acceptance case. The HTML remains an evidence viewer, not a browser editor implementation.
Keep validation code and results under Exhibits, prototypes under Experimental, production under Engine,
and a single tracker entry linking this plan. Do not create a competing progress tracker.

## Shared panel hosting continuation

The separately authorised first hosting slice embeds the personal detached-window permission in projectspace.
It uses the existing native ImGui docking patches and original browser/notification content.
Frontier now embeds versioned TOML `workspace.layout` in the personal container and rebuilds native docking.
It preserves tabs/order/selection, splits, open panels, floating geometry and draggable browser visibility.
Invalid layouts default safely and require explicit Reset before replacement; unrelated container sections survive.
Settled autosave and final shutdown flush are covered by fresh-context restart and failure-path tests.
No standalone ImGui INI is used. Optional native monitor metadata now supports work-area/DPI reconciliation;
physical Windows multi-monitor/GPU execution remains unverified. The default UI host now uses Vulkan.
The next format milestone must resolve remaining authoring/runtime contracts and identify the next approved codec
before implementation; this continuation does not authorise a wholesale migration. Project Browser follows it.
The approved sequence is tracked in `EngineUpdate.md`; evidence and limits are in `Exhibits/Docking/README.md`.
