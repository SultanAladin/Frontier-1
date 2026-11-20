# 🧩 `Engine/AudioSpace/`

> **TL;DR**: Lock-free real-time 3D spatial audio unit implementing `SuspendableParticipant`, generational waveforms/voices, surround VBAP & Woodworth binaural panning, Catmull-Rom Doppler pitch shift, tunnel waveguide + Hadamard FDN reverberation, bus ducking, and cross-platform hardware endpoints (11 source/header files under `Engine/AudioSpace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AcousticTypes.h` | Generational tokens, speaker layouts, voice/zone descriptors, spatial solutions, lock-free command & telemetry structs |
| `WaveformDecoder.h` | RIFF WAV & procedural waveform decoding, sample-rate conversion, Catmull-Rom interpolation, and resident repository declarations |
| `WaveformDecoder.cpp` | RIFF PCM/float WAV codec, sample-rate resampler, procedural synthesizer, cubic fractional sampler, and slot repository |
| `SpatialAcoustics.h` | RHR Z-up VBAP surround, Woodworth binaural ITD/ILD, physical Doppler, tunnel coupling & SDF/Physics occlusion declarations |
| `SpatialAcoustics.cpp` | Surround 5.1/7.1 VBAP rings, Doppler math & slew smoothing, tunnel portal radiation, Camera/Jolt/SDF integrations |
| `AudioBusAccumulator.h` | Zero-allocation DSP mixer, tunnel waveguide combs, Hadamard FDN, side-chain ducking & soft limiter declarations |
| `AudioBusAccumulator.cpp` | Real-time voice scheduler, loop crossfade, binaural delay lines, tunnel waveguide filters, ducking & limiting implementation |
| `AudioHardwareOutput.h` | Cross-platform audio sink & capture buffer API completing Backlog `BKL-009` |
| `AudioHardwareOutput.cpp` | Native WASAPI, runtime `dlopen` PulseAudio/ALSA & AAudio drivers with deterministic `HeadlessBuffer` & WAV export |
| `AudioExtension.h` | `SuspendableParticipant` audio orchestrator header |
| `AudioExtension.cpp` | Lifecycle orchestration bridging `AudioThread`, `AudioBusAccumulator`, `AudioHardwareOutput`, Physics & SDF |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::AudioSpace`
- **Primary Types**: `AudioExtension`, `AudioBusAccumulator`, `SpatialAcoustics`, `AudioHardwareOutput`, `WaveformDecoder`, `WaveformRepository`, `AcousticVoiceDescriptor`, `AcousticZoneDescriptor`, `AcousticReceiverPose`

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by `Exhibits/AudioSpaceValidation.cpp` (`ctest --output-on-failure -R AudioSpaceValidation`).
