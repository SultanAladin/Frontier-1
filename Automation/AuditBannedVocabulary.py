#============================================================================================================================================
#                                                    AUDITBANNEDVOCABULARY.PY
#============================================================================================================================================
# 🧩 Scan first-party identifiers, comments, file names, and folder names against Documentation/BannedWordsVocabulary.md.
#
# Usage:
#   python3 Automation/AuditBannedVocabulary.py                    (UV workspace scope, exit 1 on any finding)
#   python3 Automation/AuditBannedVocabulary.py Engine/Codecs      (explicit files or folders)
#   python3 Automation/AuditBannedVocabulary.py --summary          (word tallies only)

from pathlib import Path
import collections
import re
import sys

RepositoryRoot = Path(__file__).resolve().parents[1]
VocabularyPath = RepositoryRoot / 'Documentation/BannedWordsVocabulary.md'

#------------------------------------------------------------------------------------------------------------------------
#                                                      AUDIT SCOPE
#------------------------------------------------------------------------------------------------------------------------

UVWorkspaceScope = [
    'Engine/Workspaces/UV',
    'Engine/GeometricRaster/UV',
    'Engine/Codecs/UVCodec.h',
    'Engine/Codecs/UVCodec.cpp',
    'Engine/Lifecycle/FrontierUVLaunch.cpp',
    'Engine/GeometricRaster/Shaders/UVTileRasterization.slang',
    'Engine/GeometricRaster/Shaders/UVDistortionInspection.slang',
    'Engine/GeometricRaster/Shaders/StretchRelaxCompute.slang',
    'Engine/GeometricRaster/Shaders/ConcaveSkylineCompute.slang',
    'Exhibits/UVBoundaryValidation.cpp',
    'Exhibits/UVInteractiveAuthoringValidation.cpp',
    'Exhibits/UVParameterizationValidation.cpp',
    'Exhibits/UVRasterPickingGizmoValidation.cpp',
    'Exhibits/UVWorkspaceUIValidation.cpp',
]

AuditedSuffixes = {'.h', '.cpp', '.slang'}

#------------------------------------------------------------------------------------------------------------------------
#                                                   VOCABULARY RULES
#------------------------------------------------------------------------------------------------------------------------

# Words barred by §1.2, §2, and SKILL-Naming.md §1 that the §5 A–Z index does not repeat.
SupplementalBannedWords = {'accept', 'cache', 'cached', 'composite', 'emit', 'make', 'widget', 'widgets'}

# §1.2 boolean and bitmask prefixes (PascalCase identifiers only).
BarredPrefixExpression = re.compile(r'^(Is|Has|Was|Can|Should|On)[A-Z]')

# §3.1 narrow exemptions plus the EngineContent/FontsArchives directory exemption from §1.1.
ExemptIdentifiers = {
    'RecordEntries', 'RecordEntry', 'RecordToken', 'SelectionTraversal', 'FontsArchives',
    'RenderExtension', 'PhysicsExtension', 'InputExtension', 'AnnotationExtension',
    'WorkspaceExtension', 'RevisionExtension', 'NetworkingExtension', 'AudioExtension',
}

# Identifiers declared by a different Frontier module and only referenced inside the audited scope.
# They are reported as advisories so the owning module's audit carries the rename, never silently waived.
ForeignOwnedIdentifiers = {
    'SpaceExtensionCategory':    'Engine/Codecs/SpaceContainerCodec.h',
    'ExtensionCategory':         'Engine/Codecs/SpaceContainerCodec.h',
    'RecordParametricOperation': 'Engine/Workspaces/Revision/RevisionTracker.h',
}

# Third-party namespaces and identifier prefixes (Vulkan, volk, Dear ImGui, GLFW, stb, toml++, ISO C++ library).
ThirdPartyQualifierExpression = re.compile(r'^(std|ImGui|toml|vk|stbi|stbtt)::')
ThirdPartyPrefixExpression    = re.compile(r'^(Vk|VK_|vk|Im[A-Z]|ImGui|IM_|GLFW|glfw|stb|STB|volk|PFN_)')

# PascalCase member functions of third-party types reached through an instance, so no namespace qualifier is visible.
ThirdPartyMembers = {'GetTexDataAsRGBA32'}

IdentifierExpression = re.compile(r'(?:[A-Za-z_][A-Za-z0-9_]*::)?[A-Za-z_][A-Za-z0-9_]*')
WordSplitExpression  = re.compile(r'[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+')
StringExpression     = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def LoadBannedWords():
    BannedWords = set(SupplementalBannedWords)
    IndexActive = False
    for Line in VocabularyPath.read_text(encoding='utf-8').splitlines():
        if Line.startswith('## '):
            IndexActive = Line.startswith('## 5.')
            continue
        if not IndexActive or not Line.startswith('- **'):
            continue
        # Parenthetical notes carry approved replacements and merely-discouraged words, never barred ones.
        Stripped = re.sub(r'\*\([^)]*\)\*', '', Line)
        for Term in re.findall(r'`([A-Za-z]+)`', Stripped):
            BannedWords.add(Term.lower())
    return BannedWords


def SplitWords(Identifier):
    return [Word.lower() for Word in WordSplitExpression.findall(Identifier)]


def RemoveExemptSpans(Identifier):
    for Exempt in sorted(ExemptIdentifiers, key=len, reverse=True):
        Identifier = Identifier.replace(Exempt, ' ')
    return Identifier


#------------------------------------------------------------------------------------------------------------------------
#                                                     LINE SCANNING
#------------------------------------------------------------------------------------------------------------------------

