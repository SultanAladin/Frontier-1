# VerifyPartition.ps1 — proves the dependency partition rather than describing it.
#
# 🔴 `Module.toml` is the authority. This script reads it, derives what each unit is permitted to reach,
#    and refuses when a translation unit reaches further. Until now the partition was enforced only by the
#    link — SlateDocument was never handed SlateVulkan.lib, so a device reference failed to resolve — which
#    reports a missing symbol at the end of a whole build rather than a forbidden include at its start.
#
# 🔴 It also refuses the two leaks `14` §2 and `10` §1 name by hand: an ImGui spelling outside SlateUI, and
#    a vendored reader outside SlateDocument. Both are stated in prose in the existing comments; neither was
#    checked by anything.
#
#     powershell -File Scripts\VerifyPartition.ps1
#     powershell -File Scripts\VerifyPartition.ps1 -Detail

[CmdletBinding()]
param(
    [switch] $Detail
)

$ErrorActionPreference = 'Stop'

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$EngineRoot     = Join-Path $RepositoryRoot 'Engine'

function Write-Report([string] $Tag, [System.ConsoleColor] $Colour, [string] $Message)
{
    Write-Host ("[$Tag]".PadRight(10)) -ForegroundColor $Colour -NoNewline
    Write-Host " $Message"
}

function Write-Held([string]    $Message) { Write-Report 'Partition' Green    $Message }
function Write-Broken([string]  $Message) { Write-Report 'REFUSED'   Red      $Message }
function Write-Noted([string]   $Message) { Write-Report 'Partition' DarkGray $Message }

#---
#                                   THE DECLARED PARTITION
#---

