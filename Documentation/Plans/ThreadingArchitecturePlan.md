# Threading, Priority Budgets & Watchdog Architecture Plan

> **TL;DR:** Frontier uses a **Hybrid Domain-Thread + Work-Stealing `ExecutionChannels`** architecture. Latency-critical loops (`GameThread`, `WorkspaceThread`, `EditorThread`, `RenderThread`, `PhysicsThread`, `AudioThread`, `NetworkThread`, `AssetStreamThread`) never block on mutexes or sleep-wait on dependent tasks, **`DifferentialWorldEncoder` replication executes first** at Priority Tier 0, leftover microseconds run opportunistic background tasks, and an isolated out-of-band **`DebugThread`** monitors lock-free heartbeats to catch hangs, crashes, assertions, and logs even if another thread stalls.

---

## 1. Threading Model Comparison

*(Legend — Anti-Hang Safety / Throughput / Isolation: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest; Overhead: ✔️ = lowest/best)*

| Rank | Architecture Option | Anti-Hang Safety | Priority Determinism | Crash / Watchdog Isolation | Overhead | Summary |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — Hybrid Domain Threads + Priority-Budgeted `ExecutionChannels`** | 🔴 | 🔴 | 🔴 | ✔️ | Dedicated threads for OS/real-time boundaries (`Game`, `Workspace`, `Editor`, `Render`, `Audio`, `Network`, `Streaming`, plus isolated `Debug`/`Profiler`) backed by a shared lock-free `ExecutionChannels` for parallel sub-tasks. |
| 🥈 | **Option B — Pure Anonymous Job / Fiber Model** | 🚩 | 🚩 | ✔️ | 🚩 | Every loop floats across anonymous fibers; harder to guarantee OS window/audio thread pinning and loses true out-of-band `DebugThread` survival when jobs saturate. |
| 🥉 | **Option C — Fixed 1-Thread-Per-Domain Only (No `ExecutionChannels`)** | ✔️ | 🚩 | 🔴 | 🚩 | Simple fixed threads, but heavy parallel workloads (LSCM UV flattening, broadphase collision, mip encoding) bottleneck on a single hardware thread. |

💡 **Recommendation:** **Option A** — isolates `DebugThread`, `WorkspaceThread`, `GameThread`, `EditorThread`, and `AudioThread` so no domain can starve or freeze another, while sharing hardware CPU threads through `ExecutionChannels` + `TaskChannel` for wide parallel bursts.

---

## 2. Priority Tiers & Remaining-Slice Tick Budget

To guarantee critical work (such as network replication) **always executes first** while lower-priority work consumes **only the remaining slice of the tick** without overrunning the deadline:

```text
|<============================== SINGLE TICK BUDGET (e.g. 8.33 ms @ 120 Hz) ==============================>|
+------------------------+-----------------------------+--------------------------+-------------------+----+
| TIER 0: IMMEDIATE      | TIER 1: DETERMINISTIC TICK  | TIER 2: WORKSPACE        | TIER 3: SLACK     |RSV |
| (Runs FIRST, Always)   | (Primary Sim & Encode)      | (Async Authoring)        | (Remaining Slice) |    |
+------------------------+-----------------------------+--------------------------+-------------------+----+
  • DifferentialWorld-     • GameThread instance step    • UV conformal unwrap      • Thumbnail bakes  250us
    Encoder replication    • PhysicsThread solver        • Parametric replay        • Incremental BVH  guard
  • InputExchange intake   • RenderThread cmd encode     • Solid boolean sewing     • Region compact
  • Audio ring refill                                    • Cancellable on drag      • Stops at deadline!
```

