# NetworkingSpace, Multi-Transport (Sovereign UDP + EOS + Photon) & Gameplay/Network Policy Bringup Plan

> **Branch**: `arena/429d7dc7-frontier`  
> **Reference Audit**: `unassignedinbox/Frontier-OLD` (`arena/255c3b21-frontier`)  
> **Governance**: Strict compliance with [`FolderStructure.md`](../FolderStructure.md), [`BannedWordsVocabulary.md`](../BannedWordsVocabulary.md), and [`SKILL-Naming.md`](../AgenticInstructions/SKILL-Naming.md).

---

## 1. Executive Answers to Key Architectural Questions

### 1.1 Does `NetworkingSpace` Depend on Anything?
**No — `Engine/NetworkingSpace/` has ZERO GPU, Vulkan, Shader, MaterialSpace, or Editor UI dependencies.**

| Dependency Layer | Required by `NetworkingSpace`? | Details |
| :--- | :---: | :--- |
| **Vulkan / `.slang` Shaders / `MaterialSpace` / `GeometricRaster`** | **No (`0` files)** | Completely orthogonal to the rendering bringup (`Steps 1–5`). |
| **`Engine/Editor/` or Windowing (`GLFW`)** | **No (`0` files)** | Runs headless on Linux, Windows, Android, dedicated relays, and CI. |
| **Proprietary Third-Party Binaries (`EOSSDK`, `Photon`) at Compile Time** | **No (Stub-Isolated)** | Link-seam stubs (`EpicStubTransport`, `PhotonStubTransport`) keep every build 100% green when vendor binaries are absent, and activate live `EOS` + `Photon` when linked. |
| **Pure CPU Foundation (`C++20 STL`, `Engine/Math/`, `Engine/Codecs/`)** | **Yes (Header/CPU only)** | Uses fixed-width integer serialization, quaternion/vector quantization, and `.NetworkSpace` hybrid container encoding. |

Because `Engine/NetworkingSpace/` and the Rendering stack share **zero source files**, **we can execute Networking BEFORE Rendering, AFTER Rendering, or in isolated steps without merge conflicts**. Section 8 compares both execution orders side by side.

---

### 1.2 How Internet "Serverless" P2P Actually Connects `US ↔ China ↔ Egypt`
When Epic Online Services (`EOS`) or WebRTC advertises *"no server required"*, that means **no dedicated gameplay simulation server is required from the developer** — **not** that zero internet servers exist. Home/mobile PCs in the **US** (`NAT`), **Egypt** (strict ISP `CGNAT`), and **China** (`NAT` + cross-border `GFW` filtering) cannot address one another directly without three lightweight network mechanisms (`ICE`, `RFC 8445`):

1. **Signaling / Rendezvous (`HTTPS` / `WebSocket` / `EOS Lobby` / `Photon NameServer`)**:
   - Peers connect *outbound* to a known endpoint, verify identity tokens, join a session identifier, and exchange encrypted session keys + candidate `IP:Port` endpoints (`< 2 KB`).
2. **`STUN` Endpoint Discovery + Simultaneous UDP Hole Punching (`RFC 5389`)**:
   - Each client discovers its router's external `IP:Port` and sends simultaneous outbound UDP datagrams to its peer. Both routers register outbound pinholes, allowing **direct player-to-player UDP packets over the internet** (~70–80% of global pairs; **zero relay bandwidth after the 200 ms handshake**).
3. **`TURN` / Encrypted Packet Relay Fallback (`RFC 5766` / `EOS_P2P` Relay / `Photon Cloud` / Sovereign `RelayEndpoint`)**:
   - When Symmetric `NAT` or strict `CGNAT` (common in **Egypt** and mobile/fiber ISPs in **China**) randomizes ports per destination, hole punching is blocked. Traffic automatically routes through a geographically proximate **Packet Relay** (e.g., **Frankfurt** `~60 ms` from Egypt, **Singapore/Tokyo/Hong Kong** `~45–70 ms` from China, **US-East** for North America).
   - Because Frontier transmits **quantized binary deltas and `4–16 byte` deterministic vehicle/gameplay directives** (`~2–6 KB/s` per peer), even relayed sessions consume negligible bandwidth.

