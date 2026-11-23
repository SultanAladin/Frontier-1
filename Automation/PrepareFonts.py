#============================================================================================================================================
#                                                          PREPAREFONTS.PY
#============================================================================================================================================
# 🧩 Download pinned OFL fonts and enumerate all local TTF/OTF files into a reproducible catalogue.

from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import tempfile
import urllib.request
import io
import tarfile

RepositoryRoot = Path(__file__).resolve().parents[1]


def WriteAtomic(Destination, Payload):
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Descriptor, Temporary = tempfile.mkstemp(dir=Destination.parent, prefix='.font-')
    try:
        with os.fdopen(Descriptor, 'wb') as Output:
            Output.write(Payload)
        os.replace(Temporary, Destination)
    finally:
        Path(Temporary).unlink(missing_ok=True)


def GenerateCatalogue(FontRoot):
    from fontTools.ttLib import TTFont
    Entries = []
    Rejected = []
    Manifest = json.loads((RepositoryRoot / "Automation/FontSources.json").read_text())
    FamilyNames = {Entry["family"] + "/" + Entry["file"]: Entry["family_name"]
                   for Entry in Manifest["files"] if "family_name" in Entry}
    for FontPath in sorted(FontRoot.rglob('*')):
        if not FontPath.is_file() or FontPath.suffix.lower() not in ('.ttf', '.otf'):
            continue
        try:
            with TTFont(FontPath) as Typeface:
                Names = Typeface['name']
                Family = FamilyNames.get(FontPath.relative_to(FontRoot).as_posix()) or Names.getDebugName(16) or Names.getDebugName(1) or FontPath.parent.name
                Entries.append({
                    'family': Family,
                    'file': FontPath.relative_to(FontRoot).as_posix(),
                    'weight': Typeface['OS/2'].usWeightClass,
                    'italic': bool(Typeface['OS/2'].fsSelection & 1),
                    'sha256': hashlib.sha256(FontPath.read_bytes()).hexdigest(),
                })
        except Exception as Error:
            Rejected.append({'file': FontPath.relative_to(FontRoot).as_posix(), 'error': str(Error)})
    Report = {'format': 1, 'faces': Entries, 'rejected': Rejected}
    WriteAtomic(FontRoot / 'FontCatalogue.json', (json.dumps(Report, indent=2) + '\n').encode())
    # Runtime C++ descriptors use TOML; JSON is retained only as a tooling/report interchange.
    Lines = ['format = 1', '']
    for Entry in Entries:
        Lines.append('[[faces]]')
        for Name, Content in Entry.items():
            Lines.append(Name + ' = ' + json.dumps(Content, ensure_ascii=True))
        Lines.append('')
    WriteAtomic(FontRoot / 'FontCatalogue.toml', ('\n'.join(Lines) + '\n').encode())
    print(f'Catalogue: {len(Entries)} faces, {len(set(Entry["family"] for Entry in Entries))} families, '
          f'{len(Rejected)} rejected files')
    if Rejected:
        raise RuntimeError('Some font files are invalid; inspect FontCatalogue.json')


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--scan-only', action='store_true', help='No network; enumerate local fonts only')
    Parser.add_argument('--verify', action='store_true', help='Check pinned files without downloading')
    Parser.add_argument('--root', type=Path, default=RepositoryRoot / 'EngineContent/FontArchives')
    Arguments = Parser.parse_args()
    Manifest = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text())
    Archives = {}
    if not Arguments.scan_only:
        for Entry in Manifest['files']:
            Destination = Arguments.root / Entry['family'] / Entry['file']
            if Destination.exists():
                if hashlib.sha256(Destination.read_bytes()).hexdigest() == Entry['sha256']:
                    continue
                raise RuntimeError('Modified pinned font; preserve local edits before recovery: ' + str(Destination))
            if Arguments.verify:
                raise RuntimeError('Missing pinned file: ' + str(Destination))
            if Entry['url'] in Archives:
                Payload = Archives[Entry['url']]
            else:
                Headers = {'Accept': 'application/vnd.github.raw+json', 'User-Agent': 'Frontier-font-setup'}
                if Entry['url'].startswith('https://api.github.com/') and os.environ.get('GH_TOKEN'):
                    Headers['Authorization'] = 'Bearer ' + os.environ['GH_TOKEN']
                Request = urllib.request.Request(Entry['url'], headers=Headers)
                with urllib.request.urlopen(Request, timeout=60) as Response:
                    Payload = Response.read()
                    if 'json' in Response.headers.get('Content-Type', ''):
                        Payload = base64.b64decode(json.loads(Payload)['content'])
                Archives[Entry['url']] = Payload
            if 'archive_member' in Entry:
                if hashlib.sha256(Payload).hexdigest() != Entry['archive_sha256']:
                    raise RuntimeError('Font source archive checksum mismatch')
                with tarfile.open(fileobj=io.BytesIO(Payload), mode='r:gz') as Archive:
                    Member = Archive.getmember(Entry['archive_member'])
                    if not Member.isfile() or Member.size > 4194304:
                        raise RuntimeError('Invalid bounded font archive member')
                    Payload = Archive.extractfile(Member).read()
                if Entry.get('decode') == 'woff':
                    from fontTools.ttLib import TTFont
                    with TTFont(io.BytesIO(Payload), recalcTimestamp=False) as Typeface:
                        Typeface.flavor = None
                        Output = io.BytesIO()
                        Typeface.save(Output)
                        Payload = Output.getvalue()
            if hashlib.sha256(Payload).hexdigest() != Entry['sha256']:
                raise RuntimeError('Font checksum mismatch: ' + Entry['file'])
            WriteAtomic(Destination, Payload)
            print('Downloaded:', Entry['family'], Entry['file'], flush=True)
    GenerateCatalogue(Arguments.root)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(Main())
    except Exception as Error:
        print('Font preparation failed:', Error)
        raise SystemExit(1)
