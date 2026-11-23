#============================================================================================================================================
#                                                           PREPAREUI.PY
#============================================================================================================================================
# 🧩 Retrieve checksummed UI dependencies without importing the scene renderer.

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
    Manifest = json.loads((RepositoryRoot / 'Automation/UIDependencies.json').read_text())
    for Package in Manifest['packages']:
        Destination = RepositoryRoot / 'ExternalPackages' / Package['name']
        Stamp = Destination / '.frontier-revision'
        if Stamp.exists() and Stamp.read_text().strip() == Package['revision']:
            print('Pinned dependency present:', Package['name'])
            continue
        if VerifyOnly:
            raise RuntimeError('Dependency not prepared: ' + Package['name'])
        if Destination.exists():
            raise RuntimeError('Unmanaged dependency directory; preserve or move it first: ' + str(Destination))
        print('Downloading pinned dependency:', Package['name'])
        with urllib.request.urlopen(Package['url'], timeout=120) as Response:
            ArchiveBytes = Response.read()
        if hashlib.sha256(ArchiveBytes).hexdigest() != Package['sha256']:
            raise RuntimeError('Archive checksum mismatch: ' + Package['name'])
        Staging = RepositoryRoot / '_ClaudeScratch/tmp' / (Package['name'] + '-download')
        if Staging.exists():
            shutil.rmtree(Staging)
        Staging.mkdir(parents=True)
        try:
            with tarfile.open(fileobj=io.BytesIO(ArchiveBytes), mode='r:gz') as Archive:
                for Member in Archive.getmembers():
                    Relative = Path(*Path(Member.name).parts[1:])
                    if not Relative.parts:
                        continue
                    Output = Staging / Relative
                    if not Output.resolve().is_relative_to(Staging.resolve()) or not (Member.isfile() or Member.isdir()):
                        raise RuntimeError('Unsafe archive member: ' + Member.name)
                    if Member.isdir():
                        Output.mkdir(parents=True, exist_ok=True)
                    else:
                        Output.parent.mkdir(parents=True, exist_ok=True)
                        with Archive.extractfile(Member) as Source, Output.open('wb') as Target:
                            shutil.copyfileobj(Source, Target)
            (Staging / '.frontier-revision').write_text(Package['revision'] + '\n')
            Destination.parent.mkdir(parents=True, exist_ok=True)
            Staging.rename(Destination)
        finally:
            if Staging.exists():
                shutil.rmtree(Staging)
    Arguments = [sys.executable, str(RepositoryRoot / 'Automation/ApplyImGuiPatches.py')]
    if VerifyOnly:
        Arguments.append('--verify')
    subprocess.run(Arguments, cwd=RepositoryRoot, check=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(Main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as Error:
        print('UI setup failed:', Error, file=sys.stderr)
        sys.exit(1)