---

### 1.3 `Frontier-OLD` Audit: **Translate vs. Clean Governed Rewrite?**
**Verdict: Clean Governed C++20 Rewrite — preserving 100% of `Frontier-OLD`'s proven wire protocols, `EOS` + `Photon` auth/routing rules, and Gameplay/Network Policy semantics, while eliminating legacy coupling and banned vocabulary.**

Forensic audit of `unassignedinbox/Frontier-OLD` (`arena/255c3b21-frontier`) revealed two separate networking trees (`Frontier/Extensions/Networking/` and `Frontier/Projects/Project-Networking/Source/`) plus the pursuit policy (`Experimental/VehicleProvingGrounds/Source/PolicePursuitSequence.js`):

| `Frontier-OLD` Artifact | What Worked & Must Be Preserved | Why Direct Copy Fails & Rewrite Is Required |
| :--- | :--- | :--- |
| **`TransportCodec.h` (`'FP1'`) & `NetworkTransport.h` (`'FCNK'`)** | Bounds-checked `'FP1'` 7-byte wire header + `'FCNK'` 20-byte chunk fragmentation & reassembly (`MaxPayload = 4096 B`); `LoopbackTransport` & relay adapter pattern. | Used banned identifiers (`ReplicationKind`, `ValidKind`, `AssemblyBuffer`, `IngestFrame`, `SendFrameFn`) and wrapped verbose ASCII TOML strings (`PrintTable`) instead of binary quantized deltas. |
| **`AuthPolicy.h`, `EpicExchange.*`, `LobbyRuntime.cpp`** | Required `EOS` 1.19.2 consent scopes (`BasicProfile \| FriendsList \| Presence \| Country`), continuance-token explicit profile consent (`180 s`), `PersistentAuth` auto-login, separated `Lobby` vs. `Match Session` with reverse teardown, `EOS_ClientPolicyMissingAction` graceful fallback, zero token logging. | Written around process-global static variables (`Session`, `Phase`, `Failure[256]`) preventing multi-peer in-process validation; used banned words (`LobbyState`, `Role`, `Action`). |
| **`PhotonTransport.*`, `PhotonLinkStub.cpp`, `TransportRouter.*`** | `Photon Realtime Core` 6.0.0.347 (`/MD` x64) + Epic JWT `CustomAuthentication` + `PhotonLinkStub` compile-anywhere seam + automatic fallback from `Photon` to `EOS` on failure. | Used banned `TransportKind` / `PhotonState` identifiers and global mutable singletons; hardcoded single dev room instead of policy-driven room/region routing. |
| **`ReplicationProfile.h` & `ReplicationExchange.h`** | Opt-in per-domain authority, publish rate (`Hz`), transform slerp interpolation buffer, and `.networkspace` configuration file. | Used banned boolean/method names (`IsActive`, `ShouldPublishUpdate`, `ComposedSceneEntry`, `FieldBinding`, `RecordStore`). |
| **`PolicePursuitSequence.js` (`NeuralDrivingPolicy`)** | Deterministic `16 → 20 → 3` `Tanh` driving policy (`403` `f32` weights = `1,612 B`), 9-ray rangefan inputs, seeded deterministic PRNG, Elitist Cross-Entropy (`CEM-RL`) live self-play, and traffic rule triggers. | Authored in JS for the browser prototype; porting cleanly to SIMD-friendly C++20 lets both **local AI pursuit cruisers** and **networked autonomous vehicles** run deterministically at `480 Hz` from a `1,612 B` weight packet or `4 B` quantized control directive. |

---

## 2. Four-Transport Pluggable Architecture (`Sovereign UDP` + `EOS` + `Photon` + `Loopback`)

`Engine/NetworkingSpace/` unifies **four concrete transports** behind a single pure-virtual `TransportInterface` (zero `std::function` heap churn on the hot send/receive path, zero global singletons):

