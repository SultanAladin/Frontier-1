# Project-Defined User UI Inspectors — Architecture & Authoring Foundation

**Status**: Active Foundation (Tier 1 Native Editor Inspectors + Tier 2 Project-Folder User Inspectors)
**Scope**: Clearly separates Frontier's shipped Native Editor UI Inspectors (`Engine/Editor/Inspectors/`) from User-Authored Custom UI Inspectors stored inside each Project's own folder (`<ProjectRoot>/Inspectors/`).

---

## 1. Two-Tier Inspector Architecture

Frontier maintains a strict architectural boundary between **Engine-Owned Native Inspectors** and **Project-Owned User Inspectors**:

| Tier | Location | Authored By | Mechanism | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Tier 1 · Native Editor UI Inspectors** | `Engine/Editor/Inspectors/` | Frontier Engine | Standalone compiled C++ classes (31 inspectors across Atmosphere, Celestial, Weather, Optics/Camera Settings, 8 Lighting types, 4 Geometry categories, Post Process, Folder) | Core physically-based engine domains with custom retained GPU/software diagrams, live LUT baking, and direct `WorldSceneSpecification` synchronization |
| **Tier 2 · Project User UI Inspectors** | `<ProjectRoot>/Inspectors/*.Inspector.toml` (+ optional project C++ modules) | Project Authors / Technical Artists | Declarative TOML blueprints parsed natively via `ProjectInspectorHost` (`toml++`) and rendered through Layer A `InspectorSectionPrimitives` | Game-specific, studio-specific, or custom-component inspectors (e.g. Vehicle Telemetry, Audio Zone, Gameplay Trigger, Custom Shader Rig) without touching `Engine/` |

Both tiers render inside the **exact same 340 px Editor Inspector pane** using **Layer A `InspectorSectionPrimitives`** — guaranteeing 1:1 visual parity, consistent dark-glass typography, hover tooltips, dynamic enclosing-folder breadcrumbs, Reset/Delete semantics, and automatic multi-instance isolation keyed by `OutlinerSlotToken`.

---

## 2. Project Folder Layout & Discovery Contract

User-authored inspectors live inside the user's project directory and are never mixed into `Engine/`:

```text
<ProjectRoot>/
├── Project.toml
├── Scenes/
└── Inspectors/
    ├── VehicleAero.Inspector.toml
    ├── AudioZone.Inspector.toml
    └── CustomRigOverride.Inspector.toml
```

### Discovery & Hot-Reload
1. When a project opens (or when the user triggers Reload Project Inspectors), `ProjectInspectorHost::ScanProjectDirectory(ProjectRoot)` scans `<ProjectRoot>/Inspectors/` for every file ending in `.Inspector.toml`.
2. Each descriptor is parsed via `toml++` (`LoadBlueprintFromTomlText`) with non-fatal diagnostics collected in `OutDiagnostics`. A syntax error in one user inspector never crashes the Editor and never blocks other inspectors.
3. Project C++ plugins can also call `ProjectInspectorHost::RegisterBlueprint(...)` directly at startup.

---

## 3. Resolution Precedence & Multi-Instance Isolation

When any Outliner entry is selected in `InspectorRegistry::Construct(...)`:
1. **Project Blueprint Lookup**: `ProjectHost.ResolveBlueprint(Entry)` checks registered project blueprints against `Entry.DomainTag` (supports exact match like `"project:vehicle-aero"` or prefix wildcard like `"project:vehicle-*"`).
2. **Override Control**:
   - For custom project tags (not matching any shipped native tag), the project blueprint always renders directly.
   - For native tags (e.g., `"geom:native"`), a project blueprint only replaces the native inspector when `override_native = true` is explicitly set in `[inspector]`. Otherwise the shipped native inspector remains active.
3. **Automatic Multi-Instance Isolation**: Runtime field readings are keyed by `OutlinerSlotToken` inside `ProjectInspectorHost::AccessReadings(...)`. Two vehicles or two audio zones sharing the same `.Inspector.toml` blueprint maintain completely independent slider, toggle, choice, colour, and action-trigger readings.
4. **Reset**: Clicking the Inspector footer's **Reset** button calls `ProjectHost.EraseEntryReadings(Entry.Token)`, cleanly reverting that single instance back to the blueprint's authored defaults.

