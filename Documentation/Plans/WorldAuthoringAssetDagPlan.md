# World authoring, native asset formats and dependency DAG plan

> **Status: proposed.** This document starts the design work; it does not claim that construction, removal, persistence, or the new formats have been implemented. It extends [ExperimentalEditorUiPortPlan.md](ExperimentalEditorUiPortPlan.md), [DocumentFormatsInstancePlan.md](DocumentFormatsInstancePlan.md), [DocumentSpacesRevisionPlan.md](DocumentSpacesRevisionPlan.md), and [DataDrivenSpaceContentFormattingPlan.md](DataDrivenSpaceContentFormattingPlan.md). Existing rules remain in force except where the explicitly approved names below supersede older naming proposals.

## Decisions

| Topic | Design |
| --- | --- |
| First editor slice | Construct Sun, Clouds, Moon, Stars and Atmosphere as real world entities, grouped by default in one `Environment` folder. Support safe removal and the HTML-matching outliner context menu. |
| World authority | A versioned `.worldspace` document and its referenced native assets are authoritative. The outliner is a view of the world, not a second independent scene. Startup must load the chosen world, not silently manufacture a hardcoded showcase. |
| Names | The user's approved names below take precedence. Keep older domain formats that are not replaced; do not reinterpret old binary formats merely by changing their file suffix. |
| Container | Keep the existing hybrid UTF-8 TOML manifest plus SFNT/TTF-**style** 64-byte aligned section directory and binary sections. This is a container layout inspired by TTF, not a claim that each asset is a valid font file. `.fontspace` contains actual font data and metrics; TTF/OTF remain external interchange formats. |
| Reuse | Share editor controls and typed world commands; persist reusable authoring assets and verified derived outputs. A dependency DAG computes exactly what is dirty, not an unconditional scene-wide rebuild. |
| Scope | Editor chrome and operations are editor-only; world assets, codecs and production runtime systems may be needed in production. No HTML or editor UI dependency in Shipping. |

## 1. Existing code: what is known and what is not

- `Engine/Lifecycle/EditorLifecycle.cpp::PopulateWorldSceneSession` selects hardcoded builder entry points. `BuildVehicleClearSkyDefaultScene` in `Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp` **first attempts** `Content/Worlds/VehicleClearSkyDefault.WorldSpace` through `LoadWorldSceneFromSpaceFile`, but can generate default content and silently fall back to hand-built scene content. Blank and city branches use other builder paths. Subsequent viewport display settings overwrite portions of the loaded atmosphere. Thus a file exists and may be read, but a complete authoritative data-driven lifecycle is not yet established.
- `Engine/Codecs/WorldSpaceCodec.{h,cpp}` and `SpaceContainerCodec.{h,cpp}` exist; the current world model has geometry, luminaires, a camera and environment/solar paths, not the full requested stable folder/entity hierarchy. `RegisterShowcaseGalleryInOutliner` constructs several rows directly; these rows alone do not prove construct/delete persistence or complete renderer registration.
- `Content/Worlds/VehicleClearSkyDefault.WorldSpace` exists with TOML metadata and binary sections. Existing content uses legacy extensions such as `.VolumetricSpace`, `.PhotometrySpace` and `.ComponentSpace`. Audit each reader, writer, consumer and generator before migrating anything.
- The HTML reference identified in the previous plan is `Experimental/ProjectZeroEditor` at upstream commit `2fd4c6247b8697d18337669276da9ce88f6d6e00`; `Editor.jsx` has outliner right-click handling. Render and inspect the live reference and its actual menu entries, states, icons and interactions before writing the native menu. The exact layout and supported entries are **not yet verified**.

## 2. Approved extension registry and engine ownership map

The spelling below follows the user's preferred names. For consistency with the older rule, decoders recognize extensions case-insensitively and encoders use canonical PascalCase capitalization of the same names (e.g. `.worldspace` → `.WorldSpace`, `.fontspace` → `.FontSpace`); confirm if exact lowercase on write is intended instead. Use explicit type IDs and schema versions in headers, not just the suffix. A registry entry is a **planned domain contract**, not evidence of an existing codec or solver.

