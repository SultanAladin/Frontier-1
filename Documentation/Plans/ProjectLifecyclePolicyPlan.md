# Projects Root, Reference Project `Untitled`, Tight `RuntimeLifecycle` & Unified Policy Plan

> **Status**: 🟢 Active Implementation (`2026-11-23`)  
> **Branch**: `arena/2809d087-frontier`  
> **Reference Project**: `Projects/Untitled/` (`UntitledLifecycle`, zero Editor bloat)  
> **Governance**: Strict compliance with [`FolderStructure.md`](../FolderStructure.md), [`BannedWordsVocabulary.md`](../BannedWordsVocabulary.md), [`CodingStandards.md`](../CodingStandards.md), [`SerializationArchitecturePlan.md`](SerializationArchitecturePlan.md), and [`NetworkingPolicyBringupPlan.md`](NetworkingPolicyBringupPlan.md).

---

## 1. Executive Summary

With Frontier's 63-format `DocumentContainerCodec` (`TOML Header` + `64 B`-aligned binary sections + `MemoryMappedFileView` + `ExecutionChannels` + `AssetStreamThread`) complete and verified (`12/12` `SerializationValidation` gates), this phase delivers three tightly coupled pillars:

1. **Tight, Zero-Editor-Bloat Runtime Main Loop (`RuntimeLifecycle` + `ProjectExecutionPolicy`)**:
   - Upgrades `Engine/Lifecycle/RuntimeLifecycle.{h,cpp}` with TOML boot-chain loading (`BootConfigurationLoader`), fixed-timestep simulation accumulator with interpolation alpha and spiral-of-death clamping, opt-in `ExecutionChannels` + `AssetStreamThread` attachment, and declarative `ProjectExecutionPolicy` — strictly zero `Engine/Editor/` or `ImGui` symbols.
   - Replaces the minimal 4-tick stub in `FrontierRunShippingProjectLoop` (`ProjectExecutionRunner.cpp`) with a genuine `RuntimeLifecycle` driver.
2. **Canonical `Projects/` Root & Reference Project `Projects/Untitled/`**:
   - Establishes the top-level `Projects/` directory (`Projects/CMakeLists.txt`) isolated cleanly from `Engine/`.
   - Creates **`Projects/Untitled/`** as the clean, lean reference template (`UntitledLifecycle`, `UntitledPolicies`, `Configuration/*.{EngineSettings,RuntimeConfiguration,PlatformSettings}`, `Content/Untitled.ProjectIndex`, `Content/Worlds/MainWorld.WorldSpace`, `Source/UntitledLaunch.cpp`) where game/project code writes only what it wants with zero editor bloat.
3. **Complete Unified Policy Suite (`NetworkingPolicyBringupPlan.md` §3)**:
   - Integrates the 4 existing `Engine/NetworkingSpace/Policies/` (`AuthenticationScopePolicy`, `TransportSelectionPolicy`, `ReplicationPolicy`, `SessionLifecyclePolicy`) with the 3 new declarative C++20 policies:
     - **`Engine/Lifecycle/ProjectExecutionPolicy.{h,cpp}`** (fixed cadence, worker channels, opt-in domain mask, frame-budget guardrails)
     - **`Engine/SimulationSpace/Policies/GameplayRulePolicy.{h,cpp}`** (`< 128 B` wire sync, `64-bit` deterministic world alignment hash, host directive bounds & rate validation, traffic/pursuit rule thresholds)
     - **`Engine/SimulationSpace/Policies/DeterministicDrivingPolicy.{h,cpp}`** (`16 → 20 → 3` `403`-float / `1,612 B` `Tanh` driving MLP, deterministic seeded PRNG, `4 B` quantized control directive codec, `CEM-RL` self-play trainer, and `DocumentContainerCodec` `64 B`-aligned binary serialization).

---

## 2. Canonical `Projects/Untitled/` Reference Layout

