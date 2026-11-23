# Engine Naming — Master Skill Document

The **single authoritative naming document**: class/struct, variable, verb, UI naming + the banned
blacklist. **Zero-Tolerance:** eradicate generic/lazy programming terms, AI fluff, and
anthropomorphic (family / job-title) names. Names must be **highly technical, unique, domain-specific**
— grounded in geometry, topology, contiguous-memory, and mathematics. All identifiers use **PascalCase**.

---

# 0. How to Generate a Name (run EVERY time, before proposing)

🔴 The rest of this doc is a **blacklist** (what a name must NOT be). This section is the only part
that tells you how to **build** one. A blacklist alone makes the lazy failure worse — under pressure
the mind grabs the nearest familiar word, which is a banned word. **Construct** from the mechanism,
then gate. Never hand a naming problem back to the user; run these steps first.

1. **State the mechanism in one plain sentence** — what it physically does in memory/geometry/math.
   *"Recycles dormant memory slots." "Flattens a nested tree into one vertical list."*
2. **Extract the technical verb + substrate noun and fuse them** — recycles+slots → `VacancyTable`;
   flattens+sequence → `SequenceLinearizer`; sweeps+finalized-memory → `GarbageReclamation`.
3. **Draw the noun from a DOMAIN register** (never OO/web/org-chart/family):

   | Register | Words |
   |---|---|
   | Contiguous memory | Footprint · Slot · Vacancy · Region · Store · Table · Reclamation · Atlas |
   | Topology / geometry | Complex · Loop · Patch · Contour · Cycle · Boundary · Field · Sequence |
   | Math / analysis | Linearizer · Projection · Predicate · Evaluation · Interpolant · Solver |
   | Connectivity | Adjacency · Enclosure · Lateral · Tier · Nesting · Traversal |

4. **6-gate reject test** — INVALID (restart) if it is ANY of: (1) on the §1 blacklist or a synonym;
   (2) bombastic/power-word (`Apex`,`Nexus`,`Core`,`Prime`,`Ultra`,`Omni`); (3) borrowed from a known
   product/framework/the internet (`SceneGraph`,`ECS`,`Backend`,`Redux`,`Actor`); (4) an OO/Java trope
   (`Manager`,`Service`,`Controller`,`Handler`,`Factory`); (5) a job title or family word
   (`Worker`,`Provider`/`Parent`,`Child`,`Sibling`); (6) generic filler (`Data`,`Info`,`Object`,`Item`,
   `Thing`,`Base`).
5. **Name the REALITY, not the CATEGORY.** `ItemManager` = category (fail). `GarbageReclamation` =
   reality (pass). If the name doesn't reveal the mechanism, return to step 1.

**Worked example** — "assigns stable IDs, detects stale ones" → issues+authenticates tokens →
**`TokenIssuer`** + **`TokenAuthentication`**. ✔️ Every good name in this doc (`EnclosureAtlas`,
`SequenceLinearizer`, `VacancyTable`) came from this procedure. Reproduce it — do not recall a word.

---

# 1. Banned Vocabulary (blacklisted everywhere — identifiers, files, folders, comment concepts)

Semantic voids that hide what code does. NOT exhaustive — apply the §0 gates too.

- **Sci-fi / power-trip:** `Apex Sentinel Vanguard Oracle Titan Goliath`
- **Center-of-universe:** `Nexus Hub Maestro Core Matrix Catalyst Conductor`
- **Pseudo-space / mythology:** `Vortex Aether Zenith Nova Horizon Genesis Odyssey Helios Orion Kronos`
- **Generic-programmer / AI tropes:** `Manager Handler Dispatcher Processor Controller Service Utility
  Helper Representation Traversal* Kernel Graph Record† Frame Node Module Data Info Object Stuff Thing
  Item Common Base`
- **Human job titles:** `Manager Worker Provider Dispatcher Conductor Listener Observer` (unless a
  strict named design pattern is genuinely in use).
