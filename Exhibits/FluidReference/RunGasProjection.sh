#!/usr/bin/env bash
# 🧩 Validate Engine face-flux gas projection with analytical and independent matrix references.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
if command -v cmake >/dev/null 2>&1; then CMAKE="$(command -v cmake)"; else CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if command -v ctest >/dev/null 2>&1; then CTEST="$(command -v ctest)"; else CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
PYTHON="${PYTHON:-$ROOT/_ClaudeScratch/venv/bin/python}"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
LABEL=release
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    LABEL=sanitized
    CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Debug -DFRONTIER_PLUME_SANITIZERS=ON)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
fi
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/gas-projection-$LABEL}"
RESULT="${RESULT:-$ROOT/Exhibits/FluidReference/Results/GasProjection-$LABEL.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULT")"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$TOOLS/SwiftShader/vk_swiftshader_icd.json}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WGPU_BACKEND_TYPE="${WGPU_BACKEND_TYPE:-Vulkan}"
{
    printf 'Engine matched face-flux gas projection via wgpu/Naga Vulkan; %s\n' "$LABEL"
    printf 'Base %s, plus working changes if any; source hashes below.\n' "$(git rev-parse --short HEAD)"
    uname -srm
    "${CXX:-c++}" --version | head -1
    "$PYTHON" -c 'import sys, wgpu, numpy; print("Python", sys.version.split()[0], "wgpu", wgpu.__version__, "numpy", numpy.__version__)'
    sha256sum Frontier/Engine/Rendering/{GasProjectionFootprint.h,Shaders/GasPressureProjection.slang,CMakeLists.txt} \
        Exhibits/FluidReference/{CMakeLists.txt,Code/GasProjectionFootprintValidation.cpp,Code/GasProjectionValidation.py} \
        "$SLANG" "$TOOLS"/SwiftShader/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json}
    "$CMAKE" -S Exhibits/FluidReference -B "$BUILD" "${CMAKE_ARGS[@]}" >/dev/null
    "$CMAKE" --build "$BUILD" --target PlumeLatticeFootprintValidation GasProjectionFootprintValidation --parallel "${BUILD_JOBS:-2}" >/dev/null
    "$CTEST" --test-dir "$BUILD" --output-on-failure -V
    if [[ "$LABEL" == sanitized ]]; then
        ldd "$BUILD/GasProjectionFootprintValidation" | grep -E 'libasan|libubsan'
    fi
    for ENTRY in MeasureGasDivergence RelaxGasPressure ProjectGasVelocity; do
        "$SLANG" Frontier/Engine/Rendering/Shaders/GasPressureProjection.slang -entry "$ENTRY" \
            -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$ENTRY.spv"
    done
    sha256sum "$BUILD"/{MeasureGasDivergence,RelaxGasPressure,ProjectGasVelocity}.spv
    "$PYTHON" Exhibits/FluidReference/Code/GasProjectionValidation.py "$BUILD/GasProjectionFootprintValidation" "$BUILD"
} 2>&1 | tee "$RESULT"
