# 🧩 `Engine/`

> **TL;DR**: Subsystem umbrella grouping 21 child modules under `Engine/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`AudioSpace/`](AudioSpace/README.md) | Lock-free real-time 3D spatial audio unit with waveform codecs, HRTF spatialization, BVH4 ray acoustics, and hardware endpoints |
| [`Codecs/`](Codecs/README.md) | .ColourSpace, .ColourRamp & .ColourSwatch encoder/decoder persisting radiometric swatches, modular ramps, spectra & 64B-aligned LUTs |
| [`ColourSpace/`](ColourSpace/README.md) | Colorimetric transforms between CIE XYZ, Linear Rec.709/sRGB, ACEScg, Rec.2020, Display P3, Oklab/Oklch, HSL, CMYK & Hex strings |
| [`ComponentClass/`](ComponentClass/README.md) | Top-level ComponentClass asset encapsulating title, simulation role, revision, baked RGBA thumbnail & ComponentInstanceStorage |
| [`Components/`](Components/README.md) | Logical-to-physical 2D canvas surface for reusable engine UI controls, panels, and overlays |
| [`Configuration/`](Configuration/README.md) | Compile-time build-profile policy: enforces mutually exclusive Editor, Development, and Shipping profiles across all targets |
| [`Constants/`](Constants/README.md) | Shared compile-time engine capacities, 64-byte container alignment, and 215 MiB / 750 MiB revision memory ceilings |
| [`Editor/`](Editor/README.md) | Editor Explorer (`ProjectBrowser` and `ContentBrowser`) compiled under `FRONTIER_PROFILE_EDITOR=1` |
| [`GeometricRaster/`](GeometricRaster/README.md) | Headless & desktop Vulkan 1.2+ device interchange, MatCap & OpenPBR Direct world renderers, and unified `Atmosphere/` (`PlanetaryClouds`) |
| [`GeometrySpace/`](GeometrySpace/README.md) | 206 source/shader files |
| [`Input/`](Input/README.md) | Downstream one-way EditorInputExchange -> Dear ImGui IO signal forwarder (never polled upstream by Engine or Editor classes) |
| [`Interchange/`](Interchange/README.md) | External colour & gradient interchange: HTML Studio JSON/TOML colour blocks, CSS linear-gradient, SVG <linearGradient>, GIMP .gpl/.ggr, Adobe .ase/.aco & .cube 1D LUT |
| [`Lifecycle/`](Lifecycle/README.md) | Desktop host runner mounting FrontierUV (StandaloneWorkspace) or FrontierEditor (FeatureSet + ContentBrowser) on GLFW + Vulkan |
| [`MaterialSpace/`](MaterialSpace/README.md) | 128-byte cache-aligned bindless GPU material register and wave-uniform ActiveLobeMask table (Zero G-Buffer Bloat) |
| [`Mathematics/`](Mathematics/README.md) | Axis3D, Axis2D, zero-overhead Scalar3D/Scalar2D aliases, dedicated UVAxes, SpatialTransform, and 16B-aligned .slang GPU layout structs |
| [`NetworkingSpace/`](NetworkingSpace/README.md) | Sector-relative 24-bit sub-millimeter quantization, smallest-three 32-bit quaternions, baseline delta encoding, and slerp windowing |
| [`Shaders/`](Shaders/README.md) | Slang (`.slang`) compute and raster shaders across Atmosphere (`PlanetaryClouds`), GeometricRaster, Baking, Colour, DirectLighting, MaterialSpace, and Visibility |
| [`SimulationSpace/`](SimulationSpace/README.md) | Rigid-body physics, JoltPhysics v5.6.0 dynamics, breakable constraints, vehicle dynamics, and collision solvers (`PhysicsSpace/`) |
| [`SpatialAcceleration/`](SpatialAcceleration/README.md) | 128-triangle hierarchical cluster LOD pyramid with seam/border-locked QEM simplification and normal-cone bounds |
| [`Threading/`](Threading/README.md) | 17 source/shader files |
| [`Windowing/`](Windowing/README.md) | Cross-platform window and surface interface with directive masks, application presets, and dynamic fullscreen mode transitions |
| [`Workspaces/`](Workspaces/README.md) | Registry of active Frontier host workspace capabilities ("Frontier" World Editor & "UVWorkspace" UV Authoring) for Project Browser |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
