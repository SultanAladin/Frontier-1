#============================================================================================================================================
#                                                            RUNUPLOADTRANSFER.PY
#============================================================================================================================================
# 🧩 Reclaims the Upload folder, transfers every Engine header and translation unit into it, and rewrites the structure document.

import os
import shutil
import sys

RepositoryRoot    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EngineRoot        = os.path.join(RepositoryRoot, "Engine")
UploadRoot        = os.path.join(RepositoryRoot, "Upload")
StructureName     = "Engine-File-Structure.md"
SourceSuffixes    = (".h", ".cpp", ".slang")
AuxiliarySuffixes = (".comp", ".bat", ".ps1")

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
#                                                SOURCE COLLECTION AND TRANSFER
#------------------------------------------------------------------------------------------------------------------------

def ReclaimUpload():
    """Removes every entry of the Upload folder except the structure document."""
    Reclaimed = 0
    for Entry in os.listdir(UploadRoot):
        if Entry == StructureName:
            continue

        Target = os.path.join(UploadRoot, Entry)
        if os.path.isdir(Target):
            shutil.rmtree(Target)
        else:
            os.remove(Target)
        Reclaimed += 1

    return Reclaimed


def CollectSource():
    """Walks the Engine folder and returns every source and auxiliary path, relative to that root."""
    Collected = []
    for Folder, Folders, Files in os.walk(EngineRoot):
        Folders.sort()
        for Name in sorted(Files):
            if Name.endswith(SourceSuffixes) or Name.endswith(AuxiliarySuffixes):
                Collected.append(os.path.relpath(os.path.join(Folder, Name), EngineRoot))

    return Collected


def TransferSource(RelativePaths):
    """Copies every header, translation unit, and shader into the flat Upload folder; -1 when two names collide.
    .slang files are transferred with a .md extension (e.g. DepthReduction.slang.md).
    """
    Transferred = 0
    Occupied    = {}

    for Relative in RelativePaths:
        if not Relative.endswith(SourceSuffixes) and not Relative.endswith(AuxiliarySuffixes):
            continue

        Name = os.path.basename(Relative)
        TargetName = Name + ".md" if Name.endswith(".slang") else Name

        if TargetName in Occupied:
            Report("FAILED", ColourFailed, "{0} collides with {1}".format(Relative, Occupied[TargetName]))
            return -1

        Occupied[TargetName] = Relative
        shutil.copy2(os.path.join(EngineRoot, Relative), os.path.join(UploadRoot, TargetName))
        Transferred += 1

    return Transferred

#------------------------------------------------------------------------------------------------------------------------
#                                                  FOLDER STRUCTURE DOCUMENT
#------------------------------------------------------------------------------------------------------------------------

def Descend(Branch, Prefix, Lines):
    """Appends one indented line per entry, folders before files, recursing into each folder."""
    Folders = sorted([Name for Name in Branch if Branch[Name] is not None])
    Files   = sorted([Name for Name in Branch if Branch[Name] is None])
    Ordered = [(Name, True) for Name in Folders] + [(Name, False) for Name in Files]

    for Position, (Name, FolderEnabled) in enumerate(Ordered):
        Terminal = Position == len(Ordered) - 1
        Lines.append(Prefix + ("└── " if Terminal else "├── ") + Name + ("/" if FolderEnabled else ""))

        if FolderEnabled:
            Descend(Branch[Name], Prefix + ("    " if Terminal else "│   "), Lines)


def RenderFolderStructure(RelativePaths):
    """Reconstructs the nested folder shape from flat relative paths and renders it as ASCII lines."""
    Structure = {}
    for Relative in RelativePaths:
        Parts  = Relative.split(os.sep)
        Branch = Structure
        for Folder in Parts[:-1]:
            Branch = Branch.setdefault(Folder, {})
        Branch[Parts[-1]] = None

    Lines = ["Engine/"]
    Descend(Structure, "", Lines)
    return Lines


def WriteStructureDocument(RelativePaths, Lines):
    """Rewrites Engine-File-Structure.md from the live filesystem."""
    SourceCount    = len([Relative for Relative in RelativePaths if Relative.endswith(SourceSuffixes)])
    AuxiliaryCount = len(RelativePaths) - SourceCount
    Auxiliary      = "none found" if AuxiliaryCount == 0 else "{0} files".format(AuxiliaryCount)

    Preamble = "Source: `Engine\\` — files of type `.cpp` / `.h` / `.slang` ({0} files) and `.comp` / `.bat` / `.ps1` ({1})."
    Document = ["# Engine Folder Structure",
                "",
                Preamble.format(SourceCount, Auxiliary),
                "",
                "```"] + Lines + ["```", ""]

    with open(os.path.join(UploadRoot, StructureName), "w", encoding="utf-8", newline="\n") as Stream:
        Stream.write("\n".join(Document))

#------------------------------------------------------------------------------------------------------------------------
#                                                  UPLOAD TRANSFER EXECUTION
#------------------------------------------------------------------------------------------------------------------------

def RunUploadTransfer():
    """Reclaims Upload, transfers the Engine sources into it and rewrites the structure document."""
    if not os.path.isdir(EngineRoot):
        Report("FAILED", ColourFailed, "Engine folder absent at {0}".format(EngineRoot))
        return 2

    if not os.path.isdir(UploadRoot):
        os.makedirs(UploadRoot)

    Reclaimed = ReclaimUpload()
    Report("SKIP", ColourSkipped, "{0} retained · {1} entries reclaimed".format(StructureName, Reclaimed))

    RelativePaths = CollectSource()
    Transferred   = TransferSource(RelativePaths)

    if Transferred < 0:
        return 1

    WriteStructureDocument(RelativePaths, RenderFolderStructure(RelativePaths))
    Report("Compiled", ColourCompiled, "{0} files transferred into {1}".format(Transferred, UploadRoot))
    return 0


if __name__ == "__main__":
    sys.exit(RunUploadTransfer())
