# 🧩 Cross-Thread Snapshot Publication & Physics Constraints Report

> **TL;DR:** Replaced Unreal-style GameThread sockets with first-class JoltPhysics v5.6.0 constraints (all 8 `ProjectZeroEditor` categories plus `Distance`/`BreakableBond`), backed by a generic, modular, wait-free multi-reader `PublishedSnapshotExchange<T>` + `ThreadBoundaryExchange<S, D>` primitive and dedicated `PhysicsThread`. Verified with **0 torn reads**, **0 ThreadSanitizer races**, and **100% pass** across `CrossThreadConstraintValidation`, `ComponentClassPhysicsValidation`, and `PhysicsSpaceValidation`.

---

## 1. Cross-Thread Multi-Reader Strategy Selection

*(Legend — Multi-Reader Safety / Container Support / Zero-Tearing: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest; Reader Latency / Lock Contention: ✔️ = lowest/best)*

| Rank | Strategy | Multi-Reader Safety | Variable-Size Lists (`std::vector`) | Reader Latency | Lock Contention | Quality |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 🏆 🥇 | **`PublishedSnapshotExchange<T, N>` (Split-Reference Multi-Slot Bank)** | 🔴 | 🔴 | ✔️ *(1 atomic RMW)* | ✔️ *(Wait-free)* | ⭐⭐⭐⭐⭐ |
| 🥈 | `std::shared_mutex` + Snapshot Copy | 🔴 | 🔴 | 🚩 | 🚩 *(Writer blocks)* | ⭐⭐⭐ |
| 🥉 | Uncounted Triple Buffer | ✔️ *(Races with ≥2 readers)* | ✔️ | ✔️ | ✔️ | ⭐⭐ |
| — | Seqlock | 🚩 *(Retries under load)* | ✔️ *(UB on non-POD)* | 🚩 | ✔️ | ⭐ |

💡 **Selected Mechanism:** `PublishedSnapshotExchange<SnapshotType, BankSlotCount = 8u>` packs `(SlotIndex | PendingReaders | Epoch)` into one 64-bit atomic word. Readers acquire a wait-free RAII `SnapshotReadLease` via single `fetch_add(PendingReaderUnit, acq_rel)` and release via `ActiveReaderBalance.fetch_sub(1, release)`. Writers recycle free slots with preserved `std::vector` heap capacity — **zero locks, zero tearing, zero ABA, zero steady-state allocations**.

---

## 2. Architecture & Data Flow

```text
GameThread / RenderThread / EditorThread / ExecutionChannels
  │                                                      ▲
  │ SubmitConstraintDirective(ConstraintDirective)       │ AcquireConstraintSnapshot() -> SnapshotReadLease
  │ [Lock-free MPMC AtomicQueue]                         │ [Wait-free 1x atomic RMW, zero locks, zero alloc]
  ▼                                                      │
+----------------------------------------------------------------------------------+
| ThreadBoundaryExchange<ConstraintListSnapshot, ConstraintDirective, 8u>          |
|  • InboundDirectives : AtomicQueue<ConstraintDirective>                          |
|  • OutboundSnapshots : PublishedSnapshotExchange<ConstraintListSnapshot, 8u>     |
+----------------------------------------------------------------------------------+
  │ DrainDirectives() [Step start]                       ▲ PublishSnapshotInPlace() [Step end]
  ▼                                                      │
PhysicsThread -> PhysicsExtension::Tick(Δt)
  ① Drain cross-thread ConstraintDirectives
  ② StepConstraintDrivers() [Sweep, Spin FixedRpm/LinearSpeed/Freewheel, Dock PullTest/Snap/Key]
  ③ JPH::PhysicsSystem::Update() [Deterministic JoltPhysics v5.6.0]
  ④ EvaluateConstraintRuptures() & RefreshConstraintPoseTelemetry() [World poses + telemetry]
  ⑤ PublishConstraintSnapshot() -> Contiguous Constraints[] + O(1) SlotToDenseIndex + StepEvents[]
```

---

## 3. Delivered Modules & Files

