#============================================================================================================================================
#                                                   PACKAGEWORKSPACESWINDOWS.PY
#============================================================================================================================================
# 🧩 Verify and package the restricted Windows applications with runtime shaders and recoverable diagnostics.

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


def Digest(Filename):
    return hashlib.sha256(Filename.read_bytes()).hexdigest()



def VerifyDistributionAuthorization(Security, UnsignedPreviewEnabled=False):
    if not Security.get('distribution_ready') or not Security.get('defender', {}).get('clean') or \
            Security.get('defender', {}).get('status') != 'passed':
        raise RuntimeError('Distribution withheld: signature/antivirus verification is incomplete')
    Entries = Security.get('signatures', [])
    Signatures = {Entry['application']: Entry for Entry in Entries}
    AcceptedStatuses = {'Valid'}
    if UnsignedPreviewEnabled and Security.get('unsigned_preview_authorized') is True:
        AcceptedStatuses.add('NotSigned')
    if len(Entries) != 2 or set(Signatures) != {'Frontier', 'TextureWorkspace'} or \
            any(Entry['status'] not in AcceptedStatuses for Entry in Signatures.values()):
        raise RuntimeError('Distribution withheld: publisher signatures do not meet the authorized policy')
    return Signatures


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--build', type=Path, required=True)
    Parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Release')
    Parser.add_argument('--output', type=Path, required=True)
    Parser.add_argument('--security-report', type=Path, required=True)
    Parser.add_argument('--allow-unsigned-preview', action='store_true')
    Arguments = Parser.parse_args()
    Security = json.loads(Arguments.security_report.read_text(encoding='utf-8-sig'))
    Signatures = VerifyDistributionAuthorization(Security, Arguments.allow_unsigned_preview)
    Build = Arguments.build.resolve()
    ShaderDirectory = Build / 'ShaderArchives/Surface'
    Compilation = json.loads((ShaderDirectory / 'ShaderCompilation.json').read_text())
    if len(Compilation['entries']) != 14:
        raise RuntimeError('Expected fourteen compiled shader entries')
    for Source in Compilation['sources']:
        if Digest(RepositoryRoot / Source['file']) != Source['sha256']:
            raise RuntimeError('Shader source fingerprint differs: ' + Source['file'])
    Scratch = RepositoryRoot / '_ClaudeScratch/tmp'
    Scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=Scratch, prefix='workspace-delivery-') as Temporary:
        Installation = Path(Temporary)
        for Name in ('Frontier', 'TextureWorkspace'):
            Executable = Build / Arguments.configuration / (Name + '.exe')
            Payload = Executable.read_bytes()
            Offset = struct.unpack_from('<I', Payload, 0x3c)[0]
            if Payload[:2] != b'MZ' or Payload[Offset:Offset + 4] != b'PE\0\0' or \
                    struct.unpack_from('<H', Payload, Offset + 4)[0] != 0x8664 or \
                    struct.unpack_from('<H', Payload, Offset + 24)[0] != 0x20b:
                raise RuntimeError('Expected Windows x64 PE32+: ' + Name)
            shutil.copy2(Executable, Installation / Executable.name)
            if Digest(Installation / Executable.name) != Signatures[Name]['sha256']:
                raise RuntimeError('Executable changed after security verification: ' + Name)
        shutil.copytree(RepositoryRoot / 'EngineContent', Installation / 'EngineContent')
        RuntimeShaders = Installation / 'EngineContent/ShaderArchives/Surface'
        shutil.copytree(ShaderDirectory, RuntimeShaders, dirs_exist_ok=True)
        for Shader in Compilation['entries']:
            Filename = Shader.get('output', Shader.get('file', Shader['entry'] + '.spv'))
            Binary = RuntimeShaders / Path(Filename).name
            if not Binary.is_file():
                Binary = RuntimeShaders / (Shader['entry'] + '.spv')
            Payload = Binary.read_bytes()
            if len(Payload) % 4 or len(Payload) < 20 or struct.unpack_from('<I', Payload)[0] != 0x07230203:
                raise RuntimeError('Malformed packaged SPIR-V: ' + Binary.name)
            if Digest(Binary) != Shader['spirv_sha256']:
                raise RuntimeError('Packaged shader fingerprint differs: ' + Binary.name)
        Catalogue = json.loads((Installation / 'EngineContent/FontArchives/FontCatalogue.json').read_text())
        for Typeface in Catalogue['faces']:
            if Digest(Installation / 'EngineContent/FontArchives' / Typeface['file']) != Typeface['sha256']:
                raise RuntimeError('Packaged font differs: ' + Typeface['file'])
        if len(list((Installation / 'EngineContent/MaterialArchives/TextureStudio').glob('*.material'))) != 131:
            raise RuntimeError('Expected complete 131-material catalogue')
        Notices = Installation / 'Notices'
        Notices.mkdir()
        for Package, Filename in [('imgui', 'LICENSE.txt'), ('glfw', 'LICENSE.md'), ('tomlpp', 'LICENSE'),
                                  ('stb', 'LICENSE'), ('thorvg', 'LICENSE')]:
            shutil.copy2(RepositoryRoot / 'ExternalPackages' / Package / Filename, Notices / (Package + '-LICENSE.txt'))
        shutil.copytree(RepositoryRoot / 'ExternalPackages/vulkan-headers/LICENSES', Notices / 'Vulkan-Headers')
        shutil.copy2(RepositoryRoot / 'Engine/Interface/Lucide.NOTICE', Notices / 'Lucide.NOTICE')
        shutil.copy2(RepositoryRoot / 'Documentation/SourceNotices.md', Notices / 'SourceNotices.md')
        shutil.copy2(RepositoryRoot / 'Exhibits/TextureStudio/WindowsWorkspacesReadme.md', Installation / 'README.md')
        shutil.copy2(Arguments.security_report, Installation / 'DistributionChecks.json')
        if any(Entry['status'] == 'NotSigned' for Entry in Signatures.values()):
            (Installation / 'UNSIGNED-PREVIEW.txt').write_text(
                'This is an explicitly authorized UNSIGNED Windows preview.\n'
                'The rebuilt output passed the recorded Defender scan; that does not certify Chrome reputation.\n'
                'Source revision, executable hashes and scan results are in Package.json and DistributionChecks.json.\n'
                'Do not disable browser or antivirus protection. If Chrome blocks this download, stop and report it.\n',
                encoding='utf-8')
        Metadata = {
            'format': 1, 'target': 'Windows x64', 'toolchain': 'GitHub Actions windows-2022 / MSVC / static CRT',
            'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip(),
            'configuration': Arguments.configuration,
            'debug_pattern_gpu_logging': Arguments.configuration == 'Debug',
            'scopes': {'Frontier': ['Project Browser'], 'TextureWorkspace': ['Material Library', 'Preview', 'Inspector', 'Experimental Pattern preview']},
            'pattern_material_application': 'preview only; persistent/scene-level application disabled',
            'windows_interactive_gpu_verified': False,
            'distribution_checks': Security,
            'shader_compilation': Compilation,
            'files': {Filename.relative_to(Installation).as_posix(): Digest(Filename)
                      for Filename in sorted(Installation.rglob('*')) if Filename.is_file()},
        }
        (Installation / 'Package.json').write_text(json.dumps(Metadata, indent=2) + '\n', encoding='utf-8')
        Arguments.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(Arguments.output, 'w', zipfile.ZIP_DEFLATED) as Archive:
            for Filename in sorted(Installation.rglob('*')):
                if Filename.is_file():
                    Archive.write(Filename, Filename.relative_to(Installation))
        with zipfile.ZipFile(Arguments.output) as Archive:
            if Archive.testzip() is not None:
                raise RuntimeError('ZIP integrity validation failed')
        print('Windows application/shader package verified:', Arguments.output)
        Checksum = Digest(Arguments.output)
        Arguments.output.with_suffix('.zip.sha256').write_text(Checksum + '  ' + Arguments.output.name + '\n')
        print('SHA256:', Checksum)
        print('Interactive Windows Vulkan startup remains a user acceptance check.')


if __name__ == '__main__':
    Main()
