# PLAN — Parametric Sketch Outliner Subsystem

Clean reconstruction of the CAD/Parametric-Sketch Outliner for Project Frontier. Retired code is a
functional reference only — nothing is copied. The subsystem is layered **on top of the existing
`SceneDirectory` pillar**, not a duplicate tree.

Target executable: `Binaries\ParametricSketcher\ParametricSketcher.exe` (validation host).
Foundation exe `SceneDirectory.exe` is never overwritten.

**Backend (corrected 2026-07-27): Vulkan, not D3D11.** The SceneDirectory validation host uses
Win32 + D3D11; the ParametricSketcher is a real editor and per CLAUDE.md "the editors run on native
Vulkan". It reuses the existing `VulkanHost` + `VulkanImguiInterface` + `WindowSubstrate` +
`ImguiPlatformRelay` stack (RenderExtension pillar). Icons upload via a `VkImage` +
`ImGui_ImplVulkan_AddTexture(sampler, view, layout)` → `VkDescriptorSet` cast to `ImTextureID` — NOT
`ID3D11ShaderResourceView`. `SvgIconRegistry` takes `VulkanHost&` by reference and owns the
`VkImage`/`VkImageView`/`VkSampler` + descriptor set per icon. `SvgRasterizer` is backend-agnostic
(pure thorvg software raster) and unaffected by the backend choice.

---

## 1. Naming (locked with the user)

| Concern | Decision | Why |
|---|---|---|
| Workspace vocabulary | **`Sketch` / `ParametricSketch`** | Grounded in the target `ParametricSketcher.exe`. SKILL-Naming §2 bars `Cad`/`CAD` for workspace-level names (kernel folder `Authoring/Modeling/Cad/` keeps its name). Not `Drafting` — the exe fixes the word. |
| Tree item | **reuse pillar `RecordEntry`** | `RecordEntry` already carries `RecordClassification` + `ProfileToken` + `PresentationCategory`. `Node`/`Entity`/`Element` are banned. |
| Tree container | **reuse pillar `SceneDirectory`** | Already provides `SpawnInto` / `RelocateEntry` (cycle + depth guarded) / `TeardownSubtree` / `EvaluateEntryDepth`; `AdjacencyTable::FlattenSubtree` gives canonical draw order. Zero duplicate machinery. |

Workspace-level identifiers: `SketchDocument`, `SketchOutlinerPanel`, `SketchClassification`,
`SketchClassificationRegistry`, `SketchProfileStore`, `OutlinerPresentation`. Icon tiers keep `g-`
(global, shared across outliners) and `cad-` (CAD-kernel geometry) prefixes.

---

## 2. What the pillar already gives us (do NOT reinvent)

- `RecordEntry` — identity (`SelfToken`), connectivity (`EnclosureToken`, `NestedRegionToken`,
  `LateralSuccessor/PredecessorToken`), `DepthTier`, `OrderKey`, `Title`, `LocalPlacement`,
  `RecordClassification Classification` (runtime index into a registry the consumer owns),
  `RecordToken ProfileToken` (domain payload hook), `PresentationCategory Presentation` bits,
  `RuntimeCondition`.
- `RecordArchive` — dense SoA store (`Entries`/`Generations`/`Occupancy` + `VacancyTable`), token
  identity, `SpawnEntry`/`DespawnEntry`, `ReserveArchive`.
- `SceneDirectory` — `InitializeDirectory`, `FinalizeDirectory`, `SpawnInto`, `EvaluateEntryDepth`,
  `RelocateEntry` → `RelocationOutcome`, `TeardownSubtree`.
- `AdjacencyTable` — `InsertSite`/`InsertPlacement`, `AttachIntoEnclosure`, `DetachFromEnclosure`,
  `CollectNested`, `EnclosedBy`, `FlattenSubtree` → `std::vector<FlattenedEntry>{Entry, IndentDepth}`.
- `TokenAuthentication` — `ResolveEntry(Archive, Token)` → `RecordEntry*` / `nullptr`.
- `DiagnosticArchive` — `ISSUE_TRACE/NOTICE/CAUTION/FAULT(Tag, fmt, …)`, tag = channel string.

**Net-new** (nothing exists): the CAD classification pack + registry, the per-classification profile
payload store, the parenting-rule matrix, the SVG icon rasterizer + registry (thorvg → VkImage),
the presentation view-model, the panel, and the validation host.