```
                           ┌───────────────────────────────────────────────┐
                           │          NetworkingExtension (CPU)            │
                           │  DifferentialWorldEncoder + DirectiveRouter   │
                           └───────────────────────┬───────────────────────┘
                                                   │
                                    TransportSelectionPolicy (Pure Rule)
                                                   │
         ┌─────────────────────────┬───────────────┴───────────────┬─────────────────────────┐
         ▼                         ▼                               ▼                         ▼
┌───────────────────┐   ┌─────────────────────┐         ┌─────────────────────┐   ┌─────────────────────┐
│ LoopbackTransport │   │   SocketTransport   │         │ EpicOnlineTransport │   │PhotonRealtimeTransp.│
│ (In-memory multi- │   │ (Sovereign UDP +    │         │ (EOS 1.19.2 P2P +   │   │ (Photon Core 6.0 +  │
│  endpoint + RTT / │   │  STUN hole-punch +  │         │  Lobby + Sessions + │   │  Epic JWT Auth +    │
│  loss simulator)  │   │  ChaCha20 Relay)    │         │  RTC Voice)         │   │  EU/US/Asia Cloud)  │
└───────────────────┘   └─────────────────────┘         └─────────────────────┘   └─────────────────────┘
                                                                   ▲                         │
                                                                   └──── Automatic Failover ─┘
```

### How Each Transport Serves Global `US ↔ China ↔ Egypt` Sessions
1. **`LoopbackTransport` (Zero-OS Deterministic Validation)**:
   - Connects `2..16` virtual peers in-memory inside one test executable with configurable latency/jitter/drop emulation (e.g., simulating `US = 0 ms`, `Egypt = 65 ms + CGNAT`, `China = 210 ms + 2% loss`).
2. **`SocketTransport` + `LightweightRelayEndpoint` (100% Sovereign, Zero Third Parties)**:
   - Direct non-blocking IPv4/IPv6 UDP sockets + `RFC 5389` `STUN` simultaneous hole-punching + `ChaCha20-Poly1305` AEAD packet encryption + stateless `~600`-line C++20 relay executable deployable on any `$5` VPS (`port 443/UDP` friendly against ISP deep-packet throttling in Egypt/China).
   - Works offline on LAN, on private studio servers, or globally over the public internet **even if Epic and Photon are completely unreachable**.
3. **`EpicOnlineTransport` (`EOS` SDK `1.19.2`)**:
   - Default free internet P2P & social layer: Epic `Auth` (`PersistentAuth` first, Account Portal fallback) → `Connect` (`ProductUserId`) → separated **Lobby** (`RTC` voice room, member attributes, tag search) and **Match Session** (`EOS_Sessions` lifecycle) → **`EOS_P2P`** (`NAT` punch-through + automatic Epic AWS `TURN` relay).
   - Compiles against `EpicStubTransport.cpp` whenever `EOSSDK` headers/binaries are not staged.
4. **`PhotonRealtimeTransport` (`Photon Realtime Core` `6.0` `/MD`)**:
   - Managed low-jitter cloud room transport authenticated directly with the player's **Epic Identity JWT** (`CustomAuthentication`, zero second login prompt!).
   - Automatically selects regional clusters (`eu` Frankfurt for Europe/Egypt, `asia`/`jp`/`hk` for China/East Asia, `us` for Americas).
   - **Resilient Failover**: If `Photon` disconnects, hits CCU limits, or is blocked on a player's route, `TransportSelectionPolicy` seamlessly falls back to `EpicOnlineTransport` (`EOS_P2P`) or `SocketTransport` without dropping the player's logged-in session.

---

## 3. Unified Policy Architecture (Shared Across Gameplay & Networking)

In `Frontier`, a **Policy** is a **small, deterministic, POD/constexpr-friendly C++20 rule structure** (serialized in `.NetworkSpace` / `.GameplaySpace` via `Engine/Codecs/`) that governs **who has authority, what is permitted, how transport/routing adapts, and how autonomous or rule-enforced gameplay behaves** — with zero hardcoded `if/else` spaghetti scattered across subsystems.

### 3.1 Networking Policies (`Engine/NetworkingSpace/Policies/`)

