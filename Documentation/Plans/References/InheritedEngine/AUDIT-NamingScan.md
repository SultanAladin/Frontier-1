# AUDIT-NamingScan

Banned-name scan of `Internal/` (697 files) against `EngineDocs/AgenticInstuctions/SKILL-Naming.md`,
run 2026-08-06. **TL;DR:** `Internal/` is clean of `System`/`Core`/`Hierarchy`/`Subsystem`/
`Entity`/`Kind`/`Manager`/`Kernel` identifiers (those words appear only in "never use X" comments),
but carries real identifier violations in the kinship (`Parent`/`Child`), `Node`, `Element`,
`Object`, `Item`, `Data`, `Base`, `Mesh`, `Shell`, `Frame`, unapproved-`Record`, and `Pass`/
`Inscription`/`Stage` classes.

---

## 1. Banned-word master table (from `SKILL-Naming.md`)

| Category | Banned words |
|---|---|
| Sci-fi / power-trip | `Apex Sentinel Vanguard Oracle Titan Goliath` |
| Center-of-universe | `Nexus Hub Maestro Core Matrix Catalyst Conductor` |
| Pseudo-space / myth | `Vortex Aether Zenith Nova Horizon Genesis Odyssey Helios Orion Kronos` |
| Generic / AI tropes | `Manager Handler Dispatcher Processor Controller Service Utility Helper Representation Traversal* Kernel Graph Record† Frame Node Module Data Info Object Stuff Thing Item Common Base` |
| Job titles | `Manager Worker Provider Dispatcher Conductor Listener Observer` |
| Kinship (all barred) | `Parent Child Children Sibling Ancestor Descendant lineage family kin Orphan` |
| State-change verbs | `Commit`(→Finalize) · `Compose`(→Stack/Order) |
| Variable words | `flag state value` |
| 2026-06-20 | `system compose mesh harness shell popover Tween Kind proof glide binding Role` |
| 2026-07-15 | `stage` |
| 2026-07-25 | `Hierarchy Entity Element Subsystem Designation Trial` |
| 2026-07-25 | `backend` |
| 2026-07-26 | `Pass Emission Emit` |
| 2026-08-02 | `Inscription` |
| Verb blacklist (§6) | `Get Set Handle Manage Process Assign Sanitize Declare Update Check Do Perform Draw Delegate Watch Listen Spawn Kill Commission Decommission Emit` |

Exempt / approved: `SelectionTraversal`, `SolidShell`, `CoordinateSystem`/`TransformSystem`,
`RecordEntry`/`RecordStore`/`RecordToken`/`RecordClassification`, `…State` suffix
(`ActivationState`, `FinalizedState`, `FoldStateMemory`).

---

## 2. Raw occurrence counts (case-insensitive, all text — comments included)

| Word | Count | Word | Count | Word | Count |
|---|---:|---|---:|---|---:|
| state | 2188 | pass | 1026 | frame | 1115 |
| set (verb) | 1301 | record | 824 | value | 603 |
| binding | 485 | inscription | 441 | draw (verb) | 542 |
| mesh | 344 | object | 342 | node | 256 |
| stage | 213 | child | 171 | parent | 55 |
| item | 123 | matrix | 129 | family | 113 |
| base | 404 | data | 261 | module | 197 |
| handle | 409 | kernel | 61 | apex | 68 |
| thing | 51 | common | 26 | graph | 17 |
| info | 7 | spawn | 111 | emit | 33 |
| shell | 58 | backend | 80 | element | 69 |
| commit | 30 | flag | 96 | observer | 41 |
| sibling | 28 | worker | 24 | helper | 33 |
| system | 10 | entity | 22 | kind | 56 |

---

## 3. Identifier-level violations (the actionable findings)

