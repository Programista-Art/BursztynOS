# Bursztyn OS - Windows PowerShell Launcher
# Profil VirtIO zgodny sprzetowo z dzialajacym launcherem Linux/macOS.
# Windows zachowuje GTK jako domyslny frontend i lokalny sposob obslugi UEFI.

param(
    [string]$QemuPath = "",
    [string]$DiskSize = "4G",
    [ValidateSet("gtk", "sdl", "")]
    [string]$DisplayBackend = "gtk",
    [ValidateRange(640,1280)][int]$GuestWidth = 1280,
    [ValidateRange(360,720)][int]$GuestHeight = 720,
    [string]$Iso = "",
    [switch]$PrintArgv,
    [switch]$Audit,
    [ValidateSet("relative", "virtio-tablet", "usb-tablet", "ps2")]
    [string]$PointerDevice = "relative",
    [ValidateRange(0,65535)][int]$QmpPort = 0,
    [switch]$PointerDiagnostics,
    [string]$FirmwarePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$LauncherVersion = "2026-10-10-LINUX-PARITY"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$IsoNormal = if ([string]::IsNullOrWhiteSpace($Iso)) {
    Join-Path $ScriptDir "BursztynOS.iso"
} else {
    [System.IO.Path]::GetFullPath($Iso)
}

$IsoCiTest = Join-Path $ScriptDir ".bursztyn_ci_test.iso"
$Disk = Join-Path $ScriptDir "wirtualny_dysk.img"
$LogDir = Join-Path $ScriptDir "logi_qemu"

function Find-Qemu {
    if (-not [string]::IsNullOrWhiteSpace($QemuPath)) {
        if (Test-Path -LiteralPath $QemuPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $QemuPath).Path
        }
        throw "Podana sciezka QEMU nie istnieje: $QemuPath"
    }

    $cmd = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($p in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path -LiteralPath $p -PathType Leaf) {
            return $p
        }
    }

    throw "Nie znaleziono qemu-system-x86_64.exe. Zainstaluj QEMU albo podaj -QemuPath."
}

function Find-QemuImg {
    $cmd = Get-Command "qemu-img.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($p in @(
        "C:\Program Files\qemu\qemu-img.exe",
        "C:\qemu\qemu-img.exe"
    )) {
        if (Test-Path -LiteralPath $p -PathType Leaf) {
            return $p
        }
    }

    return $null
}

