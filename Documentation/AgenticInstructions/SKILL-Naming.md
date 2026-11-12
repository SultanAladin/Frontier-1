# Engine Naming — Master Skill Document

The **single authoritative naming construction procedure** for **Frontier**: C++20 types, templates,
concepts, functions, variables, `.slang` shaders, UI controls, and file/folder names.
**Zero-Tolerance:** eradicate generic programming placeholders, AI clichés, and anthropomorphic
(kinship / job-title) terms. All identifiers use **PascalCase** (with the sole mathematical Greek/Unicode scalar exception in
`SKILL-Formatting.md` §4). Names must be **concrete, technical, and domain-specific** — grounded in
contiguous memory, geometry, boundary connectivity, radiometry, acoustics, and hardware execution.

🔴 **Companion Blacklist:** Always audit candidate names against `Documentation/BannedWordsVocabulary.md`
before writing code or documentation.

---

## 0. How to Construct a Name (Run EVERY Time Before Proposing)

🔴 A blacklist tells you what a name must **NOT** be; this procedure builds what it **IS**. Never
reach for a familiar generic word under pressure — construct from the physical/memory mechanism first,
then run the 6-gate rejection check.

1. **State the mechanism in one plain sentence** — what it physically does in memory, math, or GPU silicon.
   *"Recycles preallocated indexed instance slots." "Encodes batched indirect draw arguments."*
2. **Extract the technical verb/operation + concrete domain noun and fuse them** — e.g.
   `RegionAllocator`, `IndexedSlotRecycler`, `TokenIssuer`, `PolygonNetwork`, `IndirectDrawEncoder`,
   `PacketSerializer`.
3. **Avoid discouraged words** even though they are not hard-banned: avoid `Substrate`, `Atlas`, and
   `Slice` whenever a direct mechanical name (`DeviceInterface`, `HardwareOutput`, `ChartPacker`,
   `PackedBits`, `BitRange`) exists.
4. **6-Gate Reject Test** — INVALID (restart at Step 1) if the candidate hits ANY gate:
   - ① Appears in `Documentation/BannedWordsVocabulary.md` or is a direct synonym (`adjacency`, `arena`, `binding`, `bloom`, `context`, `coverage`, `hub`, `interpolant`, `mesh`, `pass`, `pool`, `predicate`, `reclamation`, `reservoir`, `stage`, `store`, `submission`, `vacancy`, …).
   - ② Sci-fi / center-of-universe / mythology (`Apex`, `Nexus`, `Core`, `Matrix`, `Vortex`, `Horizon`, `Sentinel`, …).
   - ③ Framework / web jargon (`SceneGraph`, `ECS`, `Backend`, `Actor`, `Shell`, `Popover`, `Widget`, `Kit`).
   - ④ Generic OO / AI trope (`Manager`, `Service`, `Controller`, `Handler`, `Dispatcher`, `Processor`, `Helper`, `Utility`).
   - ⑤ Human job title or kinship term (`Worker`, `Provider`, `Listener` / `Parent`, `Child`, `Sibling`, `Orphan`).
   - ⑥ Empty placeholder (`Data`, `Info`, `Object`, `Item`, `Element`, `Thing`, `Common`, `Base`, `flag`, `state`, `value`).
5. **Name the REALITY, not the CATEGORY.** If the name could apply to an enterprise web backend, discard it.

---

## 1. Banned & Discouraged Vocabulary Cross-Reference

`Documentation/BannedWordsVocabulary.md` is the canonical blacklist enforced across all identifiers,
`.h`/`.cpp`/`.slang` files, folders, and comments:

- **Hard-Banned Highlights:** `action`, `adjacency`, `affinity`, `arena`, `Assembly`, `backend`, `badge`, `Base`,
  `binding`, `bloom` (→ `VeilingGlareFilter` / `LuminanceGlareDownsample`), `Cache`, `card`, `carousel`,
  `classification`, `Commit` (→ `Finalize`/`Enforce`/`Align`/`Resolve`), `Common`, `Complex`, `Compose`/`Composite`
  (→ `Stack`/`Order`), `concurrency`/`concurrent`, `context`, `continuum`, `contract`, `Core`,
  `coverage`, `Data`, `Designation` (→ `…Token`), `dev`, `Element`, `Emission`/`Emit`/`emitter`,
  `Entity`, `evaluator`, `Facet`/`Facets` (→ `Face`/`Faces`), `fence`, `flag`, `floating`, `flow`, `Frame`, `glyph`, `Graph`,
  `harness`/`Trial` (→ `…Validation`), `Hierarchy`, `hook`, `Hub`, `Info`, `Inscription`
  (→ `…Rasterization`), `interpolant`, `Item`, `Kernel`, `Kind` (→ `…Category`), `Listener`, `map`,
  `Matrix`, `member`, `menu`, `mesh` (→ `PolygonNetwork`), `Model`, `Module`,
  `mutation`, `Nexus`, `Node`, `Object`, `occupant`, `palette`, `Pass`, `pill`, `pipeline`, `pool`,
  `popup`/`popover`, `predicate`, `probe`, `reclamation`, `Record` *(bare & `RecordStore` barred; only `RecordEntry`/`RecordToken` approved)*,
  `reservoir`, `Role`, `shell`, `stage`, `state`, `store`, `submission`, `Subsystem`/`system`, `table`,
  `Topology`, `Traversal` *(exempt only at `GeometrySpace/Selection/SelectionTraversal.{h,cpp}`)*, `Tween`, `Utility`,
  `vacancy`, `value`, `Widget`.
