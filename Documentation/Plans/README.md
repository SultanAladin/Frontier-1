# 📝 `Documentation/Plans/` — Architectural Plans, Reports & Markdown Studio

> **TL;DR**: `Documentation/Plans/` stores active architectural specifications, cleanup/audit reports, superseded specifications for removed domains queued for redo, live session notes (`SessionNotes.md`), and the compiled browser viewer (`MarkdownStudioViewer.html`, rebuilt via `python3 Automation/BuildMarkdownViewer.py` and served by `Automation/ServeMarkdownStudio.py`).

---

## 🧩 1. Active Architecture & Engine Plans

| Plan Document | Domain |
| :--- | :--- |
| [WorldRenderingRedoPlan.md](WorldRenderingRedoPlan.md) | **Priority Redo Master Plan (`2026-11-20`)**: Phase 0 `Engine/` SDF/SDF-baking purge → **(1st)** Standalone Direct PBR (`0` GI, `0` SDF) → **(2nd)** GTX SDF Shadows (`Candidate 1: HybridConeShadow`) + GI (`Candidates 1 + 2: StokesContour + SurfelRadiosity`, Low-End & High-End) → **(3rd)** Unified Deterministic Path Tracer (Games + Scene Rendering via settings) |
| [DocumentationCleanupReport.md](DocumentationCleanupReport.md) | **2026-11-20** governance & documentation cleanup after retiring simplified `Fluid`, `ForceField`, `GI/SDF`, and `Particle` code + automatic HTML parity gate |
| [GameEngineBringupPlan.md](GameEngineBringupPlan.md) | Overall engine bringup roadmap & domain ordering |
| [ThreadingArchitecturePlan.md](ThreadingArchitecturePlan.md) | Priority-budgeted `ExecutionChannels`, `TaskScheduler`, `WorkspaceThread` & `AudioThread` |
| [DocumentFormatsInstancePlan.md](DocumentFormatsInstancePlan.md) | Two-Tier Hybrid `.Space` codec registry & non-kinship instances |
| [DocumentSpacesRevisionPlan.md](DocumentSpacesRevisionPlan.md) | Confirmed `.Space` codecs, `215` / `750 MiB` revision DAG & workspace tree |
| [ContentBrowserArchitecturePlan.md](ContentBrowserArchitecturePlan.md) | Pre-baked `ThumbnailSheet`, streaming, quarantine import & UV masks |
| [ColourSpaceArchitecturePlan.md](ColourSpaceArchitecturePlan.md) | Tristimulus colour spaces, spectral distributions & tone transforms |
| [MaterialVisibilityRenderingBringupPlan.md](MaterialVisibilityRenderingBringupPlan.md) | 10-lobe `MaterialSpace`, 64-bit VisBuffer & direct rendering bringup |
| [DynamicMaterialSystemPlan.md](DynamicMaterialSystemPlan.md) | Time-varying procedural/transition OpenPBR material compiler |
| [CelestialSkyCloudsSystemPlan.md](CelestialSkyCloudsSystemPlan.md) | Physical 3-LUT sky, solar/lunar ephemeris & single-source `PlanetaryClouds` |
| [NetworkingPolicyBringupPlan.md](NetworkingPolicyBringupPlan.md) | UDP/TCP socket transport & snapshot replication policy |
| [RemediationInputCameraPlan.md](RemediationInputCameraPlan.md) | Input exchange & orbit/fly camera remediation |

---

## 🧩 2. Active Workspace Plans & Completion Reports (9 Host Workspaces)

