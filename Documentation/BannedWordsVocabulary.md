# Frontier Banned Words & Naming Vocabulary

> **Enforcement Scope**: Blacklisted **everywhere** — C++ and `.slang` shader identifiers (types, structs, classes, functions, parameters, local/member variables, namespaces, macros, enums), file names, folder names, and comment concepts.
>
> **Core Principle**: Banned words are **semantic voids that hide what code actually does**. This blacklist is **NOT exhaustive** — apply the `§0` naming gates in [AgenticInstructions/SKILL-Naming.md](AgenticInstructions/SKILL-Naming.md) to every identifier even when a candidate word does not appear below.

Related governance documents: [FolderStructure.md](FolderStructure.md) · [CodingStandards.md](CodingStandards.md) · [../README.md](../README.md)

---

## 1. Categorized Banned Vocabulary

### 1.1 Primary Blacklist

- `action`
- `adjacency`
- `affinity`
- `arena`
- `badge`
- `binding`
- `bloom`
- `cadence`
- `card`
- `carousel`
- `classification`
- `concurrency` / `concurrence` / `concurrent`
- `context`
- `continuum`
- `contract`
- `coverage`
- `dev`
- `element`
- `emitter`
- `evaluator`
- `extension` *(strictly restricted: permitted **only** for designated lifecycle coordinators)*
- `fence`
- `floating`
- `flow`
- `frame`
- `glyph`
- `graph`
- `hook`
- `hub`
- `inscription`
- `interpolant`
- `item`
- `listener`
- `make` *(verb/prefix barred: use `Construct`)*
- `map`
- `member`
- `menu`
- `mesh`
- `mutation`
- `nexus`
- `occupant`
- `palette`
- `pass`
- `pill`
- `pipeline`
- `pool`
- `popup` / `popups`
- `predicate`
- `probe`
- `reclamation`
- `reservoir`
- `stage`
- `store`
- `submission`
- `table`
- `vacancy`

### 1.2 Thematic & Structural Trope Categories

| Category | Banned Terms | Notes & Guidance |
| :--- | :--- | :--- |
| **Sci-fi / Power-trip** | `Apex`, `Sentinel`, `Vanguard`, `Oracle`, `Titan`, `Goliath` | Grandiose metaphors obscure concrete mechanical behavior. |
| **Center-of-universe** | `Nexus`, `Hub`, `Maestro`, `Core`, `Matrix`, `Catalyst`, `Conductor` | Encourages god-objects and vague central bottlenecks. |
| **Pseudo-space / Mythology** | `Vortex`, `Aether`, `Zenith`, `Nova`, `Horizon`, `Genesis`, `Odyssey`, `Helios`, `Orion`, `Kronos` | Mythological and cosmic labels convey zero architectural meaning. |
| **Generic-programmer / AI tropes** | `Manager`, `Handler`, `Dispatcher`, `Processor`, `Controller`, `Service`, `Utility`, `Helper`, `Representation`, `Traversal`\*, `Kernel`, `Graph`, `Record`†, `Frame`, `Node`, `Module`, `Data`, `Info`, `Object`, `Stuff`, `Thing`, `Item`, `Common`, `Base` | Default placeholder nouns that hide ownership, memory layout, and exact transformation. |
| **Human job titles** | `Manager`, `Worker`, `Provider`, `Dispatcher`, `Conductor`, `Listener`, `Observer` | Barred unless a strict, canonical named design pattern is genuinely in use. |
| **Kinship / Family** *(whole category barred)* | `Parent`, `Child`, `Children`, `Sibling`, `Ancestor`, `Descendant`, `lineage`, `family`, `kin`, `Orphan` | **Entire category barred.** Name tree/graph links by concrete memory/index direction (`EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex`, `DepthLevel`). |
| **State-change & factory verbs** | `Commit`, `Compose` *(verb AND noun)*, `Make`, `Accept` | `Commit` → `Finalize` / `Enforce` / `Align` / `Resolve`.<br>`Compose` (verb and noun) → `Stack` / `Order`.<br>`Make` → `Construct` (`ConstructSplashWindow`, `ConstructApplicationWindow`).<br>`Accept` → `Intake` / `Enable` (`IntakeSignalPacket`, `EnableExternalDrop`). |
| **Boolean & bitmask prefixes** | `Is*`, `Has*`, `Was*`, `Can*`, `Should*`, `On*`, `*On*` *(e.g. `CenteredOnMonitor`, `FocusOnShow`)* | Name bitmask options with imperative directives **`Enable*`**, **`Toggle*`**, or **`Assign*`** (`EnableBorderDecoration`, `AssignMonitorCenter`, `AssignInitialFocus`, `TogglePanelVisibility`). Name booleans directly (`VisibilityEnabled`, `Minimized`, `Maximized`). |
| **Banned variable words** | `flag`, `flags`, `state`, `value` | Describe what the variable actually holds (`WindowAttributeMask`, `AttributeMask`, `PresentationMode`, `ScalarReading`). |

