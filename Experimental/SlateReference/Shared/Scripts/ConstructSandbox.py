#============================================================================================================================================
#                                                          CONSTRUCTSANDBOX.PY
#============================================================================================================================================
# 🧩 Runs Build/Construct.ps1's sequence on a POSIX/g++ box — same steps, same order, syntax-only translation.

# 🔴 This exists so an agent working on Linux reaches the SAME refusals a Windows build reaches, in the SAME
#    order. The divergence this removes was concrete: work done here skipped ApplyImGuiPatches and compiled a
#    hand-listed subset of SlateUI, so `ImGuiStyle::TabSlant` was never missing here and the partition was
#    never checked at all — both then failed on the Windows build. The ordering below is Construct.ps1's:
#
#       1. read the unit graph from Module.toml       (Read-UnitGraph)
#       2. refuse a duplicate subject                 (Test-SubjectUniqueness)
#       3. topologically order the units              (Resolve-TranslationOrder, Kahn)
#       4. apply the ImGui divergence                 (ApplyImGuiPatches.ps1)   ← the step that was missed
#       5. prove the dependency partition             (VerifyPartition.ps1)
#       6. translate every unit in dependency order   (Invoke-Translation)
#
# ⚠️ This is NOT a replacement for Construct.ps1 and it does not link, archive or lower shaders. MSVC is the
#    only toolchain that builds Slate for real; g++ -fsyntax-only is a front-end proxy that catches the class
#    of defect an agent actually introduces — a missing include, a renamed member, a broken declaration.
#    A clean run here means "nothing obviously refuses", not "the Windows build is green".
#
#     python3 Scripts/ConstructSandbox.py
#     python3 Scripts/ConstructSandbox.py --unit SlateUI
#     python3 Scripts/ConstructSandbox.py --skip-patches
#
# 🔴 POPULATE THE SUBMODULES AND POINT AT REAL VULKAN HEADERS, OR THIS RUN IS LYING TO YOU. For a long
#    while every ExternalPackages/ submodule was an empty directory here and VULKAN_SDK pointed at
#    Tools/VulkanParseStub, so 35 of 255 translation units were rejected on every run for reasons that
#    had nothing to do with the change under test. Those 35 covered the whole SlateVulkan device layer
#    and most of SlateUI -- which is to say a change could break the interface layer outright and this
#    script would report the same 35 failures it always reported. A standing baseline of expected
#    failures is not a baseline, it is a blind spot, and it hid real defects until the day it did not.
#
#      git submodule update --init --depth 1 --jobs 4
#      git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git /tmp/VkH
#      mkdir -p /tmp/vksdk && cp -r /tmp/VkH/include /tmp/vksdk/Include
#      export VULKAN_SDK=/tmp/vksdk
#
#    With both in place the correct result is 255 of 255 accepted. Anything less is a real failure.
#    `--skip-patches` is the one documented exception: InterfaceExchange names ImGuiStyle::TabSlant,
#    which PatchA introduces, so skipping the patches rejects exactly that one unit and no other.

import os
import re
import subprocess
import shutil
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EngineRoot     = os.path.join(RepositoryRoot, 'Engine')
PackageRoot    = os.path.join(RepositoryRoot, 'ExternalPackages')
ScratchRoot    = os.path.join(RepositoryRoot, '_AgentScratch')

sys.path.insert(0, os.path.join(RepositoryRoot, 'Scripts'))

#------------------------------------------------------------------------------------------------------------------------
#                                                       CONSOLE REPORTING
#------------------------------------------------------------------------------------------------------------------------

ColourNeutral  = "\x1b[90m"
ColourSkipped  = "\x1b[36m"
ColourFailed   = "\x1b[31m"
ColourCompiled = "\x1b[32m"
ColourReset    = "\x1b[0m"

if os.name == "nt":
    os.system("")


def Report(Tag, Colour, Message):
    print("{0}{1}{2} {3}".format(Colour, ("[" + Tag + "]").ljust(10), ColourReset, Message))


def WriteBuilding(Message): Report('Build',    ColourNeutral,  Message)
def WriteSkipped(Message):  Report('SKIP',     ColourSkipped,  Message)
def WriteRejected(Message):  Report('FAILED',   ColourFailed,   Message)
def WriteProduced(Message): Report('Compiled', ColourCompiled, Message)

#------------------------------------------------------------------------------------------------------------------------
#                                                       THE UNIT GRAPH
#------------------------------------------------------------------------------------------------------------------------

