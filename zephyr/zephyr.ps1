param(
    [Parameter(Position=0)][ValidateSet('setup','apps','sim','sim-clean','build','build-usb','ports','flash','flash-usb','flash-usb-auto','help')][string]$Action = 'help',
    [Parameter(Position=1)][string]$App,
    [Parameter(Position=2)][string]$Port,
    [string]$Bossac
)
$ErrorActionPreference = 'Stop'
$Image = 'm202a-zephyr:4.4.2'
$ProjectDir = $PSScriptRoot

function Show-Usage {
    Write-Host 'Usage: .\zephyr.ps1 setup | apps | sim APP | sim-clean APP | build APP | build-usb APP | ports | flash APP COM5 | flash-usb APP COM5 | flash-usb-auto APP [APPLICATION_COM_PORT]'
    Write-Host 'Builds use the pinned Linux container. Flashing uses host Arduino BOSSA on Windows.'
}
function Invoke-Container([string[]]$Command) {
    & docker run --rm --mount "type=bind,source=$ProjectDir,target=/work" --mount 'type=volume,source=m202a-zephyr-build,target=/build' $Image @Command
    if ($LASTEXITCODE -ne 0) { throw "Container command failed with exit code $LASTEXITCODE" }
}
function Get-App([string]$Name) {
    if (-not $Name -or $Name -notmatch '^[a-z][a-z0-9-]*$') { throw 'Supply a valid app name; use .\zephyr.ps1 apps to list them.' }
    $Record = Import-Csv -LiteralPath (Join-Path $ProjectDir 'apps.csv') | Where-Object { $_.name -eq $Name } | Select-Object -First 1
    if (-not $Record) { throw "Unknown app: $Name. Run .\zephyr.ps1 apps." }
    return $Record
}
function Invoke-Sim([string]$Mode) {
    $Record = Get-App $App
    if ($Record.native_sim -ne 'yes') { throw "$App does not support native_sim." }
    Invoke-Container -Command @('sh','-ec','if [ "$2" = clean ]; then west build -p always -b native_sim/native/64 "/work/$1" -d "/build/sim-$1"; else west build -b native_sim/native/64 "/work/$1" -d "/build/sim-$1"; fi; timeout 30 "/build/sim-$1/zephyr/zephyr.exe"','sh',$App,$Mode)
}

# A USB-console app enters the Arduino bootloader when its virtual serial
# rate changes to 1200. The bootloader usually enumerates on a new COM port.
function Enter-Bootloader([string]$ApplicationPort) {
    $Before = @([System.IO.Ports.SerialPort]::GetPortNames())
    $Serial = [System.IO.Ports.SerialPort]::new($ApplicationPort, 1200)
    $Serial.DtrEnable = $true
    Write-Host "Requesting bootloader via $ApplicationPort (1200 baud); close the serial monitor first."
    try {
        $Serial.Open()
        Start-Sleep -Milliseconds 300
    } finally {
        if ($Serial.IsOpen) { $Serial.Close() }
        $Serial.Dispose()
    }
    $SawDisappearance = $false
    for ($Attempt = 0; $Attempt -lt 80; $Attempt++) {
        Start-Sleep -Milliseconds 100
        $Current = @([System.IO.Ports.SerialPort]::GetPortNames())
        if ($Current -notcontains $ApplicationPort) { $SawDisappearance = $true }
        $New = @($Current | Where-Object { $Before -notcontains $_ })
        if ($SawDisappearance -and $New.Count -eq 1) {
            Start-Sleep -Milliseconds 300
            return $New[0]
        }
        if ($SawDisappearance -and $Current -contains $ApplicationPort) {
            Start-Sleep -Milliseconds 300
            return $ApplicationPort
        }
    }
    throw 'Bootloader port did not appear. Double-tap RESET and use flash-usb with the bootloader COM port.'
}