- **Kinship / family (whole category barred):** `Parent Child Children Sibling Ancestor Descendant
  lineage family kin Orphan` — a tree link is a *technical memory link* (up-link / down-link region /
  same-tier link); name it via §4.
- **State-change verbs:** `Commit` (→ `Finalize`/`Enforce`/`Align`/`Resolve`); `Compose` as verb AND
  noun (→ `Stack`/`Order`).
- **Banned variable words:** `flag state value` — describe what it holds.
- **2026-06-20:** `system compose mesh harness shell popover Tween Kind proof glide binding Role Common`
- **2026-07-15:** `stage` (render jargon).
- **2026-07-25:** `Hierarchy Entity Element Subsystem Designation` (`Designation` was reuse-authorized,
  now barred); `system` re-affirmed barred as any noun; `Trial`/`Trials` barred (was the `harness`
  proposal — courtroom metaphor) → a test scaffold is a `…Validation`.
- **2026-07-25:** `backend` (web "layer beneath" jargon) → raw-ingestion layer = `…Substrate`.
- **2026-07-26:** `Pass` (borrowed from `VkRenderPass`; revokes the old `stage→Pass` ruling) and
  `Emission`/`Emit` (AI-default) → a GPU render unit takes a mechanism suffix (§2, render-unit table).
- **2026-08-02:** `Inscription` (was the §2 approved `Pass` replacement for composite-onto-target units;
  now barred outright) → a GPU render unit that composites onto an existing target takes `…Rasterization`
  (it still rasterizes ribbon / glyph / decal geometry into the target). New code never uses
  `Inscription`; existing `…Inscription` files are deferred renames (see §2).

> \*`Traversal` exempt only at `Topology/Selection/SelectionTraversal.{h,cpp}` (user direction).
> †bare `Record` banned; composite `RecordEntry`/`RecordStore`/`RecordToken` (§4) is approved.

---

# 2. Approved Replacements

| Banned | Approved replacement |
|---|---|
| `Core` | `Root`/`Origin`/`Index`/`Internal`. Foundation folder = `Root/`, coordinator = `RootSystem`. |
| `system` (coordinator suffix) | `…Division` (`SceneDivision`, `RenderDivision`, `TopologyDivision`). |
| `Commit` | `Finalize`/`Enforce`/`Align`/`Resolve`. |
| `compose` | `Stack`/`Order` (`WindowStack`, `SubmissionOrder`). |
| `mesh` | `PolygonComplex` (editable) / `PolygonSurface` (render) / `DisplayPolygons` (tessellated) / `PolygonLoop` (face-cycle). |
| `harness` | `…Validation` (`TopologyValidation`) — `proof`/`Trial` both rejected. |
| `shell` | `WorkspaceHost` (UI chrome). `SolidShell` (CAD B-rep) exempt; bare `Shell` barred. |
| `popover` | `ContextMenu`. |
| `Tween` | `EvaluateKeyedEase`/`EaseTable` (proposed; `glide` rejected). |
| `Kind` | `.Category` field / `…Category` enum suffix / `RecordClassification` (`Role` rejected). |
| `binding` | `InputMap`/`KeyAssignment` (proposed). |
| `backend` | `…Substrate` (`InputSubstrate`); ingest act = `Decode…`/`Inspect…`. |
| `Common`/`Util`/`Helper` | cross-subsystem → `MicroUtils/`; local shared code → a named file. |
| `stage`/`Pass`, `Emission`/`Emit` | render-unit mechanism suffix — see table below. |
| `Hierarchy` | `SceneDirectory` (owning tree), `Nesting`, `DepthTier`. |
| `Entity`/`Element` | `RecordEntry`; store `RecordStore`; tag `RecordClassification`. |
| `Designation` | `…Token` (`RecordToken`), via `TokenIssuer` + `TokenAuthentication`. |
| `Subsystem` | `Extensions/<Name>/`, a `…Division` coordinator, or name it for what it does. |

