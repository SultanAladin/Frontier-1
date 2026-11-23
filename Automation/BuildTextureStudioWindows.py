#============================================================================================================================================
#                                                   BUILDTEXTURESTUDIOWINDOWS.PY
#============================================================================================================================================
# 🧩 Link and package the standalone Windows x64 material editor using an explicitly provisioned, pinned cross-toolchain.

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from StageThorVG import stage as StageThorvg

RepositoryRoot = Path(__file__).resolve().parents[1]


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--windows-tools', type=Path, required=True)
    Parser.add_argument('--shaders', type=Path, required=True)
    Parser.add_argument('--destination', type=Path, required=True)
    Parser.add_argument('--output', type=Path, default=RepositoryRoot / 'Binaries/TextureStudio-Windows-x64.zip')
    Parser.add_argument('--dependencies-only', action='store_true')
    Arguments = Parser.parse_args()
    Destination = Arguments.destination.resolve()
    Destination.mkdir(parents=True, exist_ok=True)
    Environment = dict(os.environ, PYTHONPATH=str(Arguments.windows_tools.resolve()))
    if hasattr(os, 'sched_getaffinity'):
        os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:4]))
    Toolchain = [sys.executable, '-m', 'ziglang']
    Version = subprocess.check_output(Toolchain + ['version'], env=Environment, text=True).strip()
    if Version != '0.13.0':
        raise RuntimeError('Expected the reviewed ziglang 0.13.0 toolchain; this script downloads nothing.')
    Staging = Destination / 'ThorVG'
    Staging.mkdir(exist_ok=True)
    StageThorvg(RepositoryRoot / 'ExternalPackages/thorvg', Staging / 'tvgInitializer.cpp')
    (Staging / 'config.h').write_text('#pragma once\n#define THORVG_VERSION_STRING "1.0.0"\n'
        '#define THORVG_SW_RASTER_SUPPORT 1\n#define THORVG_SVG_LOADER_SUPPORT 1\n'
        '#define THORVG_THREAD_SUPPORT 1\n#define THORVG_FILE_IO_SUPPORT 1\n')
    Includes = ['.', 'ExternalPackages/imgui', 'ExternalPackages/imgui/backends', 'ExternalPackages/glfw/include',
                'ExternalPackages/vulkan-headers/include', 'ExternalPackages/tomlpp/include', 'ExternalPackages/stb',
                'ExternalPackages/thorvg/inc', str(Staging)]
    ThorvgDirectories = ['src/common', 'src/renderer', 'src/renderer/sw_engine', 'src/loaders/svg', 'src/loaders/raw']
    Includes += ['ExternalPackages/thorvg/' + Name for Name in ThorvgDirectories]
    Definitions = ['FRONTIER_EDITOR=1', 'FRONTIER_UI_VULKAN=1', 'VK_NO_PROTOTYPES', 'NOMINMAX',
                   'WIN32_LEAN_AND_MEAN', 'TVG_STATIC', 'TVG_BUILD', '_GLFW_WIN32']
    Prefix = ['-target', 'x86_64-windows-gnu', '-O1', '-DNDEBUG', '-static'] + ['-I' + Name for Name in Includes]
    Prefix += ['-D' + Name for Name in Definitions]
    Thorvg = [Staging / 'tvgInitializer.cpp']
    for Name in ThorvgDirectories:
        Thorvg += [Source for Source in sorted((RepositoryRoot / 'ExternalPackages/thorvg' / Name).glob('*.cpp'))
                   if Source.name != 'tvgInitializer.cpp']
    Glfw = [RepositoryRoot / 'ExternalPackages/glfw/src' / (Name + '.c') for Name in
            ('context', 'init', 'input', 'monitor', 'platform', 'vulkan', 'window', 'egl_context', 'osmesa_context',
             'null_init', 'null_monitor', 'null_window', 'null_joystick', 'win32_module', 'win32_time', 'win32_thread',
             'win32_init', 'win32_joystick', 'win32_monitor', 'win32_window', 'wgl_context')]
    Cmake = (RepositoryRoot / 'CMakeLists.txt').read_text()
    NativeSources = []
    for Target in ('FrontierImGui', 'FrontierInterface', 'FrontierEditorUI', 'FrontierTextureStudioUI',
                   'FrontierMaterialLibrary', 'FrontierTextFiles', 'TextureStudio'):
        Section = re.search(r'add_(?:library|executable)\(' + Target + r'\s+(.*?)\)', Cmake, re.S).group(1)
        NativeSources += [RepositoryRoot / Name.replace('${IMGUI_ROOT}', 'ExternalPackages/imgui').strip('"')
                          for Name in re.findall(r'"?[^\s"]+\.cpp"?', Section)]
    NativeSources += [RepositoryRoot / 'Engine/Render' / (Name + '.cpp') for Name in
                      ('SurfaceRasterization', 'SurfaceMaterial', 'MaterialThumbnailAtlas', 'MaterialLibraryRendering')]
    NativeSources = sorted(set(NativeSources))
    DependencyHeaders = sorted(Source for Source in (RepositoryRoot / 'ExternalPackages').rglob('*')
                               if Source.is_file() and Source.suffix in ('.h', '.hpp', '.inc', '.inl'))
    NativeHeaders = sorted(Source for Folder in ('Engine', 'UI') for Source in (RepositoryRoot / Folder).rglob('*')
                           if Source.is_file() and Source.suffix in ('.h', '.inc'))
    DependencyDigest = hashlib.sha256(b''.join(Source.read_bytes() for Source in DependencyHeaders)).digest()
    NativeDigest = hashlib.sha256(DependencyDigest + b''.join(Source.read_bytes() for Source in NativeHeaders)).digest()
    Objects = Destination / 'Objects'
    Objects.mkdir(exist_ok=True)

    def Compile(Source):
        IsNative = Source in NativeSources
        SourceDigest = hashlib.sha256(Source.read_bytes() + (NativeDigest if IsNative else DependencyDigest) +
                                      json.dumps(Prefix).encode() + Version.encode()).hexdigest()
        Object = Objects / (Source.stem + '-' + SourceDigest[:20] + '.obj')
        if not Object.exists():
            Command = Toolchain + (['cc'] if Source.suffix == '.c' else ['c++', '-std=c++20']) + Prefix
            Command += ['-c', str(Source), '-o', str(Object)]
            Completed = subprocess.run(Command, cwd=RepositoryRoot, env=Environment, text=True, capture_output=True)
            if Completed.returncode:
                raise RuntimeError(Source.as_posix() + '\n' + Completed.stdout + Completed.stderr)
            print('COMPILED', Source.name, flush=True)
        return Object

    with ThreadPoolExecutor(max_workers=2) as Workers:
        DependencyObjects = list(Workers.map(Compile, Thorvg + Glfw))
        if Arguments.dependencies_only:
            print('PASS: Windows x64 ThorVG/GLFW dependencies compiled; no executable claim.', flush=True)
            return
        CompiledObjects = DependencyObjects + list(Workers.map(Compile, NativeSources))
    Payload = Destination / 'TextureStudio-Windows-x64'
    if Payload.exists():
        shutil.rmtree(Payload) # Only this tool's generated staging directory; never a user material library.
    Payload.mkdir(exist_ok=True)
    Executable = Payload / 'TextureStudio.exe'
    Link = Toolchain + ['c++', '-target', 'x86_64-windows-gnu', '-static', '-O1', '-Wl,--subsystem,windows',
                        '-Wl,--entry,mainCRTStartup'] + [str(Object) for Object in CompiledObjects]
    Link += ['-luser32', '-lgdi32', '-lshell32', '-lcomdlg32', '-lole32', '-limm32', '-luuid', '-lwinmm',
             '-ldwmapi', '-o', str(Executable)]
    subprocess.run(Link, cwd=RepositoryRoot, env=Environment, check=True)
    Bytes = Executable.read_bytes()
    Offset = struct.unpack_from('<I', Bytes, 0x3c)[0]
    if Bytes[:2] != b'MZ' or Bytes[Offset:Offset + 4] != b'PE\0\0' or struct.unpack_from('<H', Bytes, Offset + 4)[0] != 0x8664:
        raise RuntimeError('Expected a linked Windows x64 executable.')
    shutil.copytree(RepositoryRoot / 'EngineContent', Payload / 'EngineContent', dirs_exist_ok=True)
    ShaderOutput = Payload / 'EngineContent/ShaderArchives/Surface'
    ShaderOutput.mkdir(parents=True, exist_ok=True)
    ShaderManifest = json.loads((Arguments.shaders.resolve() / 'ShaderCompilation.json').read_text())
    for Source in ShaderManifest['sources']:
        if hashlib.sha256((RepositoryRoot / Source['file']).read_bytes()).hexdigest() != Source['sha256']:
            raise RuntimeError('Stale shader source: ' + Source['file'])
    for Source in Arguments.shaders.resolve().glob('*.spv'):
        shutil.copy2(Source, ShaderOutput / Source.name)
    for Entry in ShaderManifest['entries']:
        Shader = ShaderOutput / (Entry['entry'] + '.spv')
        if hashlib.sha256(Shader.read_bytes()).hexdigest() != Entry['spirv_sha256']:
            raise RuntimeError('Stale or damaged shader archive: ' + Entry['entry'])
    ShaderRoot = Arguments.shaders.resolve()
    VectorManifest = json.loads((ShaderRoot / 'VectorCompilation.json').read_text())
    for Source in VectorManifest['sources']:
        if hashlib.sha256((RepositoryRoot / Source['file']).read_bytes()).hexdigest() != Source['sha256']:
            raise RuntimeError('Stale vector shader source: ' + Source['file'])
    for Entry in VectorManifest['entries']:
        Shader = ShaderOutput / (Entry['entry'] + '.spv')
        if hashlib.sha256(Shader.read_bytes()).hexdigest() != Entry['spirv_sha256']:
            raise RuntimeError('Stale or damaged vector shader archive: ' + Entry['entry'])
    SurfaceVectorManifest = json.loads((ShaderRoot / 'VectorSurfaceCompilation.json').read_text())
    VectorSurfaceSources = [{'file': 'Engine/Render/Shader/VectorSurface.slang',
                             'sha256': SurfaceVectorManifest['source_sha256']}, *SurfaceVectorManifest['dependencies']]
    for Source in VectorSurfaceSources:
        if hashlib.sha256((RepositoryRoot / Source['file']).read_bytes()).hexdigest() != Source['sha256']:
            raise RuntimeError('Stale Float32 material shader source: ' + Source['file'])
    VectorSurfaceArchive = ShaderOutput / (SurfaceVectorManifest['entry'] + '.spv')
    if hashlib.sha256(VectorSurfaceArchive.read_bytes()).hexdigest() != SurfaceVectorManifest['binary_sha256']:
        raise RuntimeError('Stale or damaged Float32 material shader archive.')
    for ManifestName in ('ShaderCompilation.json', 'VectorCompilation.json', 'VectorSurfaceCompilation.json'):
        shutil.copy2(ShaderRoot / ManifestName, ShaderOutput / ManifestName)
    if len(list(ShaderOutput.glob('*.spv'))) != 13:
        raise RuntimeError('Expected ten surface and three vector shader entries.')
    Notices = Payload / 'Notices'
    Notices.mkdir(exist_ok=True)
    for Project, Filename in [('imgui', 'LICENSE.txt'), ('glfw', 'LICENSE.md'), ('tomlpp', 'LICENSE'),
                              ('stb', 'LICENSE'), ('thorvg', 'LICENSE')]:
        shutil.copy2(RepositoryRoot / 'ExternalPackages' / Project / Filename, Notices / (Project + '-LICENSE.txt'))
    shutil.copy2(RepositoryRoot / 'Engine/Interface/Lucide.NOTICE', Notices / 'Lucide.NOTICE')
    shutil.copy2(RepositoryRoot / 'Documentation/SourceNotices.md', Notices / 'SourceNotices.md')
    shutil.copy2(RepositoryRoot / 'Exhibits/TextureStudio/WindowsReadme.md', Payload / 'README.md')
    Report = {'target': 'Windows x64', 'toolchain': 'Zig ' + Version, 'executable_linked': True,
              'windows_execution': False, 'hardware_execution': False, 'uncommitted_changes': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=RepositoryRoot)),
              'build_source_sha256': {Name: hashlib.sha256((RepositoryRoot / Name).read_bytes()).hexdigest()
                  for Name in ('CMakeLists.txt', 'Automation/BuildTextureStudioWindows.py', 'Automation/StageThorVG.py')},
              'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip(),
              'sources': {str(Source.relative_to(RepositoryRoot)): hashlib.sha256(Source.read_bytes()).hexdigest()
                          for Source in NativeSources + NativeHeaders},
              'files': {str(Source.relative_to(Payload)): hashlib.sha256(Source.read_bytes()).hexdigest()
                        for Source in sorted(Payload.rglob('*')) if Source.is_file() and Source != Payload / 'Package.json'}}
    (Payload / 'Package.json').write_text(json.dumps(Report, indent=2) + '\n')
    Output = Arguments.output.resolve()
    Output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(Output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as Archive:
        for Source in sorted(Payload.rglob('*')):
            if Source.is_file():
                Archive.write(Source, Source.relative_to(Payload))
    print('PASS: linked Windows x64 package:', Output, flush=True)
    print('SHA256:', hashlib.sha256(Output.read_bytes()).hexdigest(), flush=True)
    print('Windows runtime/hardware testing remains required.', flush=True)


if __name__ == '__main__':
    Main()
