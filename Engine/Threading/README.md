# 🧩 `Engine/Threading/`

> **TL;DR**: Subsystem umbrella grouping 3 child modules under `Engine/Threading/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Sync/`](Sync/README.md) | Bounded Vyukov lock-free multi-producer multi-consumer ring queue with zero post-initialization heap allocations |
| [`Tasks/`](Tasks/README.md) | Cooperative work-stealing parallel execution cluster eliminating dependent-task sleep hangs |
| [`Threads/`](Threads/README.md) | 4 source/shader files |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