---

## 4. Declarative `.Inspector.toml` Authoring Schema

```toml
[inspector]
id = "project.vehicle-aero"
target_tag = "project:vehicle-aero"
subtitle = "Project custom inspector · aerodynamics"
override_native = false
tags = ["project", "aero", "telemetry"]

[[pages]]
title = "Overview"

  [[pages.sections]]
  heading = "Active Aero Surface"
  caption = "User UI"
  tooltip = "Author-defined aerodynamic downforce & drag profile for this vehicle."

    [[pages.sections.fields]]
    key = "downforce_bias"
    label = "Front downforce bias"
    control = "slider"
    unit = "%"
    format = "%.1f"
    min = 25.0
    max = 75.0
    default = 48.5
    tooltip = "Percentage of total aerodynamic downforce acting on the front axle."

    [[pages.sections.fields]]
    key = "drs_enabled"
    label = "Active DRS flap"
    control = "toggle"
    default = true

    [[pages.sections.fields]]
    key = "aero_mode"
    label = "Wing package"
    control = "choice"
    columns = 3
    options = ["Low drag", "Balanced", "Track"]
    default = 1

    [[pages.sections.fields]]
    key = "livery_accent"
    label = "Telemetry trace colour"
    control = "colour"
    default_rgb = [0.35, 0.72, 0.98]

    [[pages.sections.fields]]
    key = "solver_profile"
    label = "CFD profile"
    control = "metadata"
    default = "WindTunnel_v4"

    [[pages.sections.fields]]
    key = "recompute_aero"
    label = "Recompute Aero Curve"
    control = "action"
```

### Supported Native Field Controls (`ProjectFieldControl`)
| `control` String | Native Layer A Primitive | Supported Attributes |
| :--- | :--- | :--- |
| `"slider"` | `ConstructScalarFieldRow` | `min`, `max`, `default`, `unit`, `format`, `tooltip` |
| `"toggle"` / `"switch"` | `ConstructToggleFieldRow` | `default`, `tooltip` |
| `"choice"` / `"select"` | `ConstructChoiceRow` | `options = [...]`, `columns`, `default`, `tooltip` |
| `"colour"` / `"color"` | `ConstructColourFieldRow` | `default_rgb = [r, g, b]`, `tooltip` |
| `"metadata"` / `"readout"` | `ConstructMetadataRow` | `default`, `tooltip` |
| `"action"` / `"button"` | `ConstructSectionActionButton` | `label` (increments `TriggerCount` per click) |

---

## 5. Native C++ Foundation Files

- `Engine/Editor/Inspectors/ProjectInspectorFoundation.h` — `ProjectFieldControl`, `ProjectFieldDescriptor`, `ProjectSectionDescriptor`, `ProjectPageDescriptor`, `ProjectInspectorBlueprint`, `ProjectFieldReading`, `ProjectInstanceReadings`, and `ProjectInspectorHost`.
- `Engine/Editor/Inspectors/ProjectInspectorFoundation.cpp` — `toml++` parser, `<ProjectRoot>/Inspectors/*.Inspector.toml` directory scanner, domain-tag pattern resolver, multi-instance readings register, and Layer A renderer with live `?` hover tooltips.
- `Engine/Editor/Inspectors/InspectorRegistry.{h,cpp}` — Hosts `ProjectInspectorHost`, dispatches matching project blueprints prior to native inspectors, flushes floating tooltip overlays at pane top level, and resets project readings on Reset.

---

## 6. Next Milestone — Geometry & Preview Surface Extensions

With the Native Editor Inspector suite (31 inspectors), Camera Settings (`PBR` / `SDF` / `Ray traced` / `MatCap`), F76 Baking cards + Tooltips, and Project User Inspector Foundation in place, the next planned phase extends the retained preview surface layer (`InspectorPreviewSurface`) with interactive **UV layout**, **Local SDF slice/iso-surface**, and **Cluster-LOD pyramid** preview panels for both native geometry inspectors and custom project blueprints.
