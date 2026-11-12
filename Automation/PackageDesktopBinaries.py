#============================================================================================================================================
#                                                    PACKAGEDESKTOPBINARIES.PY
#============================================================================================================================================
# 🧩 Collect FrontierUV / FrontierEditor executables, SPIR-V shaders, EngineContent, and licences into a relocatable desktop folder.
#
# Usage:
#   python3 Automation/PackageDesktopBinaries.py --collect --build-dir Build/Windows --package-dir Build/Package/FrontierUV-Windows-x64
#   python3 Automation/PackageDesktopBinaries.py --bundle  --package-dir Build/Package/FrontierUV-Windows-x64
#
# --collect copies files and writes PackageManifest.json; code signing runs between the two steps so that
# --bundle records SHA-256 digests of the final signed binaries before compressing <package-dir>.zip.

from pathlib import Path
import argparse
import hashlib
import json
import shutil
import sys
import zipfile

RepositoryRoot = Path(__file__).resolve().parents[1]

ExecutableStems   = ['FrontierUV', 'FrontierEditor']
ContentFolders    = ['EngineContent/Geometry', 'EngineContent/FontsArchives']
LicenceCandidates = ['LICENSE', 'LICENSE.txt', 'LICENSE.md', 'LICENCE', 'LICENSE.TXT', 'COPYING', 'COPYING.txt']

# Third-party packages whose code is linked into the desktop executables (JoltPhysics, ThorVG, and stb are not linked).
LinkedPackages = ['imgui', 'glfw', 'volk', 'vulkan-headers', 'tomlpp']

LaunchNotes = '''Frontier UV - desktop preview build
===================================

Launch
------
  FrontierUV.exe        Standalone UV workspace
  FrontierEditor.exe    Feature-set editor host with the UV workspace attached

Keep CompiledShaders/ and EngineContent/ beside the executables; both are read relative to the executable location.
A Vulkan 1.2 capable GPU driver is required.

Verifying this download
-----------------------
1. Digests:      compare each file against SHA256SUMS.txt
                 (PowerShell: Get-FileHash FrontierUV.exe -Algorithm SHA256)
2. Provenance:   gh attestation verify FrontierUV.exe --repo {Repository}
                 This proves the binary was produced by the GitHub Actions workflow of that repository at the commit
                 recorded in PackageManifest.json.
3. Signature:    Right-click FrontierUV.exe -> Properties -> Digital Signatures.
                 Present only when the build was run with an Authenticode certificate configured
                 (PackageManifest.json records "AuthenticodeSigned").

Browser and SmartScreen prompts
-------------------------------
Browsers and Windows SmartScreen rate executables by publisher certificate and download reputation. A build without an
Authenticode signature from a trusted certificate authority can still be reported as "not commonly downloaded" or
"unrecognised app" even though the digests and provenance above verify. See
Documentation/Plans/UVWorkspaceAuditAndWindowsPackaging.md in the repository for the certificate options.
'''


def DigestFile(TargetPath):
    Hasher = hashlib.sha256()
    with TargetPath.open('rb') as Reader:
        for Block in iter(lambda: Reader.read(1 << 20), b''):
            Hasher.update(Block)
    return Hasher.hexdigest()


def LocateExecutable(BuildDir, Stem):
    # Single-config generators place binaries at the build root; Visual Studio generators nest them per configuration.
    for Candidate in (
        BuildDir / (Stem + '.exe'), BuildDir / 'Release' / (Stem + '.exe'), BuildDir / 'RelWithDebInfo' / (Stem + '.exe'),
        BuildDir / Stem,
    ):
        if Candidate.is_file():
            return Candidate
    return None


#------------------------------------------------------------------------------------------------------------------------
#                                                        COLLECT
#------------------------------------------------------------------------------------------------------------------------

