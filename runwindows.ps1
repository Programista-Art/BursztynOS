# Bursztyn OS - Windows PowerShell Launcher
# Plik w UTF-8 z BOM: Windows PowerShell 5.1 czyta skrypt bez BOM jako ANSI i psulby polskie komunikaty.
# Profil VirtIO zgodny sprzetowo z dzialajacym launcherem Linux/macOS.
# Windows zachowuje GTK jako domyslny frontend i lokalny sposob obslugi UEFI.
#
# Uzycie (Windows PowerShell 5.1 lub PowerShell 7):
#   .\runwindows.ps1                      menu interaktywne
#   .\runwindows.ps1 -Mode 2              bez menu: tryb 2 (UEFI + VirtIO Full); kod wyjscia = kod QEMU
#   .\runwindows.ps1 -PrintArgv -Mode 16  tylko raport JSON z argv QEMU (nic nie tworzy, nic nie uruchamia)
#   .\runwindows.ps1 -Mode 2 -AudioMode sdl                       dzwiek przez SDL zamiast DirectSound
#   .\runwindows.ps1 -Mode 2 -DisplayBackend gtk                  frontend okna QEMU: gtk (domyslny,
#                                         -display gtk,zoom-to-fit=on) albo sdl (diagnostycznie)
#   .\runwindows.ps1 -Mode 15 -TestDisk instalacja-test.img -IsoSha256 <sha256> -AudioMode sdl
#                                         instalator na NOWYM jednorazowym dysku testowym (tworzony tylko,
#                                         gdy nie istnieje; nigdy wirtualny_dysk.img / wirtualnydysk.img)
#   .\runwindows.ps1 -Mode 14 -TestDisk instalacja-test.img -UseExistingTestDisk -SerialLog -AudioMode sdl
#                                         start systemu zainstalowanego na tym dysku testowym, z logiem COM1
#   -DisplayBackend sam niczego nie uruchamia: bez -Mode otwiera sie menu, a wybrany frontend dotyczy
#   trybow uruchamianych z menu (pozycje 11/12 zmieniaja go w trakcie). Skrypt z biezacego katalogu
#   wywoluje sie z prefiksem .\ (np. .\runwindows.ps1), inaczej PowerShell go nie znajdzie.
#   .\runwindows.ps1 -Mode 2 -AudioMode wav -AudioWavPath "nagrania\próba 1.wav"
#
# Dzwiek: gosc zawsze ma Intel HDA (-device intel-hda -device hda-output,audiodev=snd, PCI 00:02.0).
# Zmienia sie tylko backend hosta: dsound (domyslny, zalecany w Windows 11), sdl, wav (nagrywanie do
# pliku), none (bez dzwieku). Backend jest sprawdzany w `qemu-system-x86_64 -audiodev help`; jawny
# wybor nigdy nie jest po cichu zamieniany na inny ani na "none". Opis: docs/qemu-dzwiek-launchery.md.
#
# wirtualny_dysk.img jest TRWALYM dyskiem uzytkownika: launcher tworzy pusty plik tylko, gdy nie istnieje,
# nigdy go nie nadpisuje, nie formatuje ani nie usuwa.
#
# Klawiatura: Windows zglasza AltGr jako LCtrl + prawy Alt. Bursztyn OS rozpoznaje Ctrl+AltGr jako AltGr
# (BWS_MOD_ALTGR) i nie wykonuje wtedy skrotow Ctrl+A/S/Z/X/N/O - polskie znaki nie uruchamiaja polecen.
# Launcher nie zmienia ustawien klawiatury ani metod wprowadzania Windows.

param(
    [string]$QemuPath = "",
    [string]$DiskSize = "4G",
    [ValidateSet("gtk", "sdl", "")]
    [string]$DisplayBackend = "gtk",
    [ValidateRange(640,4096)][int]$GuestWidth = 1280,
    [ValidateRange(480,4096)][int]$GuestHeight = 720,
    [ValidateRange(640,8192)][int]$WindowWidth = 1280,
    [ValidateRange(480,8192)][int]$WindowHeight = 720,
    [string]$Iso = "",
    [switch]$PrintArgv,
    [switch]$Audit,
    [ValidateSet("relative", "virtio-tablet", "usb-tablet", "ps2")]
    [string]$PointerDevice = "relative",
    [ValidateRange(0,65535)][int]$QmpPort = 0,
    [switch]$PointerDiagnostics,
    [string]$FirmwarePath = "",
    [ValidatePattern('^$|^([0-9]|1[0-8])$')]
    [string]$Mode = "",
    # Wyjscie dzwieku hosta (gosc: Intel HDA). Puste = dsound (domyslne, zalecane w Windows 11).
    [ValidateSet("dsound", "sdl", "wav", "none", "")]
    [string]$AudioMode = "",
    # Plik nagrania dla -AudioMode wav (domyslnie logi_qemu\nagrania_audio\bursztyn-hda-DATA.wav).
    [string]$AudioWavPath = "",
    # Pozwala nadpisac istniejacy plik nagrania (bez tego: unikalna nazwa albo odmowa).
    [switch]$AudioOverwrite,
    # Kontroler dzwieku GOSCIA (nie wyjscie hosta): hda = Intel HDA (domyslnie, profile produkcyjne),
    # ac97 = Intel ICH AC97 wylacznie do rozwoju/testow sterownika AC97 (zamiast HDA, ten sam slot PCI).
    [ValidateSet("hda", "ac97", "")]
    [string]$GuestAudio = "",
    # Jednorazowy dysk TESTOWY zamiast trwalego wirtualny_dysk.img (np. instalacja-test.img). Plik .img na
    # zwyklej sciezce; bez -UseExistingTestDisk musi NIE istniec (jest tworzony, nigdy nadpisywany).
    [string]$TestDisk = "",
    # Jawna zgoda na uzycie ISTNIEJACEGO dysku testowego (np. start zainstalowanego systemu, tryb 14).
    [switch]$UseExistingTestDisk,
    # Oczekiwana suma SHA-256 obrazu ISO; niezgodnosc przerywa przed uruchomieniem QEMU.
    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$IsoSha256 = "",
    # Zapis pelnego logu COM1 do logi_qemu\ rowniez w trybach, ktore domyslnie go nie zapisuja (np. 14).
    [switch]$SerialLog
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$LauncherVersion = "2026-11-23-DZWIEK"
# ValidateSet nie rozroznia wielkosci liter ("GTK" przechodzi), a QEMU wymaga nazwy backendu malymi literami.
$DisplayBackend = $DisplayBackend.ToLowerInvariant()

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
# Wzgledna sciezka -Iso odnosi sie do katalogu, z ktorego uruchomiono skrypt.
$InvocationDir = (Get-Location).ProviderPath
Set-Location -LiteralPath $ScriptDir
# Set-Location zmienia tylko lokalizacje PowerShell; QEMU i metody .NET uzywaja katalogu procesu.
[System.Environment]::CurrentDirectory = $ScriptDir
$script:LastQemuExitCode = 0

$IsoNormal = if ([string]::IsNullOrWhiteSpace($Iso)) {
    Join-Path $ScriptDir "BursztynOS.iso"
} elseif ([System.IO.Path]::IsPathRooted($Iso)) {
    [System.IO.Path]::GetFullPath($Iso)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $InvocationDir $Iso))
}