*\* `Traversal` is exempt **only** at `Topology/Selection/SelectionTraversal.{h,cpp}` (explicit user direction).*
*† Bare `Record` is banned; composite `RecordEntry` and `RecordToken` are approved (`RecordStore` is barred because `store` is banned).*

---

## 2. Approved Replacements & Naming Directives

When replacing or avoiding a banned concept, name the **concrete layout, index link, or hardware mechanism**:

| Banned Term / Concept | Approved Frontier Mechanism / Replacement |
| :--- | :--- |
| `Commit` | `Finalize`, `Enforce`, `Align`, or `Resolve` |
| `Make` / `Make*` | `Construct` / `Construct*` (`ConstructSplashWindow`, `ConstructApplicationWindow`, `ConstructGameWindow`) |
| `Is*`, `Has*`, `Was*`, `On*`, `Accept*`, `CenteredOnMonitor` | Bitmask directives use **`Enable*`**, **`Toggle*`**, or **`Assign*`** (`EnableBorderDecoration`, `EnableInteractiveResize`, `AssignMonitorCenter`, `AssignInitialFocus`); boolean queries use `Query*` / `Verify*` |
| `Compose` / `Composite` (verb or noun) | `Stack` or `Order` (e.g., `RadiometricToneStack`) |
| `Parent` / `Child` / `Sibling` / `Orphan` / kinship | Technical memory/index link terminology (`EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex`, `DepthLevel`) |
| `flag`, `state`, `value` | Concrete domain name describing the exact condition, quantity, or payload held |
| `harness`, `Trial`, `Trials` | `…Validation` (test scaffolds are validations, never harnesses or courtroom metaphors) |
| `backend` | Concrete hardware/OS interface name (`RenderDeviceInterface`, `AudioHardwareOutput`, `PlatformWindow*`) |
| `stage`, `Pass`, `Emission`, `Emit`, `submission` | Mechanism-specific GPU render-unit suffix (`…Rasterization`, `…Sequence`, `…Encoder`) |
| `Inscription` (composite-onto-target GPU unit) | `…Rasterization` (rasterizes geometry/decals/overlays into the target) |
| `bloom` | Physical optical glare / luminance scatter terminology (`VeilingGlareFilter`, `LuminanceGlareDownsample`) |
| `Hub` | Designated `…Extension` when acting as a domain lifecycle coordinator (e.g., `AnnotationExtension`) |
| Bare `Record` | Composite `RecordEntry` or `RecordToken`, or concrete `Register` / `Layout` |

---

## 3. Restricted Terms, Exemptions & Discouraged Words

### 3.1 Narrow Explicit Exemptions

1. **`Extension` (Strictly Restricted)**:
   Permitted **exclusively** for designated lifecycle coordinators (`RenderExtension`, `PhysicsExtension`, `InputExtension`, `AnnotationExtension`, `WorkspaceExtension`, `RevisionExtension`, `NetworkingExtension`, `AudioExtension`). Barred for general plugins, helpers, or feature add-ons.
2. **`Traversal` (Single-File Exemption)**:
   Exempt **only** at `Topology/Selection/SelectionTraversal.{h,cpp}` per explicit user direction. Barred everywhere else.
3. **`Record` (Composite-Only Exemption)**:
   Bare `Record` is banned. Only `RecordEntry` and `RecordToken` are approved (`RecordStore` is barred).
4. **Named Design Patterns (`Observer`, etc.)**:
   Human job-title nouns remain barred unless a strict, textbook named design pattern is genuinely implemented.

### 3.2 Discouraged Vocabulary (Not Banned — Avoid)

⚠️ The words below are **not** hard-blacklisted, but **avoid them** whenever a direct domain/mechanism noun exists:

| Discouraged Word | Preferred Concrete Alternatives |
| :--- | :--- |
| `atlas` | `ChartPacker`, `Sheet`, `Grid`, `Layout` (e.g., `UVChartPacker`) |
| `slice` | `PackedBits`, `BitRange`, `Subrange`, `std::span` |
| `substrate` | `DeviceInterface`, `HardwareOutput`, `PlatformWindow`, `Exchange` |

