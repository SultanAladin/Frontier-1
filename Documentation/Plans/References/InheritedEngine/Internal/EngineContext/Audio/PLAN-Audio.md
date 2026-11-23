# Audio — Engine Design Plan

> Home: `Internal/EngineContext/Audio/`. This is the authoritative design for the Frontier audio
> service. **No `.h`/`.cpp` are ported yet** — the eight spec components have no source anywhere, and
> the only real audio code that exists (a GEngine engine-synth playback path) is out-of-architecture
> for a DCC/CAD editor. This plan is what gets built when a real audio consumer lands. Zero fabrication:
> nothing here is compiled — it is the blueprint, not an implementation.

---

## 1. What Audio is (and is not)

Frontier is a Vulkan + ImGui DCC/CAD editor. Audio here is a **shared, pull-driven playback service** —
UI feedback (clicks, snaps, confirmations), authoring cues (bake-complete, solve-converged), and
optional 3D-positioned preview sources for scene work. It is **not** a game-audio engine: no combustion
synthesis, no vehicle voices, no gameplay event bus. Those belong to a game runtime, not the editor.

The service is **modular, not standalone** (per house rule): one device seam serves *many* sources and
submixes; per-instance data (gain, pitch, position, clip) is passed in at record time, never hardcoded.

### Real-time contract (non-negotiable)

The OS audio backend pulls audio on **its own real-time thread**. Every function that runs inside that
pull — the mix advance and every source render — is **allocation-free, lock-free, and wall-clock-free**.
Control input crossing from the sim/UI threads to the audio thread is a plain `std::atomic` store, read
once per block; the audio thread glides toward the target per-sample so a control jump never clicks.

**Naming note (enforced):** the audio-domain "frame" — one sample across all channels — is a
**`SamplePoint`**. `Frame` is banned. A submix/bus ordering is a **`MixOrder`**, never a "graph"/"chain".

---

## 2. Component map (the eight spec units)

Spec source: `FolderStructure.md` → `Internal/EngineContext/Audio/`. Coordinator suffix is `…Extension`.

| Component | Concern | Real-time thread? | Status |
|---|---|---|---|
| `AudioExtension.{h,cpp}` | Coordinator: device open, mix advance, per-cycle drive, finalize | drives, not on RT | ❌ no source — author when a consumer lands |
| `AudioConfiguration.{h,cpp}` | Sample rate, channel layout, buffer size (negotiated with device) | no | ❌ no source |
| `AudioDevice.{h,cpp}` | Backend output-stream ownership (miniaudio seam; `MA_IMPLEMENTATION` TU) | owns the RT thread | ❌ no source (reference: retired `AudioOutput`) |
| `SoundSource.{h,cpp}` | One playable source — gain, pitch, loop; renders into the mix | yes (render) | ❌ no source |
| `SpatialEmitter.{h,cpp}` | 3D-positioned source — attenuation + panning from listener pose | yes (render) | ❌ no source |
| `AudioListener.{h,cpp}` | Listener pose, fed from the active camera (`ViewportCamera`) | no (control) | ❌ no source |
| `MixOrder.{h,cpp}` | Bus/submix ordering (master + named submixes), gain per bus | yes (mix) | ❌ no source |
| `AudioSampleStore.{h,cpp}` | Loaded PCM / streamed clip store; hands blocks to sources | load off-thread; read on RT | ❌ no source |

★ = still to author. All eight are deferred to `EngineDocs/Backlog.md` (Batch K).

---

## 3. Data flow (one pull cycle)

```
sim / UI thread                          audio RT thread (pull-driven, no alloc / no locks)
──────────────                           ──────────────────────────────────────────────────
ConfigureListenerPose  ─┐
ConfigureSourceGain    ─┼─ atomic ─▶     AudioDevice pull callback (SamplePointCount requested)
TriggerSoundSource     ─┘                   │
                                            ├─ for each SoundSource / SpatialEmitter:
AudioSampleStore  ─ clip blocks ─▶          │     RenderSourceBlock → SamplePoints
   (loaded off-thread, read on RT)          │     (SpatialEmitter: attenuate + pan vs AudioListener)
                                            ├─ MixOrder: sum sources into submixes, submixes into master
                                            └─ write master → device output buffer
```