# 📝 Read from Module.toml, exactly as Read-UnitGraph does. `Requires` is the [link].unit order, because that
#    is what the translation order is derived from — [requires].unit is the include permission and is what
#    VerifyPartition proves.
def ReadUnitGraph():
    Declared = {}

    for Walked, Folders, Files in os.walk(EngineRoot):
        if 'Module.toml' not in Files:
            continue

        Manifest = os.path.join(Walked, 'Module.toml')

        with open(Manifest, 'r', encoding='utf-8-sig') as Reader:
            Content = Reader.read()

        UnitName = os.path.basename(Walked)
        Named    = re.search(r'(?ms)^\[unit\].*?^name\s*=\s*"([^"]+)"', Content)

        if Named:
            UnitName = Named.group(1)

        Producing = re.search(r'(?ms)^\[unit\].*?^product\s*=\s*"([^"]+)"', Content)

        # 🔴 [product] is one subject compiled with one feature macro. Construct.ps1 supplies the
        #    macro as /D; this script never did, so EditorHost was translated with NO product defined
        #    and tripped HostFeature.h's #error. A sandbox result that disagrees with a Windows one is
        #    exactly what running the pieces out of order was supposed to stop.
        Products = {}
        Table = re.search(r'(?ms)^\[product\]\s*$(.*?)(?=^\[|\Z)', Content)
        if Table:
            for Line in Table.group(1).splitlines():
                Entry = re.match(r'\s*(\w+)\s*=\s*\{\s*subject\s*=\s*"([^"]+)"\s*,\s*define\s*=\s*"([^"]+)"', Line)
                if Entry:
                    Products.setdefault(Entry.group(2), []).append((Entry.group(1), Entry.group(3)))
        Product   = Producing.group(1) if Producing else 'StaticLibrary'

        # 📝 A subject names one source folder that becomes its own link target.
        Subject  = []
        Subjects = re.search(r'(?ms)^\[unit\].*?^subject\s*=\s*\[(.*?)\]', Content)

        if Subjects:
            Subject = re.findall(r'"([^"]+)"', Subjects.group(1))

        Linked  = []
        Linking = re.search(r'(?ms)^\[link\].*?^unit\s*=\s*\[(.*?)\]', Content)

        if Linking:
            Linked = re.findall(r'"([^"]+)"', Linking.group(1))

        # 📝 `[link].carry` names files that must sit beside the executable for it to run — the GLFW DLL the
        #    import library resolves against, and the appearance file every host reads at startup. Paths are
        #    repository-relative so a manifest can name one without knowing where the build writes binaries.
        Carried  = []
        Carrying = re.search(r'(?ms)^\[link\].*?^carry\s*=\s*\[(.*?)\]', Content)

        if Carrying:
            Carried = re.findall(r'"([^"]+)"', Carrying.group(1))

        Declared[UnitName] = {
            'Name': UnitName, 'Product': Product, 'Subject': Subject, 'Products': Products,
            'Requires': Linked, 'Carry': Carried, 'Root': Walked,
        }

    return Declared


# 🔴 A subject declared twice would have one host's objects overwrite the other's in a shared folder, and both
#    would compile. Rejected here rather than discovered as a host that runs the wrong main().
def TestSubjectUniqueness(Declared):
    Seen = {}

    for UnitName in sorted(Declared):
        for Subject in Declared[UnitName]['Subject']:
            if Subject in Seen:
                raise RuntimeError("subject {0} is declared by both {1} and {2}".format(
                    Subject, Seen[Subject], UnitName))

            Seen[Subject] = UnitName


# 🔴 Kahn's algorithm, so a cycle is reported as a cycle rather than as a stack overflow or an arbitrary order.
def ResolveTranslationOrder(Declared):
    Remaining = {}
    Ordered   = []

    for UnitName in Declared:
        for Required in Declared[UnitName]['Requires']:
            if Required not in Declared:
                raise RuntimeError("{0} links {1}, which declares no Module.toml".format(UnitName, Required))

        Remaining[UnitName] = set(Declared[UnitName]['Requires'])

    while Remaining:
        # 📝 Sorted so the order is reproducible.
        Ready = sorted([Name for Name in Remaining if not Remaining[Name]])

        if not Ready:
            raise RuntimeError("the unit graph holds a cycle among: {0}".format(', '.join(sorted(Remaining))))

        for UnitName in Ready:
            Ordered.append(Declared[UnitName])
            del Remaining[UnitName]

        for UnitName in list(Remaining):
            Remaining[UnitName] -= set(Ready)

    return Ordered

