# 🧩 `Engine/Threading/Threads/Audio/`

> **TL;DR**: Dedicated real-time `AudioThread` driving deterministic block pulls, hardware sink handoff, suspend gating, and `ThreadProgressionTracker` heartbeats with zero real-time heap allocations (2 files under `Engine/Threading/Threads/Audio/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AudioThread.h` | Real-time audio thread configuration, callback signatures, and pause/pump/query interface |
| `AudioThread.cpp` | Dedicated OS loop pacing, preallocated staging buffer, suspend gating, and watchdog heartbeats |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Threading`
- **Primary Types**: `AudioThread`, `AudioThreadConfiguration`, `AudioMixPullCallback`, `AudioSinkPushCallback`

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by `Exhibits/AudioSpaceValidation.cpp` (`ctest --output-on-failure -R AudioSpaceValidation`).