$IsoCiTest = Join-Path $ScriptDir ".bursztyn_ci_test.iso"
# Kontroler dzwieku goscia: -GuestAudio > zmienna GUEST_AUDIO > hda. Bez cichego powrotu: brak AC97 w QEMU = blad.
$script:GuestAudio = if (-not [string]::IsNullOrWhiteSpace($GuestAudio)) { $GuestAudio }
    elseif ($env:GUEST_AUDIO -in @("hda", "ac97")) { $env:GUEST_AUDIO } else { "hda" }
# Wybor dzwieku (menu "A" moze go zmienic w trybie interaktywnym). Jawny -AudioMode wav bez sciezki = domyslna.
$script:AudioSelection = if ([string]::IsNullOrWhiteSpace($AudioMode)) {
    if ([string]::IsNullOrWhiteSpace($AudioWavPath)) { "dsound" } else { "wav" }
} else { $AudioMode }
if (-not [string]::IsNullOrWhiteSpace($AudioWavPath) -and $script:AudioSelection -ne "wav") {
    Write-Host "BLAD: -AudioWavPath dziala tylko z -AudioMode wav." -ForegroundColor Red
    exit 2
}
$script:AudioWavSelection = $AudioWavPath
$script:Interactive = [string]::IsNullOrWhiteSpace($Mode) -and -not ($PrintArgv -or $Audit)
$script:LastAudioFailure = ""
$Disk = Join-Path $ScriptDir "wirtualny_dysk.img"
$LogDir = Join-Path $ScriptDir "logi_qemu"
$script:DiskIsTest = $false

# -TestDisk: jawnie wybrany jednorazowy dysk testowy. Odmowa dla trwalych dyskow uzytkownika, urzadzen
# fizycznych/sciezek UNC, plikow innych niz .img, istniejacego pliku bez -UseExistingTestDisk
# i brakujacego pliku z -UseExistingTestDisk (literowka nie tworzy nowego dysku po cichu).
function Resolve-TestDisk {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ($Path -match '^\\\\' -or $Path -match '^//' -or $Path -match '(?i)physicaldrive|harddisk|volume\{') {
        throw "-TestDisk: sciezki urzadzen i UNC sa niedozwolone: $Path"
    }
    $full = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $InvocationDir $Path }))
    if ([System.IO.Path]::GetExtension($full) -ine ".img") { throw "-TestDisk: wymagane rozszerzenie .img: $full" }
    $leaf = [System.IO.Path]::GetFileName($full)
    foreach ($trwaly in @("wirtualny_dysk.img", "wirtualnydysk.img")) {
        if ($leaf -ieq $trwaly) { throw "-TestDisk: $leaf to nazwa TRWALEGO dysku uzytkownika - wybierz inna nazwe (np. instalacja-test.img)." }
        if ($full -ieq [System.IO.Path]::GetFullPath((Join-Path $ScriptDir $trwaly))) { throw "-TestDisk wskazuje trwaly dysk: $full" }
    }
    $parent = [System.IO.Path]::GetDirectoryName($full)
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { throw "-TestDisk: katalog nie istnieje: $parent" }
    if (Test-Path -LiteralPath $full) {
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "-TestDisk: istnieje, ale nie jest plikiem: $full" }
        if (-not $UseExistingTestDisk) {
            throw "-TestDisk: plik juz istnieje i NIE zostanie nadpisany: $full`nAby go uzyc (np. tryb 14), dodaj -UseExistingTestDisk; na nowa instalacje podaj nowa nazwe."
        }
    } elseif ($UseExistingTestDisk) {
        throw "-UseExistingTestDisk: plik nie istnieje: $full"
    }
    return $full
}
if (-not [string]::IsNullOrWhiteSpace($TestDisk)) {
    try { $Disk = Resolve-TestDisk -Path $TestDisk; $script:DiskIsTest = $true }
    catch { Write-Host "BLAD: $($_.Exception.Message)" -ForegroundColor Red; exit 2 }
} elseif ($UseExistingTestDisk) {
    Write-Host "BLAD: -UseExistingTestDisk wymaga -TestDisk." -ForegroundColor Red
    exit 2
}

