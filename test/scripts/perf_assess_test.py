#!/usr/bin/env python3
"""측정 중 오류를 놓치지 않고, 지정된 성능 기준만 평가하는지 검사한다."""

import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2] / "scripts/performance/evaluate-results.py"
spec = importlib.util.spec_from_file_location("perf_assess", MODULE)
assessor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assessor)


def metrics(tick, *, pending=0, connections=4, failed=0):
    return f"""ddcs_connections {connections}
ddcs_group_devices{{group="zone_a",mode="normal"}} 4
ddcs_ticks_total {tick}
ddcs_tick_duration_seconds_total {tick * 0.01}
ddcs_messages_received_total {tick * 120}
ddcs_command_rtt_seconds_sum {tick * 0.2}
ddcs_command_rtt_seconds_count {tick}
ddcs_commands_dispatched_total {tick + pending + failed}
ddcs_commands_succeeded_total {tick}
ddcs_commands_failed_total{{reason="exhausted"}} {failed}
ddcs_commands_superseded_total 0
ddcs_commands_pending {pending}
ddcs_tick_skipped_total 0
ddcs_connections_closed_total{{reason="liveness_expired"}} 0
"""


class AssessmentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        (self.path / "samples").mkdir()
        metadata = {
            "requested_agents": 4,
            "measurement_window": {
                "started_unix_ns": 0,
                "ended_unix_ns": 10_000_000_000,
            },
            "metrics_start": {"controller_cpu": {"pid": 4}},
            "metrics_end": {"controller_cpu": {"pid": 4}},
        }
        (self.path / "measurement.json").write_text(json.dumps(metadata))
        (self.path / "metrics-start.prom").write_text(metrics(1))
        (self.path / "metrics-end.prom").write_text(metrics(3))

    def assess(self, limits=None):
        return assessor.assess(self.path, "single", limits or {})

    def test_no_implicit_slo_and_successful_rtt_denominator(self):
        result = self.assess()
        self.assertEqual(result["configured_slo"]["status"], "unassessed")
        self.assertAlmostEqual(result["observations"]["mean_rtt_ms"], 200)
        self.assertEqual(result["command_accounting"]["status"], "pass")
        self.assertEqual(result["workload"]["reporting_rate"], "unassessed")

    def test_intermediate_connection_loss_and_pending_peak_are_visible(self):
        (self.path / "samples/000001.prom").write_text(
            metrics(2, connections=3, pending=8)
        )
        result = self.assess()
        self.assertEqual(result["workload"]["sampled_connections_and_groups"], "fail")
        self.assertEqual(result["gauges"]["ddcs_commands_pending"]["sampled_max"], 8)
        self.assertEqual(result["gauges"]["ddcs_commands_pending"]["end"], 0)

    def test_configured_slo_failure_does_not_invalidate_measurement(self):
        result = self.assess({"mean_rtt_ms": 100})
        self.assertEqual(result["measurement"]["status"], "valid")
        self.assertEqual(result["configured_slo"]["status"], "fail")

    def test_counter_reset_invalidates_slo(self):
        (self.path / "samples/000001.prom").write_text(metrics(5))
        result = self.assess({"mean_rtt_ms": 1000})
        self.assertEqual(result["measurement"]["status"], "invalid")
        self.assertEqual(result["configured_slo"]["status"], "unassessed")

    def test_failure_accounting_and_optional_missing_metric(self):
        (self.path / "metrics-end.prom").write_text(metrics(3, failed=2))
        result = self.assess({"terminal_failures": 0})
        self.assertEqual(result["observations"]["terminal_failures"], 2)
        self.assertEqual(result["command_accounting"]["status"], "pass")
        self.assertEqual(result["configured_slo"]["status"], "fail")
        self.assertIsNone(result["observations"]["attempt_failures"])

    def test_disappearing_counter_series_invalidates_measurement(self):
        with (self.path / "metrics-start.prom").open("a") as stream:
            stream.write('ddcs_command_dispatch_failures_total{reason="offline"} 0\n')
        self.assertEqual(self.assess()["measurement"]["status"], "invalid")

    def test_dispatch_failures_are_separate_from_terminal_failures(self):
        for filename, failures in [("metrics-start.prom", 0), ("metrics-end.prom", 2)]:
            with (self.path / filename).open("a") as stream:
                stream.write(
                    f'ddcs_command_dispatch_failures_total{{reason="offline"}} {failures}\n'
                )
        result = self.assess({"dispatch_failures": 0})
        self.assertEqual(result["observations"]["terminal_failures"], 0)
        self.assertEqual(result["observations"]["dispatch_failures"], 2)
        self.assertEqual(result["configured_slo"]["status"], "fail")

    def test_missing_core_metric_invalidates_measurement(self):
        (self.path / "samples/000001.prom").write_text("ddcs_connections 4\n")
        self.assertEqual(self.assess()["measurement"]["status"], "invalid")

    def test_saved_topology_drives_limitations(self):
        workload = {'fixed_fleet_size': True, 'uniform_policy': True,
                    'fleet_count': 20, 'agents_per_fleet': 1000}
        (self.path / 'workload.json').write_text(json.dumps(workload))
        result = self.assess()
        self.assertEqual(result['workload_configuration'], workload)
        self.assertIn('Fleet size is fixed', ' '.join(result['limitations']))
        self.assertNotIn('single uses one', ' '.join(result['limitations']))

    def test_cli_distinguishes_bad_measurements_from_slo_failure(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith('DDCS_PERF_SLO_')}
        env['DDCS_PERF_SLO_MEAN_RTT_MS'] = '100'
        command = [sys.executable, str(MODULE), str(self.path)]
        valid = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertIn('configured_slo=fail', valid.stdout)
        (self.path / 'samples/000001.prom').write_text(metrics(2, connections=3))
        invalid = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(invalid.returncode, 1)
        result = json.loads((self.path / 'assessment.json').read_text())
        self.assertEqual(result['workload']['sampled_connections_and_groups'], 'fail')
        (self.path / 'samples/000001.prom').write_text(metrics(5))
        reset = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(reset.returncode, 1)
        self.assertIn('measurement=invalid', reset.stdout)


if __name__ == "__main__":
    unittest.main()