function Get-UefiFirmware {
    param([switch]$ReadOnly)
    # Zachowujemy dzialajacy windowsowy sposob UEFI: prawdziwy obraz monolityczny
    # albo lokalny VARS+CODE podawany przez -bios. Nie modyfikujemy Program Files.
    # To jest jedyna celowa roznica firmware wzgledem launchera Linux/macOS.

    $shareDirs = @(
        "C:\Program Files\qemu\share",
        "C:\qemu\share",
        $ScriptDir
    )

    foreach ($dir in $shareDirs) {
        foreach ($name in @("OVMF.fd", "edk2-x86_64.fd")) {
            $candidate = Join-Path $dir $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $fi = Get-Item -LiteralPath $candidate
                if ($fi.Length -gt 0 -and ($fi.Length % 65536) -eq 0) {
                    return $fi.FullName
                }
            }
        }
    }

    $code = $null
    $vars = $null

    foreach ($dir in $shareDirs) {
        if (-not $code) {
            foreach ($name in @("edk2-x86_64-code.fd", "OVMF_CODE.fd", "OVMF_CODE_4M.fd")) {
                $candidate = Join-Path $dir $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $fi = Get-Item -LiteralPath $candidate
                    if ($fi.Length -gt 0) {
                        $code = $fi.FullName
                        break
                    }
                }
            }
        }

        if (-not $vars) {
            foreach ($name in @("edk2-i386-vars.fd", "edk2-x86_64-vars.fd", "OVMF_VARS.fd", "OVMF_VARS_4M.fd")) {
                $candidate = Join-Path $dir $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $fi = Get-Item -LiteralPath $candidate
                    if ($fi.Length -gt 0) {
                        $vars = $fi.FullName
                        break
                    }
                }
            }
        }
    }

    if (-not $code) {
        throw "Nie znaleziono kodu UEFI EDK2/OVMF."
    }
    if (-not $vars) {
        throw "Znaleziono kod UEFI, ale brak pliku VARS."
    }

    $firmwareDir = Join-Path $ScriptDir ".qemu_firmware"
    if ($ReadOnly) { return (Join-Path $firmwareDir "Bursztyn-EDK2-x86_64.fd") }
    if (-not (Test-Path -LiteralPath $firmwareDir)) {
        New-Item -ItemType Directory -Path $firmwareDir | Out-Null
    }

    $merged = Join-Path $firmwareDir "Bursztyn-EDK2-x86_64.fd"
    $codeInfo = Get-Item -LiteralPath $code
    $varsInfo = Get-Item -LiteralPath $vars
    $expectedLength = $varsInfo.Length + $codeInfo.Length

    $needBuild = $true
    if (Test-Path -LiteralPath $merged -PathType Leaf) {
        $mergedInfo = Get-Item -LiteralPath $merged
        if ($mergedInfo.Length -eq $expectedLength -and
            $mergedInfo.LastWriteTimeUtc -ge $codeInfo.LastWriteTimeUtc -and
            $mergedInfo.LastWriteTimeUtc -ge $varsInfo.LastWriteTimeUtc) {
            $needBuild = $false
        }
    }

    if ($needBuild) {
        Write-Host "Tworzenie lokalnego obrazu UEFI dla QEMU:"
        Write-Host "  VARS: $vars"
        Write-Host "  CODE: $code"
        Write-Host "  OUT:  $merged"

        $outStream = $null
        $varsStream = $null
        $codeStream = $null
        try {
            $outStream = [System.IO.File]::Open(
                $merged,
                [System.IO.FileMode]::Create,
                [System.IO.FileAccess]::Write,
                [System.IO.FileShare]::None
            )
            $varsStream = [System.IO.File]::OpenRead($vars)
            $varsStream.CopyTo($outStream)
            $varsStream.Dispose()
            $varsStream = $null

            $codeStream = [System.IO.File]::OpenRead($code)
            $codeStream.CopyTo($outStream)
            $codeStream.Dispose()
            $codeStream = $null
        }
        finally {
            if ($null -ne $varsStream) { $varsStream.Dispose() }
            if ($null -ne $codeStream) { $codeStream.Dispose() }
            if ($null -ne $outStream) { $outStream.Dispose() }
        }
    }

    $result = Get-Item -LiteralPath $merged
    if ($result.Length -ne $expectedLength) {
        throw "Niepoprawny rozmiar polaczonego firmware UEFI. Oczekiwano $expectedLength B, jest $($result.Length) B."
    }
    if (($result.Length % 65536) -ne 0) {
        throw "Polaczony firmware UEFI ma rozmiar niezgodny z mapowaniem PC BIOS: $($result.Length) B."
    }

    return $result.FullName
}

function Convert-DiskSizeToBytes {
    param([string]$Value)

    if ($Value -notmatch '^(?<Number>[0-9]+)(?<Unit>[KMGTP]?)$') {
        throw "Nieprawidlowy DiskSize: $Value. Przyklady: 4G, 2048M."
    }

    [Int64]$number = [Int64]$Matches["Number"]
    $unit = $Matches["Unit"].ToUpperInvariant()

    switch ($unit) {
        ""  { return $number }
        "K" { return $number * 1KB }
        "M" { return $number * 1MB }
        "G" { return $number * 1GB }
        "T" { return $number * 1TB }
        default { throw "Nieobslugiwany rozmiar dysku: $Value" }
    }
}