# Natywne polecenie z przechwyceniem stdout/stderr. W Windows PowerShell 5.1 linia stderr przy
# $ErrorActionPreference = "Stop" i przekierowaniu 2>&1 staje sie bledem konczacym skrypt.
function Invoke-NativeCapture {
    param([Parameter(Mandatory = $true)][string]$Exe, [string[]]$Arguments = @())
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = @(& $Exe @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        return [pscustomobject]@{ Lines = $lines; ExitCode = $LASTEXITCODE }
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

# Pliki z katalogu launchera sa przekazywane QEMU wzglednie (katalog procesu = $ScriptDir), wiec
# polskie/Unicode znaki w katalogach nadrzednych nie trafiaja do argv QEMU.
function ConvertTo-QemuPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    $base = $ScriptDir.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if ($full.StartsWith($base, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ".\" + $full.Substring($base.Length)
    }
    return $full
}

# Wartosc file=... w opcjach QEMU: przecinek w sciezce musi byc podwojony.
function ConvertTo-QemuOptionPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (ConvertTo-QemuPath -Path $Path).Replace(",", ",,")
}

function Test-IsoFreshness {
    param([string]$IsoPath)
    $kernel = Join-Path $ScriptDir "system_operacyjny.bin"
    if ((Test-Path -LiteralPath $kernel -PathType Leaf) -and (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
        if ((Get-Item -LiteralPath $kernel).LastWriteTimeUtc -gt (Get-Item -LiteralPath $IsoPath).LastWriteTimeUtc) {
            Write-Host "UWAGA: $(Split-Path -Leaf $IsoPath) jest starszy niz system_operacyjny.bin - obraz moze byc nieaktualny (make -j1 iso)." -ForegroundColor Yellow
        }
    }
}

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
    if ($script:DiskIsTest) {
        Write-Host "Dysk testowy (jednorazowy): $Disk" -ForegroundColor Cyan
        if (Test-Path -LiteralPath $Disk -PathType Leaf) {
            if (-not $UseExistingTestDisk) { throw "Dysk testowy pojawil sie w miedzyczasie - odmowa nadpisania: $Disk" }
            Write-Host "Uzywam ISTNIEJACEGO dysku testowego (-UseExistingTestDisk), rozmiar $((Get-Item -LiteralPath $Disk).Length) B."
            return
        }
        # Bez qemu-img (ono nadpisuje istniejacy plik): CreateNew odmawia, gdy plik juz istnieje.
        $bytes = Convert-DiskSizeToBytes -Value $DiskSize
        $drive = New-Object System.IO.DriveInfo([System.IO.Path]::GetPathRoot($Disk))
        $need = $bytes + 1GB
        if ($drive.AvailableFreeSpace -lt $need) {
            throw "Za malo miejsca na $($drive.Name): wolne $([math]::Round($drive.AvailableFreeSpace/1GB,2)) GiB, wymagane $([math]::Round($need/1GB,2)) GiB (dysk $DiskSize moze zajac cala pojemnosc + 1 GiB zapasu na logi)."
        }
        $fs = $null
        try {
            $fs = [System.IO.File]::Open($Disk, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
            $fs.SetLength($bytes)
        } finally { if ($null -ne $fs) { $fs.Dispose() } }
        Write-Host "Utworzono nowy pusty dysk testowy RAW $DiskSize ($bytes B): $Disk"
        return
    }
    if (Test-Path -LiteralPath $Disk -PathType Leaf) {
        return
    }
    if (Test-Path -LiteralPath $Disk) {
        throw "wirtualny_dysk.img istnieje, ale nie jest plikiem (katalog?) - odmowa: $Disk"
    }

    Write-Host ""
    Write-Host "Tworzenie pustego dysku wirtualnego: $DiskSize"

    $qemuImg = Find-QemuImg
    if ($qemuImg) {
        $created = Invoke-NativeCapture -Exe $qemuImg -Arguments @("create", "-f", "raw", (ConvertTo-QemuPath -Path $Disk), $DiskSize)
        $created.Lines | ForEach-Object { Write-Host $_ }
        if ($created.ExitCode -ne 0) {
            throw "qemu-img nie utworzyl dysku. Kod: $($created.ExitCode)"
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

    $probe = Invoke-NativeCapture -Exe $Qemu -Arguments @("-display", "help")
    $available = $probe.Lines
    $exitCode = $probe.ExitCode
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

# AC97 tylko na jawne zadanie: ta instalacja QEMU musi miec urzadzenie AC97.
function Assert-QemuGuestAudioDevice {
    param([Parameter(Mandatory = $true)][string]$Qemu)
    if ($script:GuestAudio -ne "ac97") { return }
    $probe = Invoke-NativeCapture -Exe $Qemu -Arguments @("-device", "help")
    if ($probe.ExitCode -ne 0 -or -not (($probe.Lines -join "`n") -match '(?m)name "AC97"')) {
        throw "Ta instalacja QEMU nie ma urzadzenia AC97 (-GuestAudio ac97). QEMU nie zostalo uruchomione; uzyj domyslnego -GuestAudio hda."
    }
}

# Zamienia kontroler goscia HDA na AC97 w tym samym miejscu listy argumentow (ten sam slot PCI 00:02.0).
function Use-Ac97GuestDevice {
    param([Parameter(Mandatory = $true)][object[]]$Arguments)
    $out = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        if ($Arguments[$i] -eq "-device" -and $i + 1 -lt $Arguments.Count -and $Arguments[$i+1] -eq "intel-hda") {
            $out.Add("-device"); $out.Add("AC97,audiodev=snd"); $i++; continue
        }
        if ($Arguments[$i] -eq "-device" -and $i + 1 -lt $Arguments.Count -and $Arguments[$i+1] -eq "hda-output,audiodev=snd") {
            $i++; continue
        }
        $out.Add($Arguments[$i])
    }
    return ,$out.ToArray()
}

function Assert-QemuVirtioDevices {
    param([Parameter(Mandatory = $true)][string]$Qemu)

    $probe = Invoke-NativeCapture -Exe $Qemu -Arguments @("-device", "help")
    $deviceHelp = $probe.Lines -join "`n"
    if ($probe.ExitCode -ne 0) {
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

# ============================================================ DZWIEK HOSTA (backend -audiodev)
# Gosc ma zawsze ten sam Intel HDA (intel-hda + hda-output,audiodev=snd, przed VirtIO). Wybierany jest tylko
# backend hosta. Lista backendow pochodzi z wykrytego QEMU (-audiodev help); jawny wybor nie jest po cichu
# zamieniany na inny backend ani na "none". DirectSound (dsound) jest domyslny i zalecany w Windows 11.
$script:AudioModes = @("dsound", "sdl", "wav", "none")
# Tylko komunikaty w formacie QEMU (prefiks backendu albo -audiodev), nie linie portu szeregowego goscia.
$script:AudioQemuLinePattern = '(?i)^(qemu[^ :]*: )?(audio|dsound|sdl|wav|spice|dbus|jack)[^:]{0,24}:|-audiodev|sdl_openaudio'

function Get-AudioModeLabel {
    param([Parameter(Mandatory = $true)][string]$Name)
    switch ($Name) {
        "dsound" { return "DirectSound" }
        "sdl"    { return "SDL" }
        "wav"    { return "Nagrywanie dźwięku do WAV" }
        "none"   { return "Brak dźwięku" }
        default  { return $Name }
    }
}

function Get-AudioModeDescription {
    param([Parameter(Mandatory = $true)][string]$Name)
    switch ($Name) {
        "dsound" { return "głośniki Windows przez DirectSound (zalecane)" }
        "sdl"    { return "alternatywne wyjście dźwięku przez SDL" }
        "wav"    { return "zapis dźwięku gościa do pliku WAV, bez odtwarzania" }
        "none"   { return "bez dźwięku (diagnostyka); Intel HDA zostaje" }
        default  { return "" }
    }
}

# Parsuje wyjscie `-audiodev help`: linie z sama nazwa [a-z0-9_]; naglowki (z ':') i puste linie pomijane.
function ConvertFrom-AudiodevHelp {
    param([string[]]$Lines = @())
    $names = @()
    foreach ($line in $Lines) {
        $t = ([string]$line).Trim()
        if ($t -match '^[a-z0-9_]+$') { $names += $t }
    }
    return ,$names
}

# Zwraca liste backendow albo $null, gdy QEMU nie podal listy (stara wersja / blad uruchomienia).
function Get-QemuAudioBackends {
    param([Parameter(Mandatory = $true)][string]$Qemu)
    try {
        $probe = Invoke-NativeCapture -Exe $Qemu -Arguments @("-audiodev", "help")
    }
    catch {
        return $null
    }
    if ($probe.ExitCode -ne 0) { return $null }
    $names = ConvertFrom-AudiodevHelp -Lines $probe.Lines
    if ($names.Count -eq 0) { return $null }
    return ,$names
}

function Get-QemuVersionLine {
    param([Parameter(Mandatory = $true)][string]$Qemu)
    try {
        $v = Invoke-NativeCapture -Exe $Qemu -Arguments @("--version")
        if ($v.Lines.Count -gt 0) { return $v.Lines[0] }
    }
    catch { }
    return "(nieznana - QEMU nie podal wersji)"
}

# Sciezka nagrania: domyslnie logi_qemu\nagrania_audio\bursztyn-hda-DATA.wav (unikalna); wzgledna wzgledem
# katalogu wywolania; katalog = nazwa domyslna w nim. Istniejacy plik: tylko z -AudioOverwrite albo po
# potwierdzeniu w trybie interaktywnym. Dowiazania (reparse point) sa odrzucane.
function Resolve-AudioWavPath {
    param([switch]$ReadOnly)
    $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $requested = $script:AudioWavSelection
    $useDefaultName = $false
    if ([string]::IsNullOrWhiteSpace($requested)) {
        $target = Join-Path $LogDir "nagrania_audio"
        $useDefaultName = $true
    }
    elseif ([System.IO.Path]::IsPathRooted($requested)) {
        $target = [System.IO.Path]::GetFullPath($requested)
    }
    else {
        $target = [System.IO.Path]::GetFullPath((Join-Path $InvocationDir $requested))
    }
    if (-not $useDefaultName -and ((Test-Path -LiteralPath $target -PathType Container) -or $requested.EndsWith("\") -or $requested.EndsWith("/"))) {
        $useDefaultName = $true
    }
    if ($useDefaultName) {
        $base = Join-Path $target "bursztyn-hda-$stamp"
        $candidate = "$base.wav"
        $n = 2
        while (Test-Path -LiteralPath $candidate) {
            $candidate = "$base-$n.wav"
            $n++
        }
        $target = $candidate
    }
    if (Test-Path -LiteralPath $target) {
        $item = Get-Item -LiteralPath $target -Force
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "Ścieżka nagrania jest dowiązaniem - odmowa: $target"
        }
        if ($item.PSIsContainer) {
            throw "Ścieżka nagrania istnieje i nie jest plikiem: $target"
        }
        if (-not $AudioOverwrite -and -not $ReadOnly) {
            if ($script:Interactive) {
                $answer = Read-Host "Plik nagrania istnieje: $target - nadpisać? [t/N]"
                if ($answer -ne "t" -and $answer -ne "T") {
                    throw "Anulowano - istniejące nagranie nie zostało nadpisane."
                }
            }
            else {
                throw "Plik nagrania już istnieje (nie nadpisuję bez -AudioOverwrite): $target"
            }
        }
    }
    if (-not $ReadOnly) {
        $dir = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
    }
    return $target
}

# Konfiguracja dzwieku: { Mode; Arg (jedna wartosc -audiodev); WavPath; HostInfo }. Bez cichego fallbacku.
function Resolve-AudioConfig {
    param([Parameter(Mandatory = $true)][string]$Qemu, [switch]$ReadOnly)
    $mode = $script:AudioSelection
    if ($script:AudioModes -notcontains $mode) {
        throw "Nieznany tryb dźwięku: $mode (dsound, sdl, wav, none)."
    }
    $hostInfo = "$(Get-AudioModeLabel -Name $mode) - $(Get-AudioModeDescription -Name $mode)"
    if (-not $ReadOnly) {
        $backends = Get-QemuAudioBackends -Qemu $Qemu
        if ($null -eq $backends) {
            Write-Host "UWAGA: nie udało się odczytać listy backendów QEMU (-audiodev help) - nie można sprawdzić '$mode'." -ForegroundColor Yellow
            $hostInfo += " (niesprawdzony)"
        }
        elseif ($backends -notcontains $mode) {
            throw ("Błąd uruchomienia dźwięku: ta instalacja QEMU nie obsługuje backendu '$mode' ($(Get-AudioModeLabel -Name $mode)).`n" +
                   "Dostępne backendy: $($backends -join ', ').`nWybierz inne wyjście dźwięku (menu 'A' albo -AudioMode ...).")
        }
        if ($mode -eq "dsound") {
            try {
                $devices = @(Get-CimInstance -ClassName Win32_SoundDevice -ErrorAction Stop)
                if ($devices.Count -eq 0) {
                    Write-Host "UWAGA: Windows nie zgłasza żadnego urządzenia dźwiękowego - DirectSound może nie mieć wyjścia." -ForegroundColor Yellow
                }
            }
            catch { }
        }
    }
    $wav = ""
    $arg = "$mode,id=snd"
    if ($mode -eq "wav") {
        $wav = Resolve-AudioWavPath -ReadOnly:$ReadOnly
        $qemuWav = ConvertTo-QemuOptionPath -Path $wav
        if (-not $qemuWav.StartsWith(".\") -and $qemuWav -match '[^\x20-\x7E]') {
            Write-Host "UWAGA: ścieżka nagrania poza katalogiem launchera zawiera znaki spoza ASCII; QEMU w Windows może jej nie otworzyć. Bezpieczniej: nagranie w katalogu launchera." -ForegroundColor Yellow
        }
        $arg = "wav,id=snd,path=$qemuWav"
        $hostInfo = "nagrywanie do pliku WAV (bez odtwarzania na głośnikach)"
    }
    return [pscustomobject]@{ Mode = $mode; Arg = $arg; WavPath = $wav; HostInfo = $hostInfo }
}

# Klasyfikacja komunikatow QEMU o dzwieku. Ostrzezenia o nagrywaniu (capture - brak mikrofonu) nie
# blokuja odtwarzania i nie sa bledem. Wynik: unsupported, device, file, init albo "".
function Get-AudioFailureKind {
    param([string[]]$Lines = @())
    # DirectSoundCapture (przechwytywanie DirectSound) to czesc inicjalizacji backendu dsound w QEMU: jej blad jest
    # bledem inicjalizacji dzwieku hosta (nie bledem sterownika HDA Bursztyn OS i nie bledem niezwiazanym z dzwiekiem).
    if (@($Lines | Where-Object { $_ -match $script:AudioQemuLinePattern -and $_ -match '(?i)DirectSoundCapture' }).Count -gt 0) { return "capture" }
    # Pozostale komunikaty o nagrywaniu (np. brak mikrofonu) nie dotycza odtwarzania - pomijane.
    $relevant = @($Lines | Where-Object { $_ -match $script:AudioQemuLinePattern -and $_ -notmatch '(?i)capture' })
    if ($relevant.Count -eq 0) { return "" }
    $text = $relevant -join "`n"
    if ($text -match "(?i)unknown audio driver|audiodev.*(invalid|unknown|not supported)|parameter 'driver'|invalid parameter value.*driver") { return "unsupported" }
    if ($text -match '(?i)no (available )?audio device|no such device|could not open.*device|DSERR_NODRIVER|no sound driver|DSERR_ALLOCATED') { return "device" }
    if ($text -match '(?i)wav: |wave file') { return "file" }
    if ($text -match '(?i)could not init|failed to (create|initiali[sz]e)|sdl_openaudio|init.*fail') { return "init" }
    return ""
}

function Write-AudioOutcome {
    param([int]$ExitCode, [string[]]$Lines = @(), [Parameter(Mandatory = $true)]$Audio)
    $kind = Get-AudioFailureKind -Lines $Lines
    $script:LastAudioFailure = $kind
    $label = Get-AudioModeLabel -Name $Audio.Mode
    Write-Host ""
    switch ($kind) {
        "unsupported" { Write-Host "Błąd uruchomienia dźwięku: backend $label nie jest obsługiwany przez to QEMU." -ForegroundColor Red }
        "device"      { Write-Host "Błąd uruchomienia dźwięku: brak urządzenia wyjściowego dla $label." -ForegroundColor Red }
        "file"        { Write-Host "Błąd nagrywania: QEMU nie mógł zapisać pliku WAV." -ForegroundColor Red }
        "init"        { Write-Host "Błąd uruchomienia dźwięku: inicjalizacja $label nie powiodła się." -ForegroundColor Red }
        "capture"     {
            if ($ExitCode -ne 0) { Write-Host "Błąd inicjalizacji dźwięku: QEMU nie mógł zainicjalizować DirectSoundCapture (przechwytywanie DirectSound na hoście)." -ForegroundColor Red }
            else { Write-Host "Ostrzeżenie dźwięku: QEMU zgłosił błąd inicjalizacji DirectSoundCapture (przechwytywanie DirectSound na hoście); odtwarzanie DirectSound mogło nie działać." -ForegroundColor Yellow }
            Write-Host "To problem backendu dźwięku hosta Windows, nie sterownika Intel HDA Bursztyn OS. Działające wyjścia: -AudioMode sdl (odsłuch), -AudioMode wav (nagranie)."
        }
    }
    if ($kind) {
        Write-Host "Komunikaty QEMU:"
        $Lines | Where-Object { $_ -match $script:AudioQemuLinePattern } | Select-Object -First 8 | ForEach-Object { Write-Host "  $_" }
        if ($script:Interactive) {
            Write-Host "Wybierz 'A' w menu, aby zmienić wyjście dźwięku (np. SDL), i uruchom ponownie." -ForegroundColor Yellow
        }
        else {
            Write-Host "Uruchom ponownie z innym wyjściem dźwięku, np. -AudioMode sdl. Launcher nie przełącza go samodzielnie." -ForegroundColor Yellow
        }
    }
    elseif ($ExitCode -ne 0) {
        Write-Host "QEMU zakończył się kodem $ExitCode bez komunikatów o dźwięku - przyczyna niezwiązana z wyborem dźwięku (zob. wyjście powyżej)."
    }
    elseif ($Audio.Mode -eq "dsound" -or $Audio.Mode -eq "sdl") {
        Write-Host "Dźwięk: brak komunikatów o błędach $label (to nie potwierdza słyszalnego odtwarzania)."
    }
    if ($Audio.WavPath) {
        if (Test-Path -LiteralPath $Audio.WavPath -PathType Leaf) {
            $size = (Get-Item -LiteralPath $Audio.WavPath).Length
            Write-Host "Nagranie WAV: $($Audio.WavPath) ($size B; dźwięk wyjścia HDA gościa, nie biblioteka muzyki Bursztyn OS)"
        }
        else {
            Write-Host "Nagranie WAV nie powstało: $($Audio.WavPath)" -ForegroundColor Yellow
        }
    }
}

# Interaktywny wybor wyjscia dzwieku (menu glowne: A).
function Show-AudioMenu {
    $backends = $null
    try { $backends = Get-QemuAudioBackends -Qemu (Find-Qemu) } catch { Write-Host "UWAGA: $($_.Exception.Message)" -ForegroundColor Yellow }
    Write-Host ""
    Write-Host "================ Wyjście dźwięku (gość: Intel HDA) ================"
    for ($i = 0; $i -lt $script:AudioModes.Count; $i++) {
        $m = $script:AudioModes[$i]
        $state = if ($null -eq $backends) { "dostępność niesprawdzona" } elseif ($backends -contains $m) { "dostępny" } else { "NIEDOSTĘPNY w tej instalacji QEMU" }
        Write-Host ("  {0} - {1,-26} {2}" -f ($i + 1), (Get-AudioModeLabel -Name $m), (Get-AudioModeDescription -Name $m))
        Write-Host "      $state"
    }
    Write-Host "  0 - powrót (bez zmian; obecnie: $(Get-AudioModeLabel -Name $script:AudioSelection))"
    $pick = Read-Host "Wybierz wyjście dźwięku"
    if ([string]::IsNullOrWhiteSpace($pick) -or $pick -eq "0") { return }
    $index = 0
    if (-not [int]::TryParse($pick, [ref]$index) -or $index -lt 1 -or $index -gt $script:AudioModes.Count) {
        Write-Host "Nieprawidłowy wybór." -ForegroundColor Red
        return
    }
    $m = $script:AudioModes[$index - 1]
    if ($null -ne $backends -and $backends -notcontains $m) {
        Write-Host "$(Get-AudioModeLabel -Name $m) jest niedostępny w tej instalacji QEMU (dostępne: $($backends -join ', '))." -ForegroundColor Red
        return
    }
    if ($m -eq "wav") {
        $script:AudioWavSelection = Read-Host "Ścieżka nagrania (Enter = logi_qemu\nagrania_audio\...)"
    }
    $script:AudioSelection = $m
    Write-Host "Wyjście dźwięku: $(Get-AudioModeLabel -Name $m)" -ForegroundColor Green
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
    # Dzwiek przed dyskiem: blad konfiguracji dzwieku nie tworzy dysku i nie uruchamia QEMU.
    $audio = Resolve-AudioConfig -Qemu $qemu -ReadOnly:($PrintArgv -or $Audit)
    if (-not ($PrintArgv -or $Audit)) { Assert-QemuGuestAudioDevice -Qemu $qemu }
    # Suma ISO PRZED utworzeniem dysku testowego: niezgodne ISO nie zostawia pustego dysku i nie uruchamia QEMU.
    if (-not ($PrintArgv -or $Audit) -and -not [string]::IsNullOrWhiteSpace($IsoSha256)) {
        if ([string]::IsNullOrWhiteSpace($IsoPath)) {
            Write-Host "-IsoSha256: ten tryb nie uzywa ISO - suma nie jest sprawdzana." -ForegroundColor Yellow
        } else {
            if (-not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) { throw "Nie znaleziono obrazu ISO: $IsoPath" }
            $actualSha = (Get-FileHash -LiteralPath $IsoPath -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actualSha -ne $IsoSha256.ToLowerInvariant()) {
                throw "SHA-256 ISO niezgodne - przerwano przed utworzeniem dysku i uruchomieniem QEMU.`n  plik:       $IsoPath`n  oczekiwano: $($IsoSha256.ToLowerInvariant())`n  jest:       $actualSha"
            }
            Write-Host "SHA-256 ISO zgodne: $actualSha ($IsoPath)" -ForegroundColor Green
        }
    }
    if (-not ($PrintArgv -or $Audit)) { Prepare-Disk }

    $hasIso = -not [string]::IsNullOrWhiteSpace($IsoPath)
    if ($hasIso) {
        if (-not ($PrintArgv -or $Audit) -and -not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
            throw "Nie znaleziono obrazu ISO: $IsoPath"
        }
        if (-not ($PrintArgv -or $Audit)) {
            $IsoPath = (Resolve-Path -LiteralPath $IsoPath).ProviderPath
            Test-IsoFreshness -IsoPath $IsoPath
        }
    }
    $diskOpt = ConvertTo-QemuOptionPath -Path $Disk

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

    # Linux dodaje HDA przed urzadzeniami VirtIO; Windows zachowuje identyczna topologie PCI. Jeden
    # -audiodev (id=snd) i jedno HDA; backend hosta z Resolve-AudioConfig (domyslnie dsound).
    $argsList += @(
        "-audiodev", $audio.Arg,
        "-device", "intel-hda",
        "-device", "hda-output,audiodev=snd"
    )
    if ($script:GuestAudio -eq "ac97") { $argsList = Use-Ac97GuestDevice -Arguments $argsList }

    if ($VirtIoFull) {
        $blkSpec = "virtio-blk-pci,drive=disk,disable-legacy=on"
        if (-not $hasIso) {
            $blkSpec += ",bootindex=1"
        }

        # Kolejnosc zgodna z dzialajacym launcherem Linux/macOS:
        # HDA -> BLK -> NET -> RNG -> GPU -> keyboard -> mouse.
        # Input celowo BEZ disable-legacy=on, tak jak na Linuxie.
        $argsList += @(
            "-drive", "id=disk,file=$diskOpt,format=raw,if=none",
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
            "-drive", "id=disk,file=$diskOpt,format=raw,if=none",
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
        $argsList = @("-bios", (ConvertTo-QemuPath -Path $ovmf)) + $argsList
    }

    if ($hasIso) {
        # Zachowujemy windowsowy wymuszony start z ISO, aby istniejacy dysk nie przejal bootu.
        $argsList += @(
            "-boot", "order=d,menu=on",
            "-cdrom", (ConvertTo-QemuPath -Path $IsoPath)
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
    if ($PointerDiagnostics -or $SerialLog) { $SaveLog = $true }

    $argsList += @("-name", "Bursztyn OS - $ModeName")

    $pciDevices = @()
    for ($i=0; $i -lt $argsList.Count-1; $i++) {
        if ($argsList[$i] -eq "-device" -and $argsList[$i+1] -match '^(intel-hda|AC97|virtio-.*-pci|virtio-vga)(,|$)') {
            $pciDevices += $argsList[$i+1]
        }
    }
    $topology = @()
    $classes = @{
        "intel-hda"="audio"; "AC97"="audio"; "virtio-blk-pci"="storage"; "virtio-net-pci"="network"
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
    $report.disk = $Disk
    $report.disk_is_test = $script:DiskIsTest
    $report.display_backend = $effectiveDisplay
    $report.audio_mode = $audio.Mode
    $report.audiodev = $audio.Arg
    if ($audio.WavPath) { $report.audio_wav_path = $audio.WavPath }
    if ($Audit) {
        $report.version = (Invoke-NativeCapture -Exe $qemu -Arguments @("--version")).Lines
        $report.machines = (Invoke-NativeCapture -Exe $qemu -Arguments @("-machine", "help")).Lines
        $report.virtio_mouse_properties = (Invoke-NativeCapture -Exe $qemu -Arguments @("-device", "virtio-mouse-pci,help")).Lines
        $report.audio_backends = Get-QemuAudioBackends -Qemu $qemu
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
    Write-Host "GPU hint:     ${GuestWidth}x${GuestHeight}; guest boot/config/Display Settings select the physical mode"
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
    Write-Host "Wyjście dźwięku: $(Get-AudioModeLabel -Name $audio.Mode) (-audiodev $($audio.Arg))"
    Write-Host "Wersja QEMU:     $(Get-QemuVersionLine -Qemu $qemu)"
    if ($script:GuestAudio -eq "ac97") {
        Write-Host "Kontroler dźwięku gościa: Intel ICH AC97 (AC97,audiodev=snd, PCI 00:02.0) - tryb deweloperski, bez Intel HDA"
    } else {
        Write-Host "Wykryte urządzenie Intel HDA: intel-hda + hda-output,audiodev=snd (PCI 00:02.0, przed VirtIO)"
    }
    Write-Host "Dźwięk hosta:    $($audio.HostInfo)"
    if ($audio.WavPath) { Write-Host "Ścieżka nagrania: $($audio.WavPath)" }
    Write-Host "ISO:          $(if ($hasIso) { $IsoPath } else { '(brak - start z dysku)' })"
    Write-Host "Dysk:         $Disk $(if ($script:DiskIsTest) { '(TESTOWY, jednorazowy - -TestDisk)' } else { '(trwaly, zapis wlaczony)' })"
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
        -MaxWidth $WindowWidth -MaxHeight $WindowHeight
    # Port szeregowy goscia nadaje UTF-8 (polskie znaki); stderr QEMU nie moze przerwac skryptu.
    $previousPreference = $ErrorActionPreference
    $previousOutputEncoding = [Console]::OutputEncoding
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $exitCode = 0
    # Kopia komunikatow QEMU o dzwieku (stdout+stderr nadal wypisywane w calosci) do klasyfikacji bledu.
    $audioLines = New-Object System.Collections.Generic.List[string]
    $audioPattern = $script:AudioQemuLinePattern
    try {
        $ErrorActionPreference = "Continue"
        try { [Console]::OutputEncoding = $utf8 } catch { }
        if ($SaveLog) {
            if (-not (Test-Path -LiteralPath $LogDir)) {
                New-Item -ItemType Directory -Path $LogDir | Out-Null
            }

            $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
            $safeName = ($ModeName -replace '[^A-Za-z0-9_-]', '_')
            $logFile = Join-Path $LogDir "${stamp}_${safeName}.log"

            Write-Host "Log COM1: $logFile"
            $writer = New-Object System.IO.StreamWriter($logFile, $false, $utf8)
            try {
                & $qemu @argsList 2>&1 | ForEach-Object {
                    $line = $_.ToString()
                    Write-Host $line
                    $writer.WriteLine($line)
                    $writer.Flush()
                    if ($line -match $audioPattern -and $audioLines.Count -lt 40) { $audioLines.Add($line) }
                }
                $exitCode = $LASTEXITCODE
            }
            finally {
                $writer.Dispose()
            }
        }
        else {
            & $qemu @argsList 2>&1 | ForEach-Object {
                $line = $_.ToString()
                Write-Host $line
                if ($line -match $audioPattern -and $audioLines.Count -lt 40) { $audioLines.Add($line) }
            }
            $exitCode = $LASTEXITCODE
        }
    }
    finally {
        $ErrorActionPreference = $previousPreference
        try { [Console]::OutputEncoding = $previousOutputEncoding } catch { }
        if ($windowLimiter) {
            Stop-Job -Job $windowLimiter -ErrorAction SilentlyContinue | Out-Null
            Remove-Job -Job $windowLimiter -Force -ErrorAction SilentlyContinue
        }
    }

    $script:LastQemuExitCode = $exitCode
    Write-AudioOutcome -ExitCode $exitCode -Lines $audioLines.ToArray() -Audio $audio
    if ($exitCode -ne 0) {
        Write-Host ""
        Write-Host "QEMU zakonczyl sie kodem $exitCode." -ForegroundColor Red
    }
}

function Invoke-LauncherMode {
    param([Parameter(Mandatory = $true)][string]$Choice)
    switch ($Choice) {
        "1"  { Start-BursztynQemu -ModeName "Normalny BIOS VirtIO Full" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false }
        "2"  { Start-BursztynQemu -ModeName "Normalny UEFI VirtIO Full" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $false }

        "3"  { Start-BursztynQemu -ModeName "Testy GRUB BIOS VirtIO" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }
        "4"  { Start-BursztynQemu -ModeName "Testy GRUB UEFI VirtIO" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

        "5"  {
            if (-not ($PrintArgv -or $Audit) -and -not (Test-Path -LiteralPath $IsoCiTest -PathType Leaf)) { throw "Nie znaleziono: $IsoCiTest" }
            Start-BursztynQemu -ModeName "CI BIOS VirtIO" -Firmware "BIOS" -IsoPath $IsoCiTest -VirtIoFull $true -SaveLog $true
        }
        "6"  {
            if (-not ($PrintArgv -or $Audit) -and -not (Test-Path -LiteralPath $IsoCiTest -PathType Leaf)) { throw "Nie znaleziono: $IsoCiTest" }
            Start-BursztynQemu -ModeName "CI UEFI VirtIO" -Firmware "UEFI" -IsoPath $IsoCiTest -VirtIoFull $true -SaveLog $true
        }

        "7"  { Start-BursztynQemu -ModeName "Interaktywne BIOS VirtIO" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }
        "8"  { Start-BursztynQemu -ModeName "Interaktywne UEFI VirtIO" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

        "9"  { Start-BursztynQemu -ModeName "Ekran BIOS VirtIO-GPU" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }
        "10" { Start-BursztynQemu -ModeName "Ekran UEFI VirtIO-GPU" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

        "11" {
            $script:DisplayBackend = "gtk"
            Write-Host "Backend ustawiony na GTK." -ForegroundColor Green
        }
        "12" {
            $script:DisplayBackend = "sdl"
            Write-Host "Backend ustawiony na SDL." -ForegroundColor Yellow
        }
        "13" { Write-Host "Tryb 13 nie istnieje w launcherze Windows (usuwanie dysku jest tylko w launcherze Linux, z potwierdzeniem)." -ForegroundColor Yellow }

        "14" { Start-BursztynQemu -ModeName "Zainstalowany UEFI VirtIO Full" -Firmware "UEFI" -IsoPath "" -VirtIoFull $true -SaveLog $false }
        "15" { Start-BursztynQemu -ModeName "Instalator UEFI VirtIO Full" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $true -SaveLog $true }

        "16" { Start-BursztynQemu -ModeName "BIOS Legacy" -Firmware "BIOS" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $false }
        "17" { Start-BursztynQemu -ModeName "UEFI Legacy" -Firmware "UEFI" -IsoPath $IsoNormal -VirtIoFull $false -SaveLog $false }
        "18" { Start-BursztynQemu -ModeName "Zainstalowany UEFI Legacy" -Firmware "UEFI" -IsoPath "" -VirtIoFull $false -SaveLog $false }

        "A" { Show-AudioMenu }   # switch PowerShell nie rozroznia wielkosci liter: "a" tez trafia tutaj

        "0" { exit 0 }

        default {
            Write-Host "Nieprawidlowy wybor." -ForegroundColor Red
        }
    }
}

if ($PrintArgv -or $Audit) {
    # Bez -Mode: dotychczasowy raport dla trybu 2 (UEFI + VirtIO Full).
    $reportMode = if ([string]::IsNullOrWhiteSpace($Mode)) { "2" } else { $Mode }
    if (@("0", "11", "12", "13") -contains $reportMode) { throw "Tryb $reportMode nie uruchamia QEMU - brak argv do wypisania." }
    Invoke-LauncherMode -Choice $reportMode
    exit 0
}
if (-not [string]::IsNullOrWhiteSpace($Mode)) {
    try {
        Invoke-LauncherMode -Choice $Mode
    }
    catch {
        Write-Host ""
        Write-Host "BLAD: $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
    exit $script:LastQemuExitCode
}
if ($PSBoundParameters.ContainsKey("DisplayBackend")) {
    Write-Host "Frontend z parametru -DisplayBackend: $($DisplayBackend.ToUpperInvariant()). Wybierz tryb z menu albo podaj -Mode (np. -Mode 2)." -ForegroundColor Cyan
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

 FRONTEND WINDOWS (obecnie: $(if ([string]::IsNullOrWhiteSpace($script:DisplayBackend)) { "GTK" } else { $script:DisplayBackend.ToUpperInvariant() }))
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

 WYJŚCIE DŹWIĘKU (gość: Intel HDA)
  A  - zmień (obecnie: $(Get-AudioModeLabel -Name $script:AudioSelection))

  0  - Wyjscie

"@

    $choice = Read-Host "Wybierz tryb [0-18, A]"

    try {
        Invoke-LauncherMode -Choice $choice
    }
    catch {
        Write-Host ""
        Write-Host "BLAD: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host ""
    }
}
