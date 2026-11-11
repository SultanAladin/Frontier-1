#============================================================================================================================================
#                                                     PREPAREDEPENDENCIES.PY
#============================================================================================================================================
# 🧩 Download, SHA-256 verify, and extract pinned third-party C/C++ packages into ExternalPackages/, then apply the ImGui patch stack.

from pathlib import Path
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import urllib.request

RepositoryRoot = Path(__file__).resolve().parents[1]


def Main():
    VerifyOnly = '--verify' in sys.argv[1:]
    Manifest = json.loads((RepositoryRoot / 'Automation/Dependencies.json').read_text(encoding='utf-8'))

    for Package in Manifest['packages']:
        PackageName = Package['name']
        Destination = RepositoryRoot / 'ExternalPackages' / PackageName
        RevisionStamp = Destination / '.frontier-revision'
        WitnessFile = Destination / Package['witness']

        if RevisionStamp.exists() and WitnessFile.exists() and RevisionStamp.read_text(encoding='utf-8').strip() == Package['revision']:
            print('[Pinned]   {0} ({1})'.format(PackageName, Package['revision'][:10]))
            continue

        if VerifyOnly:
            raise RuntimeError('Missing or mismatched pinned dependency: ' + PackageName)

        if Destination.exists():
            raise RuntimeError('Unmanaged directory present at ' + str(Destination) + '; preserve or remove before recovery.')

        print('[Fetch]    Downloading {0} ...'.format(PackageName), flush=True)
        with urllib.request.urlopen(Package['url'], timeout=120) as Response:
            ArchiveBytes = Response.read()

        Digest = hashlib.sha256(ArchiveBytes).hexdigest()
        if Digest != Package['sha256']:
            raise RuntimeError('SHA-256 mismatch for {0}: expected {1}, got {2}'.format(PackageName, Package['sha256'], Digest))

        TemporaryDirectory = RepositoryRoot / '_ClaudeScratch/tmp' / (PackageName + '-extract')
        if TemporaryDirectory.exists():
            shutil.rmtree(TemporaryDirectory)
        TemporaryDirectory.mkdir(parents=True, exist_ok=True)

        try:
            with tarfile.open(fileobj=io.BytesIO(ArchiveBytes), mode='r:gz') as Archive:
                for ArchiveEntry in Archive.getmembers():
                    RelativeParts = Path(ArchiveEntry.name).parts[1:]
                    if not RelativeParts:
                        continue
                    RelativePath = Path(*RelativeParts)
                    OutputPath = TemporaryDirectory / RelativePath
                    if not OutputPath.resolve().is_relative_to(TemporaryDirectory.resolve()):
                        raise RuntimeError('Unsafe archive path in ' + PackageName + ': ' + ArchiveEntry.name)
                    if not (ArchiveEntry.isfile() or ArchiveEntry.isdir()):
                        continue
                    if ArchiveEntry.isdir():
                        OutputPath.mkdir(parents=True, exist_ok=True)
                    else:
                        OutputPath.parent.mkdir(parents=True, exist_ok=True)
                        with Archive.extractfile(ArchiveEntry) as Reader, OutputPath.open('wb') as Writer:
                            shutil.copyfileobj(Reader, Writer)

            (TemporaryDirectory / '.frontier-revision').write_text(Package['revision'] + '\n', encoding='utf-8')
            Destination.parent.mkdir(parents=True, exist_ok=True)
            TemporaryDirectory.rename(Destination)
            print('[Verified] {0} -> {1}'.format(PackageName, Destination.relative_to(RepositoryRoot)), flush=True)
        finally:
            if TemporaryDirectory.exists():
                shutil.rmtree(TemporaryDirectory)

    PatchCommand = [sys.executable, str(RepositoryRoot / 'Automation/ApplyImGuiPatches.py')]
    if VerifyOnly:
        PatchCommand.append('--verify')
    subprocess.run(PatchCommand, cwd=RepositoryRoot, check=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(Main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as Error:
        print('Dependency preparation failed:', Error, file=sys.stderr)
        sys.exit(1)
