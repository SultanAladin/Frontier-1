[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Python = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $Python) { throw 'Python 3 is required to download Slate fonts.' }
& $Python.Source (Join-Path $PSScriptRoot 'DownloadFonts.py')
if ($LASTEXITCODE -ne 0) { throw "font download failed with exit code $LASTEXITCODE" }
