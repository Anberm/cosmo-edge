$ErrorActionPreference = "Stop"

$ProjectRootPath = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Workflow = Join-Path $ProjectRootPath "tools/agent_workflow.py"
$Python = Get-Command python3 -ErrorAction SilentlyContinue
if (-not $Python) {
    $Python = Get-Command python -ErrorAction SilentlyContinue
}
if ($Python) {
    & $Python.Source $Workflow authorize @args
    exit $LASTEXITCODE
}
$Launcher = Get-Command py -ErrorAction SilentlyContinue
if ($Launcher) {
    & $Launcher.Source -3 $Workflow authorize @args
    exit $LASTEXITCODE
}
Write-Error "Python 3 is required to record task-scoped authority."
exit 2
