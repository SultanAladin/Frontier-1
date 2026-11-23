#============================================================================================================================================
#                                                         RUNSYMBOLINDEXFLATTEN.PS1
#============================================================================================================================================
# 🧩 Invokes Scripts/RunSymbolIndexFlatten.py to mirror every Engine .symbolindex into the flat SymboLindex folder.

$ScriptPath = Join-Path $PSScriptRoot "RunSymbolIndexFlatten.py"
& python $ScriptPath @args
exit $LASTEXITCODE