| Plan / Report | Workspace |
| :--- | :--- |
| [ProjectBrowserWorkspacePlan.md](ProjectBrowserWorkspacePlan.md) · [ProjectBrowserWorkspaceImplementationReport.md](ProjectBrowserWorkspaceImplementationReport.md) | `ProjectBrowser` |
| [UVWorkspaceArchitecturePlan.md](UVWorkspaceArchitecturePlan.md) · [UVWorkspaceHtmlPortProgress.md](UVWorkspaceHtmlPortProgress.md) · [UVWorkspaceSyncViewportReport.md](UVWorkspaceSyncViewportReport.md) · [UVWorkspaceFullCompletionReport.md](UVWorkspaceFullCompletionReport.md) | `UVWorkspace` |
| [VectorGraphicsWorkspacePlan.md](VectorGraphicsWorkspacePlan.md) · [BakingHardwareClusterIconPlan.md](BakingHardwareClusterIconPlan.md) | `PatternWorkspace` · `StampWorkspace` · `BakingWorkspace` |
| [FractureWorkspaceImplementationPlan.md](FractureWorkspaceImplementationPlan.md) | `FractureWorkspace` |
| [ParametricSketcherIntegrationPlan.md](ParametricSketcherIntegrationPlan.md) · [ParametricSketcherOverhaulPlan.md](ParametricSketcherOverhaulPlan.md) | `SketcherWorkspace` (`ParametricSketcher`) |
| [ComponentEditorArchitecturePlan.md](ComponentEditorArchitecturePlan.md) | `ComponentEditor` |

---

## 🚧 3. Superseded Plans — Queued for Clean 1:1 Ground-Up Redo

⚠️ Target C++ implementations were removed (`63db120`, `daed305`) to eliminate simplified approximations and broken couplings. See [`../../Experimental/RemovedSdfGi/README.md`](../../Experimental/RemovedSdfGi/README.md) and [`../../CLAUDE.md`](../../CLAUDE.md) §1.2 & §2.1 before starting any redo.

| Plan / Reference | Queued Redo Domain |
| :--- | :--- |
| [RenderingRaytracingArchitecturePlan.md](RenderingRaytracingArchitecturePlan.md) · [WorldRenderEngineUnificationPlan.md](WorldRenderEngineUnificationPlan.md) · [DeterministicPathTracerVulkanOptimizationPlan.md](DeterministicPathTracerVulkanOptimizationPlan.md) · [DeterministicPathTracerAuditReport.md](DeterministicPathTracerAuditReport.md) | **Priority Redo (`BKL-012`)**: SDF Shadows, SDF GI, Emissive GI & Deterministic Path Tracing ([`Experimental/RemovedSdfGi/`](../../Experimental/RemovedSdfGi/README.md)) |
| [ParticleSpaceModularPortPlan.md](ParticleSpaceModularPortPlan.md) · [ParticleRendererDerezMaterialTexturePlan.md](ParticleRendererDerezMaterialTexturePlan.md) | **Deferred Redo (`BKL-013`)**: Un-simplified 1:1 `Particle` solver & studio |
| [VectorFieldArchitecturePlan.md](VectorFieldArchitecturePlan.md) · [VectorFieldVulkanConversionPlan.md](VectorFieldVulkanConversionPlan.md) | **Deferred Redo (`BKL-013`)**: Un-simplified 1:1 `ForceField` / `VectorField` solver & studio |

---

## 🔍 4. Audits, Notes & Studio Viewer

| File | Purpose |
| :--- | :--- |
| [AuditRemediationReport.md](AuditRemediationReport.md) | Historical cross-repo naming & structural audit report (`2026-11-15`) |
| [GeometryCompilerFindingsReport.md](GeometryCompilerFindingsReport.md) | Geometry compiler audit & diagnostic findings |
| [VehicleHmiRendererReflectionReport.md](VehicleHmiRendererReflectionReport.md) | Vehicle HMI renderer reflection & parity notes |
| [Note-1608.md](Note-1608.md) | Engineering note 1608 |
| [SessionNotes.md](SessionNotes.md) | Live Markdown Studio session notepad |
| `MarkdownStudioViewer.html` | Self-contained HTML viewer rebuilt via `python3 Automation/BuildMarkdownViewer.py` |
