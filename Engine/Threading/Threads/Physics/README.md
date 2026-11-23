# 🧩 `Engine/Threading/Threads/Physics/`

> **TL;DR**: Dedicated fixed-step `PhysicsThread` driving deterministic simulation ticks, suspend/resume gating, and lock-free `ThreadProgressionTracker` heartbeats (2 source/header files under `Engine/Threading/Threads/Physics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `PhysicsThread.h` | Dedicated fixed-step `PhysicsThread` configuration and lifecycle interface |
| `PhysicsThread.cpp` | Fixed-step cadence pacing, spiral-of-death clamp, suspend gating, and watchdog heartbeats |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Threading`
- **Primary Types**: `PhysicsFixedStepCallback`, `PhysicsThreadConfiguration`, `PhysicsThread`

## Usage

```cpp
#include "Engine/Threading/Threads/Physics/PhysicsThread.h"

Frontier::Threading::PhysicsThreadConfiguration Configuration {};
Configuration.FixedStepHz = 60.0f;
Configuration.StepCallback = &StepPhysicsHost;
Configuration.OpaqueHost   = &PhysicsHost;

Frontier::Threading::PhysicsThread SimThread {};
(void)SimThread.Initialize(Configuration, &WatchdogMonitor);
```

## Build & Verification

- Built as part of `FrontierThreading` in root `CMakeLists.txt` (ISO C++20).
- Validated by `CrossThreadConstraintValidation` (`Exhibits/CrossThreadConstraintValidation.cpp`) and ThreadSanitizer.
