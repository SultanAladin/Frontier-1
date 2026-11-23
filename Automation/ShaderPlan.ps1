# ==============================================================================================================================================
#                                                             SHADERPLAN.PS1
# ==============================================================================================================================================
# 🧩 Compile every GLSL shader in the tree to SPIR-V, skipping the ones already newer than their source. The pillar Build.bat files only ever
#    COPIED *.spv beside the exe — nothing compiled them — so editing a .frag silently shipped the previous binary. That failure is invisible at
#    build time and surfaces much later as a shader whose bindings disagree with the C++ that feeds it. This is the missing step.
#
# 💡 WHY A SHARED SCRIPT AND NOT A PER-TARGET LOOP. Five different Build.bat files stage the same shader dirs, so a per-target compile would
#    duplicate the rule five ways and drift. Shaders are also target-independent: the same .spv is staged by every consumer. So the compile belongs
#    once, here, keyed on the SOURCE tree, and the Build.bat files keep doing only what they are good at — copying the result into place.
#
# ⚠️ TARGET ENV IS PER-SHADER, NOT GLOBAL. Most of this tree is SPIR-V 1.0; the device runs Vulkan 1.2 semantics and REJECTS a 1.6 binary outright
#    ("Invalid SPIR-V binary version 1.6 for target environment SPIR-V 1.5"). But the software-raster pair genuinely needs subgroup + int64-atomic
#    capabilities that vulkan1.0 cannot express, so those compile at vulkan1.2. Defaulting everything to the newest env is exactly the mistake that
#    produced an unloadable ComponentOverlay.frag; defaulting everything to 1.0 would break the software raster instead. Hence the explicit table.
#
# 📝 TWO-VARIANT SOURCES. SoftwareRasterization.comp and SoftwareResolve.frag are each compiled TWICE, once per packed-target route
#    (-DPACKED_ROUTE_IMAGE=1 / -DPACKED_ROUTE_BUFFER=1), because the host picks a route at runtime and loads a differently-named module for each
#    (see SoftwareRasterization.cpp:127 / :205). Their output names therefore do NOT follow the <source>.spv rule — a compile-everything-by-name
#    script would emit SoftwareResolve.frag.spv, which nothing loads, and leave the two real modules stale forever.
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File Automation\ShaderPlan.ps1 [-Force] [-Quiet]
# Exit:   0 = every shader up to date or compiled cleanly; 1 = at least one compile failed (message on stderr).

[CmdletBinding()]
param(
    [switch] $Force,   # recompile even when the .spv is newer than its source
    [switch] $Quiet    # print only failures and the summary
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

# --- Locate glslc -----------------------------------------------------------------------------------------------------------------------------
# Prefer the SDK the build already targets (VULKAN_SDK), fall back to PATH, then to the pinned SDK the pillars hardcode.
$GlslcCandidates = @()
if ($env:VULKAN_SDK) { $GlslcCandidates += (Join-Path $env:VULKAN_SDK 'Bin\glslc.exe') }
$OnPath = (Get-Command glslc -ErrorAction SilentlyContinue)
if ($OnPath) { $GlslcCandidates += $OnPath.Source }
$GlslcCandidates += 'C:\VulkanSDK\1.4.335.0\Bin\glslc.exe'

$Glslc = $GlslcCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Glslc)
{
    [Console]::Error.WriteLine('[shaders] glslc.exe not found - set VULKAN_SDK or put glslc on PATH.')
    exit 1
}

