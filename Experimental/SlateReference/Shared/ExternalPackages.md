# ExternalPackages

Dependency manifest for all third-party libraries vendored under `ExternalPackages/`.  
All entries are Git submodules unless otherwise noted.

---

## How to initialise

```powershell
git submodule update --init --recursive
```

EOS SDK is excluded from the submodule list — see the note at the end of this document.

---

## Dependency table

| Library | Role in Slate | Intended release | Exact SHA | Upstream | Local build required | Build script |
|---------|---------------|-----------------|-----------|----------|---------------------|--------------|
| **imgui** | Immediate-mode debug / editor UI (docking branch) | 1.92.9 WIP (19281) | `12b797755` | [ocornut/imgui](https://github.com/ocornut/imgui) — `docking` branch | No — TUs compiled directly into SlateUI | — |
| **glfw** | Window creation, OpenGL/Vulkan context, raw input | post-3.5.1 (`92dcf4ce`) | `92dcf4ce` | [glfw/glfw](https://github.com/glfw/glfw) | **Yes** — DLL + import library | `Scripts/BuildGLFW.ps1` |
| **thorvg** | SVG / glyph rasterisation (GlyphDepot) | head/main | `3a2ce054` | [thorvg/thorvg](https://github.com/thorvg/thorvg) | **Yes** — static library | `Scripts/BuildThorVG.ps1` |
| **jolt** | Rigid-body physics simulation | post-v5.6.0 (`2e28006e`) | `2e28006e` | [jrouwe/JoltPhysics](https://github.com/jrouwe/JoltPhysics) | No — headers only, compiled into SlateCompute | — |
| **cgltf** | glTF 2.0 scene loading | post-v1.15 (`85cd623`) | `85cd6238` | [jkuhlmann/cgltf](https://github.com/jkuhlmann/cgltf) | No — single-header, compiled into SlateDocument | — |
| **clipper2** | Polygon clipping / offsetting | post-2.0.1 (`f9c5eb6`) | `f9c5eb6e` | [AngusJohnson/Clipper2](https://github.com/AngusJohnson/Clipper2) | No — headers only | — |
| **earcut** | Fast polygon triangulation | post-v0.10 (`f25bc76`) | `f25bc765` | [mapbox/earcut.hpp](https://github.com/mapbox/earcut.hpp) | No — single-header | — |
| **fast\_obj** | Wavefront OBJ parser | post-v1.3 (`d620667`) | `d620667f` | [thisistherk/fast\_obj](https://github.com/thisistherk/fast_obj) | No — single-header, compiled into SlateDocument | — |
| **miniaudio** | Cross-platform audio I/O | v0.11.25 | `9634bedb` | [mackron/miniaudio](https://github.com/mackron/miniaudio) | No — single-header | — |
| **stb** | Image load/save, truetype rasterisation | head/master | `2c980bb5` | [nothings/stb](https://github.com/nothings/stb) | No — single-header, compiled into SlateDocument | — |
| **tomlpp** | TOML config parsing | head/master | `1e8829b7` | [marzer/tomlplusplus](https://github.com/marzer/tomlplusplus) | No — single-header | — |
| **ufbx** | FBX scene loading | v0.23.0 | `fcc5d6ba` | [ufbx/ufbx](https://github.com/ufbx/ufbx) | No — single-file C library, compiled into SlateDocument | — |

---

## Libraries requiring a local build

### GLFW — `Scripts/BuildGLFW.ps1`

GLFW is shipped as source only. The build script compiles it with CMake + MSVC (Visual Studio 17 2022 / x64) as a **shared library** and deposits:

```
ExternalPackages/glfw/lib-vc2022/glfw3dll.lib   # import library for the linker
ExternalPackages/glfw/lib-vc2022/glfw3.dll      # runtime DLL copied next to each executable
```

`Construct.ps1` invokes `BuildGLFW.ps1` automatically when `glfw3dll.lib` is absent.

To build manually:

```powershell
powershell -File Scripts\BuildGLFW.ps1
```

**Prerequisites:** `cmake.exe` on PATH (included with Visual Studio 2022 CMake component).

---

### ThorVG — `Scripts/BuildThorVG.ps1`

ThorVG is shipped as source only. The build script compiles it directly using MSVC (`cl.exe` and `lib.exe`) as a **static library** with SVG loader only (no Python, Meson, or Ninja required), and deposits:

```
ExternalPackages/thorvg/lib/thorvg.lib
```

`Construct.ps1` invokes `BuildThorVG.ps1` automatically when `thorvg.lib` is absent.

To build manually:

```powershell
powershell -File Scripts\BuildThorVG.ps1
```

**Prerequisites:** MSVC C++ toolchain (cl.exe, lib.exe).

---

## Why EOS SDK is excluded from submodules

`ExternalPackages/EOS-SDK-53289219-Release-v1.19.1.2` contains prebuilt Epic Online Services SDK binaries distributed under Epic's own licence. The SDK cannot be redistributed via a public Git submodule because:

1. The upstream repository is private and requires a registered Epic developer account.
2. The licence prohibits redistribution of prebuilt binaries in public version control.

The folder is committed directly to this repository for the team members who have signed the licence. Developers without the SDK present should build without the EOS integration target until they have obtained it through the [Epic Developer Portal](https://dev.epicgames.com).
