#============================================================================================================================================
#                                                            VERIFYPARTITION.PY
#============================================================================================================================================
# 🧩 The POSIX twin of VerifyPartition.ps1 — same declarations, same refusals, same exit codes, no PowerShell.

# 🔴 This is a PORT, not a reimplementation. Every refusal VerifyPartition.ps1 raises is raised here with the
#    same wording, so a refusal read on Linux names the same defect a reader would meet on Windows. When the
#    .ps1 changes, this changes with it; two checkers that disagree are worse than one checker that is absent,
#    because the disagreement is discovered by a build that already passed.
#
# 📝 Why it exists: the .ps1 is the authority on Windows and stays so. An agent or a CI runner without
#    PowerShell could not run the partition check at all, so it was skipped — which is how a forbidden include
#    reaches a pull request. Parity is asserted by Scripts/VerifyToolchainParity.py.
#
#     python3 Scripts/VerifyPartition.py
#     python3 Scripts/VerifyPartition.py --detail

import os
import re
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EngineRoot     = os.path.join(RepositoryRoot, 'Engine')

#------------------------------------------------------------------------------------------------------------------------
#                                                         THE REPORT
#------------------------------------------------------------------------------------------------------------------------

# 📝 The .ps1 colours these; a pipe-friendly runner does not, so the tag alone carries the verdict.
def WriteReport(Tag, Message):
    print("{0} {1}".format("[{0}]".format(Tag).ljust(10), Message))


def WriteHeld(Message):   WriteReport('Partition', Message)
def WriteBroken(Message): WriteReport('REFUSED',   Message)
def WriteNoted(Message):  WriteReport('Partition', Message)


#------------------------------------------------------------------------------------------------------------------------
#                                                   THE DECLARED PARTITION
#------------------------------------------------------------------------------------------------------------------------

# 📝 Read from Module.toml and from nowhere else, exactly as the .ps1 does. A second copy of the graph is a
#    second thing to keep agreeing.
def ReadDeclaredUnits():
    Declared = {}

    for Walked, Folders, Files in os.walk(EngineRoot):
        if 'Module.toml' not in Files:
            continue

        Manifest = os.path.join(Walked, 'Module.toml')
        UnitName = os.path.basename(Walked)

        with open(Manifest, 'r', encoding='utf-8-sig') as Reader:
            Content = Reader.read()

        Requires = []
        Section  = re.search(r'(?ms)^\[requires\].*?^unit\s*=\s*\[(.*?)\]', Content)

        if Section:
            Requires = re.findall(r'"([^"]+)"', Section.group(1))

        Declared[UnitName] = {
            'Name':     UnitName,
            'Requires': Requires,
            'Root':     Walked,
        }

    return Declared


# 🔴 A cycle is rejected before anything else is checked. Every later question assumes the graph is acyclic.
def TestAcyclic(Declared):
    Broken = []

    for UnitName in Declared:
        Seen  = {}
        Stack = [UnitName]

        while Stack:
            Current = Stack.pop()

            for Required in Declared[Current]['Requires']:
                if Required not in Declared:
                    Broken.append("{0} requires {1}, which declares no Module.toml".format(Current, Required))
                    continue

                if Required == UnitName:
                    Broken.append("{0} participates in a dependency cycle through {1}".format(UnitName, Current))
                    continue

                if Required not in Seen:
                    Seen[Required] = True
                    Stack.append(Required)

    return Broken


# 📝 The transitive closure, because a unit may name a type it reaches through one of its own requirements.
def ResolveReachable(Declared, UnitName):
    Reached = {}
    Stack   = [UnitName]

    while Stack:
        for Required in Declared[Stack.pop()]['Requires']:
            if Required in Declared and Required not in Reached:
                Reached[Required] = True
                Stack.append(Required)

    return Reached


#------------------------------------------------------------------------------------------------------------------------
#                                                    SHELL FILE ENCODING
#------------------------------------------------------------------------------------------------------------------------