| Layer | File | Responsibility |
| :--- | :--- | :--- |
| **Generic Sync** | `Engine/Threading/Sync/PublishedSnapshotExchange.h` | C++20 `concept`-constrained (`PublishableSnapshot`, `SnapshotWriteProcedure`, `SnapshotReadConsumer`) wait-free multi-reader snapshot bank & RAII `SnapshotReadLease` |
| **Generic Boundary** | `Engine/Threading/Sync/ThreadBoundaryExchange.h` | Generic bidirectional boundary pairing `PublishedSnapshotExchange` with `AtomicQueue` (`BoundaryDirective`, `DirectiveIntakeReceiver`) |
| **Physics Thread** | `Engine/Threading/Threads/Physics/PhysicsThread.{h,cpp}` | Dedicated fixed-step OS thread with suspend/resume gating, spiral-of-death clamp, and `ThreadProgressionTracker` heartbeats |
| **Constraints Core** | `Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.{h,cpp}` | All 8 `ProjectZeroEditor` constraint kinds, `ConstraintListSnapshot`, `ConstraintDirective`, `ConstraintTransitionEvent`, Dock key matching, sweep/spin kinematics, and `ComponentClass` conversion |
| **Physics Runtime** | `Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.{h,cpp}` | JoltPhysics v5.6.0 integration of `ThreadBoundaryExchange`, live world attachment poses (replacing Unreal sockets), SwingTwist decomposition, Tether slack/taut, Dock snap/key/rupture lifecycle |
| **Validation** | `Exhibits/CrossThreadConstraintValidation.cpp` | 3-stage validation covering generic multi-thread stress, all 8 constraint kinds, and live `PhysicsThread` + 3 concurrent reader threads |

---

## 4. `ProjectZeroEditor` Constraint Parity Matrix

| # | `ProjectZeroEditor` Kind | Frontier `ConstraintCategory` | Live Physics & Snapshot Features | Status |
| :---: | :--- | :--- | :--- | :---: |
| 1 | `Fixed` | `ConstraintCategory::Fixed` | 6-DOF weld, breakable strain ratio, solved world attachment pose | 🟢 |
| 2 | `Hinge` | `ConstraintCategory::Hinge` | Angular limits, `Hold`/`Steer`/`Sweep`/`MotorPosition`, live angle & RPM | 🟢 |
| 3 | `Slider` | `ConstraintCategory::Slider` | Linear travel stops, motorized `Sweep`/`MotorPosition`, live travel & velocity | 🟢 |
| 4 | `Ball` | `ConstraintCategory::SwingTwist` | Cone + axial twist limits, cone-orbit `Sweep`, exact swing/twist decomposition | 🟢 |
| 5 | `Spring` | `ConstraintCategory::Spring` | Frequency `Hz`, damping ratio `ζ`, preload pose, runtime retuning & bump impulse | 🟢 |
| 6 | `Spin` | `ConstraintCategory::Spin` | `FixedRpm`, `LinearSpeed / RollingRadius`, and `Freewheel` coasting + live RPM | 🟢 |
| 7 | `Tether` | `ConstraintCategory::Tether` | Unilateral slack-to-taut rope, live length, `TautEngaged`, pendulum swing angle | 🟢 |
| 8 | `Dock` | `ConstraintCategory::Dock` | Key-filtered latch (`ComputeDockKeyHash`/`VerifyDockKeyAccepted`), `SnapRadiusMeters`, `PullTest`/`AeroLoad` break, `Connected`/`Released`/`Returning`/`Rejected` phases & events | 🟢 |

---

## 5. Verification Results

| Validation Target | Result | Measured Metrics |
| :--- | :---: | :--- |
| **`CrossThreadConstraintValidation`** | 🟢 PASS | 25/25 checks · 1,500 generic producer wraps (4 readers, 0 torn reads, 800/800 directives) · all 8 constraint kinds + Dock relatch/reject · live `PhysicsThread` (`851` Jolt steps, `25,090` GameReads, `56,282` RenderReads, `75,041` EditorReads, `0` errors) |
| **`ThreadSanitizer (-fsanitize=thread)`** | 🟢 PASS | `1,313` `PhysicsThread` steps · `4,001` concurrent multi-reader leases · `400` MPMC directives · **0 data races** |
| **`ComponentClassPhysicsValidation`** | 🟢 PASS | 23/23 checks (full regression pass) |
| **`PhysicsSpaceValidation`** | 🟢 PASS | Full Jolt + broadphase + breakable bond regression pass |
