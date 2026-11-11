#==========================================================================================================================================
#                                                          PREPAREFONTS.PY
#==========================================================================================================================================
# 🧩 Download pinned OFL typefaces into EngineContent/FontsArchives/ and generate FontCatalogue.FontSpace.

from pathlib import Path
import argparse
import base64
import hashlib
import io
import json
import os
import tarfile
import tempfile
import urllib.request

RepositoryRoot = Path(__file__).resolve().parents[1]


def WriteAtomic(Destination, Payload):
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Descriptor, TemporaryPath = tempfile.mkstemp(dir=Destination.parent, prefix='.font-')
    try:
        with os.fdopen(Descriptor, 'wb') as Output:
            Output.write(Payload)
        os.replace(TemporaryPath, Destination)
    finally:
        Path(TemporaryPath).unlink(missing_ok=True)


def GenerateCatalogue(FontRoot):
    from fontTools.ttLib import TTFont
    TypefaceEntries = []
    RejectedFiles = []
    Manifest = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text(encoding='utf-8'))
    GroupLabelLookup = {
        Entry['TypefaceGroup'] + '/' + Entry['FileName']: Entry['GroupDisplayName']
        for Entry in Manifest['FontFiles'] if 'GroupDisplayName' in Entry
    }

    for FontPath in sorted(FontRoot.rglob('*')):
        if not FontPath.is_file() or FontPath.suffix.lower() not in ('.ttf', '.otf'):
            continue
        try:
            with TTFont(FontPath) as Typeface:
                NamingDirectory = Typeface['name']
                RelativeKey = FontPath.relative_to(FontRoot).as_posix()
                GroupLabel = (
                    GroupLabelLookup.get(RelativeKey)
                    or NamingDirectory.getDebugName(16)
                    or NamingDirectory.getDebugName(1)
                    or FontPath.parts[-2]
                )
                TypefaceEntries.append({
                    'TypefaceGroup': GroupLabel,
                    'RelativePath': RelativeKey,
                    'WeightClass': Typeface['OS/2'].usWeightClass,
                    'ItalicStyle': bool(Typeface['OS/2'].fsSelection & 1),
                    'Sha256Digest': hashlib.sha256(FontPath.read_bytes()).hexdigest(),
                })
        except Exception as Error:
            RejectedFiles.append({'RelativePath': FontPath.relative_to(FontRoot).as_posix(), 'Reason': str(Error)})

    Lines = ['FormatVersion = 1', '']
    for Entry in TypefaceEntries:
        Lines.append('[[TypefaceEntries]]')
        for Key, FieldContent in Entry.items():
            Lines.append(Key + ' = ' + json.dumps(FieldContent, ensure_ascii=True))
        Lines.append('')
    WriteAtomic(FontRoot / 'FontCatalogue.FontSpace', ('\n'.join(Lines) + '\n').encode('utf-8'))
    print('[Fonts]    {0} faces across {1} typeface groups ({2} rejected)'.format(
        len(TypefaceEntries), len({Entry['TypefaceGroup'] for Entry in TypefaceEntries}), len(RejectedFiles)))
    if RejectedFiles:
        raise RuntimeError('Font verification rejected one or more files.')


def Main():
    Parser = argparse.ArgumentParser(description='Prepare pinned Frontier font archives.')
    Parser.add_argument('--scan-only', dest='ScanOnly', action='store_true', help='Enumerate local fonts without network access')
    Parser.add_argument('--verify', dest='VerifyOnly', action='store_true', help='Verify pinned fonts exist without downloading')
    Parser.add_argument('--root', dest='RootDirectory', type=Path, default=RepositoryRoot / 'EngineContent/FontsArchives')
    Arguments = Parser.parse_args()

    Manifest = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text(encoding='utf-8'))
    DownloadedArchives = {}

    if not Arguments.ScanOnly:
        for Entry in Manifest['FontFiles']:
            Destination = Arguments.RootDirectory / Entry['TypefaceGroup'] / Entry['FileName']
            if Destination.exists():
                if hashlib.sha256(Destination.read_bytes()).hexdigest() == Entry['Sha256Digest']:
                    continue
                raise RuntimeError('Modified pinned font file: ' + str(Destination))
            if Arguments.VerifyOnly:
                raise RuntimeError('Missing pinned font file: ' + str(Destination))

            SourceUrl = Entry['SourceUrl']
            if SourceUrl in DownloadedArchives:
                RawBytes = DownloadedArchives[SourceUrl]
            else:
                Headers = {'Accept': 'application/vnd.github.raw+json', 'User-Agent': 'Frontier-font-setup'}
                if SourceUrl.startswith('https://api.github.com/') and os.environ.get('GH_TOKEN'):
                    Headers['Authorization'] = 'Bearer ' + os.environ['GH_TOKEN']
                Request = urllib.request.Request(SourceUrl, headers=Headers)
                with urllib.request.urlopen(Request, timeout=60) as Response:
                    RawBytes = Response.read()
                    if 'json' in Response.headers.get('Content-Type', ''):
                        RawBytes = base64.b64decode(json.loads(RawBytes)['content'])
                DownloadedArchives[SourceUrl] = RawBytes

            if 'ArchiveEntryPath' in Entry:
                if hashlib.sha256(RawBytes).hexdigest() != Entry['ArchiveSha256Digest']:
                    raise RuntimeError('Font archive SHA-256 mismatch for ' + Entry['FileName'])
                with tarfile.open(fileobj=io.BytesIO(RawBytes), mode='r:gz') as Archive:
                    ArchiveEntry = Archive.getmember(Entry['ArchiveEntryPath'])
                    if not ArchiveEntry.isfile() or ArchiveEntry.size > 4194304:
                        raise RuntimeError('Invalid font archive entry: ' + Entry['ArchiveEntryPath'])
                    RawBytes = Archive.extractfile(ArchiveEntry).read()
                if Entry.get('DecodeFormat') == 'woff':
                    from fontTools.ttLib import TTFont
                    with TTFont(io.BytesIO(RawBytes), recalcTimestamp=False) as Typeface:
                        Typeface.flavor = None
                        Converted = io.BytesIO()
                        Typeface.save(Converted)
                        RawBytes = Converted.getvalue()

            if hashlib.sha256(RawBytes).hexdigest() != Entry['Sha256Digest']:
                raise RuntimeError('Font SHA-256 mismatch: ' + Entry['FileName'])
            WriteAtomic(Destination, RawBytes)
            print('[Font]     {0}/{1}'.format(Entry['TypefaceGroup'], Entry['FileName']), flush=True)

    GenerateCatalogue(Arguments.RootDirectory)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(Main())
    except Exception as Error:
        print('Font preparation failed:', Error)
        raise SystemExit(1)
