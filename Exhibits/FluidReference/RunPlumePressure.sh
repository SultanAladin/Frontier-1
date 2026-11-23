#!/usr/bin/env bash
# 📝 Execute the retained pressure WGSL on CPU Vulkan and compare with independent stencils.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PYTHON="${PYTHON:-$ROOT/_ClaudeScratch/venv/bin/python}"
TOOLS="$ROOT/ExternalPackages/VulkanCompute/SwiftShader"
if [[ ! -x "$PYTHON" ]]; then
    echo "Python with wgpu==0.32.0 and numpy==2.3.4 is required: $PYTHON" >&2
    exit 1
fi
if [[ ! -f "$TOOLS/libvulkan.so.1" || ! -f "$TOOLS/vk_swiftshader_icd.json" ]]; then
    echo 'Prepare the pinned SwiftShader tools: python3 Exhibits/LightCompute/Code/Prepare.py' >&2
    exit 1
fi
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$TOOLS/vk_swiftshader_icd.json}"
export LD_LIBRARY_PATH="$TOOLS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WGPU_BACKEND_TYPE="${WGPU_BACKEND_TYPE:-Vulkan}"
RESULTS="${RESULTS:-$ROOT/Exhibits/FluidReference/Results/PlumePressure.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Platform: '; uname -sm
    "$PYTHON" - <<'PY'
import sys, wgpu, numpy
print('Python:', sys.version.split()[0], 'wgpu:', wgpu.__version__, 'numpy:', numpy.__version__)
PY
    printf 'Reference: SultanAladin/Frontier 6afb6f00b5f25c45f7cda92c7da65b64845768f3\n'
    sha256sum Experimental/VehicleHmiReference/Shader/{PlumeRecords,PlumePressure}.wgsl \
              Exhibits/FluidReference/Code/PlumePressureValidation.py \
              "$TOOLS"/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json}
    "$PYTHON" Exhibits/FluidReference/Code/PlumePressureValidation.py
} 2>&1 | tee "$RESULTS"
