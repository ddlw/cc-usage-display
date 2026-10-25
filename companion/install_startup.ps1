$ErrorActionPreference = "Stop"

$python = (Get-Command python.exe -ErrorAction Stop).Source
$pythonw = Join-Path (Split-Path -Parent $python) "pythonw.exe"
if (-not (Test-Path -LiteralPath $pythonw)) {
    $pythonw = (Get-Command pythonw.exe -ErrorAction Stop).Source
}
$env:PYTHONPATH = $PSScriptRoot

& $python -c "import cc_usage_display" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "Cannot import cc_usage_display from $PSScriptRoot"
}

$stateDir = Join-Path $env:USERPROFILE ".cc-usage-display"
New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
$logPath = Join-Path $stateDir "companion.log"

$startup = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startup "CC Usage Display.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $pythonw
$shortcut.Arguments = "-m cc_usage_display --log-file `"$logPath`""
$shortcut.WorkingDirectory = $PSScriptRoot
$shortcut.WindowStyle = 7
$shortcut.Description = "CC Switch usage display companion"
$shortcut.Save()

Write-Host "Startup shortcut created: $shortcutPath"
Write-Host "Companion log: $logPath"