# 📝 Read from Module.toml and from nowhere else. A second copy of the graph is a second thing to keep
#    agreeing, which is the whole reason this reads the declaration instead of restating it.
function Read-DeclaredUnits
{
    $Declared = @{}

    foreach ($Manifest in Get-ChildItem $EngineRoot -Filter 'Module.toml' -Recurse -File)
    {
        $UnitName = Split-Path -Leaf (Split-Path -Parent $Manifest.FullName)
        $Content  = Get-Content $Manifest.FullName -Raw

        $Requires = @()

        if ($Content -match '(?ms)^\[requires\].*?^unit\s*=\s*\[(.*?)\]')
        {
            $Requires = [regex]::Matches($Matches[1], '"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
        }

        $Declared[$UnitName] = @{
            Name     = $UnitName
            Requires = @($Requires)
            Root     = Split-Path -Parent $Manifest.FullName
        }
    }

    return $Declared
}

# 🔴 A cycle is rejected before anything else is checked. Every later question — what may this unit reach,
#    in what order are they translated — assumes the graph is acyclic, and answers nonsense when it is not.
function Test-Acyclic([hashtable] $Declared)
{
    $Broken = @()

    foreach ($UnitName in $Declared.Keys)
    {
        $Seen  = @{}
        $Stack = New-Object System.Collections.Generic.Stack[string]
        $Stack.Push($UnitName)

        while ($Stack.Count -gt 0)
        {
            $Current = $Stack.Pop()

            foreach ($Required in $Declared[$Current].Requires)
            {
                if (-not $Declared.ContainsKey($Required))
                {
                    $Broken += "$Current requires $Required, which declares no Module.toml"
                    continue
                }

                if ($Required -eq $UnitName)
                {
                    $Broken += "$UnitName participates in a dependency cycle through $Current"
                    continue
                }

                if (-not $Seen.ContainsKey($Required))
                {
                    $Seen[$Required] = $true
                    $Stack.Push($Required)
                }
            }
        }
    }

    return $Broken
}

# 📝 The transitive closure, because a unit may name a type it reaches through one of its own requirements.
#    SlateCompute requiring SlateVulkan means it may include SlateVulkan's Api, and SlateMath's through it.
function Resolve-Reachable([hashtable] $Declared, [string] $UnitName)
{
    $Reached = @{}
    $Stack   = New-Object System.Collections.Generic.Stack[string]
    $Stack.Push($UnitName)

    while ($Stack.Count -gt 0)
    {
        foreach ($Required in $Declared[$Stack.Pop()].Requires)
        {
            if ($Declared.ContainsKey($Required) -and -not $Reached.ContainsKey($Required))
            {
                $Reached[$Required] = $true
                $Stack.Push($Required)
            }
        }
    }

    return $Reached
}

#---
#                                        THE VERDICT
#---

#---
#                                     SHELL FILE ENCODING
#---

# 🔴 A PowerShell file containing any non-ASCII byte MUST carry a UTF-8 BOM, and a batch file must carry
#    none at all. Windows PowerShell 5.1 reads a BOM-less file in the system code page, so every multi-byte
#    sequence arrives as mojibake and the parse fails at a line that looks unrelated to the character. A
#    batch file has the opposite constraint: cmd.exe reads it in the OEM code page and executes the stray
#    bytes, which is how a comment produced "'M' is not recognized as an internal or external command".
#
# 🔴 Both cost a build each to discover. Checked here so neither can be reintroduced by an editor that
#    silently re-saves without the mark.
function Test-ShellEncoding
{
    $Broken = @()
    $Mark   = [byte[]] (0xEF, 0xBB, 0xBF)

    # 🔴 Only the shell the build actually runs. `_AgentScratch/` is fully git-ignored disposable output —
    #    a throwaway probe written there is not a build input, and a build that refuses because of one is
    #    refusing over a file nobody will ever run. `Upload/` and `Patch/` are ignored for the same reason.
    #    The first version of this check swept the whole repository root and stopped a build over two
    #    scratch probes, which is the defect this list exists to remove.
    $Governed = @('Build', 'Scripts')

    foreach ($Folder in $Governed)
    {
        $FolderRoot = Join-Path $RepositoryRoot $Folder

        if (-not (Test-Path $FolderRoot))
        {
            continue
        }

        foreach ($Shell in Get-ChildItem $FolderRoot -Include '*.ps1', '*.bat' -File -Recurse)
        {
            $Bytes = [System.IO.File]::ReadAllBytes($Shell.FullName)
            $Named = $Shell.FullName.Substring($RepositoryRoot.Length + 1)

            $Marked = ($Bytes.Length -ge 3) -and
                      ($Bytes[0] -eq $Mark[0]) -and ($Bytes[1] -eq $Mark[1]) -and ($Bytes[2] -eq $Mark[2])

            $Wide = 0

            for ($Index = $(if ($Marked) { 3 } else { 0 }); $Index -lt $Bytes.Length; ++$Index)
            {
                if ($Bytes[$Index] -gt 127) { ++$Wide }
            }

            if ($Shell.Extension -eq '.bat')
            {
                if ($Wide -gt 0 -or $Marked)
                {
                    $Carried = "$Wide non-ASCII byte(s)$(if ($Marked) { ' and a BOM' })"
                    $Broken  += "$Named carries $Carried; cmd.exe requires plain ASCII"
                }
            }
            elseif ($Wide -gt 0 -and -not $Marked)
            {
                $Broken += "$Named carries $Wide non-ASCII byte(s) and no UTF-8 BOM; PowerShell misreads them"
            }
        }
    }

    return $Broken
}

$Declared = Read-DeclaredUnits

if ($Declared.Count -eq 0)
{
    throw "no Module.toml was found under $EngineRoot"
}

# 🔴 Reported on its own. Folding a missing BOM into the partition's refusal list had the build announce
#    "the dependency partition is broken in 2 place(s)" for a file-encoding fault, which names the wrong
#    subsystem and sends a reader to the wrong place entirely.
$Mistyped = Test-ShellEncoding

if ($Mistyped.Count -gt 0)
{
    foreach ($Fault in $Mistyped)
    {
        Write-Broken $Fault
    }

    Write-Host ''
    throw "shell encoding is wrong in $($Mistyped.Count) file(s)"
}

$Refusals = @()
$Refusals += Test-Acyclic $Declared

# 🔴 Every include of the form "SlateX/..." is a unit reference. Foundation/ and Shared/ are reachable from
#    everywhere by declaration — they are the shared seam, not a unit — and a vendored header is neither.
foreach ($UnitName in ($Declared.Keys | Sort-Object))
{
    $Entry     = $Declared[$UnitName]
    $Reachable = Resolve-Reachable $Declared $UnitName
    $Sources   = @(Get-ChildItem $Entry.Root -Recurse -File -Include '*.h', '*.cpp' -ErrorAction SilentlyContinue)

    foreach ($Source in $Sources)
    {
        $Index = 0

        foreach ($Line in [System.IO.File]::ReadAllLines($Source.FullName))
        {
            ++$Index

            if ($Line -notmatch '^\s*#include\s+"([^"]+)"')
            {
                continue
            }

            $Included = $Matches[1]
            $Relative = $Source.FullName.Substring($RepositoryRoot.Length + 1)

            if ($Included -match '^(Slate[A-Za-z]+)/')
            {
                $Named = $Matches[1]

                if ($Named -eq $UnitName)                { continue }
                if ($Reachable.ContainsKey($Named))      { continue }

                $Refusals += "$Relative($Index): $UnitName includes $Named, which it does not require"
                continue
            }

            # 🔴 `00` §2.2: exactly one copy of ImGui exists and SlateUI owns it. A host that includes
            #    imgui.h is a defect regardless of whether it links.
            if ($Included -match '^(imgui|backends/imgui)' -and $UnitName -ne 'SlateUI')
            {
                $Refusals += "$Relative($Index): $UnitName names an ImGui header; only SlateUI may"
                continue
            }

            # 🔴 `10` §1: the vendored readers are compiled into SlateDocument's codecs and nowhere else,
            #    so exactly one copy of each implementation exists in the process.
            if ($Included -match '^(stb|fast_obj|cgltf|ufbx)' -and $UnitName -ne 'SlateDocument')
            {
                $Refusals += "$Relative($Index): $UnitName names a vendored reader; only SlateDocument may"
            }
        }
    }

    if ($Detail)
    {
        $Reaches = if ($Reachable.Count -gt 0) { ($Reachable.Keys | Sort-Object) -join ', ' } else { 'nothing' }
        Write-Noted "$UnitName reaches $Reaches across $($Sources.Count) files"
    }
}

Write-Host ''

if ($Refusals.Count -gt 0)
{
    foreach ($Refusal in $Refusals)
    {
        Write-Broken $Refusal
    }

    Write-Host ''
    throw "the dependency partition is broken in $($Refusals.Count) place(s)"
}

Write-Held "the dependency partition holds across $($Declared.Count) units"
