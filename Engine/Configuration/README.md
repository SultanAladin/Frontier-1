# 🧩 `Engine/Configuration/`

> **TL;DR**: Compile-time build-profile policy: enforces mutually exclusive Editor, Development, and Shipping profiles across all targets (11 source/header/shader files under `Engine/Configuration/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BuildConfiguration.h` | Compile-time build-profile policy: enforces mutually exclusive Editor, Development, and Shipping profiles across all targets |
| `EngineConfiguration.cpp` | Implementation of EngineConfiguration resolving FeatureSet, StandaloneWorkspace, HeadlessBatch, and ExecutionChannels lane budgets |
| `EngineConfiguration.h` | Engine startup and host-launch configuration supporting unified FeatureSet, StandaloneWorkspace (e.g. FrontierUV), and HeadlessBatch |
| `InputSettings.cpp` | Editor shortcut chord formatting, conflict validation, and EditorInputExchange capture integration (zero direct ImGui polling) |
| `InputSettings.h` | Editor shortcut chords, .InputSpace preset selection, conflict validation, and pointer sensitivity scaling |
| `ProjectGenerator.cpp` | Implementation of ProjectGenerator producing standalone C++20 Frontier projects with 3 CMake targets and SessionOutput/ |
| `ProjectGenerator.h` | Scaffolder for standalone C++20 Frontier projects (<ProjectName>, <ProjectName>Editor, <ProjectName>Shipping) with .projectspace |
| `ProjectSpaceRepository.cpp` | Atomic PascalCase persistence for .projectspace documents containing .visualappearance, .FontSpace, and .workspacelayout |
| `ProjectSpaceRepository.h` | Single-container .projectspace persistence for .visualappearance, .FontSpace, .workspacelayout, .inputsettings, and .notificationsettings |
| `WorkspaceConfiguration.cpp` | Implementation of WorkspaceConfiguration clamping UV workspace layout ratios, tile resolutions, and TransformGizmo snap increments |
| `WorkspaceConfiguration.h` | Session layout, default UDIM tile resolution, default-off seam overlay policy, and 3D/2D TransformGizmo snap configuration |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`, `Frontier::Configuration`, `InputSettingsRepository`
- **Primary Types**: `BuildProfileCategory`, `BuildConfiguration`, `WorkspaceCategory`, `HostLaunchMode`, `EngineConfigurationDescriptor`, `EngineConfiguration`, `KeyMappingEntry`, `ShortcutCommand`, `ShortcutKey`, `ShortcutChord`, `InputPreferences`, `ProjectScaffoldSpecification`, `ProjectScaffoldOutcome`, `ProjectGenerator`, `WorkspaceLayoutSettings`, `ProjectSpaceDocument`, `ProjectSpaceRepository`, `UVViewportLayoutMode` (+5 more)

## Usage

```cpp
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Configuration/InputSettings.h"

using namespace Frontier;
BuildProfileCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
