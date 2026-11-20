# 🧩 `Engine/GeometricRaster/`

> **TL;DR**: Headless & desktop Vulkan 1.2+ device interchange using volk, Timeline Semaphores (zero fences), and SwiftShader fallback (2 source/header/shader files under `Engine/GeometricRaster/`).

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Atmosphere/`](Atmosphere/README.md) | Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, solar disc/corona, unified Slang-driven `PlanetaryClouds` (`CloudNoiseVolume`, `PlanetaryCloudGenerated.hpp`), multi-moon/starfield, height/aerial fog, and lens flare |
| [`Baking/`](Baking/README.md) | Dual GTX SAH-BVH4 + optional RTX VK_KHR_acceleration_structure (BLAS + TLAS) upload & synchronization for GPU baking |
| [`ComponentEditor/`](ComponentEditor/README.md) | Procedural & compiled geometry vertex builders for Component Editor .slang pipelines (surface channels, constraints, shells, beams) |
| [`Compute/`](Compute/README.md) | Modular Vulkan .slang compute pipeline registry and descriptor encoder shared across UV, Geometry, Texture, Material & World rendering |
| [`DirectLighting/`](DirectLighting/README.md) | Visibility-Gated Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights, Shadow Lab, & 2-Scale (0.25 m / 2.2 m) Chassis & Specular AO |
| [`Gizmo/`](Gizmo/README.md) | Modular 3D & 2D Slate GizmoPRO manipulator reusable across UV, Geometry, Material, Vector & World workspaces |
| [`GlobalIllumination/`](GlobalIllumination/README.md) | Dual-Engine Non-GI World Render Sequence (`MatCap` & `OpenPbrDirect`) + Shadow Lab + BVH4 / Sky Specular Reflections & Abbe Glass |
| [`Picking/`](Picking/README.md) | Unified 3D & 2D GPU R32G32B32A32_UINT visibility pick query supporting 1x1 cursor readback and marquee/brush VRAM reduction |
| [`Rasterization/`](Rasterization/README.md) | Native Vulkan 1.2+ swapchain & offscreen presenter executing ImGuiOverlayRasterization.slang over GLFW OS windows or headless targets |
| [`UV/`](UV/README.md) | Vulkan .slang 3D Surface/MatCap + 2D UDIM UVTile + 3D/2D TransformGizmo MRT rasterizer with GPU Selection & Stretch compute |
| [`Vector/`](Vector/README.md) | Pure Vulkan 1.3 + .slang VectorGraphics GPU rasterizer driven by ThorVG symbolic DOM/typography paths + Planar2D TransformGizmo + R32G32B32A32_UINT picking. |
| [`Visibility/`](Visibility/README.md) | Two-Phase GPU/CPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW) |

## Files

| File | Purpose |
| :--- | :--- |
| `VulkanInterchange.cpp` | Implementation of VulkanInterchange with volk dynamic loading, SwiftShader ICD auto-discovery, and Timeline Semaphore synchronization |
| `VulkanInterchange.h` | Headless & desktop Vulkan 1.2+ device interchange using volk, Timeline Semaphores (zero fences), and SwiftShader fallback |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `DeviceBufferResource`, `DeviceImageResource`, `VulkanInterchange`

## Usage

```cpp
#include "Engine/GeometricRaster/VulkanInterchange.h"

using namespace Frontier::GeometricRaster;
DeviceBufferResource Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
