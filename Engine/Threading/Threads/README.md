# 🧩 `Engine/Threading/Threads/`

> **TL;DR**: Subsystem umbrella grouping 3 child modules under `Engine/Threading/Threads/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Audio/`](Audio/README.md) | Dedicated real-time `AudioThread` driving lock-free DSP block synthesis, endpoint handoff & watchdog pulses |
| [`Streaming/`](Streaming/README.md) | Dual-path SSD random-access and HDD elevator-coalesced sequential streaming thread for .Space and ThumbnailSheet pages |
| [`Workspace/`](Workspace/README.md) | Dedicated asynchronous workspace authoring, .Space codec I/O, and parametric revision replay domain thread |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
