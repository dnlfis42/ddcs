# shellcheck shell=bash
#
# DDCS 시나리오 공용 헬퍼. 실행하지 말고 source 한다.
#
# - 각 시나리오는 COMPOSE와 SCENARIO_NAME을 정하고 stack_up/stack_down으로 스택을 관리한다.
# - 단언은 assert_*가 누적하고 summary가 종료코드를 정한다.
# - 시나리오는 raw artifact를 보존하지 않는다. 결과는 build.json의 scenario 상태 하나뿐이다.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/result-lib.sh
source "$ROOT/scripts/result-lib.sh"

METRICS_URL="${DDCS_METRICS_URL:-http://localhost:9000/metrics}"
CTRL="${DDCS_CONTROLLER_CONTAINER:-ddcs-controller}"
COMPOSE="${COMPOSE:-docker-compose.yml}"

# source 상태는 policy-reload가 config를 임시 편집하기 전에 잡아야 한다.
SCENARIO_SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
    SCENARIO_SOURCE_DIRTY=true
else
    SCENARIO_SOURCE_DIRTY=false
fi
_SCENARIO_BUILD_DIR=
_SCENARIO_RESULT_RECORDED=false

if [ -t 1 ]; then
    C_G=$'\033[32m'; C_R=$'\033[31m'; C_B=$'\033[1m'; C_D=$'\033[2m'; C_0=$'\033[0m'
else
    C_G=; C_R=; C_B=; C_D=; C_0=
fi

_PASS=0
_FAIL=0

narrate() { printf '\n%s%s%s\n' "$C_B" "$*" "$C_0"; }
info()    { printf '  %s%s%s\n' "$C_D" "$*" "$C_0"; }

compose() { docker compose -f "$ROOT/docker/$COMPOSE" "$@"; }

scenario_initialize_result() {
    local controller_image_id agent_image_id runtime_config_sha256
    [ -z "$_SCENARIO_BUILD_DIR" ] || return 0
    case "${SCENARIO_NAME:-}" in
    thermal | agent-reconnect | regime-transition | liveness-eviction | policy-reload)
        ;;
    *)
        echo "오류: SCENARIO_NAME이 올바르게 설정되지 않았습니다." >&2
        return 1
        ;;
    esac
    controller_image_id="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
    agent_image_id="$(docker image inspect --format '{{.Id}}' ddcs-agent:dev 2>/dev/null || true)"
    [ -n "$controller_image_id" ] || {
        echo "오류: Controller image ID를 읽지 못했습니다." >&2
        return 1
    }
    [ -n "$agent_image_id" ] || {
        echo "오류: Agent image ID를 읽지 못했습니다." >&2
        return 1
    }
    runtime_config_sha256="$(result_directory_sha256 "$ROOT/config")" || return 1
    result_initialize_build \
        "$ROOT" "$SCENARIO_SOURCE_REVISION" "$SCENARIO_SOURCE_DIRTY" \
        "$controller_image_id" "$agent_image_id" "$runtime_config_sha256" || return 1
    _SCENARIO_BUILD_DIR="$DDCS_RESULT_BUILD_DIR"
}

scenario_record_result() { # pass | fail
    local status="$1"
    [ -n "$_SCENARIO_BUILD_DIR" ] || {
        echo "오류: scenario build 결과가 초기화되지 않았습니다." >&2
        return 1
    }
    result_set_scenario "$_SCENARIO_BUILD_DIR" "$SCENARIO_NAME" "$status" || return 1
    _SCENARIO_RESULT_RECORDED=true
}

# 라벨 없는 메트릭 1개의 값. 없으면 빈 문자열. 이름 전체 일치라 접두어가 같은
# 다른 메트릭이나 # HELP 줄과 섞이지 않는다.
metric() { curl -s --max-time 5 "$METRICS_URL" | awk -v m="$1" '$1 == m {print $2; exit}'; }
# 정수 메트릭(없으면 0)
metric_int() { local v; v="$(metric "$1")"; printf '%s' "${v%%.*}"; [ -n "${v%%.*}" ] || printf '0'; }
# reason 라벨 counter 하나의 값. reason 어휘는 Controller 코드에서 고정하므로 라벨 순서를 명시해
# 정확히 한 sample만 읽는다.
metric_reason() {
    curl -s --max-time 5 "$METRICS_URL" |
        awk -v m="$1" -v r="$2" '$1 == m "{reason=\"" r "\"}" {print $2; exit}'
}
metric_reason_int() {
    local v
    v="$(metric_reason "$1" "$2")"
    printf '%s' "${v%%.*}"
    [ -n "${v%%.*}" ] || printf '0'
}

