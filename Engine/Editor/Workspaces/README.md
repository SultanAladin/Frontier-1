# 🧩 `Engine/Editor/Workspaces/`

> **TL;DR**: Subsystem umbrella grouping 1 child modules under `Engine/Editor/Workspaces/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Simulation/`](Simulation/README.md) | Full 1:1 ForceFields Inspector renderer: 4 tabs (Field / Formula / Layers / Cache), 19 ForceModel law schemas, |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
