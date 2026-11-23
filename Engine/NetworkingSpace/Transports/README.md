# 🧩 `Engine/NetworkingSpace/Transports/`

> **TL;DR**: Epic Online Services P2P + Lobby/Session transport adapter with mandatory 4-scope enforcement and zero-SDK open-source stub seam (11 source/header/shader files under `Engine/NetworkingSpace/Transports/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `EpicOnlineTransport.cpp` | Proprietary Epic Online Services SDK adapter compilation unit (compiled only when FRONTIER_ENABLE_EOS_SDK=ON) |
| `EpicOnlineTransport.h` | Epic Online Services P2P + Lobby/Session transport adapter with mandatory 4-scope enforcement and zero-SDK open-source stub seam |
| `EpicStubTransport.cpp` | Open-source zero-proprietary-SDK Epic Online Services stub enforcing scope/hex-key validation and graceful transport failover |
| `LoopbackTransport.cpp` | Deterministic SplitMix64 intra-application packet switch and zero-socket LoopbackTransport implementation |
| `LoopbackTransport.h` | Deterministic intra-application packet switch with SplitMix64 latency, jitter, reordering, and loss simulation for offline/CI runs |
| `PhotonRealtimeTransport.cpp` | Proprietary Photon Realtime C++ SDK adapter compilation unit (compiled only when FRONTIER_ENABLE_PHOTON_SDK=ON) |
| `PhotonRealtimeTransport.h` | Photon Realtime Core cloud room transport with Epic JWT custom-authentication hygiene, relay region assignment, and open-source stub |
| `PhotonStubTransport.cpp` | Open-source zero-proprietary-SDK Photon Realtime stub enforcing Epic JWT verification, token scrubbing, and failover routing |
| `SocketTransport.cpp` | Non-blocking BSD/Winsock2 UDP socket, RFC 5389 STUN reflexive discovery, RFC 8439 ChaCha20-Poly1305 AEAD, and zero-key relay |
| `SocketTransport.h` | Sovereign non-blocking OS UDP transport with RFC 5389 STUN NAT discovery, RFC 8439 ChaCha20-Poly1305 AEAD, and stateless relay |
| `TransportInterface.h` | Zero-vtable C++20 PacketTransportChannel concept and canonical wire datagram carrier across all four Frontier transports |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::NetworkingSpace`
- **Primary Types**: `EpicSessionCredentials`, `LoopbackImpairmentProfile`, `PhotonRoomConfiguration`, `IpSocketAddress`, `StatelessRelayRouteEntry`, `PacketDatagram`

## Usage

```cpp
#include "Engine/NetworkingSpace/Transports/EpicOnlineTransport.h"
#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"
#include "Engine/NetworkingSpace/Transports/PhotonRealtimeTransport.h"

using namespace Frontier::NetworkingSpace;
EpicSessionCredentials Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
