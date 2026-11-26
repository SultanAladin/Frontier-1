# Frontier source, Project Browser, Jolt + Photon assessment

**Assessment date:** 2026-11-25
**Working branch:** `arena/d90e3624-frontier-1`

## Source and runnable browser

- The first requested source is cloned as the `FrontierSource` Git submodule, pinned to `egoistinbox-hue/Frontier` branch `arena/f490d972-frontier`, commit `d30717808a7d0f9040de5c9bb35e5289565fdced`.
- The standalone Project Browser was copied from `eosclient0001-rgb/Frontier` branch `arena/d929ef36-frontier`, commit `b51d98c33a1e068b9f6fc7759afa7ff6b34e51b1`, into `Experimental/Project browser/` in this checkout. It is an HTML/CSS/JavaScript/WebGL prototype; its own README describes sample data and native-editor handoff as prototype boundaries. It is not yet wired into Frontier's native startup flow.
- Run it from the repository root with `python3 'Experimental/Project browser/Serve.py'` (serves on `0.0.0.0:8770`). The server is running for the live preview.

## Jolt Physics

The selected source already has a real Jolt integration path: `Automation/Dependencies.json` pins Jolt Physics 5.6.0, `Automation/JoltPhysics.cmake` configures the deterministic static build, and `CMakeLists.txt` includes/links Jolt. Runtime physics, vehicle, and validation code also references Jolt. The dependency checkout itself is external and needs to be prepared with `python3 Automation/PrepareDependencies.py` before a native build. I did not run a full CMake build in this assessment.

## EOS (Epic Online Services)

Correction to my earlier assessment: I had focused on the newly selected `FrontierSource` tree and did not make the distinction from the older project clear enough. **EOS is integrated in the legacy `Frontier-OLD` project.** Its `Frontier/Projects/Project-Networking/Source/EpicExchange.cpp` implements EOS platform initialization, Auth/Connect flow, and `LobbyRuntime.cpp` implements live lobby operations; `EosTransport.cpp` and the router provide transport wiring. The legacy `Docs/NetworkingLogin.md` documents the login/lobby integration and its test/build evidence. Some live account or cloud operations are explicitly not proven by CI, but the EOS client implementation itself is present.

The `FrontierSource` submodule is a different codebase slice, and its EOS transport was an adapter stub before this implementation pass. It now contains an SDK-backed path for Epic Account Portal Auth, explicit Connect product-user creation consent, product-user identity, EOS P2P datagrams with registered peer IDs, and EOS Sessions create/start/search/join/end/destroy. The EOS SDK remains opt-in and external; the source tree does not vendor proprietary SDK files. The adapter API is exposed through `NetworkingExtension`, and the EOS platform is ticked even if Photon or UDP is currently the active packet route. This ports the proven legacy identity sequence into the current transport seam rather than treating the legacy project as a blank slate.

I also checked the connected Drive. It contains an `EOS Flattened` folder with `Bin`, `Include`, and `Lib` subfolders, plus the EOS SDK 1.19.2.1 release archive (about 588 MB) and Win64 shipping DLL/import library files. I did **not** download or add proprietary SDK binaries to Git. A Drive filename search for `Jolt`, `JoltPhysics`, `JPH`, or `Physics` did not surface a Jolt SDK package (only a physics report), so I have not yet located the Jolt folder you mentioned.

The current machine has not staged those Drive SDK files into an EOS SDK root. I therefore have **not** compiled the SDK-enabled C++ translation unit or run a live Epic login, Connect profile-creation, P2P, or session request. The default no-SDK stub translation unit compiles; cloud/API correctness still needs an SDK-backed build and authorized dev deployment. Configure with `-DFRONTIER_ENABLE_EOS_SDK=ON -DFRONTIER_EOS_SDK_ROOT=<local SDK path>` after making an authorized platform SDK available. The client secret is runtime-only input and is scrubbed after EOS platform creation; do not put it in project TOML or source control. The source changes are also captured in `Documentation/Patches/FrontierSource-EOS-Implementation.patch` so they remain attached to this active branch without committing or switching the source submodule to a different branch.

## Photon Realtime

The selected source exposes `FRONTIER_ENABLE_PHOTON_SDK`, but it defaults to `OFF`. The currently selected source's `PhotonRealtimeTransport.cpp` is an adapter seam, not a live Photon client: it validates/stores auth configuration and currently routes through an optional loopback bridge; without that bridge it cannot transmit/receive cloud packets. The default build selects `PhotonStubTransport.cpp`. Therefore, this source should **not** be described as having a completed Photon cloud integration.

The user has now put the Photon Realtime Core 6.0.0-preview-347 ZIP (`photon-realtime-core-6.0.0-preview-347-20261126T033842Z-1-001.zip`, 193,488,030 bytes) in My Drive. I attempted to retrieve it with the connected Drive tool, but the tool refused to transfer it because its workspace download ceiling is 100 MiB. The archive is not present in the checkout. CMake now has a `FRONTIER_PHOTON_SDK_ROOT` option and links the package's documented `RealtimeCore::Matchmaking` target when enabled, but the option has not been configured because the package is not staged. The C++ transport source still does not instantiate a Photon client or implement room/event/datagram cloud wiring. A platform-specific archive or split files below that transfer limit are needed before an SDK-enabled configure, adapter implementation, and build can be completed.

## Legacy Frontier-OLD review

I checked the public `unassignedinbox/Frontier-OLD` branches and found relevant integration references:

- `arena/255c3b21-frontier` (`30728d450f6a97e4d4e7b7f4a8f0016e3fa1ebc8`, 2026-11-10) contains a substantive Photon Realtime C++ path in `Frontier/Projects/Project-Networking/Source/PhotonTransport.cpp`: Epic-token authentication, Photon connect, join-or-create room, packet/event handling, timeout/error diagnostics, and a separate honest `PhotonLinkStub.cpp` fallback. Its Jolt 5.6.0 CMake integration is in `Automation/JoltPhysics.cmake`, with the dependency pin in `Automation/UIDependencies.json`.
- The Photon SDK is proprietary/external, not checked into the inspected source. The legacy networking notes describe SDK libraries as required inputs and the stub as the no-SDK fallback. I have not verified those SDK binaries or attempted a real account/cloud connection.
- `arena/42169529-frontier` (`63b109eceeab38077a30f4c36a9b3d751f0cbb84`, 2026-11-05) is another useful packaging reference: it includes the older `PhotonAdapter` API and a `WindowsPhotonManifest.json` with expected SDK library hashes/Drive file IDs. That manifest is useful for provenance, but the referenced proprietary files still need to be made available through an authorized dependency flow.

## HTML Project Details delivered in this workspace

The Project Browser now has a theme-aware second page for project name/ID/version/icon, render output dimensions/format, terms version/text/acceptance, and EOS/Photon identifiers. It can import/export TOML. The normal project TOML omits secrets; a separate local test TOML requires an explicit plaintext-secrets confirmation, and its filename is Git-ignored. The browser page is configuration UI only: it does not connect to EOS/Photon. The page also links to the private EOS Drive folder and archive; those links still require an authorized Google account and were not made public.

## Recommendation

Keep the current source's Jolt path as the base and port/adapt the legacy Photon client into the current `NetworkingSpace` transport seam behind the existing build flag. Preserve a no-SDK stub, keep auth tokens out of logs and source control, and add deterministic codec/transport tests before any cloud smoke test. First verify the Photon SDK version, headers, import libraries, runtime DLLs, license, and CI download procedure; then run a Windows build and an explicitly credentialed staging-room test. The web Project Browser is independent of that engine integration work.