| Approved name | Authored content and intended native owner / consumers |
| --- | --- |
| `.worldspace` | World root, hierarchy, stable IDs, placements, components, links and references → world model, scene lifecycle, outliner, renderer. |
| `.interface` | Runtime screen/layout hierarchy, anchors, containers and z-order → interface layout and runtime UI; editor authoring UI remains separate. |
| `.fontspace` | Font faces, metrics, variants and licensed font payload → existing font codec and typography; import actual TTF/OTF through font interchange. |
| `.pixelatlas` | Packed sprites, UI icons or virtual texture page tables → image/texture and atlas consumers; define distinct typed variants and GPU formats. |
| `.soundscape` | Ambient beds, music stems and spatial mixer snapshots → audio domain. |
| `.distancefield` | Distance grids and metadata → distance-field codecs and rendering. Record signed/unsigned convention, units, voxel transform and truncation; do not assume interchangeability. |
| `.particlesystem` | Timeline, emitters, LOD and bounds → particle system owner. |
| `.emitternode` | Spawning, bursts, lifetime and update configuration → emitter owner. |
| `.vectorfield` | Velocity grid and procedural field specification → field owner and particle/fluid consumers. |
| `.ribbontrail` | Continuous strip generators and material bindings → particle/ribbon rendering. |
| `.swarmbehavior` | Flocking, instance and simulation rules → swarm simulation. |
| `.fluidgrid` | Eulerian solver domain and fields → fluid simulation. |
| `.fracturetree` | Fracture hierarchy, connectivity and stress rules → destruction domain. |
| `.constraintset` | Joints, ragdolls, hydraulics and welds → physics/constraint domain. |
| `.impactprofile` | Material-pair response definitions and referenced effects → physics/material/effects dispatch. |
| `.raystructure` | Derived BLAS/TLAS artifacts → renderer acceleration owner; portable recipe separate from device/driver-specific binary. |
| `.iesprofile` | Measured photometric distribution → photometry import and lighting. IES source is interchange; preserve units and orientation. |
| `.skyatmosphere` | Spectral atmosphere authoring parameters and derived scattering LUTs → atmosphere renderer. Keep source parameters separate from derived LUT provenance. |
| `.shadergraph` | Authoring node network → material/shader compiler, not executable shader code by itself. |
| `.computekernel` | Compute program definition and compiled variants → shader toolchain/GPU dispatch; validate target and permitted execution. |
| `.rendergraph` | Rendering dependency graph and resource scheduling → render-system owner. Validate acyclicity; runtime feedback requires explicit temporal resources. |
| `.poststack` | Tone, grading, bloom and lens effect settings → post-processing owner. |
| `.noisegraph` | Deterministic procedural noise graph and seeds → procedural generation consumers. |
| `.convexhull` | Collision hull and generation provenance → geometry and physics; exact collision use depends on the physics algorithm, not a guaranteed broad-phase-only role. |
| `.shardcluster` | Pre-fractured shards, mass/inertia and sleep settings → destruction/physics. |
| `.irradiance` | Precomputed diffuse lighting and directional lobes → lighting renderer. |
| `.spectralcurve` | Wavelength samples and units → colour, optics and material domains. |
| `.lensaperture` | Aperture/bokeh and camera optical parameters → camera and post-processing. |
| `.radiancecache` | Persistable real-time GI probe configuration and compatible checkpoint → GI owner; dynamic per-frame results are not assumed reusable across arbitrary worlds or devices. |
| `.component` | Reusable component definitions/configuration attached to world entities → component and world owners. Existing `.ComponentSpace` rig assets must not be silently interpreted as this format. |
| `.triggerzone` | Sensor shape and event bindings → physics and gameplay event owners. |
| `.eventbus` | Named signal schemas and routing definitions → event owner; avoid enforcing a DAG on runtime event traffic. |
| `.variabletable` | Typed game variables, defaults and scope → gameplay variable owner. |
| `.timerprofile` | Timer and time-dilation presets → time/gameplay owner. |
| `.curve` | Reusable 1D/2D curve, interpolation and domain units → animation, UI and procedural consumers. |
| `.engineconfig` | Boot settings and startup world references → configuration/lifecycle; validate platform defaults. |
| `.graphicssettings` | Player-facing quality and display choices → rendering configuration, separated from editor-only settings. |
| `.userprofile` | Player preferences, accessibility and progress → user-profile owner with privacy and safe writes. |
| `.editorlayout` | Editor panel layout, snap and gizmo settings → editor workspace only; exclude from Shipping. |
| `.timeline` | Multitrack animation, camera, audio and subtitle sequencing → animation/sequencer owner. |
| `.tooltip` | Reusable tooltip content/layout definition → interface system; editor-specific tooltip assets remain editor-only. |

