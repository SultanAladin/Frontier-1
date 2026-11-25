# 🧩 `Engine/Threading/Tasks/`

> **TL;DR**: Cooperative work-stealing parallel execution cluster eliminating dependent-task sleep hangs (7 source/header/shader files under `Engine/Threading/Tasks/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ExecutionChannels.cpp` | Cooperative parallel execution cluster implementation |
| `ExecutionChannels.h` | Cooperative work-stealing parallel execution cluster eliminating dependent-task sleep hangs |
| `TaskChannel.cpp` | Priority-stratified task channel implementation |
| `TaskChannel.h` | Four-tier priority task channel enforcing ImmediateCritical preemption and remaining-budget slack admission |
| `TaskQueue.h` | Bounded preallocated work-stealing task deque supporting LIFO local pop and FIFO cross-lane steal |
| `TaskScheduler.cpp` | Priority-budgeted tick window execution implementation |
| `TaskScheduler.h` | Priority-budgeted tick window scheduler guaranteeing critical task preemption and bounded slack execution |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Threading`
- **Primary Types**: `ChannelsTelemetrySnapshot`, `ExecutionChannels`, `TaskChannel`, `TaskPriorityTier`, `ScheduledTaskUnit`, `TaskQueue`, `BudgetWindowReport`, `TaskScheduler`

## Usage

```cpp
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Tasks/TaskChannel.h"
#include "Engine/Threading/Tasks/TaskQueue.h"

using namespace Frontier::Threading;
ChannelsTelemetrySnapshot Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
