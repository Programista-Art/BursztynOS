"""Current canonical launcher policy; no VM and no historical/comment parsing."""
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]

class WindowsLauncherConfig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.launcher=(ROOT/'runwindows.ps1').read_text(encoding='utf-8-sig')
        cls.makefile=(ROOT/'Makefile').read_text()
    def test_windows_default_and_empty_override_use_gtk(self):
        self.assertRegex(self.launcher,r'\[string\]\$DisplayBackend\s*=\s*"gtk"')
        self.assertIn('{ "gtk" } else { $DisplayBackend }',self.launcher)
        self.assertIn('"-display", $displayArg',self.launcher)
        self.assertIn('"gtk,zoom-to-fit=on"',self.launcher)
        self.assertIn('Start-QemuWindowLimiter',self.launcher)
    def test_virtio_devices(self):
        devices=re.findall(r'"-device",\s*"([^"\n]+)"',self.launcher.split('# Explicit diagnostic A/B selection.')[0])
        names=[d.split(',')[0] for d in devices]
        self.assertEqual(names.count('virtio-mouse-pci'),1)
        self.assertEqual(names.count('virtio-keyboard-pci'),1)
        self.assertNotIn('virtio-tablet-pci',names)
        gpu=next(d for d in devices if d.startswith('virtio-vga,'))
        for option in ('disable-legacy=on','xres=$GuestWidth','yres=$GuestHeight'):self.assertIn(option,gpu.split(','))
        self.assertRegex(self.launcher,r'\$GuestWidth\s*=\s*1280')
        self.assertRegex(self.launcher,r'\$GuestHeight\s*=\s*720')
    def test_pointer_probe_is_explicit_and_loopback_only(self):
        self.assertIn('[string]$PointerDevice = "relative"',self.launcher)
        self.assertIn('if ($PointerDevice -ne "relative")',self.launcher)
        self.assertIn('"virtio-tablet" { $argsList += @("-device", "virtio-tablet-pci") }',self.launcher)
        self.assertIn('"usb-tablet,bus=$tabletBus"',self.launcher)
        self.assertIn('tcp:127.0.0.1:$QmpPort,server=on,wait=off',self.launcher)
        self.assertIn('if ($QmpPort -gt 0)',self.launcher)
        self.assertIn('if ($PointerDiagnostics -or $SerialLog) { $SaveLog = $true }',self.launcher)
    def test_tcg_single_for_canonical_profiles(self):
        self.assertIn('"-accel", "tcg,thread=single"',self.launcher)
        self.assertRegex(self.makefile,r'(?m)^QEMUACCEL\s*\?=\s*tcg,thread=single$')
        self.assertNotIn('"tcg,thread=multi"',self.launcher)
    def test_explicit_sdl_and_return_to_gtk(self):
        self.assertRegex(self.launcher,r'"11"\s*\{\s*\$script:DisplayBackend = "gtk"')
        self.assertRegex(self.launcher,r'"12"\s*\{\s*\$script:DisplayBackend = "sdl"')
        self.assertIn('[ValidateSet("gtk", "sdl", "")]',self.launcher)
    def test_display_status(self):
        self.assertIn('Write-Host "Display:      $($effectiveDisplay.ToUpperInvariant())"',self.launcher)
        self.assertIn('Assert-QemuDisplay -Qemu $qemu -Backend $effectiveDisplay',self.launcher)
    def test_disposable_test_disk_is_explicit_and_never_overwrites(self):
        code=self.launcher
        self.assertIn('[string]$TestDisk = ""',code)
        self.assertIn('[switch]$UseExistingTestDisk',code)
        self.assertIn("[ValidatePattern('^$|^[0-9a-fA-F]{64}$')]",code)
        # trwale nazwy dyskow uzytkownika sa odrzucane, istniejacy plik wymaga jawnej zgody
        self.assertIn('foreach ($trwaly in @("wirtualny_dysk.img", "wirtualnydysk.img"))',code)
        self.assertIn('plik juz istnieje i NIE zostanie nadpisany',code)
        self.assertIn('-UseExistingTestDisk: plik nie istnieje',code)
        self.assertIn('physicaldrive|harddisk',code)
        # dysk testowy: tylko CreateNew (qemu-img create nadpisalby plik), kontrola wolnego miejsca
        prep=code.split('function Prepare-Disk {',1)[1].split('\n}\n',1)[0]
        test_part=prep.split('if (Test-Path -LiteralPath $Disk -PathType Leaf) {\n        return',1)[0]
        self.assertIn('[System.IO.FileMode]::CreateNew',test_part)
        self.assertNotIn('qemu-img',test_part.split('# Bez qemu-img',1)[1])
        self.assertIn('AvailableFreeSpace -lt $need',test_part)
        # trwaly dysk bez -TestDisk bez zmian
        self.assertIn('$Disk = Join-Path $ScriptDir "wirtualny_dysk.img"',code)
        self.assertIn('Get-FileHash -LiteralPath $IsoPath -Algorithm SHA256',code)
        self.assertIn('if ($PointerDiagnostics -or $SerialLog) { $SaveLog = $true }',code)
        self.assertIn("'(TESTOWY, jednorazowy - -TestDisk)'",code)
        # suma ISO sprawdzana przed utworzeniem dysku testowego
        self.assertLess(code.index('Get-FileHash -LiteralPath $IsoPath -Algorithm SHA256'),code.index('{ Prepare-Disk }'))
    def test_legacy_usb_profile(self):
        self.assertIn('"usb-kbd,id=usbkbd,bus=xhci.0"',self.launcher)
        self.assertIn('"usb-mouse,id=usbmouse,bus=xhci.0"',self.launcher)
        self.assertIn('"-vga", "std"',self.launcher)
        self.assertIn('"-device", "ich9-ahci,id=ahci"',self.launcher)
    def test_backend_unavailable_has_explicit_error_and_no_fallback(self):
        self.assertIn('Invoke-NativeCapture -Exe $Qemu -Arguments @("-display", "help")',self.launcher)
        self.assertIn('throw "QEMU nie udostepnia backendu',self.launcher)
        self.assertEqual(self.launcher.count('$script:DisplayBackend = "sdl"'),1)
        self.assertLess(self.launcher.index('Assert-QemuDisplay -Qemu $qemu'),self.launcher.index('{ Prepare-Disk }'))
    def test_known_good_pci_order_and_firmware_storage_preserved(self):
        code=self.launcher
        ordered=['"-device", "intel-hda"','$blkSpec,','"-device", "virtio-net-pci,','"-device", "virtio-rng-pci,','"-device", "virtio-vga,','"-device", "virtio-keyboard-pci"','"-device", "virtio-mouse-pci"']
        positions=[code.index(s) for s in ordered];self.assertEqual(positions,sorted(positions))
        self.assertIn('$argsList = @("-bios", (ConvertTo-QemuPath -Path $ovmf)) + $argsList',code)
        self.assertEqual(code.count('"-drive", "id=disk,file=$diskOpt,format=raw,if=none"'),2)
        # Backend dzwieku z wyboru (-AudioMode / menu A, domyslnie dsound); HDA i jego slot PCI bez zmian.
        self.assertIn('"-audiodev", $audio.Arg,',code)
        self.assertNotIn('"-audiodev", "none,id=snd"',code)
        self.assertIn('-audiodev alsa,id=snd',self.makefile)
        self.assertIn('"-boot", "order=d,menu=on"',code)
    def test_linux_canonical_and_print_only_policy(self):
        self.assertRegex(self.makefile,r'(?m)^QEMUDISPLAYARGS\s*\?=\s*\$\(if \$\(strip \$\(QEMU_DISPLAY_ARGS\)\),\$\(QEMU_DISPLAY_ARGS\),-display sdl\)$')
        common=self.makefile.split('QEMU_VIRTIO_COMMON_ARGS :=',1)[1].split('\n\n',1)[0]
        for name in ('virtio-keyboard-pci','virtio-mouse-pci','rng-builtin'):self.assertIn(name,common)
        # Linux pointer policy (2026-11-11): absolute tablet appended LAST, after
        # the relative mouse fallback; the Windows default stays relative.
        self.assertEqual(common.count('virtio-tablet-pci'),1)
        self.assertLess(common.index('virtio-mouse-pci'),common.index('virtio-tablet-pci'))
        self.assertTrue(common.rstrip().endswith('-device virtio-tablet-pci'))
        self.assertIn('[switch]$PrintArgv',self.launcher)
        self.assertIn('if ($PrintArgv -or $Audit) { $report | ConvertTo-Json -Depth 8; return }',self.launcher)
        self.assertIn('Get-UefiFirmware -ReadOnly',self.launcher)
        self.assertIn('runuefi-argv:',self.makefile)

    # Audyt 2026-11-17: bezpieczne wywolania natywne, sciezki, kod wyjscia, dysk.
    def test_native_calls_do_not_abort_on_stderr(self):
        code=self.launcher
        self.assertIn('function Invoke-NativeCapture',code)
        self.assertIn('$ErrorActionPreference = "Continue"',code)
        self.assertNotIn('Tee-Object',code)            # PS 5.1: UTF-16 i blad przy stderr
        self.assertNotIn('& $qemuImg create',code)
        self.assertIn('New-Object System.IO.StreamWriter($logFile, $false, $utf8)',code)
    def test_paths_working_directory_and_commas(self):
        code=self.launcher
        self.assertIn('[System.Environment]::CurrentDirectory = $ScriptDir',code)
        self.assertIn('Join-Path $InvocationDir $Iso',code)
        self.assertIn('.Replace(",", ",,")',code)
        self.assertIn('"-cdrom", (ConvertTo-QemuPath -Path $IsoPath)',code)
        self.assertNotIn('C:\\Users',code)
    def test_mode_parameter_and_exit_code(self):
        code=self.launcher
        self.assertIn("[ValidatePattern('^$|^([0-9]|1[0-8])$')]",code)
        self.assertIn('exit $script:LastQemuExitCode',code)
        self.assertIn('$script:LastQemuExitCode = $exitCode',code)
    def test_disk_never_deleted_or_overwritten(self):
        code=self.launcher
        self.assertIn('[System.IO.FileMode]::CreateNew',code)
        self.assertNotRegex(code,r'Remove-Item[^\n]*\$Disk')
        self.assertNotIn('FileMode]::Create,\n            [System.IO.FileAccess]::ReadWrite',code)
        self.assertIn('nie jest plikiem (katalog?) - odmowa',code)

if __name__=='__main__':unittest.main()