def CollectPackage(BuildDir, PackageDir, Repository, Revision):
    if PackageDir.exists():
        shutil.rmtree(PackageDir)
    PackageDir.mkdir(parents=True)

    CollectedExecutables = []
    for Stem in ExecutableStems:
        SourcePath = LocateExecutable(BuildDir, Stem)
        if SourcePath is None:
            continue
        shutil.copy2(SourcePath, PackageDir / SourcePath.name)
        CollectedExecutables.append(SourcePath.name)
    if not any(Name.startswith('FrontierUV') for Name in CollectedExecutables):
        raise RuntimeError('FrontierUV executable not found under ' + str(BuildDir))

    ShaderSource = BuildDir / 'CompiledShaders'
    ShaderBinaries = sorted(ShaderSource.glob('*.spv'))
    if not ShaderBinaries:
        raise RuntimeError('No compiled SPIR-V shaders under ' + str(ShaderSource))
    (PackageDir / 'CompiledShaders').mkdir()
    for ShaderBinary in ShaderBinaries:
        shutil.copy2(ShaderBinary, PackageDir / 'CompiledShaders' / ShaderBinary.name)

    for ContentFolder in ContentFolders:
        SourceFolder = RepositoryRoot / ContentFolder
        if not SourceFolder.is_dir():
            raise RuntimeError('Required content folder missing: ' + ContentFolder)
        shutil.copytree(SourceFolder, PackageDir / ContentFolder)

    NoticeDir = PackageDir / 'ThirdPartyNotices'
    NoticeDir.mkdir()
    PinnedPackages = json.loads((RepositoryRoot / 'Automation/Dependencies.json').read_text(encoding='utf-8'))['packages']
    NoticeLines = ['Third-party code linked into the Frontier desktop executables', '']
    for Package in PinnedPackages:
        if Package['name'] not in LinkedPackages:
            continue
        NoticeLines.append('{0}  https://github.com/{1}  @ {2}'.format(Package['name'], Package['repository'], Package['revision']))
        PackageRoot = RepositoryRoot / 'ExternalPackages' / Package['name']
        for Candidate in LicenceCandidates:
            if (PackageRoot / Candidate).is_file():
                shutil.copy2(PackageRoot / Candidate, NoticeDir / (Package['name'] + '-' + Candidate))
                break
    (NoticeDir / 'Notices.txt').write_text('\n'.join(NoticeLines) + '\n', encoding='utf-8')

    (PackageDir / 'README.txt').write_text(LaunchNotes.format(Repository=Repository or '<owner>/<repository>'), encoding='utf-8')

    Manifest = {
        'Package': PackageDir.name,
        'Repository': Repository,
        'Revision': Revision,
        'Executables': CollectedExecutables,
        'ShaderBinaryCount': len(ShaderBinaries),
        'AuthenticodeSigned': False,
    }
    (PackageDir / 'PackageManifest.json').write_text(json.dumps(Manifest, indent=2) + '\n', encoding='utf-8')
    print('[Collect]  {0}: {1}, {2} shaders'.format(PackageDir, ', '.join(CollectedExecutables), len(ShaderBinaries)))


#------------------------------------------------------------------------------------------------------------------------
#                                                         BUNDLE
#------------------------------------------------------------------------------------------------------------------------

def BundlePackage(PackageDir, AuthenticodeSigned):
    ManifestPath = PackageDir / 'PackageManifest.json'
    Manifest = json.loads(ManifestPath.read_text(encoding='utf-8'))
    Manifest['AuthenticodeSigned'] = bool(AuthenticodeSigned)
    ManifestPath.write_text(json.dumps(Manifest, indent=2) + '\n', encoding='utf-8')

    DigestPath = PackageDir / 'SHA256SUMS.txt'
    DigestPath.unlink(missing_ok=True)
    PackagedFiles = sorted(Entry for Entry in PackageDir.rglob('*') if Entry.is_file())
    DigestLines = ['{0}  {1}'.format(DigestFile(Entry), Entry.relative_to(PackageDir).as_posix()) for Entry in PackagedFiles]
    DigestPath.write_text('\n'.join(DigestLines) + '\n', encoding='utf-8')

    BundlePath = PackageDir.with_name(PackageDir.name + '.zip')
    BundlePath.unlink(missing_ok=True)
    with zipfile.ZipFile(BundlePath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as Bundle:
        for Entry in PackagedFiles + [DigestPath]:
            Bundle.write(Entry, (Path(PackageDir.name) / Entry.relative_to(PackageDir)).as_posix())
    print('[Bundle]   {0} ({1} files, {2:.1f} MiB, sha256 {3})'.format(
        BundlePath, len(PackagedFiles) + 1, BundlePath.stat().st_size / (1 << 20), DigestFile(BundlePath)))


def Main():
    Parser = argparse.ArgumentParser(description='Package Frontier desktop executables into a relocatable folder.')
    Parser.add_argument('--collect', action='store_true')
    Parser.add_argument('--bundle', action='store_true')
    Parser.add_argument('--build-dir', type=Path)
    Parser.add_argument('--package-dir', type=Path, required=True)
    Parser.add_argument('--repository', default='')
    Parser.add_argument('--revision', default='')
    Parser.add_argument('--authenticode-signed', action='store_true')
    Arguments = Parser.parse_args()

    if Arguments.collect:
        if Arguments.build_dir is None:
            raise RuntimeError('--collect requires --build-dir')
        CollectPackage(Arguments.build_dir.resolve(), Arguments.package_dir.resolve(), Arguments.repository, Arguments.revision)
    if Arguments.bundle:
        BundlePackage(Arguments.package_dir.resolve(), Arguments.authenticode_signed)
    if not Arguments.collect and not Arguments.bundle:
        raise RuntimeError('Choose --collect and/or --bundle')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(Main())
    except Exception as Error:
        print('Desktop packaging failed:', Error, file=sys.stderr)
        sys.exit(1)
