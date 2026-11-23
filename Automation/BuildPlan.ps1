# ============================================================================
#  BuildPlan.ps1 - Shared incremental build planner for every Frontier target
#  (each Internal\<pillar> and Executables\<target> Build.bat drives this ONE file).
#
#  Reads the build manifest from environment variables and emits a pipe-delimited
#  plan on stdout that the calling Build.bat parses:
#
#      SKIP|<name>|<reason>          object is up to date, do not recompile
#      COMPILE|<name>|<fullsource>   source must be (re)compiled
#      LINK|YES                      output artifact (.lib / .exe) must be rebuilt
#      LINK|NO                       output artifact is up to date, skip archive/link
#
#  Decision rules (identical gate for lib archive and exe link):
#    - Own source    -> recompile if its .obj is missing, the source is newer than
#                       the .obj, or (per-file gate) any header THIS unit actually
#                       included is newer than the .obj. The included-header list is
#                       read from obj\<Name>.json, which cl emits via /sourceDependencies
#                       on each compile. A unit with no JSON yet (first build / newly
#                       added source) falls back to the whole-tree newest-header gate
#                       (HEADER_ROOTS), so the plan self-seeds and never under-builds.
#    - Vendored src  -> recompile only if its .obj is missing (stable, never edited).
#    - Output        -> rebuilt if anything recompiled, the artifact is missing, or any
#                       .obj is newer than the artifact.
#
#  Required environment:
#      OBJ           obj output directory for THIS target
#      OUTPUT        full path to the artifact this target emits (.lib or .exe)
#      OWN_SRCS      ';'-separated full source paths that gate on the header graph
#      VENDOR_SRCS   ';'-separated vendored source paths (missing-obj gate only)  [optional]
#      HEADER_ROOTS  ';'-separated roots scanned recursively for the fallback header gate
#      SRC_LIST_FILE path to a newline-delimited file of own-source paths          [optional]
#                    Used INSTEAD of / IN ADDITION TO OWN_SRCS to sidestep cmd's
#                    8191-char environment-variable cap: a large pillar (EngineContext
#                    at ~80 .cpp overflows OWN_SRCS and silently drops its tail units,
#                    which then never compile). When set, its lines are unioned with
#                    OWN_SRCS so no source is lost regardless of list length.
# ============================================================================
$ErrorActionPreference = "Stop"

$ObjDir      = $env:OBJ
$Output      = $env:OUTPUT
$OwnSrcs     = @($env:OWN_SRCS    -split ';' | Where-Object { $_ })
$VendorSrcs  = @($env:VENDOR_SRCS -split ';' | Where-Object { $_ })
$HeaderRoots = @($env:HEADER_ROOTS -split ';' | Where-Object { $_ })

# File-backed source list (no length cap). Union with OWN_SRCS, de-duplicated so a
# path listed both ways is planned once. This is the primary path for large pillars.
if ($env:SRC_LIST_FILE -and (Test-Path -LiteralPath $env:SRC_LIST_FILE))
{
    $FileSrcs = @(Get-Content -LiteralPath $env:SRC_LIST_FILE | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $OwnSrcs  = @($OwnSrcs + $FileSrcs | Select-Object -Unique)
}

function Object-Path([string]$Source)
{
    Join-Path $ObjDir ([System.IO.Path]::GetFileNameWithoutExtension($Source) + ".obj")
}

function Depend-Path([string]$Source)
{
    Join-Path $ObjDir ([System.IO.Path]::GetFileNameWithoutExtension($Source) + ".json")
}

function Write-Time([string]$Path)
{
    if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).LastWriteTimeUtc }
    else { $null }
}

# Newest LastWriteTime among the headers THIS unit actually included, read from its
# /sourceDependencies JSON. Returns $null when the JSON is absent / unreadable so the
# caller falls back to the whole-tree header gate.
function Depend-HeaderMax([string]$DependFile)
{
    if (-not (Test-Path -LiteralPath $DependFile)) { return $null }
    try
    {
        $Json = Get-Content -LiteralPath $DependFile -Raw | ConvertFrom-Json
    }
    catch
    {
        return $null
    }
    $Includes = $Json.Data.Includes
    if ($null -eq $Includes) { return $null }
    $Newest = $null
    foreach ($Include in $Includes)
    {
        $Time = Write-Time $Include
        if ($null -ne $Time -and ($null -eq $Newest -or $Time -gt $Newest))
        {
            $Newest = $Time
        }
    }
    return $Newest
}

# Newest header anywhere under the source roots — the conservative fallback gate used
# only for a unit that has no dependency JSON yet (first build / newly added source).
$HeaderMax = $null
foreach ($Root in $HeaderRoots)
{
    if (-not (Test-Path -LiteralPath $Root)) { continue }
    $Headers = Get-ChildItem -LiteralPath $Root -Filter *.h -File -Recurse -ErrorAction SilentlyContinue
    foreach ($Header in $Headers)
    {
        if ($null -eq $HeaderMax -or $Header.LastWriteTimeUtc -gt $HeaderMax)
        {
            $HeaderMax = $Header.LastWriteTimeUtc
        }
    }
}

$CompileAny  = $false
$ObjectPaths = @()

foreach ($Source in $OwnSrcs)
{
    $Name   = [System.IO.Path]::GetFileNameWithoutExtension($Source)
    $Object = Object-Path $Source
    $ObjectPaths += $Object
    $ObjTime = Write-Time $Object

    $Rebuild = $false
    if ($null -eq $ObjTime)
    {
        $Rebuild = $true
    }
    elseif ((Get-Item -LiteralPath $Source).LastWriteTimeUtc -gt $ObjTime)
    {
        $Rebuild = $true
    }
    else
    {
        $DependMax = Depend-HeaderMax (Depend-Path $Source)
        if ($null -ne $DependMax)
        {
            if ($DependMax -gt $ObjTime) { $Rebuild = $true }
        }
        elseif ($null -ne $HeaderMax -and $HeaderMax -gt $ObjTime)
        {
            $Rebuild = $true
        }
    }

    if ($Rebuild)
    {
        $CompileAny = $true
        "COMPILE|$Name|$Source"
    }
    else
    {
        "SKIP|$Name|up to date"
    }
}

foreach ($Source in $VendorSrcs)
{
    $Name   = [System.IO.Path]::GetFileNameWithoutExtension($Source)
    $Object = Object-Path $Source
    $ObjectPaths += $Object

    if (Test-Path -LiteralPath $Object)
    {
        "SKIP|$Name|vendored, already built"
    }
    else
    {
        $CompileAny = $true
        "COMPILE|$Name|$Source"
    }
}

# --- Output (archive / link) decision ---------------------------------------
$LinkNeeded = $CompileAny
if (-not $LinkNeeded)
{
    $OutTime = Write-Time $Output
    if ($null -eq $OutTime)
    {
        $LinkNeeded = $true
    }
    else
    {
        foreach ($Object in $ObjectPaths)
        {
            $ObjTime = Write-Time $Object
            if ($null -eq $ObjTime -or $ObjTime -gt $OutTime)
            {
                $LinkNeeded = $true
                break
            }
        }
    }
}

if ($LinkNeeded) { "LINK|YES" } else { "LINK|NO" }