# --- Include-dependency parsing ---------------------------------------------------------------------------------------------------------------
# 📝 Reads a glslc -MD depfile and returns the files the shader INCLUDES (the prerequisites after the colon, minus the shader itself, which the
#    caller already timestamps). Make syntax: `<target>: <prereq> <prereq> ...`, backslash-newline continuations, spaces inside a path escaped as
#    `\ `. Callers get absolute paths.
# 🔴 THE COLON SPLIT CANNOT BE A BARE -split ':' — every path here is a Windows path with a drive letter, so a naive split shatters `C:\...` at its
#    own colon and yields garbage prerequisites that all fail Test-Path, which (by the deleted-dependency rule) would force a recompile of every
#    shader on every run. Only the FIRST colon that is not a drive-letter colon separates target from prerequisites.
function Read-ShaderDependencies
{
    param([string]$DependencyPath)

    $Text = (Get-Content -Path $DependencyPath -Raw) -replace '\\\r?\n', ' '

    # Split off the target: skip a leading drive letter, then take everything after the next colon.
    $Match = [regex]::Match($Text, '^\s*(?:[A-Za-z]:)?[^:]*:\s*(?<Prerequisites>.*)$', 'Singleline')
    if (-not $Match.Success) { return @() }

    # Tokenize on unescaped whitespace, then unescape `\ ` back to a plain space.
    $Tokens = [regex]::Split($Match.Groups['Prerequisites'].Value.Trim(), '(?<!\\)\s+')

    $Results = @()
    foreach ($Token in $Tokens)
    {
        $Path = $Token -replace '\\ ', ' '
        if ([string]::IsNullOrWhiteSpace($Path)) { continue }

        # ⚠️ glslc mixes separators in its own output (`.../Shaders/ShadowTileStore.glsl` under a backslash prefix), so normalize before comparing
        #    against the source path or the self-filter below silently fails to match and the shader times itself twice. Harmless, but misleading.
        $Path = $Path.Replace('/', '\')
        $Results += $Path
    }
    return $Results
}

# --- The per-shader target environment --------------------------------------------------------------------------------------------------------
# 📝 Keyed by source FILE NAME (not path): these names are unique across the tree, and keying on the name keeps the table readable. Anything absent
#    from this table uses the tree default of vulkan1.0 — deliberately the conservative choice, so a new shader can only fail LOUDLY at compile
#    time (unsupported capability) rather than quietly producing a binary the driver refuses to load.
$DefaultTargetEnv = 'vulkan1.0'
$TargetEnvForShader = @{
    # Subgroup ballot/shuffle + 64-bit image atomics: not expressible under vulkan1.0.
    'SoftwareRasterization.comp' = 'vulkan1.2'
    'SoftwareResolve.frag'       = 'vulkan1.2'
}

# --- Sources compiled to MORE THAN ONE module -------------------------------------------------------------------------------------------------
# 📝 Each entry is one output: the .spv basename plus the extra -D flags that select the route. A source listed here does NOT also get a
#    <source>.spv — the host only ever loads these names.
$VariantForShader = @{
    'SoftwareRasterization.comp' = @(
        @{ Output = 'SoftwareRasterizationImage.comp.spv';  Defines = @('-DPACKED_ROUTE_IMAGE=1')  },
        @{ Output = 'SoftwareRasterizationBuffer.comp.spv'; Defines = @('-DPACKED_ROUTE_BUFFER=1') }
    )
    'SoftwareResolve.frag' = @(
        @{ Output = 'SoftwareResolveImage.frag.spv';  Defines = @('-DPACKED_ROUTE_IMAGE=1')  },
        @{ Output = 'SoftwareResolveBuffer.frag.spv'; Defines = @('-DPACKED_ROUTE_BUFFER=1') }
    )
}

# --- Collect the work -------------------------------------------------------------------------------------------------------------------------
$ShaderDirs = Get-ChildItem -Path (Join-Path $RepoRoot 'Internal') -Recurse -Directory -Filter 'Shaders'

# One -I per Shaders directory, so a cross-subsystem #include resolves. Built from the same list the jobs come from, so a new Shaders directory is on the
# include path the moment it holds a shader — nothing to register by hand.
$IncludeArguments = @($ShaderDirs | ForEach-Object { "-I$($_.FullName)" })

$Jobs = @()
foreach ($Dir in $ShaderDirs)
{
    foreach ($Source in Get-ChildItem -Path $Dir.FullName -File -Include '*.vert','*.frag','*.comp' -Recurse)
    {
        $TargetEnv = if ($TargetEnvForShader.ContainsKey($Source.Name)) { $TargetEnvForShader[$Source.Name] } else { $DefaultTargetEnv }
        $Outputs   = if ($VariantForShader.ContainsKey($Source.Name)) { $VariantForShader[$Source.Name] }
                     else { @(@{ Output = "$($Source.Name).spv"; Defines = @() }) }

        foreach ($Variant in $Outputs)
        {
            $Jobs += [pscustomobject]@{
                Source     = $Source.FullName
                Display    = $Source.FullName.Replace("$RepoRoot\", '')
                OutputPath = Join-Path $Source.DirectoryName $Variant.Output
                OutputName = $Variant.Output
                Defines    = $Variant.Defines
                TargetEnv  = $TargetEnv
            }
        }
    }
}

# --- Compile the stale ones -------------------------------------------------------------------------------------------------------------------
$Compiled = 0; $Skipped = 0; $Failed = @()
foreach ($Job in $Jobs)
{
    # 📝 Staleness is the output's mtime against the source AND every file the source #includes. The include graph is real now:
    #    Shadow/Shaders/*.{comp,vert,frag} all include ShadowTileStore.glsl.
    # 🔴 SOURCE-MTIME ALONE IS NOT ENOUGH ONCE AN INCLUDE EXISTS, and the failure is silent. Editing ShadowTileStore.glsl leaves every dependent
    #    .comp/.vert/.frag untouched, so a source-only gate calls them all up to date and the OLD .spv ships — carrying the previous bit values while
    #    the C++ side uses the new ones. That mismatch reads as a marking bug, not a build bug, which is the worst possible place to lose an hour.
    # 📝 The dependency list comes from glslc's own -MD (a make-style depfile written next to the OUTPUT, as <output>.d), so it tracks nested and
    #    future includes with no list to maintain here. It is written by the compile below, so the FIRST build of a shader has no depfile and simply
    #    compiles — which is correct, and the depfile it leaves behind guards every build after.
    $NewestSourceTime = (Get-Item $Job.Source).LastWriteTime
    $DependencyPath   = "$($Job.OutputPath).d"
    if (Test-Path $DependencyPath)
    {
        foreach ($Dependency in Read-ShaderDependencies -DependencyPath $DependencyPath)
        {
            # ⚠️ A dependency that has since been DELETED must force a recompile, not be skipped — glslc has to be the one to report the missing
            #    include, or the stale .spv would survive indefinitely with no diagnostic.
            if (-not (Test-Path $Dependency)) { $NewestSourceTime = Get-Date; continue }

            $DependencyTime = (Get-Item $Dependency).LastWriteTime
            if ($DependencyTime -gt $NewestSourceTime) { $NewestSourceTime = $DependencyTime }
        }
    }

    $UpToDate = (-not $Force) -and (Test-Path $Job.OutputPath) -and
                ((Get-Item $Job.OutputPath).LastWriteTime -ge $NewestSourceTime)
    if ($UpToDate) { $Skipped++; continue }

    # ⚠️ THREE SEPARATE PS 5.1 TRAPS, ALL OF WHICH SILENTLY BROKE THIS LOOP IN TURN — do not simplify any of them away:
    #    1. `2>&1` on a native exe wraps every stderr line in a NativeCommandError record. Combined with the script-level
    #       $ErrorActionPreference = 'Stop', that record is a TERMINATING error, so a failed glslc aborted the whole script right here — the backdate,
    #       the $Failed tally, and the summary never ran, and the caller saw a bare exception instead of "[shader failed] <name>". Hence the local
    #       -ErrorAction override via $ErrorActionPreference around the call.
    #    2. $LASTEXITCODE must be read on the VERY NEXT statement. Piping the output through ForEach-Object first resets it to the pipeline's own
    #       success, so the failure branch never fires and a broken shader is reported as compiled.
    #    3. glslc writes its diagnostics to stderr, so they must be captured explicitly or they vanish under a caller's 2>$null.
    # 📝 -MD writes the make-style depfile as <output>.d alongside the module, which is what the staleness gate above reads on the NEXT run. It is a
    #    side effect of the normal compile, so it costs nothing and can never disagree with what glslc actually included.
    # 📝 Every Shaders directory is on the include path, so a shared include body can live in the subsystem that OWNS it and still be consumed from
    #    another. SurfaceShade.frag including Shadow/Shaders/SunShadowTrace.glsl is the first such case: a quoted include resolves relative to the
    #    including file only, so without these it fails outright ("Cannot find or open include file"). ⚠️ A LOUD failure, which is why this is the fix
    #    rather than copying the body — but note that include names are now GLOBAL across subsystems, so two same-named .glsl files in different Shaders
    #    directories would resolve by search order rather than by intent. Nothing collides today; keep these names distinct.
    $Arguments = @("--target-env=$($Job.TargetEnv)", '-O', '-MD') + $IncludeArguments + $Job.Defines + @($Job.Source, '-o', $Job.OutputPath)

    $PreviousPreference   = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $Raw      = & $Glslc @Arguments 2>&1
    $ExitCode = $LASTEXITCODE
    $ErrorActionPreference = $PreviousPreference

    $Output = @($Raw | ForEach-Object { "$_" })
    if ($ExitCode -ne 0)
    {
        # ⚠️ DELETE IS NOT ENOUGH. glslc writes no output at all on failure, so the file left behind is the PREVIOUS GOOD build, not a truncated one —
        #    and it carries a newer timestamp than the source once the author reverts their edit. The next run would then call it up to date and skip
        #    it, so a shader that failed to compile ships silently as its old self. Backdating the survivor to the epoch makes the staleness gate fail
        #    it forever, so the error re-surfaces on every subsequent build until it actually compiles.
        if (Test-Path $Job.OutputPath) { (Get-Item $Job.OutputPath).LastWriteTime = [datetime]'1980-01-01' }
        $Failed += $Job.OutputName
        [Console]::Error.WriteLine("[shader failed] $($Job.OutputName)  ($($Job.Display))")
        if ($Output) { [Console]::Error.WriteLine(($Output -join [Environment]::NewLine).TrimEnd()) }
        continue
    }
    $Compiled++
    if (-not $Quiet) { Write-Host "[shader] $($Job.OutputName)  ($($Job.TargetEnv))" }
}

$Summary = "[shaders] $Compiled compiled, $Skipped up to date"
if ($Failed.Count -gt 0)
{
    [Console]::Error.WriteLine("$Summary, $($Failed.Count) FAILED: $($Failed -join ', ')")
    exit 1
}
Write-Host "$Summary -> $($Jobs.Count) modules"
exit 0
