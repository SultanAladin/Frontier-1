# 🧩 `Engine/Threading/Threads/Workspace/`

> **TL;DR**: Dedicated asynchronous workspace authoring, .Space codec I/O, and parametric revision replay domain thread (2 source/header/shader files under `Engine/Threading/Threads/Workspace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `WorkspaceThread.cpp` | Dedicated asynchronous WorkspaceThread implementation |
| `WorkspaceThread.h` | Dedicated asynchronous workspace authoring, .Space codec I/O, and parametric revision replay domain thread |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Threading`
- **Primary Types**: `PublishedWorkspaceSnapshot`, `WorkspaceThread`

## Usage

```cpp
#include "Engine/Threading/Threads/Workspace/WorkspaceThread.h"

using namespace Frontier::Threading;
PublishedWorkspaceSnapshot Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