#------------------------------------------------------------------------------------------------------------------------
#                                                       PATH ASSEMBLY
#------------------------------------------------------------------------------------------------------------------------

# 📝 The mirror of Get-IncludePath. Foundation/ and Shared/ are reachable from every unit through the engine
#    root; the partition is enforced by the link and by VerifyPartition, not by hiding headers.
#
# 🔴 THE REPOSITORY ROOT IS NOT AN INCLUDE ROOT, BECAUSE IT IS NOT ONE ON WINDOWS. This carried it and
#    `Get-IncludePath` never did — so a header placed at the repository root instead of under `Engine/`
#    resolved here and was rejected by `cl.exe` with C1083. Every sandbox gate passed and the real build
#    stopped on the first unit that included it. A mirror more permissive than the thing it mirrors does
#    not fail earlier than the real build; it fails LATER, on someone else's machine.
#
# ⚠️ `Tools/VulkanParseStub` is the ONE exception and is reached by its own path rather than by opening
#    the repository root again. It stands in for the Vulkan SDK, which this sandbox has no copy of; on
#    Windows that headroom comes from the real SDK under `$VulkanRoot\Include`. Adding the stub is a
#    substitution for something the real build genuinely has — widening the root is not.
def GetIncludePath(UnitEntry, VulkanInclude):
    Paths = [EngineRoot,
             os.path.join(PackageRoot, 'glfw', 'include')]

    if not VulkanInclude:
        Paths.append(os.path.join(RepositoryRoot, 'Tools', 'VulkanParseStub'))

    if VulkanInclude:
        Paths.append(VulkanInclude)

    if UnitEntry['Name'] == 'SlateUI':
        Paths.append(os.path.join(PackageRoot, 'imgui'))
        Paths.append(os.path.join(PackageRoot, 'thorvg', 'inc'))

    # 🔴 `10` §1's codecs compile the vendored readers — stb and fast_obj — into their own translation units,
    #    so SlateDocument reaches the package root and no other unit does.
    if UnitEntry['Name'] == 'SlateDocument':
        Paths.append(PackageRoot)

    # 📝 GeometryRenderingExchange triangulates with SlateCompute's declared Earcut dependency.
    if UnitEntry['Name'] == 'SlateCompute':
        Paths.append(os.path.join(PackageRoot, 'earcut', 'include'))

    # 📝 A host includes the interface unit's Api and, through it, ImGui's own header.
    if UnitEntry['Name'] == 'Application':
        Paths.append(os.path.join(PackageRoot, 'imgui'))

    Assembled = []

    for Path in Paths:
        if os.path.isdir(Path):
            Assembled += ['-I', Path]

    return Assembled


def GetUnitSource(UnitEntry, Subject=''):
    UnitRoot = UnitEntry['Root']

    # 📝 A subject scopes the gather to its own folder, which is what keeps one host's main() out of another's.
    if Subject:
        UnitRoot = os.path.join(UnitRoot, Subject)

        if not os.path.isdir(UnitRoot):
            raise RuntimeError("{0} declares subject {1} but {2} does not exist".format(
                UnitEntry['Name'], Subject, UnitRoot))

    Sources = []

    for Walked, Folders, Files in os.walk(UnitRoot):
        Folders.sort()

        for Leaf in sorted(Files):
            if Leaf.endswith('.cpp'):
                Sources.append(os.path.join(Walked, Leaf))

    return Sources

#------------------------------------------------------------------------------------------------------------------------
#                                                    TOOLCHAIN ACQUISITION
#------------------------------------------------------------------------------------------------------------------------

# 📝 The mirror of Resolve-VulkanRoot. $VULKAN_SDK is honoured first exactly as on Windows; the vendored
#    Khronos headers under _AgentScratch are the sandbox fallback, and their absence is reported rather than
#    guessed at — a Vulkan-facing unit that cannot find vulkan.h should say so once, not 40 times.
def ResolveVulkanInclude():
    Declared = os.environ.get('VULKAN_SDK', '')

    if Declared:
        for Candidate in (os.path.join(Declared, 'Include'), os.path.join(Declared, 'include')):
            if os.path.isdir(Candidate):
                return Candidate

    Vendored = os.path.join(ScratchRoot, 'Vulkan-Headers', 'include')

    if os.path.isdir(os.path.join(Vendored, 'vulkan')):
        return Vendored

    return ''


