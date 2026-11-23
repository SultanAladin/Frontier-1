#!/usr/bin/env bash
# 🧩 Validate Engine plume footprints and execute retained transport WGSL on Vulkan.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
if command -v cmake >/dev/null 2>&1; then CMAKE="$(command -v cmake)"; else CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if command -v ctest >/dev/null 2>&1; then CTEST="$(command -v ctest)"; else CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
PYTHON="${PYTHON:-$ROOT/_ClaudeScratch/venv/bin/python}"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}/SwiftShader"
LABEL=release
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    LABEL=sanitized
    CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Debug -DFRONTIER_PLUME_SANITIZERS=ON)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
fi
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/plume-transport-$LABEL}"
RESULTS="${RESULTS:-$ROOT/Exhibits/FluidReference/Results/PlumeTransport-$LABEL.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$TOOLS/vk_swiftshader_icd.json}"
export LD_LIBRARY_PATH="$TOOLS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WGPU_BACKEND_TYPE="${WGPU_BACKEND_TYPE:-Vulkan}"
{
    printf 'Engine plume footprints and retained transport WGSL; %s\n' "$LABEL"
    printf 'Base %s, plus working changes if any; source hashes below.\n' "$(git rev-parse --short HEAD)"
    printf 'Platform: '; uname -sm
    "${CXX:-c++}" --version | head -1
    node --version
    "$PYTHON" -c 'import sys, wgpu, numpy; print("Python", sys.version.split()[0], "wgpu", wgpu.__version__, "numpy", numpy.__version__)'
    sha256sum Frontier/Engine/Rendering/PlumeLatticeFootprint.{h,cpp} \
        Exhibits/FluidReference/Code/{PlumeLatticeFootprintValidation.cpp,PlumeLatticeReference.mjs,PlumeTransportValidation.py} \
        Experimental/VehicleHmiReference/Shader/{PlumeRecords,PlumeAdvection,PlumeVorticity,PlumePressure,SceneRecords,DistanceRecords,DistanceTraversal}.wgsl \
        "$TOOLS"/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json}
    "$CMAKE" -S Exhibits/FluidReference -B "$BUILD" "${CMAKE_ARGS[@]}" >/dev/null
    "$CMAKE" --build "$BUILD" --target PlumeLatticeFootprintValidation GasProjectionFootprintValidation --parallel "${BUILD_JOBS:-2}" >/dev/null
    "$CTEST" --test-dir "$BUILD" --output-on-failure -V
    if [[ "$LABEL" == sanitized ]]; then
        ldd "$BUILD/PlumeLatticeFootprintValidation" | grep -E 'libasan|libubsan'
    fi
    "$PYTHON" Exhibits/FluidReference/Code/PlumeTransportValidation.py "$BUILD/PlumeLatticeFootprintValidation"
} 2>&1 | tee "$RESULTS"
