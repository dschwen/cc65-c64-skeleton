#!/usr/bin/env python3
"""Boundary tests for the mode-dependent in-place module visibility ABI."""

from __future__ import annotations

import unittest

from easyflash_layout import ModulePlacement
from validate_banked_module import resident_visible


class BankedVisibilityTests(unittest.TestCase):
    def test_roml_8k_maps_basic_over_upper_ram(self) -> None:
        for address in (0x0000, 0x7FFF, 0xC000, 0xCFFF):
            self.assertTrue(resident_visible(address, "roml"), hex(address))
        for address in (0x8000, 0x9FFF, 0xA000, 0xBFFF, 0xD000, 0xFFFF):
            self.assertFalse(resident_visible(address, "roml"), hex(address))

    def test_romh_16k_hides_both_cartridge_halves(self) -> None:
        for address in (0x0000, 0x7FFF, 0xC000, 0xCFFF):
            self.assertTrue(resident_visible(address, "romh"), hex(address))
        for address in (0x8000, 0x9FFF, 0xA000, 0xBFFF, 0xD000, 0xFFFF):
            self.assertFalse(resident_visible(address, "romh"), hex(address))

    def test_layout_separates_cpu_map_from_easyflash_control(self) -> None:
        roml = ModulePlacement("lo", 1, "roml", 0, "in_place", None)
        romh = ModulePlacement("hi", 1, "romh", 0, "in_place", None)
        self.assertEqual(roml.execution_cpu_map, 0x07)
        self.assertEqual(roml.execution_control, 0x06)
        self.assertEqual(romh.execution_cpu_map, 0x07)
        self.assertEqual(romh.execution_control, 0x07)


if __name__ == "__main__":
    unittest.main()
