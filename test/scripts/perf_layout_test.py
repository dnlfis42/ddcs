#!/usr/bin/env python3
"""Fleet 크기, 정책 일치 여부, 원본 설정 보존을 검사한다."""

import importlib.util
import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "perf_layout", ROOT / "scripts/performance/workload-config.py"
)
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class LayoutTests(unittest.TestCase):
    def test_twenty_fleets_with_balanced_or_concentrated_groups(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            config = root / "config"
            layout.prepare(ROOT / "config", config, True)
            for mode in ("balance", "single"):
                out = root / mode
                out.mkdir()
                layout.generate(config, out, 20000, mode, 1000, True)
                workload = json.loads((out / "workload.json").read_text())
                model = json.loads((out / "compose.json").read_text())["services"]
                self.assertEqual(len(model), 21)
                self.assertEqual(workload["fleet_count"], 20)
                self.assertEqual({f["agents"] for f in workload["fleets"]}, {1000})
                for group in "abcd":
                    count = sum(
                        f["agents"]
                        for f in workload["fleets"]
                        if f["group"] == f"zone_{group}"
                    )
                    self.assertEqual(
                        count,
                        5000 if mode == "balance" else 20000 if group == "a" else 0,
                    )
                self.assertTrue(
                    all("container_name" not in service for service in model.values())
                )
                self.assertTrue(
                    all(
                        "ports" not in service
                        for name, service in model.items()
                        if name != "controller"
                    )
                )
                self.assertEqual(
                    model["controller"]["ulimits"]["nofile"]["soft"], 65536
                )
                self.assertTrue(
                    all(
                        Path(s["volumes"][0]["source"]).is_absolute()
                        for s in model.values()
                    )
                )

    def test_partial_fleets_rejected_before_launch(self):
        for count, mode in [(1000, "balance"), (5000, "balance"), (1500, "single")]:
            with self.subTest(count=count, mode=mode), self.assertRaises(ValueError):
                layout.validate(count, mode, 1000)
        layout.validate(12000, "balance", 1000)
        layout.validate(1000, "single", 1000)

    def test_uniform_policy_does_not_modify_source(self):
        original = (ROOT / "config/controller.json").read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / "config"
            layout.prepare(ROOT / "config", config, True)
            data = json.loads((config / "controller.json").read_text())
            self.assertEqual(
                list(data["policy"]["groups"]), ["zone_a", "zone_b", "zone_c", "zone_d"]
            )
            self.assertTrue(
                all(
                    rule == data["policy"]["groups"]["zone_a"]
                    for rule in data["policy"]["groups"].values()
                )
            )
            self.assertEqual(original, (ROOT / "config/controller.json").read_bytes())

    def test_cli_defaults_to_thousand_agents_per_fleet(self):
        command = [sys.executable, str(ROOT / 'scripts/performance/workload-config.py'),
                   'validate', '--layout', 'balance']
        valid = subprocess.run([*command, '4000', '12000', '20000'], capture_output=True)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        small = subprocess.run([*command, '1000'], capture_output=True)
        self.assertEqual(small.returncode, 2)
        explicit = subprocess.run([*command, '--size', '250', '1000'], capture_output=True)
        self.assertEqual(explicit.returncode, 0, explicit.stderr)

    def test_legacy_four_fleets_remain_supported(self):
        with tempfile.TemporaryDirectory() as temp:
            layout.generate(ROOT / "config", Path(temp), 1000, "balance", None, False)
            workload = json.loads((Path(temp) / "workload.json").read_text())
            self.assertEqual(workload["fleet_count"], 4)
            self.assertEqual(workload["agents_per_fleet"], 250)
            self.assertFalse(workload["fixed_fleet_size"])


if __name__ == "__main__":
    unittest.main()