# Controller 로그에서 substring 발생 횟수
logcount() { docker logs "$CTRL" 2>&1 | grep -c "$1"; }
# 한 번이라도 hot으로 전환된 distinct Device 수
hot_distinct() { docker logs "$CTRL" 2>&1 | grep '"event":"policy.thermal.update"' | grep '"thermal":"hot"' | grep -oiE '"device":"[0-9a-f-]+"' | sort -u | wc -l; }
# 특정 device로 나간 command.dispatch 횟수
dispatch_count() { docker logs "$CTRL" 2>&1 | grep '"event":"command.dispatch"' | grep -c "$1"; }
# 특정 device의 등록 확정 횟수(재접속이면 2 이상)
register_count() { docker logs "$CTRL" 2>&1 | grep '"event":"session.connection.register.accept"' | grep -c "$1"; }

# 조건이 참이 될 때까지 1초 간격으로 확인한다(최대 timeout 초).
wait_for() { # desc timeout command [arg...]
    local desc="$1" timeout="$2" i=0
    shift 2
    printf '  %s대기: %s%s ' "$C_D" "$desc" "$C_0"
    while [ "$i" -lt "$timeout" ]; do
        if "$@" >/dev/null 2>&1; then printf '%sok%s\n' "$C_G" "$C_0"; return 0; fi
        sleep 1; i=$((i + 1)); printf '.'
    done
    printf '%stimeout%s\n' "$C_R" "$C_0"; return 1
}

metric_at_least() { [ "$(metric_int "$1")" -ge "$2" ]; }
metric_at_most() { [ "$(metric_int "$1")" -le "$2" ]; }
metric_reason_at_least() { [ "$(metric_reason_int "$1" "$2")" -ge "$3" ]; }
dispatched_at_least() { [ "$(dispatch_count "$1")" -ge "$2" ]; }
registered_at_least() { [ "$(register_count "$1")" -ge "$2" ]; }

soak() { # seconds, reason
    info "대기 ${1}s ($2)"
    sleep "$1"
}

_pass() { _PASS=$((_PASS + 1)); printf '  %s[PASS]%s %s\n' "$C_G" "$C_0" "$1"; }
_fail() { _FAIL=$((_FAIL + 1)); printf '  %s[FAIL]%s %s\n' "$C_R" "$C_0" "$1"; }

assert_ge() { # desc actual min
    if [ "${2:-0}" -ge "$3" ] 2>/dev/null; then _pass "$1 (=${2}, want >=$3)"; else _fail "$1 (=${2:-?}, want >=$3)"; fi
}
assert_eq() { # desc actual expected
    if [ "${2:-x}" = "$3" ]; then _pass "$1 (=$2)"; else _fail "$1 (got '${2:-}', want '$3')"; fi
}

# 항상 빌드한다. 소스가 그대로면 레이어 캐시로 수 초에 끝나고 stale image를 피한다.
ensure_images() {
    narrate "이미지 빌드"
    compose build
}

preflight() {
    command -v docker >/dev/null 2>&1 || {
        echo "오류: 'docker' 명령을 찾을 수 없습니다." >&2
        return 1
    }
    docker compose version >/dev/null 2>&1 || {
        echo "오류: docker compose v2가 필요합니다." >&2
        return 1
    }
    command -v curl >/dev/null 2>&1 || {
        echo "오류: 'curl' 명령을 찾을 수 없습니다." >&2
        return 1
    }
    result_require_jq || return 1
}

# 관측 스택(prometheus/grafana)은 단언에 필요 없어 controller+agent만 띄운다.
stack_up() { # 추가 인자(예: --scale 등)와 띄울 서비스 목록
    preflight || return 1
    ensure_images || return 1
    scenario_initialize_result || return 1
    info "스택 기동: $COMPOSE ($*)"
    if ! compose up -d "$@" >/dev/null; then
        echo "오류: docker compose up이 실패했습니다(대개 호스트 포트 8080/9000 충돌). 위 출력에서 원인을 확인하십시오." >&2
        return 1
    fi
}

stack_down() {
    narrate "스택 정리"
    compose down --remove-orphans >/dev/null 2>&1 || true
}

scenario_finalize_exit() {
    local exit_status=$?
    [ "$#" -eq 0 ] || exit_status="$1"
    trap - EXIT INT TERM
    if [ -n "$_SCENARIO_BUILD_DIR" ] && [ "$_SCENARIO_RESULT_RECORDED" != true ]; then
        if [ "$exit_status" -eq 0 ]; then
            scenario_record_result pass || exit_status=1
        else
            scenario_record_result fail || exit_status=1
        fi
    fi
    stack_down
    exit "$exit_status"
}

# Ctrl-C는 단일 EXIT trap으로 정리·실패 기록하고 130을 남긴다.
arm_cleanup() {
    trap 'scenario_finalize_exit' EXIT
    trap 'exit 130' INT TERM
}

summary() {
    local verdict=pass
    [ "$_FAIL" -eq 0 ] || verdict=fail
    if ! scenario_record_result "$verdict"; then
        _fail "build.json scenario 상태 기록"
    fi
    printf '\n%s===== 결과: %d pass, %d fail =====%s\n' "$C_B" "$_PASS" "$_FAIL" "$C_0"
    [ "$_FAIL" -eq 0 ]
}
