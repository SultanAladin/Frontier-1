# 🧩 `Documentation/` — Frontier Governance, Standards & Workspace Index

> **TL;DR**: `Documentation/` holds Frontier's mandatory agent protocols, naming blacklist, C++20 coding standards, canonical repository layout, milestone tracker, deferred backlog, and Markdown Studio architectural plans (`Plans/`).

---

## 🏷️ 1. Core Governance & Standards

| Document | Scope |
| :--- | :--- |
| **[../CLAUDE.md](../CLAUDE.md)** · **[../AGENTS.md](../AGENTS.md)** | Mandatory startup reading sequence, non-negotiable GPU/`.slang`/cross-platform rules, Markdown Studio policy |
| **[BannedWordsVocabulary.md](BannedWordsVocabulary.md)** | Canonical vocabulary blacklist, restricted exemptions, approved replacements, chronological rulings |
| **[CodingStandards.md](CodingStandards.md)** | ISO C++20 (`cxx_std_20`), explicit prototypes, `concept`-constrained templates, 3-profile build macros |
| **[FolderStructure.md](FolderStructure.md)** | Authoritative repository tree across `Engine/`, `EngineContent/`, `Exhibits/`, `Experimental/`, `Automation/` |
| **[ImportantNotes.md](ImportantNotes.md)** | Hard invariants (Vulkan 1.3 + Right-Hand Rule, `<= 1–5%` delta gate, ImGui 8-patch stack, `Experimental/` HTML-only, read-only C++ naming audit) |
| **[Tracker.md](Tracker.md)** | Active milestone roadmap across Phases 0–5+ and workspace bringup status |
| **[Backlog.md](Backlog.md)** | Deferred platform, shader, UI, and naming-compliance refactoring items |
| **[Plans/README.md](Plans/README.md)** | Index of all architectural plans, implementation reports, `SessionNotes.md`, and `MarkdownStudioViewer.html` |

---

## 📝 2. Mandatory Agentic Instructions (`AgenticInstructions/`)

| Skill Document | Authority |
| :--- | :--- |
| **[SKILL-CompactOutputProtocol.md](AgenticInstructions/SKILL-CompactOutputProtocol.md)** | Skimmer-first formatting, mandatory decision tables, Markdown Studio over chat walls |
| **[SKILL-Emoji.md](AgenticInstructions/SKILL-Emoji.md)** | Sole authority on permitted emojis (banned star emojis forbidden; dots strictly 🔴/🟢) |
| **[SKILL-Naming.md](AgenticInstructions/SKILL-Naming.md)** | Mechanism-first PascalCase construction, 7-gate naming test, zero-`And` rule |
| **[SKILL-Formatting.md](AgenticInstructions/SKILL-Formatting.md)** | 140-col file banners, 120-col section rulers, Allman braces, unit tags |

---

## 🧩 3. Workspace & Reference Documentation Index

| Area | Documentation |
| :--- | :--- |
| **Workspaces Master Index** | **[../Engine/Workspaces/README.md](../Engine/Workspaces/README.md)** |
| **World Editor & Launchers** | **[../Engine/Lifecycle/README.md](../Engine/Lifecycle/README.md)** |
| **Editor Explorer & ForceField** | **[../Engine/Editor/README.md](../Engine/Editor/README.md)** |
| **UV Authoring** | **[../Engine/Workspaces/UV/README.md](../Engine/Workspaces/UV/README.md)** |
| **Pattern · Stamp · Baking** | **[../Engine/Workspaces/Texturing/README.md](../Engine/Workspaces/Texturing/README.md)** |
| **Fracture & Shatter** | **[../Engine/Workspaces/Fracture/README.md](../Engine/Workspaces/Fracture/README.md)** |
| **Parametric Sketcher (B-Rep/NURBS)** | **[../Engine/Workspaces/ParametricSketcher/README.md](../Engine/Workspaces/ParametricSketcher/README.md)** · **[../Engine/GeometrySpace/Parametric/README.md](../Engine/GeometrySpace/Parametric/README.md)** |
| **Fluid & Pyro** | **[../Engine/Workspaces/Fluid/README.md](../Engine/Workspaces/Fluid/README.md)** |
| **Dynamic Material Studio** | **[../Engine/Workspaces/Material/README.md](../Engine/Workspaces/Material/README.md)** |
| **Particle & VFX Studio** | **[../Engine/Workspaces/Particle/README.md](../Engine/Workspaces/Particle/README.md)** |
| **Component Editor** | **[../Engine/Workspaces/ComponentEditor/README.md](../Engine/Workspaces/ComponentEditor/README.md)** |
| **Non-Linear Revision DAG** | **[../Engine/Workspaces/Revision/README.md](../Engine/Workspaces/Revision/README.md)** |
| **Reference HTML Prototypes & Exhibits** | **[../Experimental/README.md](../Experimental/README.md)** · **[../Exhibits/DeterministicPathTracer/README.md](../Exhibits/DeterministicPathTracer/README.md)** · **[../EngineContent/MatCaps/README.md](../EngineContent/MatCaps/README.md)** |
