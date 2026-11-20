# 🧩 `Documentation/` — Frontier Governance, Standards & Workspace Index

> **TL;DR**: `Documentation/` holds Frontier's mandatory agent protocols, naming blacklist, C++20 coding standards, canonical repository layout, milestone tracker, queued ground-up redo ledger, and Markdown Studio architectural plans (`Plans/`).

---

## 🏷️ 1. Core Governance & Standards

| Document | Scope |
| :--- | :--- |
| **[../CLAUDE.md](../CLAUDE.md)** · **[../AGENTS.md](../AGENTS.md)** | Mandatory startup reading sequence, automatic HTML-to-C++ screenshot & zero-simplification gates, Markdown Studio policy |
| **[BannedWordsVocabulary.md](BannedWordsVocabulary.md)** | Canonical vocabulary blacklist, restricted exemptions, approved replacements, chronological rulings |
| **[CodingStandards.md](CodingStandards.md)** | ISO C++20 (`cxx_std_20`), explicit prototypes, `concept`-constrained templates, 3-profile build macros |
| **[FolderStructure.md](FolderStructure.md)** | Authoritative repository tree across `Engine/`, `EngineContent/`, `Exhibits/`, `Experimental/`, `Automation/` |
| **[ImportantNotes.md](ImportantNotes.md)** | Hard invariants (automatic HTML parity gate, Vulkan 1.3 + Right-Hand Rule, `<= 1–5%` delta gate, ImGui 8-patch stack) |
| **[Tracker.md](Tracker.md)** | Active milestone roadmap across 9 registered workspaces and Phase 6 bringup status |
| **[Backlog.md](Backlog.md)** | Deferred platform/shader items, naming-compliance refactoring, and ground-up redo queue (`SDF/GI`, `Fluid`, `ForceField`, `Particle`) |
| **[Plans/README.md](Plans/README.md)** | Index of all architectural plans, implementation reports, `SessionNotes.md`, and `MarkdownStudioViewer.html` |

---

## 📝 2. Mandatory Agentic Instructions (`AgenticInstructions/`)

| Skill Document | Authority |
| :--- | :--- |
| **[SKILL-CompactOutputProtocol.md](AgenticInstructions/SKILL-CompactOutputProtocol.md)** | Skimmer-first formatting, mandatory decision tables, Markdown Studio over chat walls |
| **[SKILL-Emoji.md](AgenticInstructions/SKILL-Emoji.md)** | Sole authority on permitted emojis (plain `⭐` only; sparkle/fancy stars forbidden; status dots strictly `🔴`/`🟢`) |
| **[SKILL-Naming.md](AgenticInstructions/SKILL-Naming.md)** | Mechanism-first PascalCase construction, 7-gate naming test, zero-`And` rule |
| **[SKILL-Formatting.md](AgenticInstructions/SKILL-Formatting.md)** | 140-col file banners, 120-col section rulers, Allman braces, unit tags |

---

## 🧩 3. Active Workspace, Renderer & Redo Reference Index

| Area | Documentation |
| :--- | :--- |
| **Workspaces Master Index (9 Host Workspaces)** | **[../Engine/Workspaces/README.md](../Engine/Workspaces/README.md)** |
| **World Editor & 9 Desktop Launchers** | **[../Engine/Lifecycle/README.md](../Engine/Lifecycle/README.md)** |
| **Editor Explorer (`ProjectBrowser` · `ContentBrowser`)** | **[../Engine/Editor/README.md](../Engine/Editor/README.md)** |
| **UV Authoring (`FrontierUV`)** | **[../Engine/Workspaces/UV/README.md](../Engine/Workspaces/UV/README.md)** |
| **Pattern · Stamp · Baking (`Texturing/`)** | **[../Engine/Workspaces/Texturing/README.md](../Engine/Workspaces/Texturing/README.md)** |
| **Fracture & Shatter (`FrontierFracture`)** | **[../Engine/Workspaces/Fracture/README.md](../Engine/Workspaces/Fracture/README.md)** |
| **Parametric Sketcher (B-Rep/NURBS)** | **[../Engine/Workspaces/ParametricSketcher/README.md](../Engine/Workspaces/ParametricSketcher/README.md)** · **[../Engine/GeometrySpace/Parametric/README.md](../Engine/GeometrySpace/Parametric/README.md)** |
| **Dynamic Material Studio (`FrontierMaterial`)** | **[../Engine/Workspaces/Material/README.md](../Engine/Workspaces/Material/README.md)** |
| **Component Editor** | **[../Engine/Workspaces/ComponentEditor/README.md](../Engine/Workspaces/ComponentEditor/README.md)** |
| **Non-Linear Revision DAG (`Revision/`)** | **[../Engine/Workspaces/Revision/README.md](../Engine/Workspaces/Revision/README.md)** |
| **Direct World Renderer & Unified Atmosphere** | **[../Engine/GeometricRaster/GlobalIllumination/README.md](../Engine/GeometricRaster/GlobalIllumination/README.md)** · **[../Engine/GeometricRaster/Atmosphere/README.md](../Engine/GeometricRaster/Atmosphere/README.md)** |
| **SDF / GI Redo Baseline *(Priority Redo)*** | **[../Experimental/RemovedSdfGi/README.md](../Experimental/RemovedSdfGi/README.md)** · **[../Experimental/DeterministicPathTracer/README.md](../Experimental/DeterministicPathTracer/README.md)** |
| **HTML Reference Prototypes & MatCaps** | **[../Experimental/README.md](../Experimental/README.md)** · **[../EngineContent/MatCaps/README.md](../EngineContent/MatCaps/README.md)** |
