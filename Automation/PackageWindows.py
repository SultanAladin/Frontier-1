#============================================================================================================================================
#                                                         PACKAGEWINDOWS.PY
#============================================================================================================================================
# 🧩 Package an installed Windows x64 UI executable with fonts, SVGs, licences, and integrity metadata.

from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
import zipfile

RepositoryRoot = Path(__file__).resolve().parents[1]


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--build', type=Path, required=True)
    Parser.add_argument('--config', default='Release')
    Parser.add_argument('--toolchain', required=True)
    Parser.add_argument('--output', type=Path, default=RepositoryRoot / 'Binaries/ControlCentre-Windows-x64.zip')
    Arguments = Parser.parse_args()
    Scratch = RepositoryRoot / '_ClaudeScratch/tmp'
    Scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=Scratch, prefix='windows-package-') as Temporary:
        Installation = Path(Temporary)
        subprocess.run(['cmake', '--install', str(Arguments.build.resolve()), '--config', Arguments.config,
                        '--prefix', str(Installation)], cwd=RepositoryRoot, check=True)
        Executable = Installation / 'FrontierUI.exe'
        Payload = Executable.read_bytes()
        PeOffset = struct.unpack_from('<I', Payload, 0x3c)[0]
        if Payload[:2] != b'MZ' or Payload[PeOffset:PeOffset + 4] != b'PE\0\0':
            raise RuntimeError('Not a Windows executable')
        if struct.unpack_from('<H', Payload, PeOffset + 4)[0] != 0x8664:
            raise RuntimeError('Expected Windows x64 executable')
        if struct.unpack_from('<H', Payload, PeOffset + 24)[0] != 0x20b:
            raise RuntimeError('Expected PE32+ executable')
        Catalogue = json.loads((Installation / 'EngineContent/FontArchives/FontCatalogue.json').read_text())
        for Entry in Catalogue['faces']:
            Font = Installation / 'EngineContent/FontArchives' / Entry['file']
            if hashlib.sha256(Font.read_bytes()).hexdigest() != Entry['sha256']:
                raise RuntimeError('Bundled font catalogue differs from installed files')
        shutil.copy2(RepositoryRoot / 'Exhibits/ControlCentre/WindowsReadme.md', Installation / 'README.md')
        Metadata = {
            'target': 'Windows x64', 'toolchain': Arguments.toolchain, 'configuration': Arguments.config,
            'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip(),
            'uncommitted_changes': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=RepositoryRoot)),
            'windows_runtime_verified': False,
            'source_sha256': {
                FilePath.relative_to(RepositoryRoot).as_posix(): hashlib.sha256(FilePath.read_bytes()).hexdigest()
                for Folder in ('Engine', 'UI/Editor', 'Automation', 'Exhibits/UIFoundation/Code')
                for FilePath in sorted((RepositoryRoot / Folder).rglob('*'))
                if FilePath.is_file() and FilePath.suffix in ('.cpp', '.h', '.inc', '.cmake', '.json', '.py', '.patch')
                and '__pycache__' not in FilePath.parts},
            'files': {FilePath.relative_to(Installation).as_posix(): hashlib.sha256(FilePath.read_bytes()).hexdigest()
                      for FilePath in sorted(Installation.rglob('*')) if FilePath.is_file()},
        }
        Metadata['source_sha256']['CMakeLists.txt'] = hashlib.sha256((RepositoryRoot / 'CMakeLists.txt').read_bytes()).hexdigest()
        (Installation / 'Package.json').write_text(json.dumps(Metadata, indent=2) + '\n')
        Arguments.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(Arguments.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as Archive:
            for FilePath in sorted(Installation.rglob('*')):
                if FilePath.is_file():
                    Archive.write(FilePath, FilePath.relative_to(Installation))
        print('Windows-only package:', Arguments.output)
        print('SHA-256:', hashlib.sha256(Arguments.output.read_bytes()).hexdigest())
        print('Cross-compilation is not Windows runtime or GPU verification.')


if __name__ == '__main__':
    Main()
