#============================================================================================================================================
#                                                           INVOKE-SYMBOLINDEX.PS1
#============================================================================================================================================
# 🧩 Invokes the Slate symbol indexer tool (Tools/SymbolIndex.py) across the repository.

#------------------------------------------------------------------------------------------------------------------------
#                                                        SYMBOL INDEX EXECUTION
#------------------------------------------------------------------------------------------------------------------------

function Invoke-SymbolIndex {
    [CmdletBinding()]
    param(
        [Parameter(Position = 0)]
        [string]$Command = "build",

        [Parameter(Position = 1)]
        [string]$Target = "",

        [string]$Root = "Engine",

        [switch]$NoCallSites,

        [switch]$All
    )

    # 🔴 The repository root is derived from this script's own location, never from a machine-specific
    #    absolute path. The earlier hardcoded C:\Users\OS\... resolved on exactly one machine; every other
    #    checkout reported "SymbolIndex.py not found" and generated nothing.
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
    $ToolsPath      = Join-Path $RepositoryRoot 'Tools\SymbolIndex.py'

    if (-not (Test-Path -Path $ToolsPath)) {
        Write-Error " SymbolIndex.py not found at $ToolsPath"
        return
    }

    # 📝 `--root Engine` is resolved by the tool against the working folder, so a run from anywhere but the
    #    repository root indexed nothing and still exited 0. Made absolute here instead.
    if (-not [System.IO.Path]::IsPathRooted($Root)) {
        $Root = Join-Path $RepositoryRoot $Root
    }

    $ArgumentList = @($ToolsPath, "--root", $Root)

    if ($NoCallSites) {
        $ArgumentList += "--no-call-sites"
    }

    $ArgumentList += $Command

    if ($Target) {
        $ArgumentList += $Target
    }

    if ($All) {
        $ArgumentList += "--all"
    }

    & python $ArgumentList
}

Invoke-SymbolIndex @args
