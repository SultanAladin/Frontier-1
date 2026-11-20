# 🧩 `Engine/SimulationSpace/`

> **TL;DR**: Subsystem umbrella grouping `PhysicsSpace/` under `Engine/SimulationSpace/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`PhysicsSpace/`](PhysicsSpace/README.md) | JoltPhysics v5.6.0 rigid dynamics, breakable constraints, vehicle powertrain, broadphase collision channels & raycast/overlap queries |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
