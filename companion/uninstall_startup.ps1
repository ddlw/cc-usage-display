$startup = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startup "CC Usage Display.lnk"
if (Test-Path $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath
    Write-Host "Removed: $shortcutPath"
} else {
    Write-Host "Startup shortcut not found."
}