For older types without a direct approved replacement, retain existing formats, including geometry, material, texture, image, colour, project, surface-parameter and render domains as appropriate. An overlap is not automatic equivalence: `.skyatmosphere` does not automatically replace every `.VolumetricSpace` cloud/fog asset; `.iesprofile` does not automatically cover solar/area photometry; `.pixelatlas` is not every texture; `.component` is not necessarily the existing vehicle rig. Resolve each by schema and semantics, with explicit migrations and backward-compatible readers. No broad rename/delete before a dependency inventory and round-trip tests.

## 3. World model, startup and save

1. Introduce or extend a versioned typed world document with persistent unique entity IDs, parent/folder IDs, explicit entity types, enabled/visible status, transforms where meaningful, typed parameters and asset references. Validate IDs, one parent per entry, no folder cycles, missing references and unsupported schemas. Store TOML source metadata in the container; place only appropriate large binary payloads in sections.
2. Make `EditorLifecycle` obtain the selected startup world from project/engine configuration and load it through the world codec and a world-to-runtime adapter. Separate a genuinely blank/new world from opening an existing world. A missing or invalid file produces an actionable error; never silently show a hardcoded showcase in its place. Built-in test fixtures can remain explicitly invoked in validation, not as hidden user-world fallback.
3. Construct render and atmosphere components from authored world data. Keep viewport display/debug preferences in `.editorlayout` or equivalent editor settings and ensure they do not overwrite authored Sun/Stars/Moon/Cloud/Atmosphere values. Decouple the observed solar enable assignment from the star toggle. Preserve existing user scene content during migration.
4. The outliner projects the world hierarchy by stable ID; selection, inspectors and viewport refer to the same identity. One mutation transaction updates the model then renderer/registries and view, or rolls back cleanly on failure. Never treat a row as proof an entity exists.
5. Save via a versioned encoder: validate, write a temporary file on the same filesystem, flush and atomically replace where supported; surface failures without destroying the previous save. Reopen reconstructs the same IDs, hierarchy, assets, parameters and visibility. Define external asset edits separately from per-world overrides; do not modify a shared asset just because one instance changes.

## 4. First vertical slice: create and remove environment entities

**Reference gate:** run the pinned HTML, capture the Construct menu and the outliner right-click menu in normal, disabled, hover and selection states, including dismissal, scrolling and edge placement. Reuse the native context menu/control, icon, typography and motion primitives; faithfully reproduce the reference. Keep every HTML option visible in its proper place; actions not implemented or unsafe for the selected entity are disabled/greyed out with an explanation, not placeholders that pretend to work.

| Action | Authoritative change | Runtime and UI consequence |
| --- | --- | --- |
| Add Sun / Clouds / Moon / Stars / Atmosphere | Execute one typed, undoable `Create` transaction with validated defaults and an individual stable ID. Create `Environment` folder lazily if missing; reuse it for subsequent additions, including after reload. | Register the appropriate actual native scene/atmosphere component and update renderer inputs; show the correct child row and inspector. No hardcoded display-only row. |
| Remove entity | Validate reference policy, remove the world entity and its owned associations in one reversible transaction; leave shared assets untouched. | Remove its outliner row, clear or safely update selection/inspector, unregister component and subscriptions, invalidate affected render resources and synchronize GPU lifetimes. Other environment entities continue working. |
| Remove folder | Only enable when safely implemented. Define explicit recursive-delete confirmation or non-destructive reparent behavior, including undo, before enabling; do not silently delete children or files. | World, outliner and renderer remain consistent and undo restores the original hierarchy. |
| Undo/redo | Revert/reapply world transactions with the same identities and dependencies. | Restore renderer registration, selection where appropriate, hierarchy and saved dirty status. |

