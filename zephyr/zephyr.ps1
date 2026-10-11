param(
    [Parameter(Position=0)][ValidateSet('setup','apps','sim','sim-clean','build','build-usb','ports','flash','flash-usb','help')][string]$Action = 'help',
    [Parameter(Position=1)][string]$App,
    [Parameter(Position=2)][string]$Port,
    [string]$Bossac
)
$ErrorActionPreference = 'Stop'
$Image = 'm202a-zephyr:4.4.2'
$ProjectDir = $PSScriptRoot

function Show-Usage {
    Write-Host 'Usage: .\zephyr.ps1 setup | apps | sim APP | sim-clean APP | build APP | build-usb APP | ports | flash APP COM5 | flash-usb APP COM5'
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

switch ($Action) {
    'setup' {
        & docker build -t $Image $ProjectDir
        if ($LASTEXITCODE -ne 0) { throw 'Image build failed.' }
    }
    'apps' { Import-Csv -LiteralPath (Join-Path $ProjectDir 'apps.csv') | Format-Table -AutoSize }
    'sim' { Invoke-Sim 'reuse' }
    'sim-clean' { Invoke-Sim 'clean' }
    { $_ -in 'build','build-usb' } {
        $Record = Get-App $App
        if ($Record.nano_build -ne 'yes') { throw "$App has no configured Nano build." }
        New-Item -ItemType Directory -Force (Join-Path $ProjectDir 'output') | Out-Null
        $Suffix = if ($Action -eq 'build-usb') { '-usb' } else { '' }
        Invoke-Container -Command @('sh','-ec','overlays=/work/board/nano33-sense-rev2.overlay; if [ -f "/work/$1/app.overlay" ]; then overlays="$overlays;/work/$1/app.overlay"; fi; if [ "$2" = -usb ]; then west build -p always -S cdc-acm-console -b arduino_nano_33_ble/nrf52840/sense "/work/$1" -d "/build/board-$1-usb" -- "-DDTC_OVERLAY_FILE=$overlays"; else west build -p always -b arduino_nano_33_ble/nrf52840/sense "/work/$1" -d "/build/board-$1" -- "-DDTC_OVERLAY_FILE=$overlays"; fi; cp "/build/board-$1$2/zephyr/zephyr.bin" "/work/output/$1$2.bin"; cp "/build/board-$1$2/zephyr/.config" "/work/output/$1$2.config"','sh',$App,$Suffix)
        $FirmwarePath = Join-Path $ProjectDir "output/$App$Suffix.bin"
        Write-Host "Firmware: $FirmwarePath"
    }
    'ports' { [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object }
    { $_ -in 'flash','flash-usb' } {
        $Record = Get-App $App
        if ($Record.nano_flash -ne 'yes') { throw "$App is not configured for Nano flashing." }
        $Suffix = if ($Action -eq 'flash-usb') { '-usb' } else { '' }
        if ($Port -notmatch '^COM[0-9]+$') { throw 'Double-tap Nano RESET, run .\zephyr.ps1 ports, then supply the COM port.' }
        if (-not $Bossac) { $Bossac = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\arduino\tools\bossac\1.9.1-arduino2\bossac.exe' }
        if (-not (Test-Path -LiteralPath $Bossac -PathType Leaf)) { throw 'Arduino bossac.exe not found. Install Arduino Mbed OS Nano Boards or specify -Bossac.' }
        $Binary = Join-Path $ProjectDir "output/$App$Suffix.bin"
        $Config = Join-Path $ProjectDir "output/$App$Suffix.config"
        if (-not (Test-Path $Binary) -or -not (Test-Path $Config)) { throw "Run .\zephyr.ps1 build$Suffix $App before flashing." }
        if (-not (Select-String -LiteralPath $Config -Pattern '^CONFIG_FLASH_LOAD_OFFSET=0x10000$' -Quiet)) { throw 'Unexpected firmware link address; refusing to flash.' }
        if ($Suffix -eq '-usb' -and -not (Select-String -LiteralPath $Config -Pattern '^CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y$' -Quiet)) { throw 'Firmware lacks the USB CDC console; refusing to flash-usb.' }
        Write-Host "Flashing $App to $Port; this replaces the current application."
        # The Arduino bootloader supplies the 0x10000 base; do not add a BOSSA offset.
        & $Bossac -p $Port -R -w -v -b $Binary
        if ($LASTEXITCODE -ne 0) { throw 'Upload or verification failed.' }
    }
    default { Show-Usage }
}
