# Simple ST-Link serial monitor for Status Display test
# Usage: .\scripts\serial_monitor.ps1
# Optional: .\scripts\serial_monitor.ps1 -Port COM6 -Baud 115200

param(
    [string]$Port = "COM6",
    [int]$Baud = 115200
)

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 200
$serial.WriteTimeout = 500
$serial.DtrEnable = $true
$serial.RtsEnable = $true

try {
    $serial.Open()
} catch {
    Write-Host "Failed to open $Port : $($_.Exception.Message)"
    Write-Host "Close any other serial monitor (PuTTY/CubeIDE Terminal) and try again."
    exit 1
}

Write-Host "Serial monitor on $Port @ $Baud"
Write-Host "Type keys to send (h=menu, s=auto). Ctrl+C to quit."
Write-Host "----------------------------------------"
$serial.Write("h")

try {
    while ($true) {
        try {
            $incoming = $serial.ReadExisting()
            if ($incoming.Length -gt 0) {
                Write-Host -NoNewline $incoming
            }
        } catch {
        }

        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq "Enter") {
                $serial.Write("`r")
            } else {
                $serial.Write([string]$key.KeyChar)
            }
        }

        Start-Sleep -Milliseconds 30
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    Write-Host ""
    Write-Host "Serial monitor closed."
}
