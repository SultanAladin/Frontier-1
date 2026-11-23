# ApplyImGuiPatches.ps1 — applies Slate's tab-shape patches to the vendored ImGui submodule.
#
# 🔴 `ExternalPackages/` is never edited by hand. The two patches below are the whole of Slate's
#    divergence from upstream ImGui, they are tracked in `Patches/`, and this script is the only thing
#    that applies them. A silently adjusted vendored dependency is a defect that reproduces on one
#    machine only — `14` §2 — so the divergence is a file a reader can open rather than a local edit.
#
# 🔴 Both patches default every member they add to 0.0f. An unpatched build and a patched build with
#    default style emit the same command stream, so applying these changes nothing until Slate opts in.
#
#     powershell -File Scripts\ApplyImGuiPatches.ps1
#     powershell -File Scripts\ApplyImGuiPatches.ps1 -Revert
#     powershell -File Scripts\ApplyImGuiPatches.ps1 -Verify

[CmdletBinding()]
param(
    [switch] $Revert,
    [switch] $Verify
)

$ErrorActionPreference = 'Stop'

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$ImGuiRoot      = Join-Path $RepositoryRoot 'ExternalPackages\imgui'
$PatchRoot      = Join-Path $RepositoryRoot 'Patches'

# 📝 The order is the stacking order. B's context includes A's lines, so B cannot apply first.
# 🔴 Each patch is detected by a sentinel it introduces, NOT by `git apply --reverse --check`. Because B
#    edits lines that sit inside A's own context, once B is applied A no longer reverse-checks — so a
#    reverse-check would report A as absent on a fully patched tree and the script would try to apply it
#    again, aborting a build whose tree was perfectly healthy. A sentinel is stable under stacking.
$Declared = @(
    @{ Name = 'PatchA-TrapezoidalTabs.patch';  Sentinel = 'SLATE PATCH A'; Witness = 'imgui_widgets.cpp' }
    @{ Name = 'PatchB-TabOverlapZOrder.patch'; Sentinel = 'SLATE PATCH B'; Witness = 'imgui_widgets.cpp' }
    @{ Name = 'PatchC-RoundTabButtons.patch';  Sentinel = 'SLATE PATCH C'; Witness = 'imgui_widgets.cpp' }
)

# 🔴 The commit these patches were written against. `git apply` would fail loudly on a different tree,
#    but it fails with three rejected hunks rather than with the one sentence a reader can act on.
$ExpectedCommit = '83f668625ad45364de71d385aeb6a5dd04bee02e'

function Write-Report([string] $Tag, [System.ConsoleColor] $Colour, [string] $Message)
{
    Write-Host ("[$Tag]".PadRight(10)) -ForegroundColor $Colour -NoNewline
    Write-Host " $Message"
}

function Write-Applied([string] $Message) { Write-Report 'ImGui'    Green    $Message }
function Write-Skipped([string] $Message) { Write-Report 'SKIP'     Cyan     $Message }
function Write-Rejected([string] $Message) { Write-Report 'FAILED'   Red      $Message }
function Write-Noted([string]   $Message) { Write-Report 'Patch'    DarkGray $Message }

if (-not (Test-Path (Join-Path $ImGuiRoot 'imgui.cpp')))
{
    throw "the ImGui submodule is not checked out at $ImGuiRoot; run: git submodule update --init --recursive"
}

Push-Location $ImGuiRoot

try
{
    # 📝 The pin is reported rather than enforced. A deliberate ImGui upgrade should reach a message
    #    naming both commits, not an assertion that reads as a broken checkout.
    $Current = (& git rev-parse HEAD).Trim()

    if ($Current -ne $ExpectedCommit)
    {
        $Short   = $Current.Substring(0, 7)
        $Against = $ExpectedCommit.Substring(0, 7)
        Write-Noted "submodule stands at $Short; patches were cut against $Against"
        Write-Noted 'if ImGui was upgraded deliberately, re-cut the patches against the new commit'
    }

    # 🔴 Reverting runs the stack backwards. B sits on top of A, so reverting A first would leave B's
    #    hunks referring to context that no longer exists.
    $Ordered = if ($Revert) { $Declared[($Declared.Count - 1)..0] } else { $Declared }

    foreach ($Entry in $Ordered)
    {
        $PatchName = $Entry.Name
        $PatchPath = Join-Path $PatchRoot $PatchName

        if (-not (Test-Path $PatchPath))
        {
            throw "declared patch $PatchName is absent from $PatchRoot"
        }

        $WitnessPath    = Join-Path $ImGuiRoot $Entry.Witness
        $AlreadyApplied = (Select-String -Path $WitnessPath -Pattern $Entry.Sentinel -SimpleMatch -Quiet) -eq $true

        if ($Verify)
        {
            if ($AlreadyApplied) { Write-Applied "$PatchName is applied" }
            else                 { Write-Noted   "$PatchName is NOT applied" }
            continue
        }

        if ($Revert)
        {
            if (-not $AlreadyApplied)
            {
                Write-Skipped "$PatchName was not applied"
                continue
            }

            & git apply --reverse $PatchPath

            if ($LASTEXITCODE -ne 0)
            {
                Write-Rejected "could not revert $PatchName"
                throw "git apply --reverse rejected $PatchName"
            }

            Write-Applied "reverted $PatchName"
            continue
        }

        if ($AlreadyApplied)
        {
            Write-Skipped "$PatchName already applied"
            continue
        }

        & git apply --check $PatchPath

        if ($LASTEXITCODE -ne 0)
        {
            Write-Rejected "$PatchName does not apply to the standing ImGui tree"
            throw "git apply --check rejected $PatchName"
        }

        & git apply $PatchPath

        if ($LASTEXITCODE -ne 0)
        {
            Write-Rejected "could not apply $PatchName"
            throw "git apply rejected $PatchName"
        }

        Write-Applied "applied $PatchName"
    }
}
finally
{
    Pop-Location
}
