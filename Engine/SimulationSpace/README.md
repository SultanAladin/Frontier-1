# 🧩 `Engine/SimulationSpace/`

> **TL;DR**: Subsystem umbrella grouping 3 child modules under `Engine/SimulationSpace/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`FluidSpace/`](FluidSpace/README.md) | Pure TOML serializer & deserializer for FluidSceneDocument (.fluid.toml & legacy Y-up .gasscene.toml conversion) |
| [`ForceFields/`](ForceFields/README.md) | VectorFieldLang lexer, AST parser, type validator, CPU bytecode generator, and .slang GPU shader expression emitter |
| [`PhysicsSpace/`](PhysicsSpace/README.md) | Broadphase collision channels, raycast/overlap queries, and 80-byte GPU/CPU RigidColliderProxy particle collision solver |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
