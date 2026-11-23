#============================================================================================================================================
#                                                        VECTORSHADERARCHIVES.PY
#============================================================================================================================================
# 🧩 Authenticate required coverage/selection archives against their compilation manifest before Windows packaging.

import hashlib
import json
from pathlib import Path
import struct


def VerifyVectorShaderArchives(Repository, Directory):
    Directory = Path(Directory)
    Manifest = json.loads((Directory / 'VectorCompilation.json').read_text())
    Expected = {'VectorCoverageCompute', 'VectorSelectionCompute'}
    Entries = Manifest['entries']
    if len(Entries) != 2 or {Entry['entry'] for Entry in Entries} != Expected:
        raise RuntimeError('Expected coverage and selection shader archives.')
    Sources = Manifest['sources']
    if len(Sources) != 1 or Sources[0]['file'] != 'Engine/Render/Shader/VectorCoverage.slang':
        raise RuntimeError('Invalid vector shader source manifest.')
    Source = Path(Repository) / Sources[0]['file']
    if hashlib.sha256(Source.read_bytes()).hexdigest() != Sources[0]['sha256']:
        raise RuntimeError('Stale vector shader source.')
    for Entry in Entries:
        Archive = Directory / (Entry['entry'] + '.spv')
        Payload = Archive.read_bytes()
        if len(Payload) < 20 or len(Payload) % 4 or struct.unpack_from('<I', Payload)[0] != 0x07230203:
            raise RuntimeError('Malformed vector shader archive: ' + Archive.name)
        if hashlib.sha256(Payload).hexdigest() != Entry['spirv_sha256']:
            raise RuntimeError('Stale vector shader archive: ' + Archive.name)
    return [Entry['entry'] + '.spv' for Entry in Entries] + ['VectorCompilation.json']
