# BuildGLFW.ps1 — builds GLFW from the submodule source and deposits the DLL import library
#                 and DLL into ExternalPackages/glfw/lib-vc2022/.
#
# Requirements:
#   - cmake.exe on PATH  (ships with Visual Studio 2022)
#   - MSVC toolchain     (imported by Construct.ps1 before this is called)
#
#     powershell -File Scripts\BuildGLFW.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$PackageRoot    = Join-Path $RepositoryRoot 'ExternalPackages'
$GlfwRoot       = Join-Path $PackageRoot    'glfw'
$BuildDir       = Join-Path $GlfwRoot       '_build'
$OutputDir      = Join-Path $GlfwRoot       'lib-vc2022'

function Write-Report([string] $Tag, [System.ConsoleColor] $Colour, [string] $Message)
{
    Write-Host ("[$Tag]".PadRight(10)) -ForegroundColor $Colour -NoNewline
    Write-Host " $Message"
}

function Write-Building([string] $Message) { Write-Report 'Build'    DarkGray $Message }
function Write-Produced([string] $Message) { Write-Report 'Compiled' Green    $Message }

#---
#                                       PREREQUISITES
#---

if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue))
{
    throw 'cmake.exe is not on PATH; add it via Visual Studio Installer (CMake tools component) or standalone.'
}

if (-not (Test-Path $GlfwRoot))
{
    throw "GLFW submodule is absent at $GlfwRoot; run: git submodule update --init ExternalPackages/glfw"
}

if (-not (Test-Path $OutputDir))
{
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

#---
#                                         CONFIGURE
#---

Write-Building "GLFW - cmake configure ($BuildDir)"

$ConfigArgs = @(
    '-S', $GlfwRoot
    '-B', $BuildDir
    '-G', 'Visual Studio 17 2022'
    '-A', 'x64'
    '-DBUILD_SHARED_LIBS=ON'
    '-DGLFW_BUILD_EXAMPLES=OFF'
    '-DGLFW_BUILD_TESTS=OFF'
    '-DGLFW_BUILD_DOCS=OFF'
    '-DGLFW_INSTALL=OFF'
    '--no-warn-unused-cli'
)

& cmake.exe @ConfigArgs
if ($LASTEXITCODE -ne 0)
{
    throw 'cmake configure failed for GLFW'
}

#---
#                                          BUILD
#---

Write-Building 'GLFW - cmake build (Release)'

$BuildArgs = @(
    '--build', $BuildDir
    '--config', 'Release'
    '--parallel'
)

& cmake.exe @BuildArgs
if ($LASTEXITCODE -ne 0)
{
    throw 'cmake build failed for GLFW'
}

#---
#                                         DEPOSIT
#---

# 📝 The Visual Studio generator places the DLL and import library under src/Release/.
#    Both names are what Construct.ps1 expects under lib-vc2022/.
$SrcDir = Join-Path $BuildDir 'src\Release'

$DllPath = Join-Path $SrcDir 'glfw3.dll'
$LibPath = Join-Path $SrcDir 'glfw3dll.lib'

if (-not (Test-Path $DllPath))
{
    throw "Expected glfw3.dll at $DllPath but it was not produced."
}

if (-not (Test-Path $LibPath))
{
    throw "Expected glfw3dll.lib at $LibPath but it was not produced."
}

Copy-Item $DllPath (Join-Path $OutputDir 'glfw3.dll')    -Force
Copy-Item $LibPath (Join-Path $OutputDir 'glfw3dll.lib') -Force

Write-Produced (Join-Path $OutputDir 'glfw3.dll')
Write-Produced (Join-Path $OutputDir 'glfw3dll.lib')