Audit each native atmosphere feature for support, singleton restrictions and physical interactions. The plan does not assume independent arbitrary duplicate Suns or Atmospheres are valid. If uniqueness or dependencies are required, explicitly validate and disable unsupported options rather than silently changing another entity. Define what removing Atmosphere means for other still-present emitters and how an absent component is represented; Sun, Moon and Stars must not be accidentally removed with it. Verify visual output with relevant toggles and a controlled scene; absent entities must have no unintended contribution. Deletion must handle pending render work using the renderer's existing synchronization/lifetime mechanisms and not leave dangling IDs, stale acceleration or stale GPU descriptors.

## 5. On-disk format, compatibility and revision

- Retain the old plan's two-tier container: a UTF-8 TOML manifest and explicit delimiter, with a 64-byte-aligned SFNT/TTF-style binary section directory containing identifiers, offsets, lengths and 64-bit FNV-1a section checksums. Reuse the existing codec after verifying its exact wire format; specify byte order, alignment, offset base, limits, version compatibility and unknown-section handling in tests. A plain TOML editor can inspect the manifest, but the hybrid file as a whole is not necessarily a valid standalone `.toml` document.
- Preserve unknown forward-compatible fields/sections or refuse a destructive rewrite; enforce bounded lengths, overflow-safe offsets, section overlap checks, truncation detection, path containment and clear errors for malformed/untrusted files. FNV detects accidental corruption; it is not a security or collision-resistant content identity.
- Register suffix/type pairs with canonical writing and case-insensitive reading, detect collisions on case-sensitive filesystems, and define deterministic serialization. Migrate legacy files via versioned decoders and explicit conversion, preserving old files and a migration report until validated. Do not overwrite the only copy or claim renamed legacy files have the new semantics.
- Keep the older revision rules: parametric, resolution-independent operations, default **215 MiB per document** and **750 MiB globally**, global ordering and redo purge followed by prefix fold. Test exact reconstruction and document how retained branches/snapshots are protected during compaction. Separate revision DAG/history from the dependency/build DAG; the limits apply to history, not indiscriminately to all stored assets.

## 6. Persistent dependency DAG and selective recomputation

1. Give every authoring document a stable identity and schema version. Represent explicit directed edges from each derived artifact to its inputs (documents, selected subresources, parameters, imported source, compiler/algorithm version, platform and relevant device capabilities). Validate an acyclic *build* graph and report the actual cycle path. Scene parentage, runtime events and time-feedback are distinct graphs and are not forced into this DAG.
2. Canonicalize inputs deterministically and compute a collision-resistant content digest (e.g. SHA-256) for artifact identity, including all parameters, relevant colour/units, shader/compiler/tool version, output format, platform and device/driver where applicable. Keep the existing FNV section checksum for corruption checks. Hashing policy and canonical encoding must be versioned.
3. Persist immutable derived artifacts and their provenance in a bounded, configurable local artifact store; share when keys match, never infer validity from filename or modification time alone. Verify artifact type, key, checksum and compatibility on load. Cache miss, corruption or changed dependency triggers the smallest correct rebuild; unchanged downstream work is reused. Dynamic GPU resources are recreated on load, and simulation/time-dependent data is reused only when all semantic inputs and time are represented.
4. On edit, propagate dirtiness only through affected edges; schedule topologically with cancellation and deduplication. Publish results atomically only if the source generation still matches. Preserve the last valid result while rebuilding when safe; failures remain visible and never silently use stale output as current.
5. Account for refcounts/reachability, quota, garbage collection, concurrent writers, crashes, cross-platform paths and schema/algorithm upgrades. Never garbage-collect referenced authored assets, revisions or unsaved work. Provide inspection showing why a node rebuilt or was reused. Do not commit massive derived artifacts to Git by default.
6. Example: changing the Sun's intensity updates its world record and affected lighting inputs; it must not rebuild unrelated geometry, fonts or cloud authoring assets. A changed atmosphere scattering parameter invalidates the appropriate LUT and dependent render inputs. Test these dependencies against actual renderer behavior before finalizing edge granularity.