switch ($Action) {
    'setup' {
        & docker build -t $Image $ProjectDir
        if ($LASTEXITCODE -ne 0) { throw 'Image build failed.' }
    }
    'apps' {
        Import-Csv -LiteralPath (Join-Path $ProjectDir 'apps.csv') |
            Select-Object @{Name='App';Expression={$_.name}},
                          @{Name='Sim';Expression={$_.native_sim}},
                          @{Name='Nano build';Expression={$_.nano_build}},
                          @{Name='Flash';Expression={$_.nano_flash}},
                          @{Name='Sim run';Expression={if ($_.sim_run_test -eq 'not_applicable') {'n/a'} else {$_.sim_run_test}}},
                          @{Name='Build test';Expression={if ($_.nano_build_test -eq 'not_applicable') {'n/a'} else {$_.nano_build_test}}},
                          @{Name='Board run';Expression={if ($_.nano_run_test -eq 'not_applicable') {'n/a'} else {$_.nano_run_test}}} |
            Format-Table -AutoSize
    }
    'sim' { Invoke-Sim 'reuse' }
    'sim-clean' { Invoke-Sim 'clean' }
    { $_ -in 'build','build-usb' } {
        $Record = Get-App $App
        if ($Record.nano_build -ne 'yes') { throw "$App has no configured Nano build." }
        New-Item -ItemType Directory -Force (Join-Path $ProjectDir 'output') | Out-Null
        $Suffix = if ($Action -eq 'build-usb') { '-usb' } else { '' }
        Invoke-Container -Command @('sh','-ec','overlays=/work/board/nano33-sense-rev2.overlay; if [ -f "/work/$1/app.overlay" ]; then overlays="$overlays;/work/$1/app.overlay"; fi; if [ "$2" = -usb ]; then west build -p always -S cdc-acm-console -b arduino_nano_33_ble/nrf52840/sense "/work/$1" -d "/build/board-$1-usb" -- "-DDTC_OVERLAY_FILE=$overlays" -DCONFIG_REBOOT=y; else west build -p always -b arduino_nano_33_ble/nrf52840/sense "/work/$1" -d "/build/board-$1" -- "-DDTC_OVERLAY_FILE=$overlays"; fi; cp "/build/board-$1$2/zephyr/zephyr.bin" "/work/output/$1$2.bin"; cp "/build/board-$1$2/zephyr/.config" "/work/output/$1$2.config"','sh',$App,$Suffix)
        $FirmwarePath = Join-Path $ProjectDir "output/$App$Suffix.bin"
        Write-Host "Firmware: $FirmwarePath"
    }
    'ports' { [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object }
    { $_ -in 'flash','flash-usb','flash-usb-auto' } {
        $Record = Get-App $App
        if ($Record.nano_flash -ne 'yes') { throw "$App is not configured for Nano flashing." }
        $Suffix = if ($Action -in 'flash-usb','flash-usb-auto') { '-usb' } else { '' }
        if ($Action -eq 'flash-usb-auto' -and -not $Port) {
            $Candidates = @([System.IO.Ports.SerialPort]::GetPortNames())
            if ($Candidates.Count -ne 1) { throw 'Supply the application COM port explicitly when there is not exactly one serial port.' }
            $Port = $Candidates[0]
        }
        if ($Port -notmatch '^COM[0-9]+$') { throw 'Supply the Nano COM port shown by .\zephyr.ps1 ports.' }
        if (-not $Bossac) { $Bossac = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\arduino\tools\bossac\1.9.1-arduino2\bossac.exe' }
        if (-not (Test-Path -LiteralPath $Bossac -PathType Leaf)) { throw 'Arduino bossac.exe not found. Install Arduino Mbed OS Nano Boards or specify -Bossac.' }
        $Binary = Join-Path $ProjectDir "output/$App$Suffix.bin"
        $Config = Join-Path $ProjectDir "output/$App$Suffix.config"
        if (-not (Test-Path $Binary) -or -not (Test-Path $Config)) { throw "Run .\zephyr.ps1 build$Suffix $App before flashing." }
        if (-not (Select-String -LiteralPath $Config -Pattern '^CONFIG_FLASH_LOAD_OFFSET=0x10000$' -Quiet)) { throw 'Unexpected firmware link address; refusing to flash.' }
        if ($Suffix -eq '-usb' -and -not (Select-String -LiteralPath $Config -Pattern '^CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y$' -Quiet)) { throw 'Firmware lacks the USB CDC console; refusing to flash-usb.' }
        if ($Action -eq 'flash-usb-auto') {
            if (-not (Select-String -LiteralPath $Config -Pattern '^CONFIG_REBOOT=y$' -Quiet)) { throw 'Rebuild with .\zephyr.ps1 build-usb APP before automatic flashing.' }
            $Port = Enter-Bootloader $Port
        }
        Write-Host "Flashing $App to $Port; this replaces the current application."
        # The Arduino bootloader supplies the 0x10000 base; do not add a BOSSA offset.
        & $Bossac -p $Port -R -w -v -b $Binary
        if ($LASTEXITCODE -ne 0) { throw 'Upload or verification failed.' }
    }
    default { Show-Usage }
}
