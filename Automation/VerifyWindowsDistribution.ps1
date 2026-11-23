#============================================================================================================================================
#                                                   VERIFYWINDOWSDISTRIBUTION.PS1
#============================================================================================================================================
# 🧩 Record publisher signatures and local Defender results; unsigned previews require explicit authorization.

param([Parameter(Mandatory=$true)][string]$BinaryDirectory,
      [Parameter(Mandatory=$true)][string]$ReportPath,
      [switch]$AllowUnsignedPreview)
$ErrorActionPreference = 'Stop'
$Signatures = @()
foreach ($Application in @('Frontier', 'TextureWorkspace'))
{
    $Filename = Join-Path $BinaryDirectory ($Application + '.exe')
    $Signature = Get-AuthenticodeSignature -LiteralPath $Filename
    $Signatures += [ordered]@{
        application = $Application
        sha256 = (Get-FileHash -LiteralPath $Filename -Algorithm SHA256).Hash.ToLowerInvariant()
        status = $Signature.Status.ToString()
        subject = if ($Signature.SignerCertificate) { $Signature.SignerCertificate.Subject } else { $null }
    }
}
$Scanner = [ordered]@{ status = 'unavailable'; clean = $false; detail = ''; signature_updated = $null }
try
{
    $Protection = Get-MpComputerStatus -ErrorAction Stop
    $Scanner.signature_updated = $Protection.AntivirusSignatureLastUpdated.ToUniversalTime().ToString('o')
    if (-not $Protection.AntivirusEnabled) { throw 'Defender antivirus is not active.' }
    if ($Protection.AntivirusSignatureLastUpdated -lt (Get-Date).AddDays(-7)) { throw 'Defender definitions exceed seven days.' }
    $Candidates = @(Get-ChildItem "$env:ProgramData/Microsoft/Windows Defender/Platform/*/MpCmdRun.exe" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
    $Executable = if ($Candidates.Count) { $Candidates[0].FullName } else { "$env:ProgramFiles/Windows Defender/MpCmdRun.exe" }
    if (-not (Test-Path -LiteralPath $Executable)) { throw 'Defender command-line scanner is missing.' }
    $ScanOutput = & $Executable -Scan -ScanType 3 -File (Resolve-Path $BinaryDirectory).Path -DisableRemediation 2>&1
    $ScanExitCode = $LASTEXITCODE
    $Scanner.detail = ($ScanOutput | Out-String)
    $Scanner.status = if ($ScanExitCode -eq 0) { 'passed' } else { 'failed' }
    $Scanner.clean = $ScanExitCode -eq 0
}
catch
{
    $Scanner.detail = $_.Exception.Message
}
$AcceptedStatuses = if ($AllowUnsignedPreview) { @('Valid', 'NotSigned') } else { @('Valid') }
$SignaturesAccepted = @($Signatures | Where-Object { $_.status -notin $AcceptedStatuses }).Count -eq 0
$Ready = $SignaturesAccepted -and $Scanner.clean
$Report = [ordered]@{
    distribution_ready = $Ready
    unsigned_preview_authorized = [bool]$AllowUnsignedPreview
    signatures = $Signatures
    defender = $Scanner
    chrome_warning_cleared = $false
    explanation = 'Signatures and a clean scan do not guarantee browser reputation. No self-signing or protection bypass is used.'
}
$Report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding utf8
Write-Output ($Report | ConvertTo-Json -Depth 8)
if ($env:GITHUB_OUTPUT)
{
    "unsigned_preview_authorized=$(([bool]$AllowUnsignedPreview).ToString().ToLowerInvariant())" | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8
    "distribution_ready=$($Ready.ToString().ToLowerInvariant())" | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8
}
$SignatureSummary = ($Signatures | ForEach-Object { "$($_.application)=$($_.status)" }) -join '; '
Write-Output "::notice title=Distribution verification::$SignatureSummary; Defender=$($Scanner.status); ready=$Ready"
if ($env:GITHUB_STEP_SUMMARY)
{
    @(
        '## Windows distribution verification',
        '',
        '| Check | Result |',
        '|---|---|',
        "| Publisher signatures | $SignatureSummary |",
        "| Defender scan | $($Scanner.status) |",
        "| Definitions updated (UTC) | $($Scanner.signature_updated) |",
        "| Distribution ready | $Ready |",
        "| Unsigned preview authorized | $([bool]$AllowUnsignedPreview) |",
        '',
        'The full report is in Windows-build-diagnostics/DistributionChecks.json.',
        'These checks do not certify Chrome reputation; do not bypass a download block.'
    ) | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
}
if (-not $Ready)
{
    Write-Output '::warning::Binary downloads withheld: accepted signatures and a current clean antivirus scan are required; unsigned previews need explicit authorization.'
}
if ($Scanner.status -eq 'failed')
{
    throw 'Antivirus verification failed; binary distribution is blocked. Inspect DistributionChecks.json.'
}
