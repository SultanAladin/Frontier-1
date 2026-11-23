#============================================================================================================================================
#                                                        APPLYIMGUIPATCHES.PY
#============================================================================================================================================
# 🧩 Apply, verify, or revert the inherited ImGui patches and the adjacent/centred add-button correction in dependency order.
# Ported from Frontier- commit 338293863d7df4e585dec0c4acae45fe9c524ed5.
# The patches originated against 83f6686; this port verifies them against lockfile pin 3bae66c.
# Default style additions remain zero. Sentinel checks avoid incorrect reverse-checks on stacked patches.
# Use --verify for no edits, or --revert to undo the stack in reverse order.

import os
import subprocess
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ImGuiRoot      = os.path.join(RepositoryRoot, 'ExternalPackages', 'imgui')
PatchRoot      = os.path.join(RepositoryRoot, 'Automation', 'Patches')

# 📝 The order is the stacking order. B's context includes A's lines, so B cannot apply first.
# 🔴 Each patch is detected by a sentinel it introduces, NOT by `git apply --reverse --check`. Because B edits
#    lines that sit inside A's own context, once B is applied A no longer reverse-checks — so a reverse-check
#    would report A as absent on a fully patched tree and the script would try to apply it again, aborting a
#    build whose tree was perfectly healthy. A sentinel is stable under stacking.
Declared = [
    {'Name': 'PatchA-TrapezoidalTabs.patch',  'Sentinel': 'FRONTIER PATCH A', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchB-TabOverlapZOrder.patch', 'Sentinel': 'FRONTIER PATCH B', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchC-RoundTabButtons.patch',  'Sentinel': 'FRONTIER PATCH C', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchD-TabAddButton.patch',     'Sentinel': 'FRONTIER PATCH D', 'Witness': 'imgui.cpp'},
    {'Name': 'PatchE-NoTabScrollButtons.patch', 'Sentinel': 'FRONTIER PATCH E', 'Witness': 'imgui.cpp'},
    {'Name': 'PatchF-TabContentInsets.patch', 'Sentinel': 'FRONTIER PATCH F', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchG-AdjacentTabAddButton.patch', 'Sentinel': 'FRONTIER PATCH G', 'Witness': 'imgui_widgets.cpp'},
]

# 🔴 The commit these patches were written against. `git apply` would fail loudly on a different tree, but it
#    fails with three rejected hunks rather than with the one sentence a reader can act on.
ExpectedCommit = '3bae66c735670619baf51391eba7f3d90a25d125'


def WriteReport(Tag, Message):
    print("{0} {1}".format("[{0}]".format(Tag).ljust(10), Message))


def WriteApplied(Message): WriteReport('ImGui',  Message)
def WriteSkipped(Message): WriteReport('SKIP',   Message)
def WriteRejected(Message): WriteReport('FAILED', Message)
def WriteNoted(Message):   WriteReport('Patch',  Message)


def Git(Arguments):
    """Runs one git command inside the vendored tree and returns (ExitCode, Output)."""
    Finished = subprocess.run(['git'] + Arguments, cwd=ImGuiRoot,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              env=dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(ImGuiRoot)))
    return Finished.returncode, Finished.stdout.strip()


def Main(Arguments):
    Revert = '--revert' in Arguments
    Verify = '--verify' in Arguments

    if not os.path.isfile(os.path.join(ImGuiRoot, 'imgui.cpp')):
        WriteRejected("the ImGui submodule is not checked out at {0}".format(ImGuiRoot))
        WriteNoted('run: python Automation/PrepareUI.py')
        return 1

    # 📝 The pin is reported rather than enforced. A deliberate ImGui upgrade should reach a message naming
    #    both commits, not an assertion that reads as a broken checkout.
    Code, Current = Git(['rev-parse', 'HEAD'])

    if Code == 0 and Current != ExpectedCommit:
        WriteNoted("submodule stands at {0}; patches were cut against {1}".format(
            Current[:7], ExpectedCommit[:7]))
        WriteNoted('if ImGui was upgraded deliberately, re-cut the patches against the new commit')

    # 🔴 Reverting runs the stack backwards. B sits on top of A, so reverting A first would leave B's hunks
    #    referring to context that no longer exists.
    Ordered = list(reversed(Declared)) if Revert else Declared

    for Entry in Ordered:
        PatchName = Entry['Name']
        PatchPath = os.path.join(PatchRoot, PatchName)

        if not os.path.isfile(PatchPath):
            WriteRejected("declared patch {0} is absent from {1}".format(PatchName, PatchRoot))
            return 1

        WitnessPath = os.path.join(ImGuiRoot, Entry['Witness'])

        with open(WitnessPath, 'r', encoding='utf-8', errors='replace') as Reader:
            AlreadyApplied = Entry['Sentinel'] in Reader.read()

        if Verify:
            if AlreadyApplied: WriteApplied("{0} is applied".format(PatchName))
            else:
                WriteRejected("{0} is NOT applied".format(PatchName))
                return 1
            continue

        if Revert:
            if not AlreadyApplied:
                WriteSkipped("{0} was not applied".format(PatchName))
                continue

            Code, Output = Git(['apply', '--reverse', PatchPath])

            if Code != 0:
                WriteRejected("could not revert {0}".format(PatchName))
                if Output: print(Output)
                return 1

            WriteApplied("reverted {0}".format(PatchName))
            continue

        if AlreadyApplied:
            WriteSkipped("{0} already applied".format(PatchName))
            continue

        Code, Output = Git(['apply', '--check', PatchPath])

        if Code != 0:
            WriteRejected("{0} does not apply to the standing ImGui tree".format(PatchName))
            if Output: print(Output)
            return 1

        Code, Output = Git(['apply', PatchPath])

        if Code != 0:
            WriteRejected("could not apply {0}".format(PatchName))
            if Output: print(Output)
            return 1

        WriteApplied("applied {0}".format(PatchName))

    return 0


if __name__ == '__main__':
    sys.exit(Main(sys.argv[1:]))
