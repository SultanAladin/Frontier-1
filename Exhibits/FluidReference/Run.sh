#!/usr/bin/env bash
# 📝 Verify full-demo plume provenance and execute source and idealized stencil checks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
mkdir -p Exhibits/FluidReference/Results
RESULTS="${RESULTS:-$ROOT/Exhibits/FluidReference/Results/PlumeReference.txt}"
{
    printf 'Platform: '; uname -sm
    printf 'Node: '; node --version
    printf 'Reference: SultanAladin/Frontier 6afb6f00b5f25c45f7cda92c7da65b64845768f3\n'
    python3 - <<'PY'
import json
import pathlib
import subprocess
root = pathlib.Path('Experimental/VehicleHmiReference')
manifest = json.loads((root / 'SourceManifest.json').read_text())
for entry in manifest['files']:
    path = root / entry['path']
    actual = subprocess.check_output(['git', 'hash-object', str(path)], text=True).strip()
    if actual != entry['git_blob']:
        raise SystemExit(f'Blob mismatch: {path}: {actual} != {entry["git_blob"]}')
print(f'All {len(manifest["files"])} retained full-demo source blobs match their manifest')
PY
    sha256sum Exhibits/FluidReference/Code/{PlumeReferenceValidation,PressureStencilValidation}.test.mjs
    node --test Exhibits/FluidReference/Code/{PlumeReferenceValidation,PressureStencilValidation}.test.mjs
} | tee "$RESULTS"
