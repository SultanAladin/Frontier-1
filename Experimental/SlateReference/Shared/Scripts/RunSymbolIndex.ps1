#============================================================================================================================================
#                                                           RUNSYMBOLINDEX.PS1
#============================================================================================================================================
# 🧩 Invokes Scripts/RunSymbolIndex.py to build or query symbol index files.

$ScriptPath = Join-Path $PSScriptRoot "RunSymbolIndex.py"
& python $ScriptPath @args
exit $LASTEXITCODE