1. **`AuthenticationScopePolicy.{h,cpp}`** *(rewritten from `Frontier-OLD` `AuthPolicy.h` + `EpicExchange` consent guards)*:
   - Declares required identity permissions (`BasicProfile | FriendsList | Presence | Country`) so optional UI toggles (e.g., *"Load Friends List"*) never omit a mandatory portal consent scope.
   - Enforces **Continuance-Token Explicit Profile Consent** (`180 s` window; never auto-creates an unlinked account silently, never auto-retries cancelled logins).
   - Enforces **Secret & Token Redaction** (`RedactCredentialText`: JWTs, continuance tokens, and storage keys are wiped on teardown and mathematically barred from diagnostic logs).
   - Handles **Portal Client-Policy Capability Degradation**: if `EOS_Sessions_RegisterPlayers` returns `EOS_ClientPolicyMissingAction`, `SessionRegistrationPolicy` degrades cleanly to an unregistered lifecycle session with a clear diagnostic rather than aborting match start.

2. **`TransportSelectionPolicy.{h,cpp}`** *(rewritten from `Frontier-OLD` `TransportRouter.h`)*:
   - Pure, SDK-free selection rule (`SelectActiveTransport(EntitlementTier, LinkAvailabilityMask, RegionLatencyProfile, FallbackAttemptCount)`):
     - Routes `EntitlementTier::Premium` + linked Photon SDK → `TransportChannel::PhotonRealtime`, with automatic failover → `TransportChannel::EpicOnline` → `TransportChannel::SovereignSocket`.
     - Selects optimal relay region (`eu-central`, `ap-east`, `us-east`) from measured RTT probes so `US + China + Egypt` rooms minimize worst-case peer RTT.

3. **`ReplicationPolicy.{h,cpp}`** *(naming-clean C++20 upgrade of `Frontier-OLD` `ReplicationProfile.h`)*:
   - Attached opt-in to any world object, physics body, vehicle, particle effect, or force volume:
     - `AuthorityMode` (`enum class AuthorityMode : uint8_t`):
       - `Disabled` (`0`) — purely local object
       - `HostAuthoritative` (`1`) — host simulates and publishes quantized deltas
       - `InterpolatedProxy` (`2`) — remote peer buffers & slerps transforms over `InterpolationDelaySeconds`
       - `OwnerDirective` (`3`) — owning client sends deterministic input directives; host validates against `GameplayRulePolicy`
     - `DeliveryChannel` (`enum class DeliveryChannel : uint8_t`):
       - `BaselineSnapshot` (`0`), `QuantizedTransform` (`1`), `ReliableDirective` (`2`), `DeterministicInput` (`3`)
     - `PublishFrequencyHz` (`1..240 Hz`), `OwnerEndpointIndex`, `RevisionWatermark`, and `QueryPublishDue(CurrentRevision, DeltaSeconds)` (replaces banned `IsActive()` / `ShouldPublishUpdate()`).

4. **`SessionLifecyclePolicy.{h,cpp}`** *(from `Frontier-OLD` `Docs/NetworkingLobbyPlan.md` rules)*:
   - Enforces the strict **Lobby vs. Match-Session Separation & Teardown Invariant**:
     - Leaving, kicking, or disconnecting **always** ends/unregisters/destroys the active Match Session *before* leaving/destroying the Lobby.
     - Local solo-testing `SyntheticPeer` slots (`Dummy` readiness slots in `Frontier-OLD`) are explicitly tagged `SyntheticLocalOnly` so they test lobby readiness locally while **never** registering fake PUIDs, RTC audio streams, or cloud competitive statistics.

---

### 3.2 Gameplay & Autonomous Driving Policies (`Engine/GameplaySpace/Policies/` or `NetworkingSpace` Consumers)

The exact same declarative Policy model extends directly into **Gameplay** (and replicates effortlessly over `NetworkingSpace`):

