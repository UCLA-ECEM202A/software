param(
    [Parameter(Mandatory=$true, Position=0)][ValidatePattern('^COM[0-9]+$')][string]$Port,
    [Parameter(Position=1)][int]$BaudRate = 115200
)

$ErrorActionPreference = 'Stop'
$Serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate)
$Serial.DtrEnable = $true
$Serial.ReadTimeout = 250

try {
    $Serial.Open()
    Write-Host "Monitoring $Port (Ctrl+C to stop)..."
    while ($true) {
        $Text = $Serial.ReadExisting()
        if ($Text.Length -gt 0) { [Console]::Write($Text) }
        Start-Sleep -Milliseconds 50
    }
}
finally {
    if ($Serial.IsOpen) { $Serial.Close() }
    $Serial.Dispose()
}
