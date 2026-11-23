# 🧩 `Engine/NetworkingSpace/Policies/`

> **TL;DR**: Identity consent scope enforcement, explicit continuance-token approval window, token redaction, and session policy degradation (8 source/header/shader files under `Engine/NetworkingSpace/Policies/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AuthenticationScopePolicy.cpp` | Identity consent scope enforcement, continuance-token approval gate, credential redaction, and client-policy fallback |
| `AuthenticationScopePolicy.h` | Identity consent scope enforcement, explicit continuance-token approval window, token redaction, and session policy degradation |
| `ReplicationPolicy.cpp` | Standard kinematic and collaborative workspace replication contracts plus frequency-gated publication checks |
| `ReplicationPolicy.h` | Authority ownership, transport delivery reliability channels, component publication masks, and rate-limited publish scheduling |
| `SessionLifecyclePolicy.cpp` | Strict Lobby vs. Match-Session separation, synthetic participant filtering, and reverse-order teardown transitions |
| `SessionLifecyclePolicy.h` | Strict Lobby vs. Match-Session separation, synthetic participant isolation, and mandatory reverse-order teardown enforcement |
| `TransportSelectionPolicy.cpp` | Minimax global relay cluster selection (US <-> China <-> Egypt) and transport display metadata |
| `TransportSelectionPolicy.h` | Pure deterministic transport selection, entitlement routing, minimax global relay cluster choice, and failover policy |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::NetworkingSpace`
- **Primary Types**: `IdentityScopeMask`, `ContinuanceConsentPhase`, `SessionRegistrationResolution`, `ContinuanceConsentGate`, `SessionRegistrationOutcome`, `AuthorityMode`, `DeliveryChannel`, `ReplicationComponentMask`, `ReplicationScheduleSpec`, `LobbyLifecyclePhase`, `MatchSessionPhase`, `SessionParticipantOrigin`, `SessionParticipantCandidate`, `SessionLifecycleLedger`, `TransportChannelCategory`, `EntitlementAccessTier`, `TransportLinkAvailabilityMask`, `RelayRegionCluster` (+3 more)

## Usage

```cpp
#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"
#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"
#include "Engine/NetworkingSpace/Policies/SessionLifecyclePolicy.h"

using namespace Frontier::NetworkingSpace;
IdentityScopeMask Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
