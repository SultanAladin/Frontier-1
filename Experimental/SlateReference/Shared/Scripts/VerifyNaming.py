# Verifies that retired Slate vocabulary does not return to first-party Engine names.

import os
import re
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EngineRoot = os.path.join(RepositoryRoot, "Engine")
Extensions = {".h", ".hpp", ".cpp", ".c", ".slang"}
# \U0001f534 THE BAN LIVES HERE, NOT IN ANYONE'S MEMORY. Four words were agreed as retired and then merely
#    AVOIDED rather than listed -- so `DraftSolver` sat in the tree, in a banned spelling, passing a green
#    gate. A word that is not in this tuple is not banned, whatever anyone intended.
#
# \U0001f4dd Each is spelled by concatenation so that this file, which must name the words in order to
#    forbid them, does not match its own search when it is scanned.
Retired = ("Ceiling", "Ordinal", "Choice", "Boundary", "Region",
           "Con" + "tract", "con" + "tract", "Led" + "ger", "led" + "ger",
           "Dra" + "ft", "dra" + "ft", "Dra" + "ught", "dra" + "ught",
           "Pain" + "t", "pain" + "t", "Out" + "come", "out" + "come")

# \U0001f534 ONE FILE MAY DISCUSS ONE WORD, AND ONLY WHERE THE BAN ITSELF IS RECORDED. `DeliveryGuarantee.h`
#    explains WHY the fallible return is not spelled the retired way, so it must name it; a bulk rename once
#    corrupted that sentence into "`Deliver` and not `Deliver`". The exemption is a single file paired with a
#    single word, not a blanket skip, so no other retired spelling can hide behind it.
Discussed = {
    "Foundation/DeliveryGuarantee.h": {"Out" + "come", "out" + "come"},
}
VendorBindingTokens = {
    "VkDescriptorSetLayoutBinding", "pBindings", "dstBinding",
}
Failures = []

for Walked, Folders, Files in os.walk(EngineRoot):
    Folders[:] = [Folder for Folder in Folders if Folder != "ExternalPackages"]
    for FileName in Files:
        if os.path.splitext(FileName)[1] not in Extensions:
            continue
        Path = os.path.join(Walked, FileName)
        Relative = os.path.relpath(Path, RepositoryRoot).replace(os.sep, "/")
        with open(Path, "r", encoding="utf-8-sig", errors="replace") as Reader:
            for LineNumber, Line in enumerate(Reader, 1):
                SlateText = Line.replace("GetContentRegionAvail", "")
                Permitted = Discussed.get(Relative.replace("Engine/", "", 1), set())
                for Word in Retired:
                    if Word in SlateText and Word not in Permitted:
                        Failures.append((Relative, LineNumber, Word))
                if re.search(r"\bConstruct\s*\(", Line):
                    Failures.append((Relative, LineNumber, "plain Construct method"))
                for Token in re.findall(r"[A-Za-z_][A-Za-z0-9_]*Binding[A-Za-z0-9_]*", Line):
                    if Token not in VendorBindingTokens:
                        Failures.append((Relative, LineNumber, Token))

for Walked, Folders, Files in os.walk(EngineRoot):
    for Name in Folders + Files:
        for Word in Retired:
            if Word in Name:
                Failures.append((os.path.relpath(os.path.join(Walked, Name), RepositoryRoot), 0, Word + " in path"))

if Failures:
    for Relative, LineNumber, Word in Failures:
        Location = f"{Relative}:{LineNumber}" if LineNumber else Relative
        print(f"[Naming] {Location}: retired or vague spelling: {Word}")
    print(f"\n[Naming] {len(Failures)} violation(s)")
    sys.exit(1)

print("[Naming] first-party Engine vocabulary holds")