1. **`GameplayRulePolicy.{h,cpp}`** *(traffic enforcement, match rules & anti-desync authority)*:
   - Synchronized once at match start (`< 128 bytes` binary / `.NetworkSpace` section) so `US`, `China`, and `Egypt` peers enforce identical simulation rules:
     - Simulation fixed tick (`e.g., 120 Hz` or `480 Hz` vehicle substep) + periodic **`64-bit` Deterministic World Checksum (`VerifyWorldAlignmentHash`)** every `60` ticks to detect any floating-point drift immediately.
     - Pursuit & traffic law thresholds ported from `Frontier-OLD` (`SpeedLimitKmh`, `RedSignalPenalty`, `CollisionImpulseThresholdNs`, `PursuitEscalationStages`, `JoinInProgressAllowed`, `HostMigrationMode`).
     - **Server/Host Directive Validation**: When an `OwnerDirective` client sends a vehicle control or interaction directive, the host validates bounds (`Steer ∈ [-1, +1]`, rate limits, ownership `EndpointIndex`) through `GameplayRulePolicy` before applying.

2. **`DeterministicDrivingPolicy.{h,cpp}`** *(C++20 port of `Frontier-OLD` `NeuralDrivingPolicy` `16 → 20 → 3` `Tanh` + `CEM-RL`)*:
   - Compact `403`-parameter (`1,612-byte`) deterministic `16 → 20 → 3` `Tanh` MLP (`9`-ray rangefan + relative pursuit kinematics → `[Steer, Throttle, Brake]`) paired with a deterministic LCG/PCG seeded generator and Elitist Cross-Entropy (`CEM-RL`) self-play trainer.
   - **Why This Unlocks Ultra-Low-Bandwidth Global Multiplayer**:
     - Instead of replicating `60 Hz` rigid-body transforms for `20` AI pursuit cruisers & traffic cars across a `220 ms` `US ↔ China ↔ Egypt` connection (`20 × 48 B × 60 Hz = 57.6 KB/s`), the host replicates **only the `1,612-byte` `DeterministicDrivingPolicy` weight slab once + `32-bit` deterministic seed** (or **`4-byte` quantized `[Steer, Throttle, Brake]`** per bot)! Every peer steps the exact same pursuit behavior locally with near-zero bandwidth!

---

## 4. Governed Folder & File Layout (`0` Banned Words)

This layout builds directly on the **`Engine/NetworkingSpace/`** root already reserved in [`Documentation/FolderStructure.md`](../FolderStructure.md) (lines 285–292), adding the `Policies/` and `Transports/` subdirectories plus `.NetworkSpace` codec support in `Engine/Codecs/`:

```
Engine/
├── Codecs/
│   └── NetworkCodec.{h,cpp}                       // .NetworkSpace hybrid TOML header + binary policy/session payload codec
│
└── NetworkingSpace/                               // Zero-GPU distributed simulation, multi-transport & policy engine
    ├── NetworkingExtension.{h,cpp}                // Lifecycle coordinator: ticks transports, policies, deltas & directives
    ├── NetworkEndpointRegister.{h,cpp}            // Connected peer endpoints, RTT/jitter estimator, clock offset & synthetic mask
    ├── PacketSerializer.{h,cpp}                   // 'FP1' 7B envelope + 'FCNK' 20B chunk fragmentation/reassembly + CRC32C
    ├── DifferentialWorldEncoder.{h,cpp}           // Baseline-relative quantized transform/kinematics delta encoder & slerp window
    ├── RemoteDirectiveRouter.{h,cpp}              // Reliable ordered remote directives & 4-byte quantized input command ring
    │
    ├── Policies/                                  // Declarative C++20 Networking + Gameplay governance policies
    │   ├── AuthenticationScopePolicy.{h,cpp}      // EOS consent scopes, continuance-token approval, token redaction
    │   ├── TransportSelectionPolicy.{h,cpp}       // Pure Photon / EOS / Sovereign-UDP selection, region & failover rules
    │   ├── ReplicationPolicy.{h,cpp}              // Per-object AuthorityMode, DeliveryChannel, PublishFrequencyHz & watermark
    │   ├── SessionLifecyclePolicy.{h,cpp}         // Lobby vs. Match-Session split, reverse teardown & SyntheticPeer isolation
    │   ├── GameplayRulePolicy.{h,cpp}             // Fixed-step rate, 64-bit alignment hash, pursuit/traffic rules & bounds check
    │   └── DeterministicDrivingPolicy.{h,cpp}     // 16->20->3 (403-float) Tanh driving policy + deterministic CEM-RL self-play
    │
    └── Transports/                                // Pluggable wire transports behind TransportInterface
        ├── TransportInterface.h                   // Pure interface: TransmitPacket, PollPackets, QueryTransportName, MaxBytes
        ├── LoopbackTransport.{h,cpp}              // In-memory N-peer router with configurable US/China/Egypt RTT & loss model
        ├── SocketTransport.{h,cpp}                // Sovereign non-blocking UDP + RFC 5389 STUN punch + ChaCha20-Poly1305 relay
        ├── EpicOnlineTransport.{h,cpp}            // EOS 1.19.2 Auth/Connect, Lobby, Sessions, RTC Voice & EOS_P2P adapter
        ├── EpicStubTransport.cpp                  // Zero-dependency link seam when EOSSDK binaries are not staged
        ├── PhotonRealtimeTransport.{h,cpp}        // Photon Realtime 6.0 /MD + Epic JWT CustomAuth + regional cloud rooms
        └── PhotonStubTransport.cpp                // Zero-dependency link seam when Photon /MD libs are not staged
```

