# Bursztyn OS - Windows PowerShell Launcher
# QEMU BIOS / UEFI + VirtIO Full / USB HID
# Windows: GTK domyslnie; SDL tylko eksperymentalnie / diagnostycznie.
# Linux zachowuje SDL. Windows SDL z tabletem zatrzymuje boot w tescie A/B.

param(
    [string]$QemuPath = "",
    [string]$DiskSize = "4G",
    [string]$AudioBackend = "dsound",
    [ValidateSet("gtk", "sdl", "")]
    [string]$DisplayBackend = "gtk",
    [string]$Iso = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$IsoNormal = if ($Iso) { [System.IO.Path]::GetFullPath($Iso) } else { Join-Path $ScriptDir "BursztynOS.iso" }
$IsoCiTest = Join-Path $ScriptDir ".bursztyn_ci_test.iso"
$Disk = Join-Path $ScriptDir "wirtualny_dysk.img"
$LogDir = Join-Path $ScriptDir "logi_qemu"

function Find-Qemu {
    if ($QemuPath -and (Test-Path $QemuPath)) {
        return $QemuPath
    }
    $cmd = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    $standardPaths = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe"
    )
    foreach ($p in $standardPaths) {
        if (Test-Path $p) {
            return $p
        }
    }
    Write-Error "Nie znaleziono qemu-system-x86_64.exe. Upewnij sie, ze QEMU jest zainstalowane i dodane do PATH."
}

function Find-QemuImg {
    $cmd = Get-Command "qemu-img.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    $standardPaths = @(
        "C:\Program Files\qemu\qemu-img.exe",
        "C:\qemu\qemu-img.exe"
    )
    foreach ($p in $standardPaths) {
        if (Test-Path $p) {
            return $p
        }
    }
    return $null
}

function Find-Ovmf {
    $ovmfPaths = @(
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
        "C:\Program Files\qemu\share\OVMF.fd",
        "C:\Program Files\qemu\share\OVMF_CODE.fd",
        (Join-Path $ScriptDir "OVMF.fd"),
        (Join-Path $ScriptDir "OVMF_CODE.fd")
    )
    foreach ($p in $ovmfPaths) {
        if (Test-Path $p) {
            return $p
        }
    }
    return $null
}

function Prepare-Disk {
    if (-not (Test-Path $Disk)) {
        Write-Host "`nTworzenie pustego dysku wirtualnego: $DiskSize..."
        $qemuImg = Find-QemuImg
        if ($qemuImg) {
            & $qemuImg create -f raw $Disk $DiskSize
        } else {
            # Fallback: create sparse/raw file via .NET
            $fs = [System.IO.File]::Create($Disk)
            $fs.SetLength(4L * 1024 * 1024 * 1024)
            $fs.Close()
        }
    }
}

# GTK jest zweryfikowanym backendem Windows; SDL tylko jawnie diagnostycznie.
# Brak GTK konczy uruchomienie z komunikatem, bez automatycznego fallbacku SDL.
function Assert-QemuDisplay {
    param([string]$Qemu, [string]$Backend)
    $available = & $Qemu -display help 2>&1
    if ($LASTEXITCODE -ne 0 -or -not ($available | Where-Object { "$_" -match "^\s*$Backend(\s|$)" })) {
        throw "QEMU nie udostepnia backendu $Backend. Dla normalnego Windows wymagane jest GTK. Zainstaluj QEMU z GTK; SDL jest tylko opcja diagnostyczna."
    }
}

