#============================================================================================================================================
#                                                          RUNSYMBOLINDEXFLATTEN.PY
#============================================================================================================================================
# 🧩 Mirrors every Engine .symbolindex into the flat SymboLindex folder as one .md per file, named by its dotted Engine path.

import os
import shutil
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EngineRoot     = os.path.join(RepositoryRoot, "Engine")
FlatRoot       = os.path.join(RepositoryRoot, "SymboLindex")
IndexSuffix    = ".symbolindex"
MirrorSuffix   = ".symbolindex.md"

#------------------------------------------------------------------------------------------------------------------------
#                                                    CONSOLE REPORTING
#------------------------------------------------------------------------------------------------------------------------

ColourNeutral  = "\x1b[90m"
ColourSkipped  = "\x1b[36m"
ColourFailed   = "\x1b[31m"
ColourCompiled = "\x1b[32m"
ColourReset    = "\x1b[0m"

# 📝 A no-op shell call is what switches the Windows console into virtual-terminal mode; without it the
#    escape sequences below are printed literally rather than interpreted as colour.
if os.name == "nt":
    os.system("")


def Report(Tag, Colour, Message):
    """Prints one padded, colour-coded status line."""
    print("{0}{1}{2} {3}".format(Colour, ("[" + Tag + "]").ljust(10), ColourReset, Message))

#------------------------------------------------------------------------------------------------------------------------
#                                                  MIRROR NAME CONSTRUCTION
#------------------------------------------------------------------------------------------------------------------------

def ConstructMirrorName(Relative):
    """Builds the flat .md name for one Engine-relative .symbolindex path.

    Engine/Engine.symbolindex                         -> Engine.symbolindex.md
    Engine/Application/ConsoleHost/Source/Source.*    -> Application.ConsoleHost.Source.symbolindex.md
    """
    Folder = os.path.dirname(Relative)
    Leaf   = os.path.basename(Relative)[:-len(IndexSuffix)]

    if not Folder:
        return Leaf + MirrorSuffix

    Segments = Folder.replace("\\", "/").split("/")

    # 📝 A folder digest repeats its own folder name (Api/Api.symbolindex); the repetition is dropped so the
    #    mirrored name reads as the path itself rather than as the path with its last segment twice.
    if Segments[-1] != Leaf:
        Segments.append(Leaf)

    return ".".join(Segments) + MirrorSuffix

#------------------------------------------------------------------------------------------------------------------------
#                                              INDEX COLLECTION AND MIRRORING
#------------------------------------------------------------------------------------------------------------------------

def CollectSymbolIndex():
    """Walks the Engine folder and returns every .symbolindex path, relative to that root."""
    Collected = []
    for Folder, Folders, Files in os.walk(EngineRoot):
        Folders.sort()
        for Name in sorted(Files):
            if Name.endswith(IndexSuffix):
                Collected.append(os.path.relpath(os.path.join(Folder, Name), EngineRoot))

    return Collected


def ReclaimMirror():
    """Removes previously mirrored .md files so a renamed or deleted index leaves nothing stale behind."""
    Reclaimed = 0
    for Name in sorted(os.listdir(FlatRoot)):
        Target = os.path.join(FlatRoot, Name)
        if Name.endswith(MirrorSuffix) and os.path.isfile(Target):
            os.remove(Target)
            Reclaimed += 1

    return Reclaimed


def TransferSymbolIndex(RelativePaths):
    """Copies every .symbolindex into the flat folder under its dotted name; -1 when two names collide."""
    Transferred = 0
    Occupied    = {}

    for Relative in RelativePaths:
        Name = ConstructMirrorName(Relative)

        if Name in Occupied:
            Report("FAILED", ColourFailed, "{0} collides with {1} at {2}".format(Relative, Occupied[Name], Name))
            return -1

        Occupied[Name] = Relative

        # 📝 Copied byte for byte rather than read and rewritten — the digests are UTF-8 without a byte-order
        #    mark, and a re-encode is exactly where a mark would be introduced.
        shutil.copy2(os.path.join(EngineRoot, Relative), os.path.join(FlatRoot, Name))
        Transferred += 1

    return Transferred

#------------------------------------------------------------------------------------------------------------------------
#                                                   MIRRORING EXECUTION
#------------------------------------------------------------------------------------------------------------------------

def RunSymbolIndexFlatten():
    """Mirrors every Engine .symbolindex into the flat SymboLindex folder and reports the result."""
    if not os.path.isdir(EngineRoot):
        Report("FAILED", ColourFailed, "Engine folder absent at {0}".format(EngineRoot))
        return 2

    os.makedirs(FlatRoot, exist_ok=True)

    RelativePaths = CollectSymbolIndex()
    if not RelativePaths:
        Report("Skipped", ColourSkipped, "no .symbolindex files found under {0}".format(EngineRoot))
        return 0

    Reclaimed   = ReclaimMirror()
    Transferred = TransferSymbolIndex(RelativePaths)

    if Transferred < 0:
        return 1

    if Reclaimed:
        Report("Neutral", ColourNeutral, "{0} earlier mirrored files reclaimed".format(Reclaimed))

    Report("Compiled", ColourCompiled, "{0} indexes mirrored into {1}".format(Transferred, FlatRoot))
    return 0


if __name__ == "__main__":
    sys.exit(RunSymbolIndexFlatten())