# 🔴 A PowerShell file containing any non-ASCII byte MUST carry a UTF-8 BOM, and a batch file must carry none
#    at all. Windows PowerShell 5.1 reads a BOM-less file in the system code page, so every multi-byte sequence
#    arrives as mojibake and the parse fails at a line that looks unrelated to the character. A batch file has
#    the opposite constraint: cmd.exe reads it in the OEM code page and executes the stray bytes.
#
# ⚠️ This check is the reason the port matters most. A Linux editor writes UTF-8 without a BOM by default, so
#    an agent editing a .ps1 here breaks the Windows build in a way nothing on Linux would otherwise notice.
def TestShellEncoding():
    Broken = []
    Mark   = b'\xef\xbb\xbf'

    # 🔴 Only the shell the build actually runs. `_AgentScratch/` is git-ignored disposable output.
    Governed = ['Build', 'Scripts']

    for Folder in Governed:
        FolderRoot = os.path.join(RepositoryRoot, Folder)

        if not os.path.isdir(FolderRoot):
            continue

        for Walked, Folders, Files in os.walk(FolderRoot):
            for Leaf in sorted(Files):
                Extension = os.path.splitext(Leaf)[1].lower()

                if Extension not in ('.ps1', '.bat'):
                    continue

                Full = os.path.join(Walked, Leaf)

                with open(Full, 'rb') as Reader:
                    Bytes = Reader.read()

                Named  = os.path.relpath(Full, RepositoryRoot).replace(os.sep, '/')
                Marked = Bytes.startswith(Mark)
                Wide   = sum(1 for Byte in Bytes[3 if Marked else 0:] if Byte > 127)

                if Extension == '.bat':
                    if Wide > 0 or Marked:
                        Carried = "{0} non-ASCII byte(s){1}".format(Wide, ' and a BOM' if Marked else '')
                        Broken.append("{0} carries {1}; cmd.exe requires plain ASCII".format(Named, Carried))
                elif Wide > 0 and not Marked:
                    Broken.append(
                        "{0} carries {1} non-ASCII byte(s) and no UTF-8 BOM; PowerShell misreads them".format(
                            Named, Wide))

    return Broken


def TestIncludeRootsReachable():
    """🔴 Every `#include "Foundation/..."` and `#include "Shared/..."` must name a file that exists
    UNDER `Engine/`, because `Engine/` is the only first-party include root the Windows build opens.

    A header placed at the repository root instead resolves in the sandbox — which used to add the
    repository root as well — and is rejected by `cl.exe` with C1083 on the first unit that includes
    it. That divergence cost a whole build: 45 green gates here, a stopped build there. The sandbox
    no longer opens the root, and this states the rule so the answer does not depend on remembering
    which mirror is the strict one."""

    Broken  = []
    Wanted  = re.compile(r'^\s*#\s*include\s+"((?:Foundation|Shared)/[^"]+)"')
    Skipped = ('ExternalPackages', '_AgentScratch', '.git')

    for Walked, Folders, Files in os.walk(RepositoryRoot):
        Folders[:] = [Folder for Folder in Folders if Folder not in Skipped]

        for Leaf in sorted(Files):
            if os.path.splitext(Leaf)[1].lower() not in ('.h', '.cpp'):
                continue

            Full = os.path.join(Walked, Leaf)

            try:
                with open(Full, 'r', encoding='utf-8', errors='ignore') as Reader:
                    Lines = Reader.read().splitlines()
            except OSError:
                continue

            for Number, Line in enumerate(Lines, start=1):
                Found = Wanted.match(Line)

                if not Found:
                    continue

                Named = Found.group(1)

                if os.path.isfile(os.path.join(EngineRoot, Named.replace('/', os.sep))):
                    continue

                Where = os.path.relpath(Full, RepositoryRoot).replace(os.sep, '/')
                Elsewhere = os.path.isfile(os.path.join(RepositoryRoot, Named.replace('/', os.sep)))

                Broken.append(
                    "{0}:{1} includes \"{2}\", which is not under Engine/{3}".format(
                        Where, Number, Named,
                        " -- it sits at the repository root, which is not an include root"
                        if Elsewhere else " and was not found at all"))

    return Broken


