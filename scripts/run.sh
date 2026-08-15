#!/usr/bin/env bash
#
# DDCS 로컬 실행. 빌드된 바이너리로 Controller 1대와 Agent N대를 띄우고 Ctrl-C까지 유지한다.
#
# 사용법: scripts/run.sh [Agent 수]   (기본 1)
#
# 환경 변수:
#   DDCS_RUN_PRESET      바이너리를 찾을 build 디렉터리 이름 (기본 debug)
#   DDCS_RUN_ZONES       Agent를 돌아가며 배정할 Group 목록 (기본 zone_a~zone_d)
#   DDCS_TRANSPORT_PORT  wire 포트 (기본 8080). 점유 검사와 안내에 함께 쓴다
#   DDCS_PROMETHEUS_PORT 메트릭 포트 (기본 9000)
#
# 종료 상태:
#   0    정상 종료
#   1    실행 실패 (바이너리 없음, 포트 점유, 프로세스 조기 종료)
#   2    사용법 오류
#   130  Ctrl-C (SIGINT)
#   143  SIGTERM

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root" # 설정 기본 경로(config/controller.json, config/agent.json)가 cwd 기준이다

count="${1:-1}"
case "$count" in
'' | *[!0-9]*)
    echo "사용법: $0 [Agent 수]" >&2
    exit 2
    ;;
esac
if [ "$count" -lt 1 ]; then
    echo "사용법: $0 [Agent 수]" >&2
    exit 2
fi

preset="${DDCS_RUN_PRESET:-debug}"
bin_dir="build/${preset}/bin"
wire_port="${DDCS_TRANSPORT_PORT:-8080}"
metrics_port="${DDCS_PROMETHEUS_PORT:-9000}"
read -r -a zones <<<"${DDCS_RUN_ZONES:-zone_a zone_b zone_c zone_d}"
run_dir="data/run"

if [ -t 1 ]; then
    C_B=$'\033[1m'; C_D=$'\033[2m'; C_R=$'\033[31m'; C_0=$'\033[0m'
else
    C_B=; C_D=; C_R=; C_0=
fi

for exe in ctrl agent; do
    if [ ! -x "${bin_dir}/${exe}" ]; then
        echo "오류: '${bin_dir}/${exe}'를 찾을 수 없습니다." >&2
        echo "빌드: cmake --workflow --preset ${preset}" >&2
        exit 1
    fi
done

# Controller가 뜬 뒤 실패하면 Agent만 남아 재접속을 반복하므로 먼저 막는다.
if command -v ss >/dev/null 2>&1; then
    for port in "$wire_port" "$metrics_port"; do
        if [ -n "$(ss -ltnH "sport = :${port}" 2>/dev/null)" ]; then
            echo "오류: 포트 ${port}를 이미 다른 프로세스가 쓰고 있습니다." >&2
            echo "확인: ss -ltnp 'sport = :${port}'" >&2
            exit 1
        fi
    done
fi

rm -rf "$run_dir"
mkdir -p "$run_dir"

pids=()
names=()
tail_pid=

# 각 프로세스의 JSONL을 자기 로그 파일로 보낸다. 파이프를 끼우면 그 자식이 셸에 남아
# 종료할 때 wait이 멎으므로, 표시는 뒤에서 tail 하나가 맡는다.
start() { # tag command [arg...]
    local tag="$1"
    shift
    "$@" >"${run_dir}/${tag}.log" 2>&1 &
    pids+=("$!")
    names+=("$tag")
}

cleanup() {
    trap - EXIT INT TERM
    if [ -n "$tail_pid" ]; then
        kill "$tail_pid" 2>/dev/null || true
    fi
    local i
    # Agent를 먼저 내려야 Controller가 축출 경로를 거치지 않고 끝난다.
    for ((i = ${#pids[@]} - 1; i >= 0; i--)); do
        kill "${pids[i]}" 2>/dev/null || true
    done
    if [ ${#pids[@]} -gt 0 ]; then
        wait "${pids[@]}" 2>/dev/null || true
    fi
    printf '\n%s정리 완료%s\n' "$C_B" "$C_0"
}
trap cleanup EXIT
trap 'exit 130' INT  # 128 + SIGINT(2)
trap 'exit 143' TERM # 128 + SIGTERM(15)

printf '%sDDCS 로컬 실행: Controller 1대 + Agent %s대 (preset %s)%s\n' "$C_B" "$count" "$preset" "$C_0"

start ctrl "${bin_dir}/ctrl"

for ((n = 1; n <= count; n++)); do
    tag="$(printf 'agent-%02d' "$n")"
    # DDCS_DEVICE_ID_FILE을 주지 않으므로 Agent마다 기동할 때 새 DeviceId를 발급받는다.
    DDCS_DEVICE_GROUP="${zones[$(((n - 1) % ${#zones[@]}))]}" \
        start "$tag" "${bin_dir}/agent"
done

# curl이 있으면 실제 등록까지 확인하고, 없으면 안내만 하고 넘어간다.
if command -v curl >/dev/null 2>&1; then
    printf '  %s대기: Agent %s대 연결%s ' "$C_D" "$count" "$C_0"
    connected=0
    for _ in $(seq 30); do
        connected="$(curl -s --max-time 2 "http://127.0.0.1:${metrics_port}/metrics" |
            awk '$1 == "ddcs_connections" {print $2; exit}')"
        connected="${connected%%.*}"
        [ -n "$connected" ] || connected=0
        if [ "$connected" -ge "$count" ]; then
            printf 'ok\n'
            break
        fi
        sleep 1
        printf '.'
    done
    if [ "$connected" -lt "$count" ]; then
        printf '%s미달%s\n' "$C_R" "$C_0"
        echo "오류: Agent 연결이 ${count}대에 이르지 못했습니다 (현재 ${connected}대)." >&2
        tail -n 20 "${run_dir}"/*.log >&2
        exit 1
    fi
fi

printf '\n%s기동 완료. Ctrl-C로 종료합니다.%s\n' "$C_B" "$C_0"
printf '  %-8s 127.0.0.1:%s\n' "wire" "$wire_port"
printf '  %-8s http://127.0.0.1:%s/metrics\n' "메트릭" "$metrics_port"
printf '  %-8s %s/\n\n' "로그" "$run_dir"

tail -n +1 -F "${run_dir}"/*.log &
tail_pid=$!

# 어느 프로세스든 스스로 끝나면 나머지를 정리하고 실패로 끝낸다. 그냥 두면 Controller가
# 죽어도 Agent가 재접속을 반복하며 스크립트가 멎은 것처럼 보인다.
while :; do
    dead_pid=
    set +e
    wait -n -p dead_pid
    status=$?
    set -e
    if [ -z "$dead_pid" ]; then
        [ "$status" -eq 127 ] && break # 남은 자식이 없다
        continue
    fi
    for i in "${!pids[@]}"; do
        if [ "$dead_pid" = "${pids[i]}" ]; then
            printf '\n오류: %s이(가) 종료되었습니다 (상태 %s).\n' "${names[i]}" "$status" >&2
            exit 1
        fi
    done
done