def PartitionCodeAndComment(Line, BlockCommentActive):
    Code = ''
    Comment = ''
    Cursor = 0
    Masked = StringExpression.sub(lambda Match: '"' + ' ' * (len(Match.group(0)) - 2) + '"', Line)
    while Cursor < len(Masked):
        if BlockCommentActive:
            Close = Masked.find('*/', Cursor)
            if Close < 0:
                Comment += Masked[Cursor:]
                break
            Comment += Masked[Cursor:Close]
            Cursor = Close + 2
            BlockCommentActive = False
            continue
        LineOpen  = Masked.find('//', Cursor)
        BlockOpen = Masked.find('/*', Cursor)
        if LineOpen >= 0 and (BlockOpen < 0 or LineOpen < BlockOpen):
            Code += Masked[Cursor:LineOpen]
            Comment += Masked[LineOpen + 2:]
            break
        if BlockOpen >= 0:
            Code += Masked[Cursor:BlockOpen]
            Cursor = BlockOpen + 2
            BlockCommentActive = True
            continue
        Code += Masked[Cursor:]
        break
    return Code, Comment, BlockCommentActive


def ScanSourceFile(SourcePath, BannedWords, Findings, Advisories):
    RelativePath = SourcePath.relative_to(RepositoryRoot).as_posix()
    BlockCommentActive = False
    for LineNumber, Line in enumerate(SourcePath.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        Code, Comment, BlockCommentActive = PartitionCodeAndComment(Line, BlockCommentActive)

        for Match in IdentifierExpression.finditer(Code):
            Identifier = Match.group(0)
            if ThirdPartyQualifierExpression.match(Identifier):
                continue
            Identifier = Identifier.split('::')[-1]
            # Frontier identifiers are PascalCase; lowercase-initial tokens are keywords or third-party API fields.
            if not Identifier[0].isupper() or ThirdPartyPrefixExpression.match(Identifier) or Identifier in ThirdPartyMembers:
                continue
            if Identifier.isupper():
                continue  # preprocessor macros and file-banner captions
            Hits = [Word for Word in SplitWords(RemoveExemptSpans(Identifier)) if Word in BannedWords]
            if BarredPrefixExpression.match(Identifier):
                Hits.append(BarredPrefixExpression.match(Identifier).group(1) + '* prefix')
            if not Hits:
                continue
            Entry = (RelativePath, LineNumber, 'identifier', Identifier, ', '.join(Hits))
            if Identifier in ForeignOwnedIdentifiers:
                Advisories.append(Entry + (ForeignOwnedIdentifiers[Identifier],))
            else:
                Findings.append(Entry)

        for Match in IdentifierExpression.finditer(Comment):
            Token = Match.group(0).split('::')[-1]
            if ThirdPartyPrefixExpression.match(Token) or Token in ForeignOwnedIdentifiers:
                continue
            Hits = [Word for Word in SplitWords(RemoveExemptSpans(Token)) if Word in BannedWords]
            if Hits:
                Findings.append((RelativePath, LineNumber, 'comment', Token, ', '.join(Hits)))


def ScanPathName(SourcePath, BannedWords, Findings):
    RelativePath = SourcePath.relative_to(RepositoryRoot).as_posix()
    for Part in SourcePath.relative_to(RepositoryRoot).parts:
        Stem = RemoveExemptSpans(Part.split('.')[0])
        Hits = [Word for Word in SplitWords(Stem) if Word in BannedWords]
        if Hits:
            Findings.append((RelativePath, 0, 'path', Part, ', '.join(Hits)))


def EnumerateSources(ScopeEntries):
    Sources = []
    for ScopeEntry in ScopeEntries:
        ScopePath = (RepositoryRoot / ScopeEntry).resolve()
        if ScopePath.is_dir():
            Sources += [Path_ for Path_ in sorted(ScopePath.rglob('*')) if Path_.is_file() and Path_.suffix in AuditedSuffixes]
        elif ScopePath.is_file():
            Sources.append(ScopePath)
        else:
            raise RuntimeError('Audit scope entry does not exist: ' + ScopeEntry)
    return Sources


#------------------------------------------------------------------------------------------------------------------------
#                                                        ENTRY
#------------------------------------------------------------------------------------------------------------------------

def Main():
    Arguments   = sys.argv[1:]
    SummaryOnly = '--summary' in Arguments
    ScopeEntries = [Argument for Argument in Arguments if not Argument.startswith('--')] or UVWorkspaceScope

    BannedWords = LoadBannedWords()
    Sources     = EnumerateSources(ScopeEntries)
    Findings    = []
    Advisories  = []
    for SourcePath in Sources:
        ScanPathName(SourcePath, BannedWords, Findings)
        ScanSourceFile(SourcePath, BannedWords, Findings, Advisories)

    if SummaryOnly:
        Tally = collections.Counter(Entry[4] for Entry in Findings)
        for Words, Count in Tally.most_common():
            print('{0:5d}  {1}'.format(Count, Words))
    else:
        for RelativePath, LineNumber, Category, Token, Words in Findings:
            print('{0}:{1}: [{2}] {3} -> {4}'.format(RelativePath, LineNumber, Category, Token, Words))

    for RelativePath, LineNumber, Category, Token, Words, Owner in Advisories:
        print('[Advisory] {0}:{1}: {2} -> {3} (declared in {4})'.format(RelativePath, LineNumber, Token, Words, Owner))

    print('[Audit]    {0} files, {1} barred words, {2} findings, {3} foreign-owned advisories'.format(
        len(Sources), len(BannedWords), len(Findings), len(Advisories)))
    return 1 if Findings else 0


if __name__ == '__main__':
    try:
        sys.exit(Main())
    except Exception as Error:
        print('Banned vocabulary audit failed:', Error, file=sys.stderr)
        sys.exit(2)
