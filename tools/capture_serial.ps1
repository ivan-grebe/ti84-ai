param(
    [string]$Port = "auto",
    [int]$Baud = 115200,
    [int]$Seconds = 20,
    [string]$LogPath = "",
    [switch]$EnableDtr,
    [switch]$EnableRts
)

$ErrorActionPreference = "Stop"

function Append-LogEntry {
    param(
        [string]$Path,
        [string]$Text
    )

    for ($attempt = 0; $attempt -lt 6; $attempt++) {
        try {
            $fileStream = [System.IO.File]::Open($Path,
                                                 [System.IO.FileMode]::Append,
                                                 [System.IO.FileAccess]::Write,
                                                 [System.IO.FileShare]::ReadWrite)
            try {
                $writer = New-Object System.IO.StreamWriter($fileStream)
                $writer.WriteLine($Text)
                $writer.Flush()
            } finally {
                if ($writer) { $writer.Dispose() }
                $fileStream.Dispose()
            }
            return
        } catch {
            if ($attempt -eq 5) {
                throw
            }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Write-LogLine {
    param(
        [string]$Message
    )

    $entry = "{0} {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $Message
    try {
        Append-LogEntry -Path $LogPath -Text $entry
    } catch {
        Write-Output ("{0} [log write skipped: {1}]" -f $entry, $_.Exception.Message)
        return
    }
    Write-Output $entry
}

function Get-PortCandidates {
    param(
        [string]$RequestedPort
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPort) -and $RequestedPort -ne "auto") {
        return @($RequestedPort)
    }

    $ordered = New-Object System.Collections.Generic.List[string]
    foreach ($candidate in @("COM4", "COM3", "COM5", "COM6", "COM7", "COM8", "COM9", "COM10", "COM11", "COM12")) {
        if (-not $ordered.Contains($candidate)) {
            $ordered.Add($candidate)
        }
    }

    try {
        $allSerialPorts = @(Get-CimInstance Win32_SerialPort -ErrorAction Stop | Where-Object {
            $_.DeviceID -match '^COM\d+$'
        } | Select-Object -ExpandProperty DeviceID)

        foreach ($candidate in $allSerialPorts) {
            if (-not $ordered.Contains($candidate)) {
                $ordered.Add($candidate)
            }
        }
    } catch {
    }

    return @($ordered)
}

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path (Split-Path $PSScriptRoot -Parent) "log.txt"
}

$logDir = Split-Path -Parent $LogPath
if ($logDir -and -not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}
if (-not (Test-Path -LiteralPath $LogPath)) {
    New-Item -ItemType File -Path $LogPath | Out-Null
}

$sessionStamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
Append-LogEntry -Path $LogPath -Text ""
Append-LogEntry -Path $LogPath -Text ("===== {0} | port={1} baud={2} seconds={3} =====" -f $sessionStamp, $Port, $Baud, $Seconds)

$captured = 0
$serial = $null
$activePort = $null
$lastWaitLog = [datetime]::MinValue

try {
    $end = (Get-Date).AddSeconds($Seconds)

    while ((Get-Date) -lt $end) {
        if (-not $serial -or -not $serial.IsOpen) {
            $opened = $false
            $targetPorts = Get-PortCandidates -RequestedPort $Port

            foreach ($targetPort in $targetPorts) {
                if ([string]::IsNullOrWhiteSpace($targetPort)) {
                    continue
                }

                try {
                    $serial = New-Object System.IO.Ports.SerialPort $targetPort, $Baud, "None", 8, "one"
                    $serial.ReadTimeout = 500
                    $serial.DtrEnable = $EnableDtr.IsPresent
                    $serial.RtsEnable = $EnableRts.IsPresent
                    $serial.Open()

                    if ($activePort -ne $targetPort) {
                        Write-LogLine ("[attached to {0}]" -f $targetPort)
                        $activePort = $targetPort
                    }

                    $opened = $true
                    break
                } catch {
                    if ($serial) {
                        try {
                            if ($serial.IsOpen) {
                                $serial.Close()
                            }
                        } catch {
                        }
                    }
                    $serial = $null
                }
            }

            if (-not $opened) {
                if (((Get-Date) - $lastWaitLog).TotalSeconds -ge 3) {
                    Write-LogLine "[waiting for ESP32 serial port]"
                    $lastWaitLog = Get-Date
                }
                Start-Sleep -Milliseconds 500
                continue
            }
        }

        try {
            $line = $serial.ReadLine()
            $line = $line.TrimEnd("`r", "`n")
            if ($line.Length -gt 0) {
                Write-LogLine $line
                $captured++
            }
        } catch [TimeoutException] {
        } catch {
            Write-LogLine ("[serial read error on {0}: {1}]" -f $activePort, $_.Exception.Message)
            try {
                if ($serial -and $serial.IsOpen) {
                    $serial.Close()
                }
            } catch {
            }
            $serial = $null
            Start-Sleep -Milliseconds 500
        }
    }

    if ($captured -eq 0) {
        Write-LogLine "[no serial output captured]"
    }
} catch {
    Write-LogLine ("ERROR: {0}" -f $_.Exception.Message)
    throw
} finally {
    if ($serial) {
        try {
            if ($serial.IsOpen) {
                $serial.Close()
            }
        } catch {
        }
    }
}
