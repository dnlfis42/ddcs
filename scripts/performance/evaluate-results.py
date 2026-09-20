#!/usr/bin/env python3

"""저장된 메트릭으로 측정 유효성과 설정된 성능 기준을 평가하고 assessment.json에 기록한다."""

import argparse
import json
import math
import os
from pathlib import Path

THRESHOLDS = {
    "mean_rtt_ms": "DDCS_PERF_SLO_MEAN_RTT_MS",
    "tick_skipped": "DDCS_PERF_SLO_MAX_TICK_SKIPPED",
    "liveness_closed": "DDCS_PERF_SLO_MAX_LIVENESS_CLOSED",
    "terminal_failures": "DDCS_PERF_SLO_MAX_TERMINAL_FAILURES",
    "dispatch_failures": "DDCS_PERF_SLO_MAX_DISPATCH_FAILURES",
}


def thresholds():
    result = {}
    for metric, name in THRESHOLDS.items():
        raw = os.environ.get(name)
        if raw is not None:
            value = float(raw)
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"{name} must be a finite nonnegative number")
            result[metric] = value
    return result


def read_metrics(path):
    values = {}
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        key, raw, *_ = line.split()
        if key in values:
            raise ValueError(f"duplicate metric: {key}")
        value = float(raw)
        if not math.isfinite(value) or value < 0:
            raise ValueError(f"invalid metric: {key}")
        values[key] = value
    return values


def family(values, name):
    items = [v for k, v in values.items() if k == name or k.startswith(name + "{")]
    return sum(items) if items else None


