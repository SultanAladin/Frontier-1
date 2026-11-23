#============================================================================================================================================
#                                                             RUNSYMBOLINDEX.PY
#============================================================================================================================================
# 🧩 Provides python function to execute the Slate symbol indexer tool.

import os
import sys

# 🔴 The repository root is derived from this file's own location, never from a machine-specific absolute
#    path. The earlier `C:\Users\OS\Documents\Slate\Tools\SymbolIndex.py` resolved on exactly one machine:
#    every other checkout — a second clone, a CI runner, an agent sandbox — reported "SymbolIndex.py not
#    found" and generated nothing, which reads as a broken indexer rather than as a wrong path.
RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ToolsRoot      = os.path.join(RepositoryRoot, "Tools")

#------------------------------------------------------------------------------------------------------------------------
#                                                        SYMBOL INDEX EXECUTION
#------------------------------------------------------------------------------------------------------------------------

def RunSymbolIndex(Command="build", Target=None, Root="Engine", NoCallSites=False, All=False):
    """Executes Tools/SymbolIndex.py with specified options, from any working folder."""
    ToolsPath = os.path.join(ToolsRoot, "SymbolIndex.py")
    if not os.path.isfile(ToolsPath):
        print("🔴 SymbolIndex.py not found at {0}".format(ToolsPath))
        return 2

    sys.path.insert(0, ToolsRoot)
    import SymbolIndex

    # 📝 `--root Engine` is resolved by the tool against the working folder, so a run from anywhere but the
    #    repository root indexed nothing and still exited 0. Made absolute here instead.
    if not os.path.isabs(Root):
        Root = os.path.join(RepositoryRoot, Root)

    Arguments = ["--root", Root]
    if NoCallSites:
        Arguments.append("--no-call-sites")
    Arguments.append(Command)

    if Target:
        Arguments.append(Target)

    if All and Command == "find":
        Arguments.append("--all")

    return SymbolIndex.Main(Arguments)


if __name__ == "__main__":
    sys.exit(RunSymbolIndex(*sys.argv[1:]))
