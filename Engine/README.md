# 🧩 `Engine/`

> **TL;DR**: Subsystem umbrella grouping 23 child modules under `Engine/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Codecs/`](Codecs/README.md) | .ColourSpace, .ColourRamp & .ColourSwatch encoder/decoder persisting radiometric swatches, modular ramps, spectra & 64B-aligned LUTs |
| [`ColourSpace/`](ColourSpace/README.md) | Colorimetric transforms between CIE XYZ, Linear Rec.709/sRGB, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex strings |
| [`ComponentClass/`](ComponentClass/README.md) | Top-level ComponentClass asset encapsulating title, simulation role, revision, baked RGBA thumbnail & ComponentInstanceStorage |
| [`Components/`](Components/README.md) | Logical-to-physical 2D canvas surface for reusable engine UI controls, panels, and overlays |
| [`Configuration/`](Configuration/README.md) | Compile-time build-profile policy: enforces mutually exclusive Editor, Development, and Shipping profiles across all targets |
| [`Constants/`](Constants/README.md) | Shared compile-time engine capacities, 64-byte container alignment, and 215 MiB / 750 MiB revision memory ceilings |
| [`DistanceFieldSpace/`](DistanceFieldSpace/README.md) | Sparse 8x8x8 narrow-band brick 3D Signed Distance Volume for Tier 1 GTX solar cone penumbra and 6-cone diffuse GI |
| [`Editor/`](Editor/README.md) | 10 source/shader files |
| [`GeometricRaster/`](GeometricRaster/README.md) | Headless & desktop Vulkan 1.2+ device interchange using volk, Timeline Semaphores (zero fences), and SwiftShader fallback |
| [`GeometrySpace/`](GeometrySpace/README.md) | 206 source/shader files |
| [`Input/`](Input/README.md) | Downstream one-way EditorInputExchange -> Dear ImGui IO signal forwarder (never polled upstream by Engine or Editor classes) |
| [`Interchange/`](Interchange/README.md) | External colour & gradient interchange: HTML Studio JSON/TOML colour blocks, CSS linear-gradient, SVG <linearGradient>, GIMP .gpl/.ggr, Adobe .ase/.aco & .cube 1D LUT |
| [`Lifecycle/`](Lifecycle/README.md) | Desktop host runner mounting FrontierUV (StandaloneWorkspace) or FrontierEditor (FeatureSet + ContentBrowser) on GLFW + Vulkan |
| [`MaterialSpace/`](MaterialSpace/README.md) | 128-byte cache-aligned bindless GPU material register and wave-uniform ActiveLobeMask table (Zero G-Buffer Bloat) |
| [`Mathematics/`](Mathematics/README.md) | Axis3D, Axis2D, zero-overhead Scalar3D/Scalar2D aliases, dedicated UVAxes, SpatialTransform, and 16B-aligned .slang GPU layout structs |
| [`NetworkingSpace/`](NetworkingSpace/README.md) | Sector-relative 24-bit sub-millimeter quantization, smallest-three 32-bit quaternions, baseline delta encoding, and slerp windowing |
| [`ParticleSpace/`](ParticleSpace/README.md) | 16-byte-aligned GPU & CPU particle simulation buffer layouts (80B ParticleSlot, 16B StrandVertex, 32B Anchor, Uniforms) |
| [`Shaders/`](Shaders/README.md) | 110 source/shader files |
| [`SimulationSpace/`](SimulationSpace/README.md) | 48 source/shader files |
| [`SpatialAcceleration/`](SpatialAcceleration/README.md) | 128-triangle hierarchical cluster LOD pyramid with seam/border-locked QEM simplification and normal-cone bounds |
| [`Threading/`](Threading/README.md) | 17 source/shader files |
| [`Windowing/`](Windowing/README.md) | Cross-platform window and surface interface with directive masks, application presets, and dynamic fullscreen mode transitions |
| [`Workspaces/`](Workspaces/README.md) | Registry of active Frontier host workspace capabilities ("Frontier" World Editor & "UVWorkspace" UV Authoring) for Project Browser |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
