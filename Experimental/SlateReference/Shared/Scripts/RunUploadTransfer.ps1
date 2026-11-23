#============================================================================================================================================
#                                                           RUNUPLOADTRANSFER.PS1
#============================================================================================================================================
# 🧩 Invokes Scripts/RunUploadTransfer.py to reclaim Upload, transfer sources (.h, .cpp, .slang.md), and write Engine-File-Structure.md.

$ScriptPath = Join-Path $PSScriptRoot "RunUploadTransfer.py"
& python $ScriptPath @args
exit $LASTEXITCODE
