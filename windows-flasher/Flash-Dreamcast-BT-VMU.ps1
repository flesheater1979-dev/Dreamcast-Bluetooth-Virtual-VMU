param(
    [ValidateSet("Factory","Update")]
    [string]$Mode,
    [string]$Port
)

$ErrorActionPreference = "Stop"
$Host.UI.RawUI.WindowTitle = "Dreamcast Bluetooth + Virtual VMU Beta 0.9 Flasher"

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here
$Fw = Join-Path $Root "firmware"

$Files = @{
    Bootloader = Join-Path $Fw "bootloader.bin"
    Partitions = Join-Path $Fw "partition-table.bin"
    OtaData    = Join-Path $Fw "ota_data_initial.bin"
    App        = Join-Path $Fw "Dreamcast-BT-VMU-Beta-v0.9-app.bin"
}

$Expected = @{
    "bootloader.bin" = "810354C808D72804C8C242BCEE4EB131B762C081A3BA466FB2AD789638503A0E"
    "partition-table.bin" = "49265EC0998331DBE5F180E443FD71B24725B34D25C683648BC3787E7FA079B0"
    "ota_data_initial.bin" = "7D2C7AC4888BFD75CD5F56E8D61F69595121183AFC81556C876732FD3782C62F"
    "Dreamcast-BT-VMU-Beta-v0.9-app.bin" = "65135228CF51037A317D39D58F59AF1A1CFFCB07C6044C6BB1F873737726DD0A"
}

function Header {
    Clear-Host
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host " Dreamcast Bluetooth + Virtual VMU - Beta 0.9" -ForegroundColor Cyan
    Write-Host " NiceMCU-32S-DEV 2.8`" / ESP32 / Stage 5.10" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "BETA POWER WARNING:" -ForegroundColor Yellow
    Write-Host "  Power the NiceMCU from USB-C." -ForegroundColor Yellow
    Write-Host "  Leave the Dreamcast +5 V / BLUE test wire DISCONNECTED." -ForegroundColor Yellow
    Write-Host ""
}

function Find-Python {
    try {
        & py -3 -c "import sys" 2>$null
        if ($LASTEXITCODE -eq 0) { return @{Exe="py"; Prefix=@("-3")} }
    } catch {}
    try {
        & python -c "import sys" 2>$null
        if ($LASTEXITCODE -eq 0) { return @{Exe="python"; Prefix=@()} }
    } catch {}
    return $null
}

function Invoke-Python($py, [string[]]$Args) {
    & $py.Exe @($py.Prefix) @Args
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

function Ensure-Esptool($py) {
    try {
        & $py.Exe @($py.Prefix) -m esptool version *> $null
        if ($LASTEXITCODE -eq 0) { return }
    } catch {}

    Write-Host "Espressif esptool is not installed for this Python." -ForegroundColor Yellow
    $ans = Read-Host "Install esptool now with pip? [Y/N]"
    if ($ans -notmatch '^[Yy]') {
        throw "esptool is required."
    }
    Invoke-Python $py @("-m","pip","install","--user","esptool")
}

function Verify-Firmware {
    Write-Host "Checking firmware files..." -ForegroundColor Gray
    foreach ($path in $Files.Values) {
        if (-not (Test-Path $path)) { throw "Missing firmware file: $path" }
        $name = Split-Path $path -Leaf
        $hash = (Get-FileHash $path -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($Expected.ContainsKey($name) -and $hash -ne $Expected[$name]) {
            throw "SHA-256 mismatch for $name. Do not flash this package."
        }
    }
    Write-Host "Firmware hashes OK." -ForegroundColor Green
}

function Choose-Port {
    if ($Port) { return $Port }

    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    Write-Host ""
    if ($ports.Count -gt 0) {
        Write-Host "Detected serial ports:" -ForegroundColor Gray
        foreach ($p in $ports) { Write-Host "  $p" }
    } else {
        Write-Host "No serial ports were auto-detected." -ForegroundColor Yellow
    }

    if ($ports.Count -eq 1) {
        $v = Read-Host "Press Enter to use $($ports[0]), or type another COM port"
        if ([string]::IsNullOrWhiteSpace($v)) { return $ports[0] }
        return $v.Trim()
    }

    $v = Read-Host "Enter the NiceMCU COM port (example COM10)"
    if ([string]::IsNullOrWhiteSpace($v)) { throw "No COM port selected." }
    return $v.Trim()
}

Header
Verify-Firmware

$py = Find-Python
if (-not $py) {
    Write-Host "Python 3 was not found." -ForegroundColor Red
    Write-Host "Install Python 3, then run this flasher again."
    Write-Host "The browser/web flasher is the no-Python option once hosted over HTTPS."
    Read-Host "Press Enter to close"
    exit 1
}
Ensure-Esptool $py

if (-not $Mode) {
    Write-Host ""
    Write-Host "Choose flash mode:" -ForegroundColor Cyan
    Write-Host "  1 - FACTORY INSTALL (new/recovery board; ERASES internal VMU + pairings)"
    Write-Host "  2 - UPDATE ONLY (existing compatible install; preserves internal VMU + pairings)"
    Write-Host "  3 - Cancel"
    $choice = Read-Host "Selection"
    switch ($choice) {
        "1" { $Mode = "Factory" }
        "2" { $Mode = "Update" }
        default { exit 0 }
    }
}

$Port = Choose-Port
Write-Host ""
Write-Host "Selected: $Mode on $Port" -ForegroundColor Cyan

if ($Mode -eq "Factory") {
    Write-Host ""
    Write-Host "FACTORY INSTALL WILL ERASE THE ESP32 INTERNAL FLASH." -ForegroundColor Yellow
    Write-Host "This removes internal VMU saves and Bluetooth pairing data." -ForegroundColor Yellow
    Write-Host "The microSD card is not erased by this flasher." -ForegroundColor Yellow
    $confirm = Read-Host "Type ERASE to continue"
    if ($confirm -cne "ERASE") { Write-Host "Cancelled."; exit 0 }

    Write-Host ""
    Write-Host "Erasing flash..." -ForegroundColor Cyan
    Invoke-Python $py @("-m","esptool","--chip","esp32","--port",$Port,"--baud","460800","erase-flash")

    Write-Host ""
    Write-Host "Writing factory firmware..." -ForegroundColor Cyan
    Invoke-Python $py @(
        "-m","esptool","--chip","esp32","--port",$Port,"--baud","460800",
        "write-flash","--flash-mode","dio","--flash-size","4MB","--flash-freq","40m",
        "0x1000",$Files.Bootloader,
        "0x8000",$Files.Partitions,
        "0xD000",$Files.OtaData,
        "0x10000",$Files.App
    )
}
else {
    Write-Host ""
    Write-Host "UPDATE ONLY writes the app partition at 0x10000." -ForegroundColor Yellow
    Write-Host "Use this only when the board already has this project's compatible partition layout." -ForegroundColor Yellow
    $confirm = Read-Host "Type UPDATE to continue"
    if ($confirm -cne "UPDATE") { Write-Host "Cancelled."; exit 0 }

    Write-Host ""
    Write-Host "Writing app update..." -ForegroundColor Cyan
    Invoke-Python $py @(
        "-m","esptool","--chip","esp32","--port",$Port,"--baud","460800",
        "write-flash","--flash-mode","dio","--flash-size","4MB","--flash-freq","40m",
        "0x10000",$Files.App
    )
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host " FLASH COMPLETE" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Normal cold boot may restart once while it performs the SD backup phase."
Write-Host "Expected runtime display: BT status, VMU status, and SD backup status."
Write-Host ""
Read-Host "Press Enter to close"
