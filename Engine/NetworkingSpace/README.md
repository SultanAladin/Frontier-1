# 🧩 `Engine/NetworkingSpace/`

> **TL;DR**: Sector-relative 24-bit sub-millimeter quantization, smallest-three 32-bit quaternions, baseline delta encoding, and slerp windowing (10 source/header/shader files under `Engine/NetworkingSpace/`).

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Policies/`](Policies/README.md) | Identity consent scope enforcement, explicit continuance-token approval window, token redaction, and session policy degradation |
| [`Transports/`](Transports/README.md) | Epic Online Services P2P + Lobby/Session transport adapter with mandatory 4-scope enforcement and zero-SDK open-source stub seam |

## Files

| File | Purpose |
| :--- | :--- |
| `DifferentialWorldEncoder.cpp` | Sector-relative 24-bit XYZ quantization, smallest-three 32-bit quaternions, baseline delta codec, slerp, and 64-bit alignment digest |
| `DifferentialWorldEncoder.h` | Sector-relative 24-bit sub-millimeter quantization, smallest-three 32-bit quaternions, baseline delta encoding, and slerp windowing |
| `NetworkEndpointRegister.cpp` | Peer endpoint registration, RFC 6298 RTT/jitter EWMA smoothing, modular 32-bit ACK windowing, and synthetic filler isolation |
| `NetworkEndpointRegister.h` | Peer endpoint roster, RFC 6298 EWMA RTT/jitter/clock-offset telemetry, sliding 32-bit ACK tracking, and synthetic slot isolation |
| `NetworkingExtension.cpp` | SuspendableParticipant networking orchestrator: deterministic failover, framing, chunk reassembly, and reverse-order teardown |
| `NetworkingExtension.h` | SuspendableParticipant networking orchestrator unifying transports, endpoint telemetry, policies, framing, and chunk reassembly |
| `PacketSerializer.cpp` | Castagnoli CRC32C checksum, 'FP1' + 24B envelope wire framing, and bounded 64-slot 'FCNK' chunk fragmentation/reassembly |
| `PacketSerializer.h` | Deterministic 'FP1' wire framing, 24-byte sequence/ACK/CRC32C envelope, and bounded 'FCNK' 4 MiB chunk fragmentation/reassembly |
| `RemoteDirectiveRouter.cpp` | 5-byte input quantization, 3-tick packet-loss healing redundancy ring, and reliable in-order command directive delivery |
| `RemoteDirectiveRouter.h` | 5-byte quantized input directives with 3-tick rolling loss redundancy plus reliable ordered workspace/session directive ring |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::NetworkingSpace`
- **Primary Types**: `UnitQuaternion`, `ReplicatedActorKinematics`, `QuantizedSpatialEntry`, `WorldSnapshotView`, `EndpointParticipation`, `EndpointAttributeMask`, `EndpointTelemetryEntry`, `InboundEnvelopeEntry`, `PacketMessageCategory`, `NetworkEnvelopeHeader`, `DecodedWireDatagram`, `ChunkTransferHeader`, `CompletedChunkTransfer`, `DiscreteTriggerMask`, `QuantizedInputDirective`, `ReliableDirectiveEntry`

## Usage

```cpp
#include "Engine/NetworkingSpace/DifferentialWorldEncoder.h"
#include "Engine/NetworkingSpace/NetworkEndpointRegister.h"
#include "Engine/NetworkingSpace/NetworkingExtension.h"

using namespace Frontier::NetworkingSpace;
UnitQuaternion Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