---

## 4. Chronological Rulings & Amendments

- **2026-06-20**: Barred `system`, `compose`, `mesh`, `harness`, `shell`, `popover`, `Tween`, `Kind`, `proof`, `glide`, `binding`, `Role`, `Common`.
- **2026-07-15**: Barred `stage` (render jargon).
- **2026-07-25**: Barred `Hierarchy`, `Entity`, `Element`, `Subsystem`, `Designation` (`Designation` was previously reuse-authorized, now barred); `system` re-affirmed barred as any noun; `Trial` / `Trials` barred (was the `harness` proposal — courtroom metaphor) → a test scaffold is a `…Validation`.
- **2026-07-25**: Barred `backend` (web "layer beneath" jargon).
- **2026-07-26**: Barred `Pass` (borrowed from `VkRenderPass`; revokes the old `stage` → `Pass` ruling) and `Emission` / `Emit` (AI-default) → a GPU render unit takes a mechanism suffix.
- **2026-08-02**: Barred `Inscription` (was the approved `Pass` replacement for composite-onto-target units; now barred outright) → a GPU render unit that composites onto an existing target takes `…Rasterization`. New code never uses `Inscription`.
- **2026-11-10**:
  - Barred `bloom` (→ optical glare / luminance scatter such as `VeilingGlareFilter`, `LuminanceGlareDownsample`) and re-affirmed `Hub` barred (`AnnotationHub` → `AnnotationExtension`).
  - Barred `coverage`, `submission`, `adjacency`, `vacancy`, `store` (revoking `RecordStore`), `reclamation`, `predicate`, and `interpolant`.
  - Marked `substrate`, `atlas`, and `slice` as **discouraged (avoid)**.

---

## 5. Consolidated A–Z Quick-Lookup Index

Use this combined index to audit identifiers, filenames, directory names, and comments prior to finalizing changes:

- **A**: `action`, `adjacency`, `Aether`, `affinity`, `Ancestor`, `Apex`, `arena` *(avoid: `atlas`)*
- **B**: `backend`, `badge`, `Base`, `binding`, `bloom`
- **C**: `cadence`, `card`, `carousel`, `Catalyst`, `Child`, `Children`, `classification`, `Commit`, `Common`, `Compose` / `compose`, `concurrence` / `concurrency` / `concurrent`, `Conductor`, `context`, `continuum`, `contract`, `Controller`, `Core`, `coverage`
- **D**: `Data`, `Descendant`, `Designation`, `dev`, `Dispatcher`
- **E**: `Element` / `element`, `Emission` / `Emit` / `emitter`, `Entity`, `evaluator`, `extension` *(restricted — §3.1)*
- **F**: `family`, `fence`, `flag`, `floating`, `flow`, `Frame` / `frame`
- **G**: `Genesis`, `glide`, `glyph`, `Goliath`, `Graph` / `graph`
- **H**: `Handler`, `harness`, `Helios`, `Helper`, `Hierarchy`, `hook`, `Horizon`, `Hub` / `hub`
- **I**: `Info`, `Inscription` / `inscription`, `interpolant`, `Item` / `item`
- **K**: `Kernel`, `kin`, `Kind`, `Kronos`
- **L**: `lineage`, `Listener` / `listener`
- **M**: `Maestro`, `Manager`, `map`, `Matrix`, `member`, `menu`, `Mesh` / `mesh`, `Module`, `mutation`
- **N**: `Nexus` / `nexus`, `Node`, `Nova`
- **O**: `Object`, `Observer` *(restricted — §3.1)*, `occupant`, `Odyssey`, `Oracle`, `Orion`, `Orphan`
- **P**: `palette`, `Parent`, `Pass` / `pass`, `pill`, `pipeline`, `pool`, `popover`, `popup` / `popups`, `predicate`, `probe`, `Processor`, `proof`, `Provider`
- **R**: `reclamation`, `Record` *(bare & `RecordStore` barred — §3.1)*, `Representation`, `reservoir`, `Role`
- **S**: `Sentinel`, `Service`, `shell`, `Sibling`, `stage`, `state`, `store`, `Stuff`, `submission`, `Subsystem`, `system` *(avoid: `slice`, `substrate`)*
- **T**: `table`, `Thing`, `Titan`, `Traversal` *(exempt only at `SelectionTraversal` — §3.1)*, `Trial` / `Trials`, `Tween`
- **U**: `Utility`
- **V**: `vacancy`, `value`, `Vanguard`, `Vortex`
- **W**: `Worker`
- **Z**: `Zenith`
