# 🧩 `Engine/Components/Camera/`

> **TL;DR**: Unified 6-mode physical thin-lens CameraComponent with exponential smoothing, constraints, and non-Shipping diagnostics (6 source/header/shader files under `Engine/Components/Camera/`).

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Constraints/`](Constraints/README.md) | Spatial 3D AABB volume clamp and 2D UDIM planar canvas center constraint |
| [`Diagnostics/`](Diagnostics/README.md) | Non-Shipping normalized 2D viewport composition overlay segments (Rule of Thirds, Broadcast Safe Gate, Optical Reticle) |
| [`Kinematics/`](Kinematics/README.md) | Step-rate-independent exponential smoothing 1 - exp(-lambda * dt) and logarithmic zoom interpolation |
| [`Modes/`](Modes/README.md) | Eye-anchored world-up-aligned first-person navigation translating on the horizontal ground plane |
| [`Projection/`](Projection/README.md) | Aspect-locked parallel 3D & 2D planar orthographic projection transforms |
| [`VisualEffects/`](VisualEffects/README.md) | Vulkan .slang GPU compute encoder executing CameraVisualEffects.comp.spv (DoF, ChromaticAberration, LensFlare, Vignette & Guides) |

## Files

| File | Purpose |
| :--- | :--- |
| `CameraComponent.cpp` | Implementation of CameraComponent across Orbit, Fly, FirstPerson, Pan2D, FollowTarget & SplineRail modes |
| `CameraComponent.h` | Unified 6-mode physical thin-lens CameraComponent with exponential smoothing, constraints, and non-Shipping diagnostics |
| `CameraConfiguration.h` | Physical sensor gate, thin-lens optics, kinematics, constraints, GPU visual effects, and non-Shipping diagnostics parameters |
| `CameraPosture.h` | Smoothed vs target spatial/planar posture, orthonormal camera basis, thin-lens optical telemetry, and Vulkan clip transform |
| `ViewportProjection.cpp` | Implementation of ViewportProjection camera presets, orbit/pan/frame math, 3D/2D picking rays, and aspect-locked planar bounds |
| `ViewportProjection.h` | Reusable 3D spatial orbit/pan/frame camera & 2D aspect-locked planar projection state shared across WorkspaceDocument sessions |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components`, `Frontier::Editor`
- **Primary Types**: `CameraComponent`, `SensorGatePreset`, `CameraOperatingMode`, `CameraProjectionMode`, `ApertureGhostShape`, `LensFlareArchetype`, `CompositionGuideStyle`, `MotionBlurStyle`, `WorldDistortionCategory`, `OpticalLensParameters`, `CameraKinematicsParameters`, `BoundsConstraintParameters`, `CollisionObstacleSphere`, `CollisionAvoidanceParameters`, `LensFlareParameters`, `ChromaticAberrationParameters`, `DepthOfFieldParameters`, `VignetteParameters` (+13 more)

## Usage

```cpp
#include "Engine/Components/Camera/CameraComponent.h"
#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"

using namespace Frontier::Components;
CameraComponent Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
