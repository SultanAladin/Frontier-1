# 📝 `Documentation/Plans/` — Architectural Plans, Reports & Markdown Studio

> **TL;DR**: `Documentation/Plans/` stores all architectural specifications, audit reports, live session notes (`SessionNotes.md`), and the compiled browser viewer (`MarkdownStudioViewer.html`, rebuilt via `python3 Automation/BuildMarkdownViewer.py` and served by `Automation/ServeMarkdownStudio.py`).

---

## 🧩 1. Architecture & System Plans

| Plan Document | Domain |
| :--- | :--- |
| [GameEngineBringupPlan.md](GameEngineBringupPlan.md) | Overall engine bringup roadmap & subsystem ordering |
| [ThreadingArchitecturePlan.md](ThreadingArchitecturePlan.md) | Priority-budgeted `ExecutionChannels`, `TaskScheduler` & `WorkspaceThread` |
| [DocumentFormatsInstancePlan.md](DocumentFormatsInstancePlan.md) | Two-Tier Hybrid `.Space` codec registry & non-kinship instances |
| [DocumentSpacesRevisionPlan.md](DocumentSpacesRevisionPlan.md) | Confirmed `.Space` codecs, `215`/`750 MiB` revision DAG & workspace tree |
| [ContentBrowserArchitecturePlan.md](ContentBrowserArchitecturePlan.md) | Pre-baked `ThumbnailSheet`, streaming, quarantine import & UV masks |
| [ColourSpaceArchitecturePlan.md](ColourSpaceArchitecturePlan.md) | Tristimulus color spaces, spectral distributions & tone transforms |
| [RenderingRaytracingArchitecturePlan.md](RenderingRaytracingArchitecturePlan.md) | Dual-tier GTX Hybrid & Deterministic RTX architecture |
| [WorldRenderEngineUnificationPlan.md](WorldRenderEngineUnificationPlan.md) | Shared Cornell-Studio World renderer & SDF Clipmap GI unification (historical) |
| [WorldGiShadowPathTracerPlan.md](WorldGiShadowPathTracerPlan.md) | Proposed real-mesh GTX GI, shadows, and deterministic GTX/RTX path tracer |
| [MaterialVisibilityRenderingBringupPlan.md](MaterialVisibilityRenderingBringupPlan.md) | 10-lobe `MaterialSpace`, 64-bit VisBuffer & rendering bringup |
| [DynamicMaterialSystemPlan.md](DynamicMaterialSystemPlan.md) | Time-varying procedural/transition OpenPBR material compiler |
| [DeterministicPathTracerVulkanOptimizationPlan.md](DeterministicPathTracerVulkanOptimizationPlan.md) | Vulkan compute optimization for deterministic path tracing |
| [CelestialSkyCloudsSystemPlan.md](CelestialSkyCloudsSystemPlan.md) | Physical sky, atmosphere & cloud scattering plan |
| [NetworkingPolicyBringupPlan.md](NetworkingPolicyBringupPlan.md) | UDP/TCP socket transport & snapshot replication policy |
| [RemediationInputCameraPlan.md](RemediationInputCameraPlan.md) | Input exchange & orbit/fly camera remediation |

---

## 🧩 2. Workspace Plans & Completion Reports

| Plan / Report | Workspace |
| :--- | :--- |
| [ProjectBrowserWorkspacePlan.md](ProjectBrowserWorkspacePlan.md) · [ProjectBrowserWorkspaceImplementationReport.md](ProjectBrowserWorkspaceImplementationReport.md) | `ProjectBrowser` |
| [UVWorkspaceArchitecturePlan.md](UVWorkspaceArchitecturePlan.md) · [UVWorkspaceHtmlPortProgress.md](UVWorkspaceHtmlPortProgress.md) · [UVWorkspaceSyncViewportReport.md](UVWorkspaceSyncViewportReport.md) · [UVWorkspaceFullCompletionReport.md](UVWorkspaceFullCompletionReport.md) | `UVWorkspace` |
| [VectorGraphicsWorkspacePlan.md](VectorGraphicsWorkspacePlan.md) · [BakingHardwareClusterIconPlan.md](BakingHardwareClusterIconPlan.md) | `PatternWorkspace` · `StampWorkspace` · `BakingWorkspace` |
| [FractureWorkspaceImplementationPlan.md](FractureWorkspaceImplementationPlan.md) | `FractureWorkspace` |
| [ParametricSketcherIntegrationPlan.md](ParametricSketcherIntegrationPlan.md) · [ParametricSketcherOverhaulPlan.md](ParametricSketcherOverhaulPlan.md) | `ParametricSketcher` |
| [ParticleSpaceModularPortPlan.md](ParticleSpaceModularPortPlan.md) · [ParticleRendererDerezMaterialTexturePlan.md](ParticleRendererDerezMaterialTexturePlan.md) | `ParticleWorkspace` |
| [VectorFieldArchitecturePlan.md](VectorFieldArchitecturePlan.md) · [VectorFieldVulkanConversionPlan.md](VectorFieldVulkanConversionPlan.md) | `ForceFieldWorkspace` |
| [ComponentEditorArchitecturePlan.md](ComponentEditorArchitecturePlan.md) | `ComponentEditor` |

---

## 🔍 3. Audits, Notes & Studio Viewer

| File | Purpose |
| :--- | :--- |
| [AuditRemediationReport.md](AuditRemediationReport.md) | Cross-repo audit findings & remediation log |
| [GeometryCompilerFindingsReport.md](GeometryCompilerFindingsReport.md) | Geometry compiler audit & diagnostic findings |
| [DeterministicPathTracerAuditReport.md](DeterministicPathTracerAuditReport.md) | Path tracer shader & convergence audit |
| [VehicleHmiRendererReflectionReport.md](VehicleHmiRendererReflectionReport.md) | Vehicle HMI renderer reflection & parity notes |
| [Note-1608.md](Note-1608.md) | Engineering note 1608 |
| [SessionNotes.md](SessionNotes.md) | Live Markdown Studio session notepad |
| `MarkdownStudioViewer.html` | Self-contained HTML viewer rebuilt via `python3 Automation/BuildMarkdownViewer.py` |