| Banned | Violating identifiers | Representative files | Severity |
|---|---|---|---|
| `Parent`/`Child` | `ArenaParent` `TreeParent` `FirstChild` `LeftChild` `RightChild` `FarChild` `NearChild` `BeginChild` `EndChild` `ReplaceChild` `ChildBase` `GeometryTreeNoParent` `ActiveSectionIsChild` | `Graphics/Acceleration/GeometryTreeBuild.cpp`, `Graphics/Acceleration/Shaders/InstanceTreeRefit.comp`, `Authoring/Modeling/Draughting/DraughtBoolean.cpp`, `Authoring/ParametricAuthoring/Operations/Boolean/ParametricSketchBoolean.cpp` | 🔴 |
| `Node` | `ArenaNode` `BlockNode` `TreeNode` `LeafNode` `ObjectNode` `BuildNode` `EnclosingNode` `…WordsPerNode` | `EngineContext/Scene/RecordEntry.h`, `Graphics/Acceleration/GeometryTreeBuild.cpp`, `Graphics/Acceleration/Shaders/TwoLevelTrace.glsl`, `EngineContext/Interface/WorkspaceHost/SketchOutliner/SketchOutlinerPanel.h`, `Authoring/Geometry/Interchange/InterchangeGltfDecoder.cpp` | 🔴 |
| `Mesh` | `DrawVisibilityMesh` `BoundMesh` `SourceMesh` `GlyphMesh` `TranslateMesh` `SuzanneMesh` `fastObjMesh` | `Graphics/Visibility/VisibilityRasterization.h/.cpp`, `EngineContext/Interface/WorkspaceHost/SketchOutliner/SceneContentProfile.cpp`, `Graphics/Render/Surface/SurfaceShadeInscription.cpp` | 🔴 |
| `Shell` | `OpenShell` `LinkedShell` `MultipleShell` `SeparateShell` `ToolCardShell` `PaintCardShell` `ReduceCellsToOuterShell` | `Authoring/ParametricAuthoring/ParametricSketchShapeStore.cpp`, `EngineContext/Interface/Components/Menus/ToolCard/ToolCardShell.cpp`, `EngineContext/Interface/Components/Menus/PolygonMutation/SelectionPredicate.h` (`SolidShell` exempt) | 🔴 |
| `Element` | `RayElement` `GpuRayElement` `RayBoundaryElement` `Element` `VolumeBoundsElementCount` `InstanceBoundsElementCount` | `Graphics/Surfel/SurfelIrradianceSubmission.cpp`, `Graphics/Acceleration/VolumeBoundsSubmission.h/.cpp`, `Graphics/Acceleration/InstanceBoundsSubmission.h/.cpp`, `EngineContext/Interface/WorkspaceHost/SketchOutliner/SketchOutlinerPanel.h` | 🔴 |
| `Object` | `FaceObject` `ModeObject` `AppendObject` `TranslateObject` `RegisterAuthoredObject` `StratumBadgeObject` `WorkspaceObject` `ObjectPickReadback` | `Graphics/RenderExtension/RenderExtension.*`, `Graphics/Visibility/ComponentOverlayInscription.*`, `EngineContext/Scene/WorkspaceDocumentDecoder.cpp`, `EngineContext/Scene/WorkspaceDocumentRegister.cpp` | 🔴 |
| `Item` | `MenuItem` `ActionToolItem` `CarouselItem` `ConstructDropdownItem` | `EngineContext/Interface/WorkspaceHost/SketchOutliner/SketchOutlinerPanel.cpp`, `EngineContext/Interface/Components/Cards/ContentCarousel.cpp`, `EngineContext/Interface/Components/Cards/ActionToolbar.cpp`, `EngineContext/Interface/Components/Controls/MenuPill.cpp` | 🔴 |
| `Data` | `CallbackData` `DrawData` `FrameData` `MessageData` `NativeMessageData` `getVertexData` `prepareShadingData` | `Platform/Windowing/PlatformWindowWayland.cpp`, `Graphics/RenderExtension/Device/VulkanImguiInterface.*`, `EngineContext/Interface/Workspaces/TexturePaint/LayerStackPanel.cpp` | 🔴 |
| `Base` | `Base` `BlockBase` `ChildBase` `FaceBase` `FloorBase` `FloorIndexBase` `HeaderBase` `VertexBase` `PBase` `MieScatteringBase` | `Graphics/HierarchicalDepth/Shaders/HierarchicalDepthReduce.comp`, `Graphics/Acceleration/Shaders/RadixBlockBaseAdd.comp`, `EngineContext/SpatialAcceleration/ToroidalClipmapField.h` | 🔴 |
| `Helper` | `const vec3 Helper` | `Graphics/Surfel/Shaders/SurfelSampling.glsl:107` | 🔴 |
| `Worker` | `WorkerPool` `WorkerCount` | `Platform/Concurrency/WorkerPool.h` | 🔴 |
| `Frame` | `BeginImguiFrame` `AddMenuOpenedFrame` `EvaluateProjectionFrame` `SolveWorkplaneFrame` `SeatPrincipalFrame` `ResetInstanceCullFrame` `ShadowFrame` `TintFrame` `FrameGraph` | `EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.*`, `Authoring/ParametricAuthoring/Workplane.*`, `EngineContext/Interface/Instrumentation/*` (⚠️ "frame" as a coordinate basis in `Workplane` is legit math usage, still on blacklist) | 🚩 |
| `Record` (unapproved composites) | `PartitionCullRecord` `SurfelRecycleRecord` `ClipmapCellRecord` `CullRecord` `FaceRecord` `VertexRecord` `HeadRecord` `TerminusRecord` + local `Record`/`Records` | `Graphics/Visibility/SurfacePartition.h/.cpp`, `Graphics/Surfel/SurfelTypes.h`, `Graphics/Surfel/SurfelStore.h/.cpp`, `Graphics/Visibility/InstanceCullSubmission.cpp` (only `…Entry`/`…Store`/`…Token`/`…Classification` are approved) | 🚩 |
| `Pass` (new code) | `RecordPass` `ResolvePass` `LiveReadoutPass` `FloatingPass` `ExactPass` `NarrowPass` `FloodPass` `DispatchComputePass` `GroundGridPass` `SkyAtmospherePass` | `Graphics/Grid/GroundGridPass.*`, `Graphics/Atmosphere/SkyAtmosphere.*`, `Graphics/Surfel/*Submission.*` — known **deferred renames** (`…Pass`→`…Rasterization`) | 🚩 |
| `Inscription` | `VisibilityInscription` `SurfaceShadeInscription` `SelectionOutlineInscription` `ComponentOverlayInscription` `ParametricSketchSurfaceInscription` | `Graphics/Visibility/*Inscription.*`, `Graphics/Render/Surface/ParametricSketchSurfaceInscription.*` — known **deferred renames** (2026-08-02) | 🚩 |
| `Stage` | `ColourSourceStage` `DestinationStage` `SrcStage` `DstStage` `WaitStage` `Stage` | `Graphics/Atmosphere/SkyAtmosphere.cpp:356`, `Graphics/Render/Resources/BufferAllocation.cpp`, `EngineContext/Interface/Icons/SvgIconRegistry.cpp` | 🚩 |
| `Matrix` | `WorldMatrix` `ViewMatrix` `NormalMatrix` `ComposeModelMatrix` `ComposeInverseModelMatrix` `drawMatrix` | `EngineContext/Navigation/Camera/CameraProjection/*`, `Authoring/*` (`Compose` + `draw` are banned verbs too) | 🚩 |
| `Apex` | `Apex` `PointApex` | `EngineContext/Interface/Components/Controls/MenuPill.cpp:56`, `Authoring/Geometry/Modeling/Primitives/PrimitiveShape.cpp`, `Authoring/Modeling/Draughting/DraughtLoft.cpp` (⚠️ cone-tip geometry is legit math usage, still on blacklist) | ✔️ |
| `Info` | `AllocInfo` `DeviceInfo` `BufferInfo` `SwapchainInfo` `VertexInfo` `…` | `Graphics/RenderExtension/Device/VulkanHost.cpp`, `Graphics/RenderExtension/Device/VulkanImguiInterface.*` (`Vk…Info`/ImGui names are third-party — keep; the internal `…Info` structs violate) | ✔️ |

