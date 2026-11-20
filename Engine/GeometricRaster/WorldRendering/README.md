# Frontier :: GeometricRaster/WorldRendering

World direct-lighting, shadows, specular reflections, and GPU Vulkan compute showcase sequence.

## Key Files

| File | Description |
|------|-------------|
| `ShowcaseGalleryScene.cpp` | Cornell-Studio Showcase Gallery Scene with Outliner Synchronization & Interactive Asset Placement |
| `ShowcaseGalleryScene.h` | Cornell-Studio Showcase Gallery Scene with Outliner Synchronization & Interactive Asset Placement |
| `SpecularReflectionSequence.cpp` | BVH4 specular raytracing, analytical sky dome fallback, and chromatic dielectric refraction |
| `SpecularReflectionSequence.h` | BVH4 specular raytracing, analytical sky dome fallback, and chromatic dielectric refraction |
| `TieredWorldRenderSequence.cpp` | World Render Sequence (MatCap & OpenPBR Direct + Shadow Lab + BVH4 Reflections + Unified Atmosphere/PlanetaryClouds) |
| `TieredWorldRenderSequence.h` | World Render Sequence (MatCap & OpenPBR Direct + Shadow Lab + BVH4 Reflections + Unified Atmosphere/PlanetaryClouds) |
| `VulkanWorldRenderSequence.cpp` | Vulkan .slang GPU Compute World Render Sequence (MatCap, OpenPBR Direct, Sky LUT, PlanetaryClouds, Cascaded Shadows, 2912 B UBO) |
| `VulkanWorldRenderSequence.h` | Vulkan .slang GPU Compute World Render Sequence (MatCap, OpenPBR Direct, Sky LUT, PlanetaryClouds, Cascaded Shadows) |

## Architecture Overview

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `WorldRenderEngine`, `WorldRenderTier`, `WorldRenderConfiguration`, `WorldSceneSpecification`, `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`, `ShowcaseWorldSceneBundle`

## Usage Example

```cpp
#include "Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h"

Frontier::GeometricRaster::TieredWorldRenderSequence WorldRenderer {};
```
