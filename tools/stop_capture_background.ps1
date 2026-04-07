param(
    [string]$PidPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($PidPath)) {
    $PidPath = Join-Path (Split-Path $PSScriptRoot -Parent) "capture.pid"
}

if (-not (Test-Path -LiteralPath $PidPath)) {
    Write-Output "No capture PID file found."
    exit 0
}

$pidText = (Get-Content -LiteralPath $PidPath | Select-Object -First 1).Trim()
if (-not $pidText) {
    Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
    Write-Output "Capture PID file was empty."
    exit 0
}

$capturePid = [int]$pidText

try {
    $proc = Get-Process -Id $capturePid -ErrorAction Stop
    Stop-Process -Id $capturePid -Force
    Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
    Write-Output ("Stopped capture. PID={0}" -f $capturePid)
} catch {
    Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
    Write-Output ("Capture process was not running. PID={0}" -f $capturePid)
}