---

## 4. Clean / no violations

`Entity`, `Kind`, `System`, `Core`, `Hierarchy`, `Subsystem`, `Trial`, `Emission`, `Backend`,
`Manager`, `Kernel`, `Controller`, `Processor` — present only in "never use X" comments or `.md`
docs. Sole `Hierarchy`-derived name is the folder `Graphics/HierarchicalDepth/`.

Renames already in place (explicitly cited in comments, e.g. `PolygonInset.h:33` "replaces the
banned `Kind`"): the `…Category` enum-suffix migration is done.

## 5. Notes

- The `…Pass` / `…Inscription` sets are listed in `SKILL-Naming.md` §2 as **user-owned deferred
  renames** — do not touch without direction.
- Third-party identifiers (`ImGui_*`, `Vk*CreateInfo`, `ImDrawData`, `XFreeEventData`,
  `requestAnimationFrame`, `QuartzCore`) are excluded.
- "frame" in `SolveWorkplaneFrame`/`EvaluateProjectionFrame` means a coordinate basis (math), not
  the generic-programmer `Frame`; still on the blacklist by the letter of the skill.
- Scan method: ripgrep 15 with PCRE2, `\b` word boundaries, case-insensitive counts; identifier
  scan case-sensitive over PascalCase forms. Verbally, "boundary" was excluded — it is approved
  (`MinimumBoundary`/`MaximumBoundary`).
