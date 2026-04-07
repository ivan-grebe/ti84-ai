param(
    [string]$Port = "auto",
    [int]$Baud = 115200,
    [int]$Seconds = 3600,
    [string]$LogPath = "",
    [string]$PidPath = "",
    [string]$StdoutPath = "",
    [string]$StderrPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path (Split-Path $PSScriptRoot -Parent) "log.txt"
}
if ([string]::IsNullOrWhiteSpace($PidPath)) {
    $PidPath = Join-Path (Split-Path $PSScriptRoot -Parent) "capture.pid"
}
if ([string]::IsNullOrWhiteSpace($StdoutPath)) {
    $StdoutPath = Join-Path (Split-Path $PSScriptRoot -Parent) "capture.stdout.txt"
}
if ([string]::IsNullOrWhiteSpace($StderrPath)) {
    $StderrPath = Join-Path (Split-Path $PSScriptRoot -Parent) "capture.stderr.txt"
}

if (Test-Path -LiteralPath $PidPath) {
    $existingPid = (Get-Content -LiteralPath $PidPath | Select-Object -First 1).Trim()
    if ($existingPid) {
        try {
            $existingProc = Get-Process -Id ([int]$existingPid) -ErrorAction Stop
            Write-Output ("Capture already running. PID={0}" -f $existingProc.Id)
            exit 0
        } catch {
            Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
        }
    }
}

$captureScript = Join-Path $PSScriptRoot "capture_serial.ps1"
$quotedCaptureScript = '"' + $captureScript + '"'
$quotedLogPath = '"' + $LogPath + '"'
$argumentList = "-NoProfile -ExecutionPolicy Bypass -File $quotedCaptureScript -Port $Port -Baud $Baud -Seconds $Seconds -LogPath $quotedLogPath"

Remove-Item -LiteralPath $StdoutPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $StderrPath -Force -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath "powershell.exe" -ArgumentList $argumentList -WindowStyle Hidden -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -PassThru
Start-Sleep -Milliseconds 800

if ($proc.HasExited) {
    $stderr = ""
    if (Test-Path -LiteralPath $StderrPath) {
        $stderr = (Get-Content -LiteralPath $StderrPath -Raw).Trim()
    }
    if (-not $stderr -and (Test-Path -LiteralPath $StdoutPath)) {
        $stderr = (Get-Content -LiteralPath $StdoutPath -Raw).Trim()
    }
    if (-not $stderr) {
        $stderr = "background capture exited immediately"
    }
    throw $stderr
}

Set-Content -LiteralPath $PidPath -Value $proc.Id
Write-Output ("Started capture. PID={0} Log={1}" -f $proc.Id, $LogPath)