function Start-BursztynQemu {
    param(
        [string]$ModeName,
        [string]$Firmware,
        [string]$IsoPath,
        [bool]$VirtIoFull,
        [bool]$SaveLog
    )

    $qemu = Find-Qemu
    $effectiveDisplay = if ($DisplayBackend) { $DisplayBackend } else { "gtk" }
    Assert-QemuDisplay -Qemu $qemu -Backend $effectiveDisplay
    Prepare-Disk

    $argsList = @(
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=multi",
        "-smp", "1",
        "-m", "2G",
        "-display", $effectiveDisplay
    )

    # Audio
    $argsList += @(
        "-audiodev", "$AudioBackend,id=snd",
        "-device", "intel-hda",
        "-device", "hda-output,audiodev=snd"
    )

    if ($VirtIoFull) {
        # VirtIO Full: GPU, BLK, NET, RNG, INPUT
        # GTK jest bezpiecznym frontendem Windows; SDL sluzy do diagnostyki.
        # Wyeliminowano miniaturowe okno 640x480 przez ustawienie xres=1280,yres=720 w virtio-vga
        # oraz -vga none zapobiegajacy rozdzielaniu konsoli na std-vga i virtio-gpu.
        $argsList += @(
            "-drive", "id=disk,file=$Disk,format=raw,if=none",
            "-device", "virtio-blk-pci,drive=disk,disable-legacy=on",
            "-netdev", "user,id=n1",
            "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",
            "-device", "virtio-rng-pci,disable-legacy=on",
            "-vga", "none",
            "-device", "virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720",
            "-device", "virtio-keyboard-pci,disable-legacy=on",
            # Tablet jest produkcyjnym urzadzeniem wskazujacym (bez virtio-mouse).
            "-device", "virtio-tablet-pci,disable-legacy=on"
        )
    } else {
        # Standard emulation: AHCI + E1000 + xHCI USB + Bochs/STD VGA
        $argsList += @(
            "-drive", "id=disk,file=$Disk,format=raw,if=none",
            "-device", "ich9-ahci,id=ahci",
            "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
            "-netdev", "user,id=n1",
            "-device", "e1000,netdev=n1",
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-kbd,id=usbkbd,bus=xhci.0",
            "-device", "usb-mouse,id=usbmouse,bus=xhci.0",
            "-vga", "std",
            "-global", "VGA.vgamem_mb=64"
        )
    }

    # Firmware
    if ($Firmware -eq "UEFI") {
        $ovmf = Find-Ovmf
        if (-not $ovmf) {
            Write-Error "Nie znaleziono pliku firmware EDK2/OVMF dla UEFI."
            return
        }
        $argsList += @("-bios", $ovmf)
    }

    # ISO
    if ($IsoPath -and (Test-Path $IsoPath)) {
        $argsList += @("-cdrom", $IsoPath)
    }

    $argsList += @(
        "-serial", "stdio",
        "-name", "Bursztyn OS - $ModeName"
    )

    Write-Host "`n============================================================"
    Write-Host "Uruchamianie: $ModeName"
    Write-Host "Firmware:     $Firmware"
    Write-Host "Profil:       $(if ($VirtIoFull) { 'VirtIO Full' } else { 'Standard VGA/USB' })"
    $displayLabel = if ($effectiveDisplay -match '^sdl(,|$)') { 'SDL eksperymentalny / diagnostyczny' } else { $effectiveDisplay.ToUpperInvariant() }
    Write-Host "Display:      $displayLabel"
    Write-Host "ISO:          $IsoPath"
    Write-Host "Dysk:         $Disk"
    Write-Host "Rozdzielczosc poczatkowa okna: 1280x720"
    Write-Host "============================================================`n"

    Write-Host "QEMU executable: $qemu"
    Write-Host "QEMU argv: $($argsList | ConvertTo-Json -Compress)"

    if ($SaveLog) {
        if (-not (Test-Path $LogDir)) {
            New-Item -ItemType Directory -Path $LogDir | Out-Null
        }
        $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
        $logFile = Join-Path $LogDir "${stamp}_qemu.log"
        & $qemu @argsList 2>&1 | Tee-Object -FilePath $logFile
    } else {
        & $qemu @argsList
    }
}

while ($true) {
    Write-Host @"

================================================================
                  Bursztyn OS - QEMU Launcher
                            Windows
================================================================

 VirtIO FULL (GPU + BLK + NET + RNG + INPUT) - Zalecane
  1  - BIOS / VirtIO Full (1280x720 poczatkowe okno)
  2  - UEFI / VirtIO Full (1280x720 poczatkowe okno)

 STANDARDOWA EMULACJA (STD VGA + AHCI + E1000 + USB)
  3  - BIOS / Standard VGA
  4  - UEFI / Standard VGA

 TESTY AUTOMATYCZNE CI
  5  - BIOS  (.bursztyn_ci_test.iso)
  6  - UEFI  (.bursztyn_ci_test.iso)

 TESTY INTERAKTYWNE - aplikacja Testy systemu
  7  - BIOS
  8  - UEFI

 USTAWIENIA EKRANU - TEST RECZNY
  9  - BIOS / Bochs VBE (FHD, QHD, UWQHD, 4K, skala, rollback)
 10  - UEFI (backend + skala)

 FRONTEND WINDOWS - VIRTIO FULL (aktualnie: $DisplayBackend)
 11  - GTK (zalecany, domyslny)
 12  - SDL eksperymentalny / diagnostyczny

 ZAINSTALOWANY SYSTEM - BEZ ISO
 14  - UEFI z wirtualny_dysk.img (odpowiednik make run-installed)

 INSTALATOR
 15  - UEFI z ISO (wybierz Instalator Bursztyn OS w GRUB)

  0  - Wyjscie

"@
    $choice = Read-Host "Wybierz tryb [0-15]"
    switch ($choice) {
        "1" { Start-BursztynQemu -ModeName "BIOS VirtIO Full" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false }
        "2" { Start-BursztynQemu -ModeName "UEFI VirtIO Full" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false }
        "3" { Start-BursztynQemu -ModeName "Normalny BIOS STD-VGA" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $false }
        "4" { Start-BursztynQemu -ModeName "Normalny UEFI STD-VGA" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $false }
        "5" { Start-BursztynQemu -ModeName "Automatyczne CI BIOS" -Firmware "BIOS" -IsoPath $IsoCiTest -VirtIoFull $false -SaveLog $true }
        "6" { Start-BursztynQemu -ModeName "Automatyczne CI UEFI" -Firmware "UEFI" -IsoPath $IsoCiTest -VirtIoFull $false -SaveLog $true }
        "7" { Start-BursztynQemu -ModeName "Interaktywne testy BIOS" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $true }
        "8" { Start-BursztynQemu -ModeName "Interaktywne testy UEFI" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $true }
        "9" { Start-BursztynQemu -ModeName "Ustawienia ekranu BIOS Bochs VBE" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $true }
        "10" { Start-BursztynQemu -ModeName "Ustawienia ekranu UEFI" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $true }
        "11" { $DisplayBackend = "gtk" }
        "12" { $DisplayBackend = "sdl" }
        "14" { Start-BursztynQemu -ModeName "Zainstalowany UEFI" -Firmware "UEFI" -IsoPath "" -VirtIoFull $false -SaveLog $false }
        "15" { Start-BursztynQemu -ModeName "Instalator UEFI" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $true }
        "0" { exit 0 }
        default { Write-Host "Nieprawidlowy wybor." -ForegroundColor Red }
    }
}