---

## 3. Folder / file layout (user approved "yes like that")

```text
Internal/EngineContext/Interface/Icons/
    SvgRasterizer.{h,cpp}          thorvg → RGBA8 raster at a requested pixel size
    SvgIconRegistry.{h,cpp}        RGBA8 → VkImage → ImTextureID, content-hash cached; g-/cad- tiers
    IconPackGlobal.{h,cpp}         registers the g-* tier (chrome/system) into the registry
    IconPackCad.{h,cpp}            registers the cad-* tier (sketch geometry/constraints/dims) into the registry

Internal/EngineContext/Interface/Workspaces/SketchOutliner/
    SketchClassification.{h,cpp}   catalogue of CAD classifications + descriptor (label, icon key, tint)
    SketchClassificationRegistry.{h,cpp}  runtime registry; hands out RecordClassification indices
    ParentingRuleMatrix.{h,cpp}    allowed-enclosure guard (which classification nests under which)
    SketchProfileStore.{h,cpp}     per-classification …Profile payloads keyed by ProfileToken
    SketchDocument.{h,cpp}         SceneDirectory + registry + profile store composed; CAD spawn/relocate API
    SketchDocumentFixture.{h,cpp}  builds the canonical demo tree for validation
    OutlinerPresentation.{h,cpp}   view-model: cached FlattenSubtree, selection set, filter/search state

Executables/Validation/SketchOutliner/
    SketchOutlinerHost.cpp         Vulkan (VulkanHost + VulkanImguiInterface + WindowSubstrate) + ImGui host
    SketchOutlinerValidationPanel.{h,cpp}  drives SketchDocument + OutlinerPresentation
    Build.bat                      → Binaries\ParametricSketcher\ParametricSketcher.exe
```

Reuse existing `Scene/` + `MicroUtils/` verbatim. No pillar file is modified except (if needed) a
new consumer include.

---

## 4. Logging channels (per directive)

Tag string passed to `ISSUE_*` is the channel. Microsecond timestamps handled by the archive sink.

| Channel | Emits |
|---|---|
| `[CAD_OUTLINER]` | panel lifecycle, row build counts, selection events, filter/search changes |
| `[ENTITY_TREE]` | spawn / relocate (with `RelocationOutcome`) / teardown, flatten cache rebuilds |
| `[PARAMETRIC_SOLVER]` | constraint/dimension profile updates (stub hooks this phase) |
| `[SVG_RENDERER]` | icon cache miss → rasterize → upload, cache hit lookups, texture eviction |

---

## 5. Phase order

1. **Phase 2 — classification + parenting** *(in progress)*: `SketchClassification` catalogue,
   `SketchClassificationRegistry`, `ParentingRuleMatrix`. Pure logic, no GPU — unit-testable first.
2. **Phase 3 — icons**: `SvgRasterizer` (thorvg), `SvgIconRegistry` (Vulkan upload + hash cache),
   `IconPackGlobal`, `IconPackCad`. Icon SVGs sourced from the approved `zip` set + the fixed
   IconGallery.html designs.
3. **Phase 4a — document + view-model**: `SketchProfileStore`, `SketchDocument`,
   `SketchDocumentFixture`, `OutlinerPresentation`.
4. **Phase 4b — panel**: `SketchOutlinerPanel` (rows, twisty ease, icons, eye toggle, rename,
   multi-select, drag-reparent via `RelocateEntry`, search/filter chips).
5. **Phase 5 — validation exe**: host + panel + `Build.bat` → `ParametricSketcher.exe`; run and
   confirm it builds without touching `SceneDirectory.exe`.

---

## 6. Data-oriented / zero-bloat commitments

- No per-item heap link nodes — connectivity is intrusive tokens on `RecordEntry` (already so).
- Classification is a `uint32_t` index; descriptors live once in the registry, not per entry.
- Domain payload hangs off `ProfileToken` in a dense `SketchProfileStore`, so `RecordEntry`
  footprint never grows as CAD classifications are added.
- `OutlinerPresentation` caches the flattened draw order and rebuilds only on a tree-revision bump,
  not per frame.
- Icon textures are content-hash cached; identical SVGs at the same pixel size upload once.