#------------------------------------------------------------------------------------------------------------------------
#                                                        THE VERDICT
#------------------------------------------------------------------------------------------------------------------------

def Main(Arguments):
    Detail   = '--detail' in Arguments
    Declared = ReadDeclaredUnits()

    if len(Declared) == 0:
        WriteBroken("no Module.toml was found under {0}".format(EngineRoot))
        return 1

    # 🔴 Reported on its own. Folding a missing BOM into the partition's refusal list names the wrong subsystem.
    Mistyped = TestShellEncoding()

    if Mistyped:
        for Fault in Mistyped:
            WriteBroken(Fault)

        print('')
        WriteBroken("shell encoding is wrong in {0} file(s)".format(len(Mistyped)))
        return 1

    # 🔴 Reported on its own, before the partition: an unreachable include root is a build that never
    #    starts, and folding it into the unit-reference list names the wrong subsystem.
    Unreachable = TestIncludeRootsReachable()

    if Unreachable:
        for Fault in Unreachable:
            WriteBroken(Fault)

        print('')
        WriteBroken("{0} include(s) do not resolve under Engine/".format(len(Unreachable)))
        return 1

    Refusals = []
    Refusals += TestAcyclic(Declared)

    # 🔴 Every include of the form "SlateX/..." is a unit reference. Foundation/ and Shared/ are reachable from
    #    everywhere by declaration — they are the shared seam, not a unit — and a vendored header is neither.
    for UnitName in sorted(Declared):
        Entry     = Declared[UnitName]
        Reachable = ResolveReachable(Declared, UnitName)
        Sources   = []

        for Walked, Folders, Files in os.walk(Entry['Root']):
            for Leaf in sorted(Files):
                if os.path.splitext(Leaf)[1].lower() in ('.h', '.cpp'):
                    Sources.append(os.path.join(Walked, Leaf))

        for Source in sorted(Sources):
            Relative = os.path.relpath(Source, RepositoryRoot).replace(os.sep, '/')

            with open(Source, 'r', encoding='utf-8-sig', errors='replace') as Reader:
                Lines = Reader.read().split('\n')

            for Index, Line in enumerate(Lines, start=1):
                Named = re.match(r'^\s*#include\s+"([^"]+)"', Line)

                if not Named:
                    continue

                Included = Named.group(1)
                UnitEdge = re.match(r'^(Slate[A-Za-z]+)/', Included)

                if UnitEdge:
                    Reached = UnitEdge.group(1)

                    if Reached == UnitName:      continue
                    if Reached in Reachable:     continue

                    Refusals.append("{0}({1}): {2} includes {3}, which it does not require".format(
                        Relative, Index, UnitName, Reached))
                    continue

                # 🔴 `00` §2.2: exactly one copy of ImGui exists and SlateUI owns it.
                if re.match(r'^(imgui|backends/imgui)', Included) and UnitName != 'SlateUI':
                    Refusals.append("{0}({1}): {2} names an ImGui header; only SlateUI may".format(
                        Relative, Index, UnitName))
                    continue

                # 🔴 `10` §1: the vendored readers are compiled into SlateDocument's codecs and nowhere else.
                if re.match(r'^(stb|fast_obj|cgltf|ufbx)', Included) and UnitName != 'SlateDocument':
                    Refusals.append("{0}({1}): {2} names a vendored reader; only SlateDocument may".format(
                        Relative, Index, UnitName))

        if Detail:
            Reaches = ', '.join(sorted(Reachable)) if Reachable else 'nothing'
            WriteNoted("{0} reaches {1} across {2} files".format(UnitName, Reaches, len(Sources)))

    print('')

    if Refusals:
        for Refusal in Refusals:
            WriteBroken(Refusal)

        print('')
        WriteBroken("the dependency partition is broken in {0} place(s)".format(len(Refusals)))
        return 1

    WriteHeld("the dependency partition holds across {0} units".format(len(Declared)))
    return 0


if __name__ == '__main__':
    sys.exit(Main(sys.argv[1:]))