## 7. Additional requested domain scope

The following domains are explicitly included in this plan, in addition to the initial environment-object slice. The registry above already lists their formats; this section specifies their persistence and delivery obligations. These are planned capabilities, not completed implementations.

| Domain | Persist and connect | Dependency / safety rule | Verification |
| --- | --- | --- | --- |
| **Audio** (`.soundscape`) | Ambient beds, stems, mixer snapshots, source references, routing and spatial parameters connect to `Engine/AudioSpace/` and the existing acoustic codec after an API audit. Preserve source audio and its licensing; import/decoder details are a separate contract. Audio emitters and world bindings must have real native ownership and clean unregister on removal. | Persist authored routing and compatible decoded/converted artifacts, not live device handles or continuously changing playback position as authoritative asset content. Rebuild affected conversions when source or codec settings change. | Import/playback, spatial response, routing, save/reopen, device loss and removal; audio must not block the UI or break runtime use. |
| **Vector fields** (`.vectorfield`) | Versioned authored 3D grid or generator recipe, transform, dimensions, physical units, seed and sampling/wrap rules; bind to particle/fluid consumers only when those systems are actually available. | Include generation algorithm/version and all inputs in artifact identity; reusable grid tiles are derived, and unsupported solvers remain explicitly blocked rather than mocked. | Deterministic round-trip and sampling, transform/units, invalidation and known-field comparisons. |
| **Pixel atlas** (`.pixelatlas`) | Typed sprite/icon atlas versus virtual-texture page-table variants, source images, rectangles, padding, scale, colour space and GPU representation. Feed native icon/image consumers through shared facilities. | Repack only when inputs or packing rules change; separate editor-only icon assets from production atlases. Ensure mip/gutter handling and deterministic placement. | Pixel, alpha, colour and mip comparisons, region bounds, source update invalidation and Shipping resource audit. |
| **Ray structure** (`.raystructure`) | Portable geometry references, build options and bounds plus optional derived acceleration artifact for compatible renderer/hardware. | Key device-specific artifacts by geometry, build flags, API, device and driver; never serialize live handles or assume acceleration binaries are portable. Safely rebuild when incompatible. | Compare intersection results to fresh builds; verify reuse, invalidation, driver mismatch and safe resource lifetime. |
| **IES profile** (`.iesprofile`) | Import real IES distributions with provenance, photometric units, orientation and normalization into a typed native profile; connect to existing photometry/lighting after verifying the codec. | Derived sampling grids depend on source, parser/version and resampling parameters; retain source and do not conflate IES with solar or area-light authoring. | Known IES fixtures, intensity/orientation and energy checks, rendering comparison and round-trip. |
| **Post-processing stack** (`.poststack`) | Ordered effect definitions, colour spaces, exposure, curves, LUT references and camera/world overrides connect to the native post renderer. | Invalidate only affected compiled/derived outputs; preserve ordering and per-view parameters; graphics presets choose quality without rewriting authored effects. | Round-trip, effect ordering, reference images, missing LUT/error handling and runtime compatibility. |
| **Radiance cache** (`.radiancecache`) | Persist authored probe configuration and, only where supported, a versioned derived checkpoint and provenance for real-time GI. | Include relevant scene geometry, lighting, materials, algorithm, time and hardware dependencies. Never treat a stale temporal probe result as current; rebuild or reinitialize when validity cannot be established. | Valid reuse versus invalidation, camera/scene changes, visual comparison, corruption recovery and GPU synchronization. |
| **Curves** (`.curve`) | Shared 1D/2D keys, time/domain units, interpolation, tangents and extrapolation; reusable by animation, audio, UI and effects. | Consumers reference immutable versions or update their dependency keys; editing one curve invalidates only true dependents. Preserve the exact interpolation formula and precision. | Exact key evaluation, interpolation boundaries, extrapolation, determinism and save/reopen. |
| **User profile** (`.userprofile`) | Typed local preferences, accessibility and progress, stored separately from project/world assets, with explicit ownership and privacy policy. | Atomic writes, schema migration, backup/recovery and platform-appropriate user storage; never include personal profiles or secrets in the project, Git or the shared artifact store. | Persistence, migration, corrupt-file recovery, permissions and isolation between users/projects. |
| **Graphics settings** (`.graphicssettings`) | Player quality and display preferences connect to renderer configuration; distinguish authored defaults from per-user overrides and hardware capability fallback. | Apply validated changes at safe boundaries; do not write temporary hardware choices into the world or rebuild unrelated assets. | Save/reopen, unsupported resolutions and features, rollback on apply failure and runtime behavior. |
| **Editor layout** (`.editorlayout`) | Editor-only workspace arrangement, panels, snap and gizmo preferences; connect to docking and editor configuration without overwriting authored world properties. | Store per-user/project as appropriate; exclude editor UI implementation and layout resources from Shipping; tolerate missing panels and future versions. | Restart/reopen, resize, unknown panels, layout migration and Shipping package/link audit. |