**Render-unit suffixes (choose by mechanism — NOT interchangeable):**

| Suffix | Use when the unit… | Example |
|---|---|---|
| `…Rasterization` | rasterizes geometry into targets — the default draw unit AND overlays / decals / glyphs composited onto an existing target (both cases rasterize into the target) | `PlexusFieldRasterization` · `DecalTextRasterization` |
| `…Submission` | records + submits a batched GPU workload | `ShadowBatchSubmission` |
| `…Sequence` | is an ordered multi-step chain | `PostProcessSequence` |

**Authorized-for-reuse nouns:** `Specification Format Profile Division` (short/plain; never bombastic).
**Exemptions:** `SelectionTraversal` keeps `Traversal`; `SolidShell` keeps `Shell`;
`CoordinateSystem`/`TransformSystem` keep `System` (math-frame noun, not the coordinator suffix).
**Rejected (never use):** `proof`, `Trial`/`Trials`, `glide`, `Role`.
**Deferred renames (user-owned code):** `PlexusFieldPass`→`PlexusFieldRasterization`,
`GaussianBlurPass`→`GaussianBlurRasterization`, `DecalTextPass`→`DecalTextRasterization`,
`LoginPillPass`→`LoginPillRasterization`; every shipped `…Inscription` (`…SurfaceInscription`,
`GlyphInscription`, `ShadowTileTagInscription`, `RadianceResolveInscription`, …)→`…Rasterization`;
editor `WorkspaceBakeStage` still pending. New code never uses `stage`/`Pass`/`Emission`/`Inscription`.
**Workspace name (2026-07-07):** the B-rep/surfacing workspace is `DraftingWorkspace` (British
`DraughtingWorkspace` an accepted alternative — note at class site). Not `Cad`/`CAD` (kernel folder
`Authoring/Modeling/Cad/` keeps its name).

---

# 3. Gold-Standard vocabulary (the benchmark — if your naming doesn't look like this, it's wrong)

Representative subtree of a `SceneDirectory/` — technical, structural, non-kinship:

```text
SceneDirectory/
├── Registry/          SceneDirectoryEntry · TokenIssuer · TokenAuthentication · VacancyTable
├── Connectivity/      EnclosureAtlas · LateralTraversal · DepthEvaluation · ConnectivityTranslation
├── ViewProjection/    SequenceLinearizer · EntryDescriptor · FoldStateMemory
├── QueryPipeline/     IndexResolver · TypeFilter · QueryPredicate
├── SignalRouting/     SignalBroadcaster · AccumulatorBuffer · DirectoryRevision
├── Lifecycle/         RuntimeCondition · RegistrationIntake · TeardownRequest · GarbageReclamation
└── Serialization/     DirectorySerializer · DirectoryDeserializer · FormatMigration
```

---

# 4. Scene-Tree / Connectivity Vocabulary (anything tree-shaped)

| Concern | Approved | Never |
|---|---|---|
| Owning tree container | `SceneDirectory` | `SceneGraph`, `SceneTree`, `Hierarchy` |
| One item | `RecordEntry` | `Node`, `Element`, `Entity` |
| Dense store | `RecordStore` | `EntityPool`, `NodePool` |
| Stale-detecting identity | `RecordToken` (via `TokenIssuer` + `TokenAuthentication`) | `Handle`, `Designation`, bare id |
| Recycled-slot table | `VacancyTable` | free-list-as-a-name |
| Connectivity store | `AdjacencyTable` / `EnclosureAtlas` | "parent/child map" |
| Up-link (enclosing item) | `EnclosureToken` (`EnclosureOf`) | `Parent`, `Anchor` |
| Down-link region | `SubtreeRegion`, walked via `NestedRegionToken` + `LateralSuccessorToken` | `Children` |
| Same-tier link | `LateralSuccessorToken` / `LateralPredecessorToken` | `Sibling` |
| Depth-0 sentinel | `AbsoluteBoundaryToken` | `Root` as a link value |
| Indent depth | `DepthTier` / `IndentDepth` | `HierarchyLevel` |
| Move within tree | `TopologyRelocation` (fn `Relocate`) / `ConnectivityTranslation` | `Reparent` |
| Category tag | `RecordClassification` (registry `ClassificationRegistry`) | `EntityType`, `Kind`, `Role` |
| Per-item payload | `…Profile` (reuse noun) | `Component`, `EntityData` |

