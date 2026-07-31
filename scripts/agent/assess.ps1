$ErrorActionPreference = "Stop"

$ProjectRootPath = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Workflow = Join-Path $ProjectRootPath "tools/agent_workflow.py"
$Python = Get-Command python3 -ErrorAction SilentlyContinue
if (-not $Python) {
    $Python = Get-Command python -ErrorAction SilentlyContinue
}
if ($Python) {
    & $Python.Source $Workflow assess @args
    exit $LASTEXITCODE
}
$Launcher = Get-Command py -ErrorAction SilentlyContinue
if ($Launcher) {
    & $Launcher.Source -3 $Workflow assess @args
    exit $LASTEXITCODE
}
Write-Error "Python 3 is required for the read-only route assessment."
exit 2