*(Note: Every identifier above was audited against `Documentation/BannedWordsVocabulary.md` — zero occurrences of `Role`, `Kind`, `State`, `Frame`, `Node`, `Store`, `Table`, `Binding`, `Compose`, `Assembly`, `Action`, `Member`, `Pass`, `Pipeline`, `Proof`, `Is*`, `Has*`, or `Should*`.)*

---

## 5. Wire Protocol & Bandwidth Budget (`US ↔ China ↔ Egypt`)

1. **Outer Envelope (`PacketSerializer`, `'FP1'` + `'FCNK'`)**:
   - `'FP1'` 7-byte header: `Magic[3] = {'F','P','1'}`, `WireVersion (u8 = 1)`, `PayloadCategory (u8: Handshake=0, WorldDelta=1, InputDirective=2, ReliableControl=3, PolicySync=4)`, `PayloadBytesBE (u16, max 4096)`.
   - Messages exceeding `3,840 B` (such as initial level baseline or `1,612 B` `DeterministicDrivingPolicy` weight sync) fragment deterministically via `'FCNK'` 20-byte chunk headers with bounded per-endpoint reassembly (`<= 64` in-flight messages, `<= 4 MiB` cap, zero OOM amplification).
2. **Quantized Vehicle & World Delta (`DifferentialWorldEncoder`)**:
   - Camera/sector-relative position (`3 × int24` = `9 B`, `0.1 mm` precision over `±838 m` sector + `int16` sector index),
   - Smallest-three quaternion orientation (`uint32` = `4 B`, `10 bits/component`, `< 0.08°` error),
   - Quantized linear & angular velocity (`6 B`),
   - Vehicle input directive (`Steer int16`, `Throttle uint8`, `Brake uint8`, `GearAndHandbrake uint8` = **`5 bytes` per tick**).
   - **Result**: A `4`-player `US + China + Egypt` vehicle pursuit session uses **`< 4.5 KB/s`** per player (`~36 kbps`), surviving `220 ms` trans-Pacific/Eurasian RTT and `CGNAT` relays effortlessly.

---

## 6. Step-by-Step Bringup Sequence (When Networking Is Executed)