| Tier | `TaskChannel` Lane | Scheduling Rule | Example Workloads |
| :---: | :--- | :--- | :--- |
| **Tier 0** | **`ImmediatePriority`** | Drained **first** before any Tier 1–3 task; preempts opportunistic steals between task boundaries | `DifferentialWorldEncoder` replication, `RemoteDirectiveRouter`, `InputExchange`, `AudioBusAccumulator` |
| **Tier 1** | **`TickRequired`** | Must finish within current simulation/render tick; `ExecutionChannels` assists until `CompletionCounter == 0` | `PhysicsThread` narrow-phase batches, `GameThread` spatial queries, `RenderThread` command encoding |
| **Tier 2** | **`WorkspaceAuthoring`** | Runs on `WorkspaceThread` + `ExecutionChannels`; decoupled from `EditorThread`/`GameThread` FPS via generation snapshots | `UVUnwrapOperations`, `UVPackOperations`, `OperationRecorder` prefix fold, `.Space` `Codecs` |
| **Tier 3** | **`OpportunisticSlack`** | Executes **only while `NowUs + EstimatedTaskUs < TickDeadlineUs - SafetyMarginUs`**; yields leftover work to next tick | Progressive `StretchRelaxer` iterations, background mip generation, autosave compression |

---

## 3. Four Anti-Hang & Zero-Deadlock Guarantees

1. **Never Sleep-Wait on Dependent Tasks (`CompletionCounter` Continuations + Cooperative Assist):**
   - Dependent tasks attach as **lock-free continuations** to `CompletionCounter` — when prerequisite count hits `0`, the continuation automatically enters `TaskChannel` without parking any thread.
   - If a thread ever awaits a `CompletionCounter` inline, it calls **`AssistPendingTasks()`** (pulling Tier 0 / dependency tasks from `TaskQueue`) rather than sleeping — eliminating thread-starvation deadlocks.
2. **Decoupled `GameThread`, `WorkspaceThread`, and `EditorThread` (Generation-Stamped Snapshots):**
   - `EditorThread` never waits on `WorkspaceThread` or `GameThread`. If a complex UV pack or solid boolean takes `35 ms` on `WorkspaceThread`, `EditorThread` and `RenderThread` continue presenting the latest finalized generation at full refresh rate (`120+ Hz`).
3. **Lock-Ordering & Bounded Spin Policy:**
   - Cross-thread pipes use lock-free `AtomicQueue` / SPSC rings (`0` OS mutex locks on real-time paths). `SpinLock` is strictly leaf-only (`<= 64` CPU cycles, never nested).
4. **Isolated Out-of-Band `DebugThread` (`Editor` & `Development` Profiles):**
   - Runs on its own dedicated OS thread sharing **zero locks or allocators** with engine threads.
   - Polls cache-line-aligned `ThreadProgressionTracker` timestamps every `5 ms`:
     - **Stall / Hang Detection:** Reports any thread whose monotonic heartbeat exceeds `250 ms` (warning) or `2000 ms` (hang), logging the stuck `TaskTag` and stack trace while the UI diagnostic socket stays responsive.
     - **Crash & Assertion Vault:** Drains lock-free per-thread log/assertion SPSC rings to disk and `DebugConsole`, and flushes last-flight breadcrumbs on OS fault signals (`SIGSEGV`/`SIGABRT`, Win32 SEH).

---

## 4. Thread Allocation Across Build Profiles

| Dedicated Thread | `Editor` Profile | `Development` Profile | `Shipping` Profile | Responsibility |
| :--- | :---: | :---: | :---: | :--- |
| **`EditorThread`** | 🟢 Active | 🔴 Excluded | 🔴 Excluded | ImGui docking, panels, toolbars & viewport interaction |
| **`WorkspaceThread`** | 🟢 Active | 🟢 Active *(CLI/Headless)* | 🔴 Excluded | `.Space` document edits, `RevisionTracker`, parametric replay & codecs |
| **`DebugThread`** | 🟢 Active | 🟢 Active | 🔴 Excluded | Watchdog hang detection, crash capture, async log sink & live terminal |
| **`ProfilerThread`** | 🟢 Active | 🟢 Active | 🔴 Excluded | Lock-free cycle timing, memory region telemetry & capture export |
| **`GameThread`** | 🟢 Active *(Play/Sim)* | 🟢 Active | 🟢 Active | Deterministic world ticks, instance logic & simulation rules |
| **`PhysicsThread`** | 🟢 Active | 🟢 Active | 🟢 Active | Fixed-step rigid dynamics, constraints & collision |
| **`RenderThread`** | 🟢 Active | 🟢 Active | 🟢 Active | GPU command encoding & `VulkanInterchange` presentation |
| **`AudioThread`** | 🟢 Active | 🟢 Active | 🟢 Active | Lock-free `AudioBusAccumulator` & hardware ring output |
| **`NetworkThread`** | 🟢 Active | 🟢 Active | 🟢 Active | Socket polling, `PacketSerializer` & `NetworkEndpointRegister` |
| **`AssetStreamThread`** | 🟢 Active | 🟢 Active | 🟢 Active | Asynchronous disk reads, `.Space` decoding & mip streaming |
| **`ExecutionChannels`** | 🟢 `max(2, Cores - Reserved)` | 🟢 `max(2, Cores - Reserved)` | 🟢 `max(2, Cores - Reserved)` | Work-stealing parallel execution across Priority Tiers 0–3 |

