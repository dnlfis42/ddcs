#!/usr/bin/env python3

"""측정용 설정을 복사하고, Agent 수와 Group 배치에 맞는 Fleet 구성을 생성한다."""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil


def validate(total, layout, size):
    if not 1 <= total <= 65504 or (layout == "balance" and total % 4):
        raise ValueError(
            "총 Agent 수는 1..65504 범위여야 하며, balance 배치에서는 4의 배수여야 합니다"
        )
    if size is not None:
        if not 1 <= size <= 65504:
            raise ValueError("Fleet당 Agent 수는 1..65504 범위여야 합니다")
        divisor = size * (4 if layout == "balance" else 1)
        if total % divisor:
            raise ValueError(
                f"{layout}: 총 Agent 수는 {divisor}의 배수여야 합니다. 모든 Fleet의 Agent 수가 같아야 합니다"
            )


def prepare(source, destination, uniform):
    # 원본을 보존하기 위해 정책은 복사본에서만 바꾼다.
    shutil.copytree(source, destination, dirs_exist_ok=True)
    if uniform:
        path = destination / "controller.json"
        config = json.loads(path.read_text())
        rule = config["policy"]["groups"]["zone_a"]
        config["policy"]["groups"] = {
            f"zone_{letter}": copy.deepcopy(rule) for letter in "abcd"
        }
        path.write_text(json.dumps(config, indent=2) + "\n")


def policy_hash(config):
    # 공백과 키 순서가 달라도 같은 정책이면 같은 해시를 만든다.
    policy = json.loads((config / "controller.json").read_text())["policy"]
    return hashlib.sha256(
        json.dumps(policy, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def generate(config, output, total, layout, size, uniform):
    validate(total, layout, size)
    config = config.resolve()
    nofile = {"nofile": {"soft": 65536, "hard": 65536}}
    mount = [
        {"type": "bind", "source": str(config), "target": "/config", "read_only": True}
    ]
    services = {
        "controller": {
            "image": "ddcs-controller:dev",
            "ports": ["8080:8080", "9000:9000"],
            "environment": {"DDCS_LOG_LEVEL": "info"},
            "volumes": mount,
            "ulimits": nofile,
        }
    }
    count = total // size if size else 4 if layout == "balance" else 1
    per_fleet = size or total // count
    fleets = []
    for i in range(count):
        letter = "abcd"[i % 4] if layout == "balance" else "a"
        name = f"fleet-{i + 1:04d}" if size else f"fleet-zone-{letter}"
        services[name] = {
            "image": "ddcs-agent-fleet:dev",
            "depends_on": ["controller"],
            "command": [str(per_fleet), f"zone_{letter}"],
            "environment": {
                "DDCS_TRANSPORT_HOST": "controller",
                "DDCS_LOG_LEVEL": "warn",
            },
            "volumes": mount,
            "ulimits": nofile,
        }
        fleets.append({"service": name, "agents": per_fleet, "group": f"zone_{letter}"})
    compose = output / "compose.json"
    compose.write_text(json.dumps({"services": services}, indent=2) + "\n")
    metadata = {
        "requested_agents": total,
        "layout": layout,
        "fleet_count": count,
        "agents_per_fleet": per_fleet,
        "fixed_fleet_size": size is not None,
        "uniform_policy": uniform,
        "policy_sha256": policy_hash(config),
        "compose_sha256": hashlib.sha256(compose.read_bytes()).hexdigest(),
        "fleets": fleets,
    }
    (output / "workload.json").write_text(json.dumps(metadata, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser(
        "validate", help="배치 방식과 Fleet 크기에 맞는 Agent 수인지 검사"
    )
    check.add_argument(
        "--layout",
        choices=("single", "balance"),
        required=True,
        help="single: zone_a 집중, balance: 4개 Group 균등 배치",
    )
    check.add_argument(
        "--size", type=int, default=1000, help="Fleet당 Agent 수(기본: 1000)"
    )
    check.add_argument("levels", nargs="+", type=int, help="검사할 총 Agent 수 목록")
    prep = commands.add_parser("prepare", help="원본 설정을 측정용 디렉터리에 복사")
    prep.add_argument("source", type=Path, help="원본 설정 디렉터리")
    prep.add_argument("destination", type=Path, help="측정용 설정을 복사할 디렉터리")
    prep.add_argument(
        "--uniform", action="store_true", help="복사본의 모든 Group에 zone_a 정책 적용"
    )
    policy = commands.add_parser("policy-hash", help="Controller 정책의 SHA-256 출력")
    policy.add_argument(
        "config", type=Path, help="controller.json이 있는 설정 디렉터리"
    )
    gen = commands.add_parser(
        "generate", help="compose.json과 구성 기록 workload.json 생성"
    )
    gen.add_argument("config", type=Path, help="컨테이너에서 사용할 설정 디렉터리")
    gen.add_argument("output", type=Path, help="생성한 파일을 저장할 기존 디렉터리")
    gen.add_argument("--total", type=int, required=True, help="총 Agent 수")
    gen.add_argument(
        "--layout",
        choices=("single", "balance"),
        required=True,
        help="single: zone_a 집중, balance: 4개 Group 균등 배치",
    )
    gen.add_argument(
        "--size", type=int, default=1000, help="Fleet당 Agent 수(기본: 1000)"
    )
    gen.add_argument(
        "--uniform",
        action="store_true",
        help="설정 준비 시 동일 정책을 적용했음을 구성 기록에 표시",
    )
    args = parser.parse_args()
    try:
        if args.command == "validate":
            for total in args.levels:
                validate(total, args.layout, args.size)
        elif args.command == "prepare":
            prepare(args.source, args.destination, args.uniform)
        elif args.command == "policy-hash":
            print(policy_hash(args.config))
        else:
            generate(
                args.config,
                args.output,
                args.total,
                args.layout,
                args.size,
                args.uniform,
            )
    except (ValueError, KeyError, OSError) as error:
        parser.exit(2, f"측정용 부하 구성 오류: {error}\n")


if __name__ == "__main__":
    main()