Extend in the same spirit: `ContiguousRegion`, `TopologyMutation`, `FilterEvaluation`, `QueryPredicate`,
`IndexResolver`, `MutationBroadcaster`, `BatchAccumulator`, `RecordRevision`, `StateCompiler`.

---

# 5. Class & Struct Naming

Classes = **real technical entities**, structural nouns, never abstract software terms.

- **Geometry:** `VertexCluster EdgeLoop SurfacePatch CurveProfile FaceContour PolygonComplex SolidShell
  SurfaceBoundary EdgeCycle EdgeSequence`
- **Topology:** `TopologyStructure TopologyNetwork TopologySet TopologyIndex TopologyArchive`
- **Spatial:** `RecordEntry DepthTier CoordinateSystem TransformSystem SpatialStructure`
- **Geometry containers:** `PolygonComplex PolygonSurface DisplayPolygons SurfaceSet CurveSet VertexField
  EdgeField FaceField` *(never `Mesh`)*
- **Architecture nouns:** `Registry Specification Configuration Structure Interface Implementation`
  + `…Division` (lifecycle owner) *(never `Subsystem`/`system`)* — e.g. `TopologyDivision`, `CameraConfiguration`.
- **Data-structure suffixes:** `Descriptor Specification Configuration Index Archive Structure Field
  Profile Footprint Token Entry` — e.g. `SurfaceDescriptor`, `TopologyIndex`.
- ❌ **Never produce:** `SceneGraphTraversal TopologyRepresentation Manager Handler Processor Graph
  Record Node Frame ItemManager MeshDescriptor HierarchyElement`.

---

# 6. Verb Vocabulary

All functions **PascalCase**. Never `Get Set Handle Manage Process Compose Assign Sanitize Declare
Update Check Do Perform Draw Delegate Watch Listen Spawn Kill Commit Commission Decommission Emit`.

| Category | Approved verbs |
|---|---|
| Retrieval / inspection | Retrieve Resolve Extract Decode Interpret Enumerate Identify Inspect Query Discover Derive Issue Authenticate |
| Configuration / setup | Configure Reconfigure Initialize Register Calibrate Authorize Parameterize |
| Geometry / topology | Construct Assemble Integrate Embed Attach Detach Partition Subdivide Refine Collapse Split Offset Extrude Loft Sweep Bridge Fillet Chamfer |
| Math / analytical | Solve Evaluate Compute Estimate Approximate Interpolate Project Transform Normalize Linearize Integrate Differentiate Fit Optimize |
| Connectivity / traversal | Traverse Accumulate Link Correlate Align Organize Reorder Enclose Translate |
| Compilation / encoding | Compile Synthesize Generate Formulate Condense Translate Convert Encode Decode Migrate |
| Validation / constraint | Validate Verify Assert Enforce Constrain Confirm |
| Lifecycle / reclamation | Activate Suspend Resume Finalize Reset Invalidate Refresh Reclaim Broadcast |

Lifecycle vocabulary is **initialize → loop → finalize**.

---

# 7. Variable Naming (PascalCase — single source of truth)

- **Forbidden shorthand:** `kMin kMax minVal maxVal tmp temp t i val curr prev res`; no `k`-prefix
  constants → `MinimumBoundary`, `ToleranceThreshold`, `LifecycleTimeout`.