function Prepare-Disk {
    if (Test-Path -LiteralPath $Disk -PathType Leaf) {
        return
    }

    Write-Host ""
    Write-Host "Tworzenie pustego dysku wirtualnego: $DiskSize"

    $qemuImg = Find-QemuImg
    if ($qemuImg) {
        & $qemuImg create -f raw $Disk $DiskSize
        if ($LASTEXITCODE -ne 0) {
            throw "qemu-img nie utworzyl dysku. Kod: $LASTEXITCODE"
        }
        return
    }

    $bytes = Convert-DiskSizeToBytes -Value $DiskSize
    $fs = $null
    try {
        $fs = [System.IO.File]::Open(
            $Disk,
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None
        )
        $fs.SetLength($bytes)
    }
    finally {
        if ($null -ne $fs) {
            $fs.Dispose()
        }
    }
}

function Assert-QemuDisplay {
    param(
        [Parameter(Mandatory = $true)][string]$Qemu,
        [Parameter(Mandatory = $true)][string]$Backend
    )

    $available = @(& $Qemu -display help 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    $escapedBackend = [Regex]::Escape($Backend)
    $found = $false

    foreach ($line in $available) {
        if ($line -match ("^\s*" + $escapedBackend + "(?:\s|,|$)")) {
            $found = $true
            break
        }
    }

    if ($exitCode -ne 0 -or -not $found) {
        $list = $available -join [Environment]::NewLine
        throw "QEMU nie udostepnia backendu '$Backend'.`n`nQEMU -display help:`n$list"
    }
}

function Assert-QemuVirtioDevices {
    param([Parameter(Mandatory = $true)][string]$Qemu)

    $deviceHelp = @(& $Qemu -device help 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Nie mozna pobrac listy urzadzen QEMU."
    }

    foreach ($dev in @(
        "virtio-blk-pci",
        "virtio-net-pci",
        "virtio-rng-pci",
        "virtio-vga",
        "virtio-keyboard-pci",
        "virtio-mouse-pci"
    )) {
        if ($deviceHelp -notmatch [Regex]::Escape($dev)) {
            throw "Ta instalacja QEMU nie udostepnia wymaganego urzadzenia: $dev"
        }
    }
}

function Start-QemuWindowLimiter {
    param(
        [Parameter(Mandatory = $true)][string]$TitleFragment,
        [Parameter(Mandatory = $true)][int]$MaxWidth,
        [Parameter(Mandatory = $true)][int]$MaxHeight
    )

    Start-Job -ArgumentList $TitleFragment, $MaxWidth, $MaxHeight -ScriptBlock {
        param($fragment, $maxWidth, $maxHeight)

        Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class BursztynQemuWindowApi {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(
        IntPtr hWnd, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
}
"@ | Out-Null

        $SWP_NOMOVE = 0x0002
        $SWP_NOZORDER = 0x0004
        $SWP_NOACTIVATE = 0x0010
        while ($true) {
            $processes = @(Get-Process -Name "qemu-system-x86_64" -ErrorAction SilentlyContinue)
            foreach ($process in $processes) {
                try {
                    $process.Refresh()
                    if ($process.MainWindowHandle -eq [IntPtr]::Zero -or
                        $process.MainWindowTitle -notlike "*$fragment*") { continue }
                    $rect = New-Object BursztynQemuWindowApi+RECT
                    if (-not [BursztynQemuWindowApi]::GetWindowRect(
                            $process.MainWindowHandle, [ref]$rect)) { continue }
                    $width = $rect.Right - $rect.Left
                    $height = $rect.Bottom - $rect.Top
                    if ($width -gt $maxWidth -or $height -gt $maxHeight) {
                        $targetWidth = [Math]::Min($width, $maxWidth)
                        $targetHeight = [Math]::Min($height, $maxHeight)
                        [BursztynQemuWindowApi]::SetWindowPos(
                            $process.MainWindowHandle, [IntPtr]::Zero, 0, 0,
                            $targetWidth, $targetHeight,
                            $SWP_NOMOVE -bor $SWP_NOZORDER -bor $SWP_NOACTIVATE) | Out-Null
                    }
                    exit 0
                } catch {
                    # The GTK window can be recreated during startup; retry.
                }
            }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Start-BursztynQemu {
    param(
        [Parameter(Mandatory = $true)][string]$ModeName,
        [Parameter(Mandatory = $true)][ValidateSet("BIOS", "UEFI")][string]$Firmware,
        [string]$IsoPath,
        [Parameter(Mandatory = $true)][bool]$VirtIoFull,
        [Parameter(Mandatory = $true)][bool]$SaveLog
    )

    $qemu = if ($PrintArgv -and -not $Audit -and $QemuPath) { $QemuPath } else { Find-Qemu }
    $effectiveDisplay = if ([string]::IsNullOrWhiteSpace($DisplayBackend)) { "gtk" } else { $DisplayBackend }
    # Keep the guest framebuffer at the requested mode while allowing GTK to
    # fit it inside the bounded native QEMU window on HiDPI/smaller displays.
    $displayArg = if ($effectiveDisplay -eq "gtk") {
        "gtk,zoom-to-fit=on"
    } else {
        $effectiveDisplay
    }

    if (-not ($PrintArgv -or $Audit)) { Assert-QemuDisplay -Qemu $qemu -Backend $effectiveDisplay }
    if ($VirtIoFull -and -not ($PrintArgv -or $Audit)) {
        Assert-QemuVirtioDevices -Qemu $qemu
    }
    if (-not ($PrintArgv -or $Audit)) { Prepare-Disk }

    $hasIso = -not [string]::IsNullOrWhiteSpace($IsoPath)
    if ($hasIso) {
        if (-not ($PrintArgv -or $Audit) -and -not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
            throw "Nie znaleziono obrazu ISO: $IsoPath"
        }
        if (-not ($PrintArgv -or $Audit)) { $IsoPath = (Resolve-Path -LiteralPath $IsoPath).Path }
    }

    # Zgodnosc z Linux/macOS:
    # pc+hpet, cpu=max, TCG single-thread, 1 vCPU, 2 GiB RAM.
    $argsList = @(
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-smp", "1",
        "-m", "2G",
        "-serial", "stdio",
        "-display", $displayArg
    )

    # Linux dodaje HDA przed urzadzeniami VirtIO. Na Windows zachowujemy identyczna
    # topologie PCI, ale uzywamy backendu audio 'none', aby nie wracal blad DirectSound.
    $argsList += @(
        "-audiodev", "none,id=snd",
        "-device", "intel-hda",
        "-device", "hda-output,audiodev=snd"
    )

    if ($VirtIoFull) {
        $blkSpec = "virtio-blk-pci,drive=disk,disable-legacy=on"
        if (-not $hasIso) {
            $blkSpec += ",bootindex=1"
        }

        # Kolejnosc zgodna z dzialajacym launcherem Linux/macOS:
        # HDA -> BLK -> NET -> RNG -> GPU -> keyboard -> mouse.
        # Input celowo BEZ disable-legacy=on, tak jak na Linuxie.
        $argsList += @(
            "-drive", "id=disk,file=$Disk,format=raw,if=none",
            "-device", $blkSpec,

            "-netdev", "user,id=n1",
            "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",

            "-object", "rng-builtin,id=rng0",
            "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",

            "-vga", "none",
            "-device", "virtio-vga,disable-legacy=on,xres=$GuestWidth,yres=$GuestHeight",

            "-device", "virtio-keyboard-pci",
            "-device", "virtio-mouse-pci"
        )
    }
    else {
        $ahciDisk = "ide-hd,drive=disk,bus=ahci.0,unit=0"
        if (-not $hasIso) {
            $ahciDisk += ",bootindex=1"
        }

        $argsList += @(
            "-drive", "id=disk,file=$Disk,format=raw,if=none",
            "-device", "ich9-ahci,id=ahci",
            "-device", $ahciDisk,

            "-netdev", "user,id=n1",
            "-device", "e1000,netdev=n1",

            "-vga", "std",
            "-global", "VGA.vgamem_mb=64",

            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-kbd,id=usbkbd,bus=xhci.0",
            "-device", "usb-mouse,id=usbmouse,bus=xhci.0"
        )
    }

    $ovmf = $null
    if ($Firmware -eq "UEFI") {
        $ovmf = if ($FirmwarePath) { $FirmwarePath } elseif ($PrintArgv -or $Audit) { Get-UefiFirmware -ReadOnly } else { Get-UefiFirmware }
        $argsList = @("-bios", $ovmf) + $argsList
    }

    if ($hasIso) {
        # Zachowujemy windowsowy wymuszony start z ISO, aby istniejacy dysk nie przejal bootu.
        $argsList += @(
            "-boot", "order=d,menu=on",
            "-cdrom", $IsoPath
        )
    }
    else {
        $argsList += @("-boot", "order=c")
    }

    # Explicit diagnostic A/B selection. Normal profiles retain their relative
    # devices and frontend; the guest never compensates for host decorations.
    if ($PointerDevice -ne "relative") {
        $filtered = @()
        for ($i=0; $i -lt $argsList.Count; $i++) {
            if ($argsList[$i] -eq "-device" -and $i+1 -lt $argsList.Count -and
                $argsList[$i+1] -match '^(virtio-mouse-pci|usb-mouse)(,|$)') { $i++; continue }
            $filtered += $argsList[$i]
        }
        $argsList = $filtered
        switch ($PointerDevice) {
            "virtio-tablet" { $argsList += @("-device", "virtio-tablet-pci") }
            "usb-tablet" {
                if ($VirtIoFull) { $argsList += @("-device", "qemu-xhci,id=pointerprobe") }
                $tabletBus = if ($VirtIoFull) { "pointerprobe.0" } else { "xhci.0" }
                $argsList += @("-device", "usb-tablet,bus=$tabletBus")
            }
            # pc's existing i8042 supplies PS/2 without adding a second device.
        }
    }
    if ($QmpPort -gt 0) {
        $argsList += @("-qmp", "tcp:127.0.0.1:$QmpPort,server=on,wait=off")
    }
    if ($PointerDiagnostics) { $SaveLog = $true }

    $argsList += @("-name", "Bursztyn OS - $ModeName")

    $pciDevices = @()
    for ($i=0; $i -lt $argsList.Count-1; $i++) {
        if ($argsList[$i] -eq "-device" -and $argsList[$i+1] -match '^(intel-hda|virtio-.*-pci|virtio-vga)(,|$)') {
            $pciDevices += $argsList[$i+1]
        }
    }
    $topology = @()
    $classes = @{
        "intel-hda"="audio"; "virtio-blk-pci"="storage"; "virtio-net-pci"="network"
        "virtio-rng-pci"="rng"; "virtio-vga"="gpu"; "virtio-keyboard-pci"="keyboard"
        "virtio-mouse-pci"="relative mouse"; "virtio-tablet-pci"="absolute tablet"
    }
    for ($i=0; $i -lt $pciDevices.Count; $i++) {
        $deviceType = ($pciDevices[$i] -split ',')[0]
        $deviceClass = if ($classes.ContainsKey($deviceType)) { $classes[$deviceType] } else { "unknown" }
        $topology += [ordered]@{order=$i;device=$pciDevices[$i];expected_class=$deviceClass;expected_bdf=("00:{0:x2}.0" -f ($i+2))}
    }
    $report = [ordered]@{argv=@($qemu)+$argsList;host_os=[Environment]::OSVersion.ToString();qemu_binary=$qemu;topology=$topology}
    $report.pointer_selection = $PointerDevice
    $report.display_backend = $effectiveDisplay
    if ($Audit) {
        $report.version = @(& $qemu --version 2>&1 | ForEach-Object { $_.ToString() })
        $report.machines = @(& $qemu -machine help 2>&1 | ForEach-Object { $_.ToString() })
        $report.virtio_mouse_properties = @(& $qemu -device virtio-mouse-pci,help 2>&1 | ForEach-Object { $_.ToString() })
    }
    if ($PrintArgv -or $Audit) { $report | ConvertTo-Json -Depth 8; return }
    Write-Host "PCI topology: $($topology | ConvertTo-Json -Compress)"
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "Uruchamianie: $ModeName"
    Write-Host "Launcher:     $LauncherVersion"
    Write-Host "Firmware:     $Firmware"
    if ($Firmware -eq "UEFI") {
        Write-Host "UEFI image:   $ovmf"
    }
    Write-Host "Profil:       $(if ($VirtIoFull) { 'VirtIO Full - Linux parity' } else { 'Legacy/Standard' })"
    Write-Host "Display:      $($effectiveDisplay.ToUpperInvariant())"
    Write-Host "CPU:          TCG thread=single / 1 vCPU"
    Write-Host "RAM:          2 GiB"
    Write-Host "Guest video:  ${GuestWidth}x${GuestHeight} (bounded viewport <= 1280x720)"
    if ($VirtIoFull) {
        Write-Host "Storage:      VirtIO-BLK"
        Write-Host "Network:      VirtIO-NET"
        Write-Host "RNG:          VirtIO-RNG / rng-builtin"
        Write-Host "GPU:          VirtIO-GPU / virtio-vga"
        Write-Host "Input:        VirtIO keyboard + mouse"
    }
    else {
        Write-Host "Storage:      AHCI"
        Write-Host "Network:      E1000"
        Write-Host "GPU:          std VGA / Bochs"
        Write-Host "Input:        xHCI + USB HID"
    }
    Write-Host "Audio guest:  Intel HDA obecne, host audio = none"
    Write-Host "ISO:          $(if ($hasIso) { $IsoPath } else { '(brak - start z dysku)' })"
    Write-Host "Dysk:         $Disk"
    Write-Host "Boot:         $(if ($hasIso) { 'ISO najpierw' } else { 'dysk' })"
    Write-Host "============================================================"
    Write-Host ""
    Write-Host "QEMU executable: $qemu"
    Write-Host "QEMU argv: $($argsList | ConvertTo-Json -Compress)"
    Write-Host "Pointer selection: $PointerDevice (relative = canonical profile)"
    if ($PointerDiagnostics) {
        Write-Host "Pointer probe: use an ISO built with BURSZTYNDIAGABSMAP=1 for raw REL/ABS and generation logs."
        Write-Host "Compare window sizes, fullscreen and return; record host Windows DPI separately from guest scale."
    }

    $windowLimiter = Start-QemuWindowLimiter `
        -TitleFragment "Bursztyn OS - $ModeName" `
        -MaxWidth 1280 -MaxHeight 720
    try {
    if ($SaveLog) {
        if (-not (Test-Path -LiteralPath $LogDir)) {
            New-Item -ItemType Directory -Path $LogDir | Out-Null
        }

        $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
        $safeName = ($ModeName -replace '[^A-Za-z0-9_-]', '_')
        $logFile = Join-Path $LogDir "${stamp}_${safeName}.log"

        Write-Host "Log COM1: $logFile"
        & $qemu @argsList 2>&1 | Tee-Object -FilePath $logFile
        $exitCode = $LASTEXITCODE
    }
    else {
        & $qemu @argsList
        $exitCode = $LASTEXITCODE
    }
    } finally {
        if ($windowLimiter) {
            Stop-Job -Job $windowLimiter -ErrorAction SilentlyContinue | Out-Null
            Remove-Job -Job $windowLimiter -Force -ErrorAction SilentlyContinue
        }
    }

    if ($exitCode -ne 0) {
        Write-Host ""
        Write-Host "QEMU zakonczyl sie kodem $exitCode." -ForegroundColor Red
    }
}

if ($PrintArgv -or $Audit) {
    Start-BursztynQemu -ModeName "UEFI VirtIO Full" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false
    exit 0
}
while ($true) {
    Write-Host @"

================================================================
                  Bursztyn OS - QEMU Launcher
                            Windows
                 wersja: $LauncherVersion
================================================================

 VIRTIO FULL - PROFIL ZGODNY Z LINUX/macOS
  1  - BIOS + VirtIO Full + BursztynOS.iso
  2  - UEFI + VirtIO Full + BursztynOS.iso

 TESTY AUTOMATYCZNE - kanoniczne BursztynOS.iso / VirtIO
  3  - BIOS
  4  - UEFI

 TESTY CI - .bursztyn_ci_test.iso / VirtIO
  5  - BIOS
  6  - UEFI

 TESTY INTERAKTYWNE / VirtIO
  7  - BIOS
  8  - UEFI

 USTAWIENIA EKRANU / VIRTIO-GPU
  9  - BIOS
 10  - UEFI

 FRONTEND WINDOWS
 11  - GTK (zalecany, domyslny)
 12  - SDL (diagnostycznie)

 ZAINSTALOWANY SYSTEM
 14  - UEFI + VirtIO Full, bez ISO

 INSTALATOR
 15  - UEFI + VirtIO Full + BursztynOS.iso

 TRYB ZGODNOSCI / LEGACY
 16  - BIOS: AHCI + E1000 + std VGA + USB HID
 17  - UEFI: AHCI + E1000 + std VGA + USB HID
 18  - Zainstalowany UEFI Legacy, bez ISO

  0  - Wyjscie

"@

    $choice = Read-Host "Wybierz tryb [0-18]"

    try {
        switch ($choice) {
            "1"  { Start-BursztynQemu -ModeName "Normalny BIOS VirtIO Full" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false }
            "2"  { Start-BursztynQemu -ModeName "Normalny UEFI VirtIO Full" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false }

            "3"  { Start-BursztynQemu -ModeName "Testy GRUB BIOS VirtIO" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }
            "4"  { Start-BursztynQemu -ModeName "Testy GRUB UEFI VirtIO" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

            "5"  {
                if (-not (Test-Path -LiteralPath $IsoCiTest -PathType Leaf)) { throw "Nie znaleziono: $IsoCiTest" }
                Start-BursztynQemu -ModeName "CI BIOS VirtIO" -Firmware "BIOS" -IsoPath $IsoCiTest -VirtIoFull $true -SaveLog $true
            }
            "6"  {
                if (-not (Test-Path -LiteralPath $IsoCiTest -PathType Leaf)) { throw "Nie znaleziono: $IsoCiTest" }
                Start-BursztynQemu -ModeName "CI UEFI VirtIO" -Firmware "UEFI" -IsoPath $IsoCiTest -VirtIoFull $true -SaveLog $true
            }

            "7"  { Start-BursztynQemu -ModeName "Interaktywne BIOS VirtIO" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }
            "8"  { Start-BursztynQemu -ModeName "Interaktywne UEFI VirtIO" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

            "9"  { Start-BursztynQemu -ModeName "Ekran BIOS VirtIO-GPU" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }
            "10" { Start-BursztynQemu -ModeName "Ekran UEFI VirtIO-GPU" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

            "11" {
                $DisplayBackend = "gtk"
                Write-Host "Backend ustawiony na GTK." -ForegroundColor Green
            }
            "12" {
                $DisplayBackend = "sdl"
                Write-Host "Backend ustawiony na SDL." -ForegroundColor Yellow
            }

            "14" { Start-BursztynQemu -ModeName "Zainstalowany UEFI VirtIO Full" -Firmware "UEFI" -IsoPath "" -VirtIoFull $true -SaveLog $false }
            "15" { Start-BursztynQemu -ModeName "Instalator UEFI VirtIO Full" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

            "16" { Start-BursztynQemu -ModeName "BIOS Legacy" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $false }
            "17" { Start-BursztynQemu -ModeName "UEFI Legacy" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $false }
            "18" { Start-BursztynQemu -ModeName "Zainstalowany UEFI Legacy" -Firmware "UEFI" -IsoPath "" -VirtIoFull $false -SaveLog $false }

            "0" { exit 0 }

            default {
                Write-Host "Nieprawidlowy wybor." -ForegroundColor Red
            }
        }
    }
    catch {
        Write-Host ""
        Write-Host "BLAD: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host ""
    }
}
