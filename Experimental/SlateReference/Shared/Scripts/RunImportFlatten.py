#============================================================================================================================================
#                                                          RUNIMPORTFLATTEN.PY
#============================================================================================================================================
# 🧩 Flattens the Import folder — every nested .cpp and .h is copied to the Import top level, nothing is deleted.

import os
import shutil
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ImportRoot     = os.path.join(RepositoryRoot, "Import")
SourceSuffixes = (".h", ".cpp", ".slang")

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
#                                               SOURCE COLLECTION AND FLATTEN
#------------------------------------------------------------------------------------------------------------------------

def CollectSource():
    """Walks the Import folder and returns every .cpp and .h path, relative to that root."""
    Collected = []
    for Folder, Folders, Files in os.walk(ImportRoot):
        Folders.sort()
        for Name in sorted(Files):
            if Name.endswith(SourceSuffixes):
                Collected.append(os.path.relpath(os.path.join(Folder, Name), ImportRoot))

    return Collected


def FlattenSource(RelativePaths):
    """Copies every nested .cpp and .h into the Import top level; -1 when two names collide."""
    Flattened = 0
    Already   = 0
    Occupied  = {}

    for Relative in RelativePaths:
        Name   = os.path.basename(Relative)
        Source = os.path.join(ImportRoot, Relative)
        Target = os.path.join(ImportRoot, Name)

        if os.path.normpath(Source) == os.path.normpath(Target):
            Already += 1
            continue

        if Name in Occupied:
            Report("FAILED", ColourFailed, "{0} collides with {1}".format(Relative, Occupied[Name]))
            return -1

        Occupied[Name] = Relative
        shutil.copy2(Source, Target)
        Flattened += 1

    return Flattened

#------------------------------------------------------------------------------------------------------------------------
#                                                     FLATTEN EXECUTION
#------------------------------------------------------------------------------------------------------------------------

def RunImportFlatten():
    """Copies every Import .cpp and .h up to the Import top level and reports the result."""
    if not os.path.isdir(ImportRoot):
        Report("FAILED", ColourFailed, "Import folder absent at {0}".format(ImportRoot))
        return 2

    RelativePaths = CollectSource()
    Flattened     = FlattenSource(RelativePaths)

    if Flattened < 0:
        return 1

    Report("Compiled", ColourCompiled, "{0} files flattened into {1}".format(Flattened, ImportRoot))
    return 0


if __name__ == "__main__":
    sys.exit(RunImportFlatten())