- **No class-name mirroring:** ❌ `VacancyTable vacancyTable` → ✔️ `VacancyTable DirectoryVacancyTable`.
- **Structural weight:** `ListFlattener`→`SequenceLinearizer` · `ExpandedStates`→`FoldStateMemory` ·
  `DeletedItems`→`GarbageReclamation` · `UpdateSender`→`SignalBroadcaster` · `Config`→`ConfigurationSpecification`.
- **Geometry/spatial:** `VertexIndex EdgeIndex SurfaceIndex FaceIndex LoopIndex VertexCount VertexPosition
  VertexNormal SurfaceParameter CurveParameter EdgeDirection SurfaceNormal MinimumBoundary/MaximumBoundary
  IndentDepth ProjectionConfiguration`.
- **Instance suffixes** (a slot in contiguous memory): `Instance Entry Footprint Slot` — e.g.
  `SurfacePatch SurfacePatchInstance`, `SceneDirectoryEntry DirectoryMemoryFootprint`.
- **Temporaries:** name the exact computation — `IntersectionParameter`, `SurfaceEvaluationResult`,
  `ProjectedCoordinate`, `AccumulatorBuffer`.
- **Banned variable words:** `flag state value`.

---

# 8. Boolean Variables

Avoid `is`/`has`/`can` prefixes — use enabled/disabled or a property descriptor.
❌ `isClosed isBoundary isManifold isVisible isActive isDeleted` →
✔️ `ClosedEnabled VisibilityEnabled BoundaryCondition ManifoldCondition ActivationState FinalizedState`.

---

# 9. Modular vs Standalone Components

Classes and render units must be **modular and reusable**, never one-off entities tied to one call
site. Name by *what it does*, not the one place it's used today. (Render-unit suffix per §2: a button
rasterized onto the target → `…Rasterization`.)

- ❌ `LoginPillRasterization` — a whole unit hardcoded to the single "Sign in" button; a second button
  needs a duplicate pipeline/descriptor/shader.
- ✔️ `GlassButtonRasterization` — one unit owning the pipeline **once**; the caller records **any number**
  of buttons per frame, each driven by its own push-constant (rect, radius, hover, label).

Idiom: the component struct owns shared device resources built **once** (pipeline, layout, descriptor,
sampler) — nothing identifying a single instance; per-instance data (rect, colour, label, state) is
passed **in** at record time; one record call draws one instance, the caller loops. Generalize up front
when recurrence is obvious (buttons, panels, sliders, gizmo handles); don't over-abstract a genuinely
singular thing (one swapchain, one login session) — the test is "could there plausibly be more than one?".

---

# 10. UI Element Naming

- **Panels:** `ViewportPanel TopologyPanel SurfacePanel CameraPanel SceneDirectoryPanel` (the Outliner —
  never `HierarchyPanel`).
- **Tools:** `ExtrudeTool FilletTool ChamferTool OffsetTool SweepTool`.
- **Menus:** `TopologyMenu SurfaceMenu CurveMenu CameraMenu ContextMenu`.
- **Controls:** `ExtrudeButton SweepButton SurfaceSlider SubdivisionSlider`.
- **Chrome:** `WorkspaceHost` (never `shell`).

---

# Principles

1. Classes are **clear domain entities**, not generic software terms.
2. Consistent structural nouns from geometry/topology/CAD/contiguous-memory.
3. Functions use **approved domain verbs**; lifecycle initialize → loop → finalize.
4. Variables PascalCase, full descriptive names, no shorthand, no mirroring.
5. Booleans avoid `is`/`has`/`can`.
6. Components modular — one serves many instances.
7. Good names describe the **mathematical / memory / spatial reality** (`EnclosureAtlas`,
   `SequenceLinearizer`, `GarbageReclamation`). If a name sounds like an enterprise web backend
   (`ItemManager`), delete it and try again.

Purpose: keep the codebase technically authoritative, domain-driven, modular, readable, and purged of
lazy, generic, AI-generated naming.