# 🔴 The Windows build defines these through Get-CompilationFlags. GLFW_INCLUDE_NONE is the one addition the
#    sandbox needs and it is not a divergence: it tells glfw3.h not to pull a GL header in, and Slate reaches
#    Vulkan through its own loader, never GL. Windows ships GL/gl.h with the platform SDK so the omission is
#    invisible there; a Linux box without mesa headers fails on it, which is noise rather than a defect.
def GetCompilationFlags(Selection):
    Common = ['-std=c++20', '-fsyntax-only', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX',
              '-DGLFW_DLL', '-DGLFW_INCLUDE_NONE']

    # 📝 🔴 SLATE_DEBUG selects every debug path in the engine. _DEBUG is never defined.
    if Selection == 'Debug':
        return Common + ['-DSLATE_DEBUG=1']

    return Common + ['-DNDEBUG']

#------------------------------------------------------------------------------------------------------------------------
#                                                        TRANSLATION
#------------------------------------------------------------------------------------------------------------------------

def InvokeTranslation(UnitEntry, Selection, VulkanInclude, Compiler, Warn, Subject='', Define=None, Product=None):
    Sources = GetUnitSource(UnitEntry, Subject)
    Named   = "{0}{1}{2}".format(UnitEntry['Name'], "/" + Subject if Subject else "",
                                 " [" + Product + "]" if Product else "")

    # 🔴 REFUSED, NOT SKIPPED, BECAUSE `Construct.ps1` REFUSES. This printed a cheerful `[SKIP]` and
    #    carried on, while the Windows build threw on the same unit and stopped before `Application` was
    #    linked. The editor was therefore never relinked, a stale executable was run against a fix it did
    #    not contain, and the defect was reported as unfixed. Second time this exact divergence has cost a
    #    build: a mirror more forgiving than the thing it mirrors does not fail earlier, it fails later on
    #    someone else's machine. If a unit legitimately has no sources, that is a fact about the unit and
    #    belongs in its manifest — not a fact about which of the two build scripts happens to be running.
    if not Sources:
        WriteRejected("{0} declares no translation unit".format(Named))
        return 0, 1

    Include  = GetIncludePath(UnitEntry, VulkanInclude)
    Flags    = GetCompilationFlags(Selection) + (['-Wall', '-Wextra', '-Wno-unused-parameter'] if Warn else ['-w'])

    if Define:
        Flags = Flags + ['-D' + Define]
    Rejected  = 0
    LogRoot  = os.path.join(ScratchRoot, 'logs', 'construct')

    os.makedirs(LogRoot, exist_ok=True)

    for Source in Sources:
        Relative = os.path.relpath(Source, RepositoryRoot).replace(os.sep, '/')
        Finished = subprocess.run([Compiler] + Flags + Include + [Source],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        if Finished.returncode != 0:
            Rejected += 1
            WriteRejected(Relative)

            with open(os.path.join(LogRoot, Relative.replace('/', '_') + '.log'), 'w') as Writer:
                Writer.write(Finished.stdout)

            for Line in [L for L in Finished.stdout.split('\n') if 'error:' in L][:4]:
                print("    " + Line.strip())

    if Rejected == 0:
        WriteProduced("{0} — {1} translation unit(s)".format(Named, len(Sources)))

    return len(Sources), Rejected

#------------------------------------------------------------------------------------------------------------------------
#                                                        THE SEQUENCE
#------------------------------------------------------------------------------------------------------------------------

#---
#                                            CARRIED FILES
#---

# 📝 The mirror of Construct.ps1's carry step. This script does not link, so nothing here is required for a
#    translation to succeed — it is run so the same manifest field is exercised on Linux and a carry that
#    names a path no longer present is reported here rather than discovered on Windows at run time.
# 🔴 An absent carried file is reported and skipped, never fatal. glfw3.dll arrives with the vendored
#    package rather than the repository, so a checkout without it must still construct.
def ApplyCarried(Selected):
    BinaryRoot = os.path.join(ScratchRoot, 'build', 'Binary')

    for UnitEntry in Selected:
        for Carried in UnitEntry.get('Carry', []):
            Origin = os.path.join(RepositoryRoot, Carried.replace('/', os.sep))
            Leaf   = os.path.basename(Origin)

            # 📝 🔴 A carried FOLDER is placed whole. EngineContent is named this way: naming several hundred
            #    files one at a time in the manifest would make the manifest a second copy of the folder
            #    listing, and it would drift the first time content is added. The leaf name is preserved so
            #    the run-time path below the binary seat is the same on both platforms.
            if os.path.isdir(Origin):
                AppliedRoot = os.path.join(BinaryRoot, Leaf)

                os.makedirs(BinaryRoot, exist_ok = True)
                shutil.copytree(Origin, AppliedRoot, dirs_exist_ok = True)
                WriteProduced("carried {0}{1}".format(Leaf, os.sep))
                continue

            if not os.path.isfile(Origin):
                WriteSkipped("carry \u2014 {0} is absent at {1}".format(Leaf, Carried))
                continue

            os.makedirs(BinaryRoot, exist_ok = True)
            Applied = os.path.join(BinaryRoot, Leaf)

            # 📝 🔴 An appearance the artist has since edited is left alone. Overwriting on every construct
            #    would discard their theme each time they rebuilt, which reads as the editor forgetting.
            if os.path.isfile(Applied) and os.path.getmtime(Applied) >= os.path.getmtime(Origin):
                continue

            shutil.copyfile(Origin, Applied)
            WriteProduced("carried {0}".format(Leaf))


def Main(Arguments):
    Selection = 'Debug' if '--debug' in Arguments else 'Release'
    Warn      = '--warn' in Arguments
    Compiler  = os.environ.get('CXX', 'g++')
    Unit      = ''

    if '--unit' in Arguments:
        Unit = Arguments[Arguments.index('--unit') + 1]

    print("Slate \u2014 {0} (sandbox, syntax-only)".format(Selection))

    Declared = ReadUnitGraph()

    if not Declared:
        WriteRejected("no Module.toml was found under {0}".format(EngineRoot))
        return 1

    TestSubjectUniqueness(Declared)
    UnitOrder = ResolveTranslationOrder(Declared)

    VulkanInclude = ResolveVulkanInclude()

    if VulkanInclude:
        WriteBuilding("vulkan {0}".format(VulkanInclude))
    else:
        WriteSkipped("no Vulkan headers; VULKAN_SDK is unset and no vendored copy is present")

    # 🔴 Slate's ImGui divergence is applied before any unit is translated, because SlateUI compiles the
    #    vendored ImGui sources directly. Skipping this is what made a Linux run disagree with a Windows one:
    #    InterfaceExchange names ImGuiStyle::TabSlant, which only exists once PatchA has been applied.
    if '--skip-patches' in Arguments:
        WriteSkipped('ImGui patches — asked to skip')
    else:
        import ApplyImGuiPatches

        if ApplyImGuiPatches.Main([]) != 0:
            WriteRejected('ApplyImGuiPatches rejected; the ImGui tab-shape patches were not applied')
            return 1

    # 🔴 The dependency partition is proven before anything is translated, because a forbidden include should
    #    refuse at the start of a build rather than as an unresolved symbol at the end of one.
    import VerifyPartition

    if VerifyPartition.Main([]) != 0:
        WriteRejected('VerifyPartition rejected; a unit reaches past what it declares')
        return 1

    Naming = subprocess.run([sys.executable, os.path.join(RepositoryRoot, 'Scripts', 'VerifyNaming.py')],
                            cwd=RepositoryRoot)
    if Naming.returncode != 0:
        WriteRejected('VerifyNaming rejected; retired vocabulary remains')
        return 1

    print('')

    Selected = [Entry for Entry in UnitOrder if Entry['Name'] == Unit] if Unit else UnitOrder

    if not Selected:
        WriteRejected("no unit is named {0}".format(Unit))
        return 1

    Translated = 0
    Rejected    = 0

    for UnitEntry in Selected:
        if UnitEntry['Product'] == 'StaticLibrary':
            Count, Broken = InvokeTranslation(UnitEntry, Selection, VulkanInclude, Compiler, Warn)
            Translated += Count
            Rejected    += Broken
        else:
            # 📝 One host per subject, so one host's main() never enters another's link. A subject
            #    named by [product] is translated ONCE PER PRODUCT with that product's macro, which is
            #    what Construct.ps1 does; a subject with no product keeps the plain single pass.
            for Subject in UnitEntry['Subject']:
                for Product, Define in UnitEntry.get('Products', {}).get(Subject, [(None, None)]):
                    Count, Broken = InvokeTranslation(UnitEntry, Selection, VulkanInclude, Compiler,
                                                      Warn, Subject, Define, Product)
                    Translated += Count
                    Rejected    += Broken

    print('')

    if Rejected:
        WriteRejected("{0} of {1} translation unit(s) rejected".format(Rejected, Translated))
        return 1

    ApplyCarried(Selected)

    WriteProduced("{0} translation unit(s) accepted across {1} unit(s)".format(Translated, len(Selected)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(Main(sys.argv[1:]))
    except RuntimeError as Fault:
        WriteRejected(str(Fault))
        sys.exit(1)
