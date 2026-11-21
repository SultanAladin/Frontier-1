# 🧩 `Engine/Input/`

> **TL;DR**: Downstream one-way EditorInputExchange -> Dear ImGui IO signal forwarder (never polled upstream by Engine or Editor classes) (6 source/header/shader files under `Engine/Input/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ImGuiInputAdapter.cpp` | Implementation of one-way EditorInputExchange -> ImGuiIO downstream packet translation |
| `ImGuiInputAdapter.h` | Downstream one-way EditorInputExchange -> Dear ImGui IO signal forwarder (never polled upstream by Engine or Editor classes) |
| `InputExchange.cpp` | Implementation of bi-directional OS-to-Engine signal queueing and Engine-to-OS cursor/haptic directive exchange |
| `InputExchange.h` | Bi-directional OS-to-Engine hardware signal ring intake and Engine-to-OS cursor confinement & haptic feedback exchange |
| `InputExtension.cpp` | Implementation of InputExtension dual RuntimeInputExchange & EditorInputExchange routing and snapshot consolidation |
| `InputExtension.h` | Input lifecycle participant: hardware signal intake, split RuntimeInputExchange & EditorInputExchange routing, and OS feedback |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Input`, `InputModifierBits`
- **Primary Types**: `InputSignalCategory`, `InputLogicalKey`, `ButtonTransitionCategory`, `TouchContactCategory`, `CursorConfinementMode`, `InputExchange`, `InputRoutingTarget`, `TouchContactSample`, `PointerSnapshot`, `EditorInputSnapshot`, `InputExtension`

## Usage

```cpp
#include "Engine/Input/ImGuiInputAdapter.h"
#include "Engine/Input/InputExchange.h"
#include "Engine/Input/InputExtension.h"

using namespace Frontier::Input;
InputSignalCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
