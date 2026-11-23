#============================================================================================================================================
#                                                           RUNIMPORTFLATTEN.PS1
#============================================================================================================================================
# 🧩 Invokes Scripts/RunImportFlatten.py to flatten nested sources in Import/.

$ScriptPath = Join-Path $PSScriptRoot "RunImportFlatten.py"
& python $ScriptPath @args
exit $LASTEXITCODE
