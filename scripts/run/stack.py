#!/usr/bin/env python3
"""구역별 Fleet 스택의 실행 구성을 저장하고 실행·종료한다."""

import argparse
import copy
import fcntl
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / "var/run/stack"
PROJECT = "ddcs-stack"


def positive(value):
    if not re.fullmatch(r"[1-9][0-9]{0,4}", value) or int(value) > 65504:
        raise argparse.ArgumentTypeError("대수는 1..65504의 정수여야 합니다")
    return int(value)


def group(value):
    parts = value.split(",")
    if len(parts) != 2 or not re.fullmatch(r"[A-Za-z0-9_-]+", parts[0]):
        raise argparse.ArgumentTypeError("그룹은 NAME,COUNT 형식이어야 합니다")
    return parts[0], positive(parts[1])


def docker(*args, **kwargs):
    return subprocess.run(["docker", "compose", *args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(prog="stack.sh", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    up = commands.add_parser("up", help="Controller와 구역별 Fleet 실행")
    up.add_argument(
        "-m", "--monitoring", action="store_true", help="Prometheus·Grafana 포함"
    )
    up.add_argument("-g", "--group", action="append", type=group, metavar="NAME,COUNT")
    up.add_argument(
        "--group-count",
        type=positive,
        metavar="COUNT",
        help="zone_a부터 자동 생성할 그룹 수 (1..26)",
    )
    up.add_argument("--agents-per-group", type=positive, metavar="COUNT")
    commands.add_parser("down", help="저장한 구성으로 전체 스택 종료")
    args = parser.parse_args()

    groups = []
    if args.command == "up":
        automatic = args.group_count is not None or args.agents_per_group is not None
        if args.group:
            if automatic:
                parser.error("--group과 자동 배치 옵션은 함께 사용할 수 없습니다")
            groups = args.group
        else:
            if args.group_count is None or args.agents_per_group is None:
                parser.error(
                    "--group 또는 --group-count와 --agents-per-group을 지정하세요"
                )
            if args.group_count > 26:
                parser.error("자동 생성 그룹 수는 1..26입니다")
            groups = [
                (f"zone_{chr(97 + i)}", args.agents_per_group)
                for i in range(args.group_count)
            ]
        if len({name for name, _ in groups}) != len(groups):
            parser.error("같은 그룹을 중복 지정할 수 없습니다")
        if sum(count for _, count in groups) > 65504:
            parser.error("총 Agent 수는 65504 이하여야 합니다")

    STATE.mkdir(parents=True, exist_ok=True)
    with (STATE / "lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("다른 stack 명령이 실행 중입니다")
        saved = STATE / "compose.json"
        if args.command == "down":
            if not saved.exists():
                print("저장된 실행 구성이 없습니다.")
                return
            docker("-p", PROJECT, "-f", str(saved), "down", "--remove-orphans")
            saved.unlink()
            return

        policies = json.loads((ROOT / "config/controller.json").read_text())["policy"][
            "groups"
        ]
        for name, _ in groups:
            if name not in policies:
                print(
                    f"경고: {name}의 정책이 없습니다. 구역별 부하 제어에서 제외됩니다.",
                    file=sys.stderr,
                )
        base = ROOT / "docker/docker-compose.fleet-monitoring.yml"
        result = docker(
            "-f",
            str(base),
            "config",
            "--format",
            "json",
            capture_output=True,
            text=True,
        )
        config = json.loads(result.stdout)
        config["name"] = PROJECT
        template = config["services"].pop("agent-fleet")
        for index, (name, count) in enumerate(groups, 1):
            fleet = copy.deepcopy(template)
            fleet["command"] = [str(count), name]
            fleet["image"] = "ddcs-agent-fleet:dev"
            config["services"][f"fleet-{index}"] = fleet
        if not args.monitoring:
            for service in ("prometheus", "grafana"):
                del config["services"][service]
            config.pop("volumes", None)
        for service in config["services"].values():
            service.pop("container_name", None)
        for kind in ("networks", "volumes"):
            for name, resource in config.get(kind, {}).items():
                resource["name"] = f"{PROJECT}_{name}"
        content = json.dumps(config, ensure_ascii=False, indent=2) + "\n"
        if saved.exists() and saved.read_text() != content:
            parser.error("저장된 구성과 다릅니다. 먼저 stack.sh down을 실행하세요")
        pending = STATE / "pending.json"
        pending.write_text(content)
        try:
            docker("-p", PROJECT, "-f", str(pending), "config", "--quiet")
            pending.replace(saved)
        finally:
            pending.unlink(missing_ok=True)
        # 실행이 중간에 실패해도 down으로 정리할 수 있도록 구성을 먼저 보존한다.
        docker("-p", PROJECT, "-f", str(saved), "up", "--build", "-d")
        print(
            f"총 {sum(n for _, n in groups)}대: "
            + ", ".join(f"{g}={n}" for g, n in groups)
        )
        if args.monitoring:
            print("Grafana: http://localhost:3000")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"오류: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
