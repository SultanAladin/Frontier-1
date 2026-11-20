#============================================================================================================================================
#                                                       APPLYIMGUIPATCHES.PY
#============================================================================================================================================
# 🧩 Apply, verify, or revert the 8-patch ImGui stack (PatchA through PatchH) against ExternalPackages/imgui in strict dependency order.

import os
import subprocess
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ImGuiRoot      = os.path.join(RepositoryRoot, 'ExternalPackages', 'imgui')
PatchRoot      = os.path.join(RepositoryRoot, 'Automation', 'Patches')

# 📝 Stacking order is mandatory: PatchB context depends on PatchA lines, so patches apply A->H and revert H->A.
# 🔴 Presence is detected via the literal comment tag each patch introduces rather than `git apply --reverse --check`,
#    because later patches modify lines inside earlier hunks' context windows.
DeclaredPatches = [
    {'Name': 'PatchA-TrapezoidalTabs.patch',      'Tag': 'FRONTIER PATCH A', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchB-TabOverlapZOrder.patch',     'Tag': 'FRONTIER PATCH B', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchC-RoundTabButtons.patch',      'Tag': 'FRONTIER PATCH C', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchD-TabAddButton.patch',         'Tag': 'FRONTIER PATCH D', 'Witness': 'imgui.cpp'},
    {'Name': 'PatchE-NoTabScrollButtons.patch',   'Tag': 'FRONTIER PATCH E', 'Witness': 'imgui.cpp'},
    {'Name': 'PatchF-TabContentInsets.patch',     'Tag': 'FRONTIER PATCH F', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchG-AdjacentTabAddButton.patch', 'Tag': 'FRONTIER PATCH G', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchH-SmoothOverscroll.patch',     'Tag': 'FRONTIER PATCH H', 'Witness': 'imgui.cpp'},
]

ExpectedRevision = '3bae66c735670619baf51391eba7f3d90a25d125'


def WriteReport(Category, Message):
    print('{0} {1}'.format('[{0}]'.format(Category).ljust(10), Message))


def RunGit(Arguments, PatchBytes=None):
    Environment = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(ImGuiRoot))
    Completed = subprocess.run(
        ['git', '-c', 'core.autocrlf=false'] + Arguments,
        cwd=ImGuiRoot,
        input=PatchBytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=PatchBytes is None,
        env=Environment,
    )
    Output = Completed.stdout.decode(errors='replace') if isinstance(Completed.stdout, bytes) else Completed.stdout
    return Completed.returncode, Output.strip()


def Main(Arguments):
    RevertRequested = '--revert' in Arguments
    VerifyRequested = '--verify' in Arguments

    if not os.path.isfile(os.path.join(ImGuiRoot, 'imgui.cpp')):
        WriteReport('FAILED', 'ExternalPackages/imgui is missing at {0}'.format(ImGuiRoot))
        WriteReport('Patch', 'Run: python3 Automation/PrepareDependencies.py')
        return 1

    RevisionFile = os.path.join(ImGuiRoot, '.frontier-revision')
    if os.path.isfile(RevisionFile):
        with open(RevisionFile, 'r', encoding='utf-8') as Reader:
            StandingRevision = Reader.read().strip()
        if StandingRevision != ExpectedRevision:
            WriteReport('Patch', 'ImGui stands at {0}; patches target {1}'.format(
                StandingRevision[:7], ExpectedRevision[:7]))

    OrderedPatches = list(reversed(DeclaredPatches)) if RevertRequested else DeclaredPatches

    for Entry in OrderedPatches:
        PatchName = Entry['Name']
        PatchPath = os.path.join(PatchRoot, PatchName)

        if not os.path.isfile(PatchPath):
            WriteReport('FAILED', 'Declared patch {0} is missing from {1}'.format(PatchName, PatchRoot))
            return 1

        WitnessPath = os.path.join(ImGuiRoot, Entry['Witness'])
        with open(WitnessPath, 'r', encoding='utf-8', errors='replace') as Reader:
            PatchPresent = Entry['Tag'] in Reader.read()

        if VerifyRequested:
            if PatchPresent:
                WriteReport('ImGui', '{0} verified'.format(PatchName))
            else:
                WriteReport('FAILED', '{0} is NOT applied'.format(PatchName))
                return 1
            continue

        if RevertRequested:
            if not PatchPresent:
                WriteReport('SKIP', '{0} was not applied'.format(PatchName))
                continue
            PatchBytes = open(PatchPath, 'rb').read().replace(b'\r\n', b'\n')
            ExitCode, Output = RunGit(['apply', '--reverse'], PatchBytes)
            if ExitCode != 0:
                WriteReport('FAILED', 'Could not revert {0}'.format(PatchName))
                if Output:
                    print(Output)
                return 1
            WriteReport('ImGui', 'Reverted {0}'.format(PatchName))
            continue

        if PatchPresent:
            WriteReport('SKIP', '{0} already applied'.format(PatchName))
            continue

        PatchBytes = open(PatchPath, 'rb').read().replace(b'\r\n', b'\n')
        ExitCode, Output = RunGit(['apply', '--check'], PatchBytes)
        if ExitCode != 0:
            WriteReport('FAILED', '{0} rejected by standing ImGui source'.format(PatchName))
            if Output:
                print(Output)
            return 1

        ExitCode, Output = RunGit(['apply'], PatchBytes)
        if ExitCode != 0:
            WriteReport('FAILED', 'Could not apply {0}'.format(PatchName))
            if Output:
                print(Output)
            return 1

        WriteReport('ImGui', 'Applied {0}'.format(PatchName))

    return 0


if __name__ == '__main__':
    sys.exit(Main(sys.argv[1:]))