**Audio naming:** The user's prior approved list names `.soundscape` for ambient beds, stems and mixer snapshots, but does not specify a separate extension for individual audio clips. Use `.soundscape` for that approved scope and keep clip storage/import under the existing audio/interchange contracts until a separate audio asset extension is approved; do not invent `.audio` as a canonical format.

These additions do not displace Sun/Clouds/Moon/Stars/Atmosphere as the first implementation slice. Each domain gets a concrete schema/consumer audit, a versioned codec where needed, migration from any genuinely equivalent legacy type, and the listed tests before being marked complete. Editor controls for these domains still require the HTML reference gate and must not be shipped as production UI.

## 8. Sequence, validation and acceptance

| Phase | Deliverable and exit gate |
| --- | --- |
| 0 | Render HTML and record screenshots/interaction sequences. Inventory exact construct/context menu entries and native equivalents; identify missing renderer functionality and establish file migration test fixtures. |
| 1 | World identity/hierarchy and codec migration, configuration-selected startup, safe load/save and explicit errors. Prove hardcoded fallback is not used for user worlds. |
| 2 | Reusable editor menu and world command adapter; five environment creation/removal commands, default single folder, renderer registration and undo/redo. Unsupported reference actions stay grey. |
| 3 | Persistent artifact DAG infrastructure and one measured end-to-end vertical slice (e.g. atmosphere LUT). Record cache hit/miss and invalidation evidence before scaling. |
| 4 | Add remaining approved formats in dependency order, each with typed schema, owner, codec, tests and a real consumer; preserve old formats where needed. No claim that all domains already exist. |
| 5 | Broaden HTML-to-native fidelity and complete regression, production separation and performance gates. Commit and push each logical milestone on the active branch. |

**Required tests and proof:** world save/reopen equality; stable IDs and folder order; create each of five via Construct and verify actual render/model/row; remove each via right click, save/reopen and undo/redo; repeated add/delete and GPU synchronization; missing/corrupt world and failed save; shared asset preservation; duplicate/singleton and dependency policy; scene selection after deletion; context-menu visual and disabled-entry parity; codec bounds/unknown versions/checksum/round-trip/migration/case tests; DAG hit, transitive invalidation, cycle, failure, cancellation and crash recovery; idle and edit-time performance measurements. Capture browser and native screenshots plus timed animation frames, and inspect side by side under matched settings. Test that editor resources and symbols are not introduced into Shipping while runtime world data remains usable. Build/run only `Frontier`, `FrontierSketcher` and directly relevant validations, not unrelated standalone workspace applications.

## Open decisions before implementation

1. Confirm canonical output casing if the approved names were intended to mandate lowercase filenames rather than case-insensitive recognition with old-plan PascalCase writing.
2. Confirm the `ProjectZeroEditor` HTML reference visually and enumerate its exact menu items. Decide the precise singleton/multiple-object policy based on native capability and reference behavior.
3. Confirm whether an empty default `Environment` folder should persist after its last child is removed. Proposed default: preserve the folder unless explicitly deleted.
4. Confirm which legacy asset conversions are in scope for the first slice; keep old assets loadable until each mapping is verified. No guessed timetable or claim of complete format support.
