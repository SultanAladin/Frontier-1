# BuildThorVG.ps1 — compiles ThorVG as a static library using MSVC directly (cl.exe and lib.exe).
#                   Requires no Python, Meson, or Ninja.
#
#     powershell -File Scripts\BuildThorVG.ps1

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$PackageRoot    = Join-Path $RepositoryRoot 'ExternalPackages'
$ThorVGRoot     = Join-Path $PackageRoot    'thorvg'
$BuildDir       = Join-Path $ThorVGRoot     "_build\$Configuration"
$OutputDir      = Join-Path $ThorVGRoot     'lib'

function Write-Report([string] $Tag, [System.ConsoleColor] $Colour, [string] $Message)
{
    Write-Host ("[$Tag]".PadRight(10)) -ForegroundColor $Colour -NoNewline
    Write-Host " $Message"
}

function Write-Building([string] $Message) { Write-Report 'Build'    DarkGray $Message }
function Write-Produced([string] $Message) { Write-Report 'Compiled' Green    $Message }

#---
#                                       TOOLCHAIN
#---

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
{
    $Candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
    )

    $Selected = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($null -eq $Selected)
    {
        throw 'no vcvarsall.bat was found; cl.exe is not on PATH'
    }

    $Captured = cmd.exe /c "`"$Selected`" x64 > nul & set"
    foreach ($Line in $Captured)
    {
        if ($Line -match '^([^=]+)=(.*)$')
        {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }
}

if (-not (Test-Path $ThorVGRoot))
{
    throw "ThorVG submodule is absent at $ThorVGRoot; run: git submodule update --init ExternalPackages/thorvg"
}

if (-not (Test-Path $BuildDir))
{
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}

if (-not (Test-Path $OutputDir))
{
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

#---
#                                   CONFIG.H GENERATION
#---

$ConfigHPath = Join-Path $BuildDir 'config.h'
$ConfigHContent = @"
#pragma once

#define THORVG_VERSION_STRING "1.0.0"
#define THORVG_CPU_ENGINE_SUPPORT 1
#define THORVG_SVG_LOADER_SUPPORT 1
#define THORVG_THREAD_SUPPORT 1
#define THORVG_FILE_IO_SUPPORT 1
#define WIN32_LEAN_AND_MEAN 1
"@

[System.IO.File]::WriteAllText($ConfigHPath, $ConfigHContent, [System.Text.Encoding]::UTF8)

#---
#                                       SOURCES
#---

$Sources = @(
    # common
    (Join-Path $ThorVGRoot 'src\common\tvgCompressor.cpp')
    (Join-Path $ThorVGRoot 'src\common\tvgMath.cpp')
    (Join-Path $ThorVGRoot 'src\common\tvgStr.cpp')

    # renderer
    (Join-Path $ThorVGRoot 'src\renderer\tvgAccessor.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgAnimation.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgCanvas.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgFill.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgInitializer.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgLoaderMgr.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgPaint.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgPicture.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgRender.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgSaver.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgScene.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgShape.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgTaskScheduler.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\tvgText.cpp')

    # cpu_engine
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwBlendOp.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwFill.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwImage.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwMemPool.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwPostEffect.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwRaster.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwRenderer.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwRle.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwShape.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwStroke.cpp')
    (Join-Path $ThorVGRoot 'src\renderer\cpu_engine\tvgSwUtil.cpp')

    # loaders
    (Join-Path $ThorVGRoot 'src\loaders\raw\tvgRawLoader.cpp')
    (Join-Path $ThorVGRoot 'src\loaders\svg\tvgSvgBuilder.cpp')
    (Join-Path $ThorVGRoot 'src\loaders\svg\tvgSvgCssStyle.cpp')
    (Join-Path $ThorVGRoot 'src\loaders\svg\tvgSvgLoader.cpp')
    (Join-Path $ThorVGRoot 'src\loaders\svg\tvgSvgPath.cpp')
    (Join-Path $ThorVGRoot 'src\loaders\svg\tvgSvgUtil.cpp')
    (Join-Path $ThorVGRoot 'src\loaders\svg\tvgXmlParser.cpp')
)

$IncludePaths = @(
    "/I$ThorVGRoot\inc"
    "/I$BuildDir"
    "/I$ThorVGRoot\src\common"
    "/I$ThorVGRoot\src\renderer"
    "/I$ThorVGRoot\src\renderer\cpu_engine"
    "/I$ThorVGRoot\src\loaders\svg"
    "/I$ThorVGRoot\src\loaders\raw"
)

$Flags = @(
    '/nologo'
    '/c'
    '/EHsc'
    '/MD'
    '/std:c++20'
    '/permissive-'
    '/W3'
    '/utf-8'
    '/DTVG_STATIC'
    '/DTVG_BUILD'
    '/DNOMINMAX'
    '/DWIN32_LEAN_AND_MEAN'
)

if ($Configuration -eq 'Debug')
{
    $Flags += @('/Od', '/Zi', '/DSLATE_DEBUG=1')
}
else
{
    $Flags += @('/O2', '/DNDEBUG')
}

#---
#                                       COMPILATION
#---

Write-Building "ThorVG - compiling $($Sources.Count) translation units ($Configuration)"

$ObjectFiles = @()

foreach ($Src in $Sources)
{
    $Stem = [System.IO.Path]::GetFileNameWithoutExtension($Src)
    $Obj = Join-Path $BuildDir "$Stem.obj"
    $ObjectFiles += $Obj

    if ((Test-Path $Obj) -and ((Get-Item $Obj).LastWriteTimeUtc -gt (Get-Item $Src).LastWriteTimeUtc) -and ((Get-Item $Obj).LastWriteTimeUtc -gt (Get-Item $ConfigHPath).LastWriteTimeUtc))
    {
        continue
    }

    $Args = $Flags + $IncludePaths + @("/Fo$Obj", $Src)
    $Diagnostics = & cl.exe @Args
    if ($LASTEXITCODE -ne 0)
    {
        $Diagnostics | ForEach-Object { Write-Host "    $_" }
        throw "cl.exe failed for $Stem"
    }
}

#---
#                                       ARCHIVING
#---

Write-Building 'ThorVG - archiving thorvg.lib'

$Destination = Join-Path $OutputDir 'thorvg.lib'
$LibArgs = @('/nologo', "/OUT:$Destination") + $ObjectFiles

$LibOutput = & lib.exe @LibArgs
if ($LASTEXITCODE -ne 0)
{
    $LibOutput | ForEach-Object { Write-Host "    $_" }
    throw 'lib.exe failed for thorvg.lib'
}

Write-Produced $Destination