def assess(directory, layout, limits):
    metadata = json.loads((directory / "measurement.json").read_text())
    n = metadata["requested_agents"]
    window = metadata["measurement_window"]
    seconds = (window["ended_unix_ns"] - window["started_unix_ns"]) / 1e9
    if seconds <= 0:
        raise ValueError("nonpositive measurement window")
    paths = [
        directory / "metrics-start.prom",
        *sorted((directory / "samples").glob("*.prom")),
        directory / "metrics-end.prom",
    ]
    samples = [read_metrics(p) for p in paths]
    first, last = samples[0], samples[-1]
    errors = []
    start_pid = metadata["metrics_start"]["controller_cpu"]["pid"]
    end_pid = metadata["metrics_end"]["controller_cpu"]["pid"]
    if start_pid is not None and end_pid is not None and start_pid != end_pid:
        errors.append("controller PID changed")
    required = (
        "ddcs_ticks_total",
        "ddcs_tick_duration_seconds_total",
        "ddcs_messages_received_total",
        "ddcs_command_rtt_seconds_sum",
        "ddcs_command_rtt_seconds_count",
        "ddcs_connections",
        "ddcs_commands_pending",
    )
    for path, sample in zip(paths, samples):
        for key in required:
            if key not in sample:
                errors.append(f"missing metric in {path.name}: {key}")
        if not sample:
            errors.append(f"empty snapshot: {path.name}")
    for before, after in zip(samples, samples[1:]):
        for key in before.keys() - after.keys():
            if key.split("{")[0].endswith(("_total", "_count", "_sum", "_bucket")):
                errors.append(f"counter series disappeared: {key}")
        for key in before.keys() & after.keys():
            base = key.split("{")[0]
            if (
                base.endswith(("_total", "_count", "_sum", "_bucket"))
                and after[key] < before[key]
            ):
                errors.append(f"counter regression: {key}")

    def delta(name):
        a, b = family(first, name), family(last, name)
        return None if a is None or b is None else b - a

    ticks = delta("ddcs_ticks_total")
    if ticks is None or ticks <= 0:
        errors.append("missing or nonpositive tick count")
    # 샘플 사이에 발생한 연결 끊김은 감지하지 못할 수 있다.
    sampled_ready = []
    for sample in samples:
        counts = {
            g: sum(
                v
                for k, v in sample.items()
                if k.startswith(f'ddcs_group_devices{{group="{g}",')
            )
            for g in ("zone_a", "zone_b", "zone_c", "zone_d")
        }
        expected = {
            g: (n / 4 if layout == "balance" else n if g == "zone_a" else 0)
            for g in counts
        }
        sampled_ready.append(sample.get("ddcs_connections") == n and counts == expected)
    accounting = []
    for sample in samples:
        values = [
            family(sample, k)
            for k in (
                "ddcs_commands_dispatched_total",
                "ddcs_commands_succeeded_total",
                "ddcs_commands_failed_total",
                "ddcs_commands_superseded_total",
                "ddcs_commands_pending",
            )
        ]
        accounting.append(None if None in values else values[0] == sum(values[1:]))
    count, total = (
        delta("ddcs_command_rtt_seconds_count"),
        delta("ddcs_command_rtt_seconds_sum"),
    )
    observations = {
        "mean_rtt_ms": total * 1000 / count
        if count is not None and count > 0 and total is not None
        else None,
        "rtt_success_samples": count,
        "tick_skipped": delta("ddcs_tick_skipped_total"),
        "liveness_closed": delta(
            'ddcs_connections_closed_total{reason="liveness_expired"}'
        ),
        "terminal_failures": delta("ddcs_commands_failed_total"),
        "attempt_failures": delta("ddcs_command_attempt_failures_total"),
        "dispatch_failures": delta("ddcs_command_dispatch_failures_total"),
        "received_messages_per_second": delta("ddcs_messages_received_total") / seconds
        if delta("ddcs_messages_received_total") is not None
        else None,
    }
    gauges = {}
    for metric in (
        "ddcs_commands_pending",
        "ddcs_send_queue_messages",
        "ddcs_tick_start_lateness_seconds",
        "ddcs_tick_duration_seconds",
    ):
        values = [s.get(metric) for s in samples]
        gauges[metric] = {
            "sampled_max": max(v for v in values if v is not None)
            if any(v is not None for v in values)
            else None,
            "start": values[0],
            "end": values[-1],
        }
    checks = {
        metric: {
            "limit": limit,
            "observed": observations[metric],
            "status": "unassessed"
            if observations[metric] is None or errors
            else "pass"
            if observations[metric] <= limit
            else "fail",
        }
        for metric, limit in limits.items()
    }
    statuses = [c["status"] for c in checks.values()]
    workload_path = directory / "workload.json"
    workload = json.loads(workload_path.read_text()) if workload_path.exists() else None
    limitations = ["Sampling can miss transient peaks and disconnections."]
    if workload is None:
        limitations.append("Fleet topology and policy identity are unavailable.")
    else:
        limitations.append(
            "Fleet size is fixed; compare Fleet counts and group placement in workload_configuration."
            if workload["fixed_fleet_size"]
            else "Fleet process counts differ across layouts; group placement is not the only difference."
        )
        limitations.append(
            "Group policies are identical; input phase and simulator randomness remain uncontrolled."
            if workload["uniform_policy"]
            else "Group policies may differ; latency differences do not isolate group placement cost."
        )
    return {
        "workload_configuration": workload,
        "schema_name": "ddcs.perf_assessment",
        "schema_version": 1,
        "measurement": {
            "status": "invalid" if errors else "valid",
            "errors": errors,
            "seconds": seconds,
            "sample_count": len(samples),
        },
        "workload": {
            "sampled_connections_and_groups": "pass" if all(sampled_ready) else "fail",
            "continuous_coverage": False,
            "reporting_rate": "unassessed",
            "nominal_periodic_messages_per_second": 3 * n,
            "limitation": "3N assumes heartbeat=500ms/status=1000ms; total ingress includes command responses and cannot prove each periodic stream rate.",
        },
        "command_accounting": {
            "status": "fail"
            if False in accounting
            else "unassessed"
            if None in accounting
            else "pass",
            "invariant": "dispatched = succeeded + failed + superseded + pending",
        },
        "observations": observations,
        "gauges": gauges,
        "configured_slo": {
            "status": "fail"
            if "fail" in statuses
            else "unassessed"
            if not statuses or "unassessed" in statuses
            else "pass",
            "checks": checks,
            "scope": "Only explicitly configured limits; successful outcomes only for RTT. Not an overall capacity certification.",
        },
        "limitations": limitations,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "directory",
        nargs="?",
        type=Path,
        help="measurement.json과 메트릭 원문이 있는 단계별 결과 디렉터리",
    )
    parser.add_argument(
        "--layout",
        choices=("single", "balance"),
        default="single",
        help="검증할 Group 배치 방식(기본: single)",
    )
    parser.add_argument(
        "--validate-config",
        action="store_true",
        help="DDCS_PERF_SLO_* 환경변수의 기준값만 검사하고 종료",
    )
    args = parser.parse_args()
    try:
        limits = thresholds()
        if args.validate_config:
            return
        if args.directory is None:
            parser.error(
                "--validate-config를 사용하지 않을 때는 결과 디렉터리가 필요합니다"
            )
        result = assess(args.directory, args.layout, limits)
        (args.directory / "assessment.json").write_text(
            json.dumps(result, indent=2, allow_nan=False) + "\n"
        )
        print(
            f"measurement={result['measurement']['status']} "
            f"workload={result['workload']['sampled_connections_and_groups']} "
            f"accounting={result['command_accounting']['status']} "
            f"configured_slo={result['configured_slo']['status']}"
        )
        # 성능 기준을 넘었더라도 측정 자체는 성공일 수 있다.
        if (
            result["measurement"]["status"] != "valid"
            or result["workload"]["sampled_connections_and_groups"] == "fail"
            or result["command_accounting"]["status"] == "fail"
        ):
            parser.exit(
                1,
                "측정 데이터, Agent 배치 또는 명령 집계 검증에 실패했습니다. assessment.json을 확인하십시오.\n",
            )

    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"측정 결과 평가 오류: {error}\n")


if __name__ == "__main__":
    main()