- **Pull, not push.** No per-cycle call from the frame loop renders audio; the device asks. The
  coordinator's per-cycle drive only publishes control (new poses, triggers) via atomics.
- **Listener from camera.** `AudioListener` reads the active `ViewportCamera` pose (position + basis)
  each cycle on the control side — the RT thread sees only the latched atomic snapshot.
- **Clip loading is off-thread.** `AudioSampleStore` decodes/loads on a worker; the RT thread only reads
  already-resident blocks. A not-yet-loaded clip renders silence, never blocks.

---

## 4. Backend

Vendored **miniaudio** (`ExternalPackages/miniaudio/`) is the device seam — single-header, compiled with
`MA_IMPLEMENTATION` in **exactly one** translation unit (`AudioDevice.cpp`), hidden behind an internal
struct so the 4 MB amalgam never leaks into the rest of the engine. Feature cuts: keep WASAPI + the null
fallback (headless CI opens silently); the decode path is enabled here (unlike the retired engine-synth,
the editor loads real clips). No capture/recording device.

`AudioConfiguration` requests f32 / stereo / device-native rate; whatever the device *grants* is latched
and every source's phase math runs against the granted rate.

---

## 5. House-style conformance

- Everything in `namespace Frontier`; struct + free-function (no method-heavy classes).
- Headers: banner ruler, `🧩` one-liner, `#pragma once` **and** `#ifndef FRONTIER_ENGINECONTEXT_AUDIO_<NAME>_H`.
- Lifecycle verbs: `InitializeAudioExtension` → (device runs) → `FinalizeAudioExtension`. Control verbs:
  `ConfigureSourceGain`, `TriggerSoundSource`, `ConfigureListenerPose`. Render verbs: `RenderSourceBlock`,
  `EvaluateMixOrder`. Never `Get`/`Set`/`Process`/`Handle`.
- C++17 only. `[[nodiscard]]` on pure queries/factories; `noexcept` on void mutators that never allocate
  (all RT-thread functions qualify); functions that may allocate (clip load) get neither.
- No banned words: no `system`, no `Frame` (→ `SamplePoint`), no kinship terms, no "graph"/"chain"
  (→ `MixOrder`).

---

## 6. Build order (when authored)

1. `AudioConfiguration` (pure data) → `AudioDevice` (miniaudio seam, silence-fallback verified).
2. `AudioSampleStore` (load path) → `SoundSource` (render one clip).
3. `MixOrder` (sum sources) → `AudioExtension` (coordinator, initialize→drive→finalize).
4. `AudioListener` (camera pose) → `SpatialEmitter` (attenuation + panning).
5. `…Validation.cpp` smoke: open device (null backend on CI), render N blocks, assert finite + no
   underrun. Build via `Build.bat` through the PowerShell tool.

---

## 7. Reference: retired engine-synth (do NOT port as-is)

`RetiredProject/.retired/GEngine/Internal/Runtime/Audio/` holds real, working code —
`AudioOutput.{h,cpp}` (miniaudio device seam, pull callback, `MA_IMPLEMENTATION`) and
`EngineToneSynth.{h,cpp}` (procedural four-stroke engine tone: order-harmonic pulse train + speed-tracking
resonant state-variable filter). It is **out-of-architecture** for the editor (vehicle-sound feature,
depends on GEngine's `DiagnosticLedger`, guards `GENGINE_RUNTIME_*`). Its **value here is as a pattern
reference**, not a port:

- `AudioOutput` → the seam technique for `AudioDevice` (hidden `Internal`, static RT callback reaching a
  voice via `pUserData`, silence-fallback on device-open failure).
- `EngineToneSynth` → the RT-safe per-sample discipline (atomic control read once, per-sample glide,
  no alloc/locks) that every `SoundSource`/`SpatialEmitter` render must follow.

If a procedural-audio need ever returns, that synth is a resurrectable component — but it lives under a
game runtime, not `EngineContext/Audio/`.
