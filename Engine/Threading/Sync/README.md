# 🧩 `Engine/Threading/Sync/`

> **TL;DR**: Bounded Vyukov lock-free multi-producer multi-consumer ring queue with zero post-initialization heap allocations (6 source/header/shader files under `Engine/Threading/Sync/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AtomicQueue.h` | Bounded Vyukov lock-free multi-producer multi-consumer ring queue with zero post-initialization heap allocations |
| `CompletionCounter.h` | Wait-free atomic countdown latch and non-blocking continuation trigger preventing dependent-task sleep waits |
| `CriticalSection.h` | Adaptive mutual exclusion primitive for infrequent OS boundary and container registration regions |
| `PublishedReadingExchange.h` | Generic wait-free multi-reader, single-writer generation-stamped reading bank with split-reference slot recycling |
| `SpinLock.h` | Leaf-only exponential-backoff atomic spin lock for short bounded critical regions |
| `ThreadBoundaryExchange.h` | Generic bidirectional cross-thread boundary pairing `PublishedReadingExchange` with lock-free `AtomicQueue` directive intake |
| `ThreadProgressionTracker.cpp` | Lock-free monotonic thread heartbeat implementation |
| `ThreadProgressionTracker.h` | Cache-line-isolated lock-free thread liveness and active task-tag monitor for out-of-band stall detection |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Threading`
- **Primary Types**: `AtomicQueue`, `CompletionCounter`, `CriticalSection`, `PublishedReadingExchange`, `ReadingEpochHeader`, `ReadingLease`, `ScopedCriticalGuard`, `ScopedSpinGuard`, `ThreadBoundaryExchange`, `ThreadLivenessCondition`, `ThreadProgressionReading`, `ThreadProgressionTracker`

## Usage

```cpp
#include "Engine/Threading/Sync/AtomicQueue.h"
#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/CriticalSection.h"

using namespace Frontier::Threading;
AtomicQueue Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
