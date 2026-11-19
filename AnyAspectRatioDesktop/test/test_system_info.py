import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from backend import plugin_root

sys.path.insert(0, str(plugin_root()))

import system_info


class FakeBottle:
    def __init__(self, name: str, registry: dict[str, dict[str, str]]) -> None:
        self.prefix = Path("/bottles") / name
        self.registry = registry

    def read(self, key: str) -> dict[str, str]:
        return self.registry.get(key, {})


class SystemInfoTests(unittest.TestCase):
    def test_supported_game_folders_count_once_against_installed_apps(self) -> None:
        scan = {"report": {"steam_scan": {"apps_scanned": 32}, "supported_games": [
            {"game_path": "/a"}, {"game_path": "/a"}, {"game_path": "/b"}, {"game_path": None},
        ]}}
        self.assertEqual({"supported": 2, "installed": 32}, system_info.game_counts(scan))
        self.assertIsNone(system_info.game_counts(None))
        self.assertIsNone(system_info.game_counts({"report": {"supported_games": []}}))

    def test_disks_skip_unready_drives_and_volumes_listed_twice(self) -> None:
        usage = {"/": (500, 100), "/Volumes/Data": (500, 100), "/Volumes/Card": (256, 200)}

        def disk_usage(mount):
            if mount not in usage:
                raise OSError("not ready")
            total, free = usage[mount]
            return system_info.shutil._ntuple_diskusage(total, total - free, free)

        with (
            patch.object(system_info, "_mount_points", return_value=["/", "/Volumes/Data", "/Volumes/Card", "Z:\\"]),
            patch.object(system_info.shutil, "disk_usage", side_effect=disk_usage),
        ):
            self.assertEqual([
                {"path": "/", "free_bytes": 100, "total_bytes": 500},
                {"path": "/Volumes/Card", "free_bytes": 200, "total_bytes": 256},
            ], system_info.disks())

    def test_firmware_placeholders_fall_back_to_a_real_name(self) -> None:
        self.assertEqual("", system_info._model("System manufacturer", "System Product Name"))
        self.assertEqual("TUF GAMING X570-PLUS", system_info._model("To be filled by O.E.M.", "TUF GAMING X570-PLUS"))
        self.assertEqual("Valve Jupiter", system_info._model("Valve", "Jupiter"))
        self.assertEqual("Valve Jupiter", system_info._model("Valve", "Valve Jupiter"))
        self.assertEqual("PC (AMD Ryzen 7)", system_info._with_cpu("PC", "  AMD   Ryzen 7 "))

    def test_bottles_with_a_virtual_desktop_report_its_size(self) -> None:
        explorer, desktops = r"HKCU\Software\Wine\Explorer", r"HKCU\Software\Wine\Explorer\Desktops"
        bottles = {
            Path("/root/a"): FakeBottle("Games", {explorer: {"desktop": "Default"}, desktops: {"default": "1280x800"}}),
            Path("/root/b"): FakeBottle("Plain", {}),
            Path("/root/c"): FakeBottle("Broken", {explorer: {"desktop": "Big"}, desktops: {"big": "huge"}}),
        }
        with (
            patch.object(system_info.steam_layout, "system_roots", return_value=list(bottles)),
            patch("launch_setup.bottle_for", side_effect=lambda root, home: bottles[root]),
        ):
            self.assertEqual([{"label": "Games virtual desktop", "width": 1280, "height": 800}],
                             system_info.virtual_desktops(Path("/home")))
        self.assertEqual((2560, 1600), system_info.parse_resolution(" 2560 x 1600 "))
        self.assertIsNone(system_info.parse_resolution("1280"))


if __name__ == "__main__":
    unittest.main()
