# 🧩 `Engine/Editor/Workspaces/`

> **TL;DR**: Editor-only workspace staging umbrella (simulation workspace removed; active authoring workspaces reside under `Engine/Workspaces/` and `Engine/Editor/Explorer/`).

---

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir Build --output-on-failure`).