---

## 5. Canonical `Engine/Constants/` & `Engine/Threading/` Folder Layout

```text
Engine/
├── Constants/                                     // Shared compile-time constants across all domains
│   ├── MathConstants.h                            // Pi, Tau, Epsilon, DegToRad, RadToDeg & IEEE-754 bounds
│   ├── PhysicsConstants.h                         // Gravity, AirDensity, WaterDensity & FrictionLimits
│   ├── EngineConstants.h                          // MaxPlayers, DefaultTickRate, BufferCapacities & Limits
│   ├── TimeConstants.h                            // FixedDeltaTime, MicrosecondsPerSecond & WatchdogThresholds
│   ├── OpticalConstants.h                         // VacuumLightSpeed, PlanckConstant, CIEWhitePoints & IOR
│   └── AcousticConstants.h                        // ReferenceSoundPressure, DryAirSoundSpeed & OctaveBands
│
└── Threading/                                     // Priority-budgeted execution, lock-free sync & domain threads
    ├── Tasks/                                     // Non-blocking task routing, priority channels & cluster
    │   ├── TaskScheduler.{h,cpp}                  // 4-tier priority & remaining-slice tick budget scheduler
    │   ├── TaskQueue.h                            // Cache-aligned lock-free work-stealing task deque
    │   ├── TaskChannel.{h,cpp}                    // Typed priority intake & continuation routing channel
    │   └── ExecutionChannels.{h,cpp}               // Hardware-pinned parallel task execution cluster
    │
    ├── Sync/                                      // Lock-free and leaf synchronization primitives
    │   ├── SpinLock.h                             // Leaf-only exponential-backoff atomic spin lock
    │   ├── AtomicQueue.h                          // Bounded lock-free MPMC / SPSC ring queue
    │   ├── CriticalSection.h                      // OS adaptive sleep-capable mutual exclusion primitive
    │   ├── CompletionCounter.h                    // Wait-free atomic countdown & continuation trigger
    │   └── ThreadProgressionTracker.{h,cpp}               // Cache-line-isolated thread liveness & stall detector
    │
    └── Threads/                                   // Dedicated domain & diagnostic thread loops
        ├── Game/
        │   └── GameThread.{h,cpp}                 // Deterministic world ticks, instance logic & gameplay rules
        ├── Workspace/
        │   └── WorkspaceThread.{h,cpp}            // Async .Space authoring, RevisionTracker & solver execution
        ├── Physics/
        │   └── PhysicsThread.{h,cpp}              // Fixed-step rigid dynamics, constraint solver & collisions
        ├── Render/
        │   └── RenderThread.{h,cpp}               // GPU command encoding, stream upload & VulkanInterchange
        ├── Audio/
        │   └── AudioThread.{h,cpp}                // Real-time lock-free AudioBusAccumulator & ring streaming
        ├── Editor/
        │   └── EditorThread.{h,cpp}               // Responsive UI docking, viewport input & tool triggers
        ├── Debug/
        │   ├── DebugThread.{h,cpp}                // Dedicated crash/hang watchdog, async log sink & terminal
        │   └── ProfilerThread.{h,cpp}             // Lock-free cycle metrics, telemetry & memory region tracking
        ├── Network/
        │   └── NetworkThread.{h,cpp}              // Non-blocking socket poll, replication & packet transport
        └── Streaming/
            └── AssetStreamThread.{h,cpp}          // Asynchronous disk reads & .Space container decoding
```
