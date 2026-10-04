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
        cls.virtio = cls.launcher.split("    if ($VirtIoFull) {", 1)[1].split(
            "    } else {", 1
        )[0]
        cls.legacy = cls.launcher.split(
            "# Standard emulation:", 1
        )[1].split("    # Audio", 1)[0]

    def test_windows_default_and_empty_override_use_gtk(self):
        self.assertRegex(self.launcher, r'\[string\]\$DisplayBackend\s*=\s*"gtk"')
        self.assertIn('else { "gtk" }', self.virtio)
        self.assertIn('@("-display", $effectiveDisplay)', self.virtio)

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

    def test_tcg_multi_for_virtio_only(self):
        self.assertIn(
            '"-accel", $(if ($VirtIoFull) { "tcg,thread=multi" } else { "tcg,thread=single" })',
            self.launcher,
        )

    def test_explicit_sdl_and_return_to_gtk(self):
        self.assertIn('"11" { $DisplayBackend = "gtk" }', self.launcher)
        self.assertIn('"12" { $DisplayBackend = "sdl" }', self.launcher)
        self.assertIn("12  - SDL eksperymentalny / diagnostyczny", self.launcher)
        self.assertIn('if ($DisplayBackend) { $DisplayBackend }', self.virtio)

    def test_display_status(self):
        self.assertIn('Write-Host "Display:      $displayLabel"', self.launcher)
        self.assertIn("$effectiveDisplay.ToUpperInvariant()", self.launcher)
        self.assertIn("'SDL eksperymentalny / diagnostyczny'", self.launcher)

    def test_legacy_usb_profile(self):
        self.assertIn('"usb-kbd,id=usbkbd,bus=xhci.0"', self.legacy)
        self.assertIn('"usb-mouse,id=usbmouse,bus=xhci.0"', self.legacy)
        self.assertIn('"-vga", "std"', self.legacy)
        self.assertNotIn('"-display"', self.legacy)
        self.assertNotIn("virtio-tablet-pci", self.legacy)

    def test_linux_policy_unchanged(self):
        self.assertRegex(self.makefile, r'(?m)^QEMUDISPLAYARGS\s*\?=\s*-display sdl$')
        self.assertRegex(self.makefile, r'(?m)^QEMUACCEL\s*\?=\s*tcg,thread=multi$')
        common = self.makefile.split("QEMU_VIRTIO_COMMON_ARGS", 1)[1].split("\n\n", 1)[0]
        self.assertIn("virtio-tablet-pci", common)
        self.assertIn("virtio-keyboard-pci", common)
        self.assertNotIn("virtio-mouse-pci", common)


if __name__ == "__main__":
    unittest.main(verbosity=2)
