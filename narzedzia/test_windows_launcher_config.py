"""Static launcher policy regressions; never execute QEMU or modify disks."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WindowsLauncherConfig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.launcher = (ROOT / "runwindows.ps1").read_text()
        cls.makefile = (ROOT / "Makefile").read_text()
        cls.linux_virtio = (ROOT / "rununix_virtio_latest.sh").read_text()
        cls.virtio = cls.launcher.split("    if ($VirtIoFull) {", 1)[1].split(
            "    } else {", 1
        )[0]
        cls.legacy = cls.launcher.split(
            "# Standard emulation:", 1
        )[1].split("    # Firmware", 1)[0]

    def test_windows_default_and_empty_override_use_gtk(self):
        self.assertRegex(self.launcher, r'\[string\]\$DisplayBackend\s*=\s*"gtk"')
        self.assertIn('else { "gtk" }', self.launcher)
        self.assertIn('"-display", $effectiveDisplay', self.launcher)

    def test_virtio_devices(self):
        devices = re.findall(r'"-device",\s*"([^"\n]+)"', self.virtio)
        names = [device.split(",", 1)[0] for device in devices]
        self.assertEqual(names.count("virtio-tablet-pci"), 1)
        self.assertEqual(names.count("virtio-keyboard-pci"), 1)
        self.assertNotIn("virtio-mouse-pci", names)
        self.assertNotIn("usb-mouse", names)
        gpu = next(device for device in devices if device.startswith("virtio-vga,"))
        for option in ("disable-legacy=on", "xres=1280", "yres=720"):
            self.assertIn(option, gpu.split(","))

    def test_tcg_multi_for_all_windows_profiles(self):
        self.assertIn(
            '"-accel", "tcg,thread=multi"',
            self.launcher,
        )

    def test_explicit_sdl_and_return_to_gtk(self):
        self.assertIn('"11" { $DisplayBackend = "gtk" }', self.launcher)
        self.assertIn('"12" { $DisplayBackend = "sdl" }', self.launcher)
        self.assertIn("12  - SDL eksperymentalny / diagnostyczny", self.launcher)
        self.assertIn('if ($DisplayBackend) { $DisplayBackend }', self.launcher)

    def test_display_status(self):
        self.assertIn('Write-Host "Display:      $displayLabel"', self.launcher)
        self.assertIn("$effectiveDisplay.ToUpperInvariant()", self.launcher)
        self.assertIn("'SDL eksperymentalny / diagnostyczny'", self.launcher)

    def test_legacy_usb_profile(self):
        self.assertIn('"usb-kbd,id=usbkbd,bus=xhci.0"', self.legacy)
        self.assertIn('"usb-mouse,id=usbmouse,bus=xhci.0"', self.legacy)
        self.assertIn('"-vga", "std"', self.legacy)
        self.assertLess(self.launcher.index('"-display", $effectiveDisplay'), self.launcher.index('    if ($VirtIoFull) {'))
        self.assertNotIn("virtio-tablet-pci", self.legacy)

    def test_backend_unavailable_has_explicit_error_and_no_fallback(self):
        self.assertIn('& $Qemu -display help', self.launcher)
        self.assertIn('throw "QEMU nie udostepnia backendu $Backend.', self.launcher)
        self.assertLess(self.launcher.index('Assert-QemuDisplay -Qemu'), self.launcher.index('    Prepare-Disk'))
        self.assertEqual(self.launcher.count('$DisplayBackend = "sdl"'), 1)
        self.assertIn('[ValidateSet("gtk", "sdl", "")]', self.launcher)

    def test_known_good_pci_order_and_firmware_storage_preserved(self):
        self.assertLess(self.launcher.index('"-device", "intel-hda"'), self.launcher.index('"-device", "virtio-blk-pci'))
        self.assertLess(self.virtio.index('virtio-vga,'), self.virtio.index('virtio-keyboard-pci,'))
        self.assertIn('$argsList += @("-bios", $ovmf)', self.launcher)
        self.assertIn('$argsList += @("-cdrom", $IsoPath)', self.launcher)
        self.assertIn('"-drive", "id=disk,file=$Disk,format=raw,if=none"', self.virtio)
        self.assertIn('"-audiodev", "$AudioBackend,id=snd"', self.launcher)
        self.assertIn('[string]$AudioBackend = "dsound"', self.launcher)

    def test_linux_policy_unchanged(self):
        self.assertRegex(self.makefile, r'(?m)^QEMUDISPLAYARGS\s*\?=\s*-display sdl$')
        self.assertRegex(self.makefile, r'(?m)^QEMUACCEL\s*\?=\s*tcg,thread=multi$')
        common = self.makefile.split("QEMU_VIRTIO_COMMON_ARGS", 1)[1].split("\n\n", 1)[0]
        self.assertIn("virtio-tablet-pci", common)
        self.assertIn("virtio-keyboard-pci", common)
        self.assertNotIn("virtio-mouse-pci", common)
        self.assertIn('QEMU_DISPLAY_ARGS="${QEMU_DISPLAY_ARGS:--display sdl}"', self.linux_virtio)
        self.assertIn('local mouse_dev="virtio-tablet-pci"', self.linux_virtio)
        self.assertIn('local kbd_dev="virtio-keyboard-pci"', self.linux_virtio)


if __name__ == "__main__":
    unittest.main(verbosity=2)