- **Discouraged (Avoid):** `atlas`, `slice`, `substrate`.
- **Restricted Lifecycle Suffix (`Extension`):** Permitted **solely** on designated domain lifecycle
  coordinators: `RenderExtension`, `PhysicsExtension`, `InputExtension`, `AnnotationExtension`,
  `WorkspaceExtension`, `RevisionExtension`, `NetworkingExtension`, `AudioExtension`.

---

## 2. GPU Render-Unit Suffixes

Choose suffixes strictly by GPU mechanism:

| Suffix            | Mechanism                                                                                     | Examples                                           |
| ----------------- | --------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `…Rasterization`  | GPU unit rasterizing geometry, grids, gizmos, decals, or vector overlays via `.slang` shaders | `GizmoRasterization` · `GridOverlayRasterization`  |
| `…Encoder`        | Batches and encodes direct or indirect GPU draw/dispatch commands                             | `IndirectDrawEncoder`                              |
| `…Sequence`       | Ordered multi-step GPU execution chain                                                        | `PostProcessSequence`                              |
| `…Register`       | Indexed layout or descriptor slot register                                                    | `DescriptorSlotRegister`                           |

🔴 **Viewport Overlays Rule:** Grids, gizmos, debug wireframes, and callouts are **always** GPU
`.slang` `…Rasterization` units — never CPU pixel loops and never ImGui `ImDrawList` primitives.

---

## 3. Non-Kinship Tree & Graph Rule

- **Entire kinship category barred:** Never use `Parent`, `Child`, `Children`, `Sibling`, `Ancestor`,
  `Descendant`, `Orphan`, `kin`, `family`, or `lineage` — and never use `Node`, `Hierarchy`,
  `SceneGraph`, or `Adjacency`.
- Name structural links by their concrete index or memory direction (e.g., `EnclosingIndex`,
  `FirstNestedIndex`, `NextPeerIndex`, `DepthLevel`).

---

## 4. Function & Concept Verb Vocabulary

All functions and C++20 `concept` names use **PascalCase**.
❌ **Barred verbs:** `Make`, `Accept`, `Get`, `Set`, `Handle`, `Manage`, `Process`, `Compose`, `Update`,
`Do`, `Perform`, `Watch`, `Listen`, `Spawn`, `Kill`, `Commit`, `Emit`.

| Domain                    | Approved Frontier Verbs                                                                                      |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Retrieval / inspection    | `Retrieve` · `Resolve` · `Extract` · `Decode` · `Interpret` · `Enumerate` · `Inspect` · `Query` · `Derive`   |
| Lifecycle / configuration | `Initialize` · `Configure` · `Reconfigure` · `Register` · `Activate` · `Suspend` · `Resume` · `Finalize`     |
| Allocation / recycling    | `Issue` · `Authenticate` · `Acquire` · `Reclaim` · `Reset` · `Invalidate`                                    |
| Geometry / math           | `Construct` · `Assemble` · `Partition` · `Subdivide` · `Collapse` · `Solve` · `Evaluate` · `Project` · `Align` |
| Routing / encoding        | `Route` · `Intake` · `Exchange` · `Broadcast` · `Compile` · `Encode` · `Serialize`                           |
| Verification              | `Validate` · `Verify` · `Assert` · `Enforce` · `Constrain`                                                   |

Lifecycle sequence is always **Initialize → Tick → Finalize**.

---

## 5. Variable & Boolean Naming

- **PascalCase everywhere:** local variables, parameters, struct fields, template parameters, and `constexpr` constants.
- **No shorthand or Hungarian prefixes:** never `kMin`, `m_count`, `s_inst`, `tmp`, `val`, `idx`, `ptr`. Write `MinimumExtent`, `RegisteredSlotCount`, `ProjectedCoordinate`.
- **No type-name mirroring:** ❌ `WorkspaceToken workspaceToken` → ✔️ `WorkspaceToken ActiveDocumentToken`.
- **Booleans & Bitmask Directives:**
  - **NEVER** prefix booleans, queries, or bitmask options with `Is*`, `Has*`, `Was*`, `Can*`, `Should*`, `On*`, `Accept*`, or prepositional phrases like `CenteredOnMonitor` / `FocusOnShow` (and never use the banned words `flag`/`flags`, `state`, or `value`).
  - **Bitmask option enumerators (`…AttributeMask` / `…Directives`)** MUST start with imperative verbs **`Enable*`**, **`Toggle*`**, or **`Assign*`** (`EnableBorderDecoration`, `EnableInteractiveResize`, `AssignMonitorCenter`, `EnableTopmostOrder`, `AssignInitialFocus`, `EnableExternalDrop`).
  - **Booleans** describe the property directly (`VisibilityEnabled`, `CaptureActive`, `EditorIncluded`, `RepeatSuppressed`), and boolean functions use `Query*` or `Verify*` (`QueryAttributeEnabled`, `VerifyDockTransferPermitted`).
- **Modular Reuse:** design GPU and UI structures once to serve arbitrary instance counts via per-call parameters/push-constants rather than hardcoding single-purpose types.