```text
Projects/
├── CMakeLists.txt                                      // Wires active standalone projects cleanly into the root build
└── Untitled/                                           // Reference lean project (zero Editor / ImGui bloat)
    ├── CMakeLists.txt                                  // Builds standalone executable: Untitled (links FrontierEngine only)
    ├── Configuration/
    │   ├── EngineDefault.EngineSettings                // Fixed cadence, memory ceilings, startup ProjectIndex link
    │   ├── RuntimeDefault.RuntimeConfiguration         // ProjectExecutionPolicy: opt-in domains, thread channels, budget
    │   └── PlatformDefault.PlatformSettings            // Desktop / Headless window & timing overrides
    ├── Content/
    │   ├── Untitled.ProjectIndex                       // Startup world & project asset root declaration
    │   └── Worlds/
    │       └── MainWorld.WorldSpace                    // Sectioned DocumentContainer world file
    └── Source/
        ├── UntitledLifecycle.{h,cpp}                   // Reference tight project loop driving RuntimeLifecycle
        ├── UntitledPolicies.{h,cpp}                    // Project-owned Execution / Gameplay / Driving / Networking policies
        └── UntitledLaunch.cpp                          // Lean CLI/window entrypoint (--headless, --ticks=N, --verify-policy)
```

---

## 3. Architecture Decisions

| Decision Area | Selected Design | Why |
| :--- | :--- | :--- |
| **Loop Isolation** | `Untitled` links strictly `FrontierEngine` (zero `FrontierImGui`, zero `Engine/Editor/`). | Keeps shipping/reference executable tight, fast, and compile-time verified free of Editor bloat (`CLAUDE.md` §2.4). |
| **Simulation Timing** | Fixed-timestep accumulator (`FixedStepSeconds` + `MaximumSubstepsPerTick` + `InterpolationAlpha`) inside `RuntimeLifecycle`. | Guarantees deterministic `GameplayRulePolicy` 64-bit world alignment hashes and identical `DeterministicDrivingPolicy` rollouts across machines. |
| **Opt-In Subsystems** | `ProjectExecutionPolicy::EnabledDomainMask` bitmask (`WorkerChannels`, `AssetStreaming`, `Physics`, `Audio`, `Networking`, `WorldRender`). | Projects never allocate thread pools, audio buses, or networking sockets they did not request. |
| **Policy Persistence** | Every new policy serializes cleanly via `Serialization::DocumentContainerCodec` (`TOML Header` + `64 B`-aligned binary section) and compact binary wire packets (`< 128 B` rules, `1,612 B` MLP weights, `4 B` control directives). | One unified wire + disk story across local authoring, `Projects/Untitled/`, and `NetworkingSpace`. |

---

## 4. Implementation Milestones & Automated Verification

| Phase | Deliverables | Verification Gate (`ProjectLifecyclePolicyValidation`) |
| :---: | :--- | :--- |
| **P1** | `Engine/Lifecycle/ProjectExecutionPolicy.{h,cpp}`, upgraded `RuntimeLifecycle.{h,cpp}`, upgraded `ProjectExecutionRunner.cpp`. | TOML boot chain loading from `Projects/Untitled/`, fixed-step accumulator determinism, opt-in worker/stream channels, and zero-Editor shipping runner. |
| **P2** | `Engine/SimulationSpace/Policies/GameplayRulePolicy.{h,cpp}` & `DeterministicDrivingPolicy.{h,cpp}`. | `< 128 B` wire pack/unpack, `64-bit` alignment hash mismatch detection, host directive rejection, `1,612 B` `DocumentContainerCodec` round-trip, `4 B` quantized controls, and deterministic `CEM-RL` epoch progression. |
| **P3** | `Projects/CMakeLists.txt` + `Projects/Untitled/` (`Configuration/`, `Content/`, `Source/UntitledLifecycle.{h,cpp}`, `Source/UntitledPolicies.{h,cpp}`, `Source/UntitledLaunch.cpp`) + `CMakeLists.txt` wiring. | Standalone `Untitled` binary builds cleanly without `FrontierImGui` and passes `Untitled --headless --ticks=12 --verify-policy` (`exit 0`). |
| **P4** | `Exhibits/ProjectLifecyclePolicyValidation.cpp` + `Exhibits/Images/Projects/UntitledLifecyclePolicyVerification.png` + full CTest pass + `MarkdownStudioViewer.html` rebuild. | 100% pass across `ProjectLifecyclePolicyValidation`, `SerializationValidation`, `LifecycleValidation`, `NetworkingSpaceValidation`, `DataDrivenSpaceContentValidation`, and `ProjectBrowserValidation`. |