| Step | Scope | Deliverable & Automated Validation (`Exhibits/NetworkingSpaceValidation`) |
| :--- | :--- | :--- |
| **N1** | **Wire Protocol, `.NetworkSpace` Codec & `LoopbackTransport`** | `PacketSerializer` (`'FP1'` + `'FCNK'` + fuzz/truncation bounds check), `NetworkCodec` (`.NetworkSpace` round-trip), `NetworkEndpointRegister`, and `LoopbackTransport` with `US ↔ China ↔ Egypt` latency (`65 ms` / `210 ms`), jitter (`±18 ms`), and `2%` packet-loss simulator. |
| **N2** | **Networking & Gameplay Policy Suite (`Policies/`)** | `AuthenticationScopePolicy` (token redaction + scope & continuance consent checks), `TransportSelectionPolicy` (`Photon` → `EOS` → `Socket` failover matrix), `ReplicationPolicy`, `SessionLifecyclePolicy` (reverse teardown invariant), `GameplayRulePolicy` (`64-bit` world hash), and C++20 `DeterministicDrivingPolicy` (`16 → 20 → 3` `CEM-RL` convergence check + `1,612 B` binary weight sync). |
| **N3** | **`DifferentialWorldEncoder`, `RemoteDirectiveRouter` & `NetworkingExtension`** | Full baseline + quantized transform/kinematics delta replication, `SpatialInterpolationWindow` quaternion slerp proxy, reliable ordered directive ring, and 3-peer (`US`, `China`, `Egypt`) simulated vehicle pursuit lockstep/interpolation verification with `0` desyncs. |
| **N4** | **Sovereign `SocketTransport` (`UDP` + `STUN` Punch + Encrypted Relay)** | Real non-blocking OS UDP sockets (Linux epoll/poll + Windows Winsock2), `RFC 5389` `STUN` endpoint-discovery & hole-punch sequence, `ChaCha20-Poly1305` packet AEAD, and standalone stateless `FrontierRelay` daemon verified over localhost UDP sockets. |
| **N5** | **`EpicOnlineTransport` (`EOS`) & `PhotonRealtimeTransport` (`Photon`) + Visual Exhibit** | Clean instance-owned `EpicOnlineTransport` + `EpicStubTransport` and `PhotonRealtimeTransport` + `PhotonStubTransport`, CMake auto-detection when `EOSSDK` / `Photon` `/MD` libraries are present, plus deterministic `1920×1080` network-route PNG snapshot in `Exhibits/Images/NetworkingSpace/`. |

---

## 7. Decision Matrix: Should We Build **Networking First** or **Rendering First**?

Both plans (`MaterialVisibilityRenderingBringupPlan.md` and `NetworkingPolicyBringupPlan.md`) are now fully specified, zero-overlap, and ready to execute:

| Criterion | Option A: **Networking + Policy First** (`Steps N1–N5`) | Option B: **Rendering + MaterialSpace First** (`Steps R1–R5`) |
| :--- | :--- | :--- |
| **External Dependencies** | **Zero** GPU/Vulkan/shader dependencies; pure C++20 CPU + OS UDP + EOS/Photon link seams. | Uses `VulkanInterchange` (`VK_KHR_ray_query`, `shaderInt64Atomics`) + `.slang` compiler + `GeometrySpace`. |
| **Compile & CI Speed** | **Very fast** (`< 5 s` incremental C++20 builds; 100% deterministic headless tests on every OS). | Moderate (`.slang` SPIR-V compilation + Vulkan offscreen validation). |
| **Immediate Payoff** | Locks in `.NetworkSpace`, `EOS` + `Photon` + Sovereign UDP transports, and the Gameplay/Network `Policy` backbone so every future vehicle/world feature is multiplayer-ready from Day 1. | Brings the calibrated `:8097` RTX visual quality (`10`-lobe `MaterialSpace`, `64-bit` Visibility Raster, `BVH4` shadows, `DDGI` `16×16` Chebyshev, `LTC`) into native C++20/Vulkan. |
| **Risk of Rework Later** | **Zero** — Rendering never changes `NetworkingSpace` headers. | **Zero** — Networking never changes `MaterialSpace` or `GeometricRaster` headers. |

> **Recommendation**: Either choice is 100% clean.
> - Choose **Networking First (`Steps N1–N5`)** if you want multiplayer (`EOS` + `Photon` + Sovereign UDP) and the Gameplay/Networking `Policy` architecture locked in before gameplay/vehicle systems expand.
> - Choose **Rendering First (`Steps R1–R5`)** if you want the native C++20 Vulkan viewport upgraded immediately to match the `:8097` RTX benchmark while fresh in mind.
