$ErrorActionPreference = "Stop"

$ProjectRootPath = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Workflow = Join-Path $ProjectRootPath "tools/remote_access.py"
$Python = Get-Command python3 -ErrorAction SilentlyContinue
if (-not $Python) {
    $Python = Get-Command python -ErrorAction SilentlyContinue
}
if ($Python) {
    & $Python.Source $Workflow @args
    exit $LASTEXITCODE
}
$Launcher = Get-Command py -ErrorAction SilentlyContinue
if ($Launcher) {
    & $Launcher.Source -3 $Workflow @args
    exit $LASTEXITCODE
}
Write-Error "Python 3 and OpenSSH are required for interactive remote access."
exit 2
