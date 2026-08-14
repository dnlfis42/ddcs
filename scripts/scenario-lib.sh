# shellcheck shell=bash
#
# DDCS 시나리오 공용 헬퍼. 실행하지 말고 source 한다.
#
# - 각 시나리오는 COMPOSE와 (선택) SERVICES를 정하고 stack_up/stack_down으로 스택을 관리한다.
# - 단언은 assert_*가 누적하고 summary가 종료코드를 정한다. 실패가 하나라도 있으면 비정상 종료한다.
# - 메트릭은 controller의 raw 노출(:9000)을 직접 읽어 Grafana 없이도 CI에서 돌아간다.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
METRICS_URL="${DDCS_METRICS_URL:-http://localhost:9000/metrics}"
CTRL="${DDCS_CONTROLLER_CONTAINER:-ddcs-controller}"
COMPOSE="${COMPOSE:-docker-compose.yml}"

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

# 라벨 없는 메트릭 1개의 값. 없으면 빈 문자열. 이름 전체 일치라 접두어가 같은
# 다른 메트릭이나 # HELP 줄과 섞이지 않는다.
metric() { curl -s --max-time 5 "$METRICS_URL" | awk -v m="$1" '$1 == m {print $2; exit}'; }
# 정수 메트릭(없으면 0)
metric_int() { local v; v="$(metric "$1")"; printf '%s' "${v%%.*}"; [ -n "${v%%.*}" ] || printf '0'; }

# controller 로그에서 substring 발생 횟수
logcount() { docker logs "$CTRL" 2>&1 | grep -c "$1"; }
# 과열 latch에 들어간 적 있는 distinct device 수
hot_distinct() { docker logs "$CTRL" 2>&1 | grep '"event":"policy.thermal.update"' | grep '"thermal":"hot"' | grep -oiE '"device":"[0-9a-f-]+"' | sort -u | wc -l; }
# 특정 device로 나간 command.dispatch 횟수
dispatch_count() { docker logs "$CTRL" 2>&1 | grep '"event":"command.dispatch"' | grep -c "$1"; }
# 특정 device의 등록 확정 횟수(재접속이면 2 이상)
register_count() { docker logs "$CTRL" 2>&1 | grep '"event":"session.connection.register.accept"' | grep -c "$1"; }

# 조건이 참이 될 때까지 1초 간격으로 확인한다(최대 timeout 초).
# 셋째 인자부터가 조건 명령이고, 확인할 때마다 그대로 실행한다.
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

# wait_for에 넘길 조건. 라벨 없는 메트릭 값을 기준과 비교한다.
metric_at_least() { [ "$(metric_int "$1")" -ge "$2" ]; }
metric_at_most() { [ "$(metric_int "$1")" -le "$2" ]; }
# 특정 device의 dispatch/등록 횟수가 기준 이상인지 확인한다.
dispatched_at_least() { [ "$(dispatch_count "$1")" -ge "$2" ]; }
registered_at_least() { [ "$(register_count "$1")" -ge "$2" ]; }

soak() { # seconds, reason
    info "안정화 대기 ${1}s ($2)"
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

# 항상 빌드한다. 소스가 그대로면 레이어 캐시로 수 초에 끝나고, stale 이미지 탓에 시나리오가 옛 바이너리로 도는 일도 없다.
ensure_images() {
    narrate "이미지 빌드 (미변경이면 캐시로 수 초에 끝난다)"
    compose build
}

# docker / compose v2 / curl 선행 확인. 없으면 명확히 실패한다.
preflight() {
    command -v docker >/dev/null 2>&1 || {
        echo "오류: 'docker' 명령을 찾을 수 없습니다." >&2
        echo "설치: https://docs.docker.com/engine/install/ 안내를 따르십시오." >&2
        return 1
    }
    docker compose version >/dev/null 2>&1 || {
        echo "오류: docker compose v2가 필요합니다(구 docker-compose v1은 지원하지 않습니다)." >&2
        return 1
    }
    command -v curl >/dev/null 2>&1 || {
        echo "오류: 'curl' 명령을 찾을 수 없습니다." >&2
        echo "설치: sudo apt install curl" >&2
        return 1
    }
}

# 관측 스택(prometheus/grafana)은 시나리오 단언에 필요 없어 controller+agent만 띄운다.
# 실패(대개 호스트 포트 8080/9000 충돌)는 감추지 않고 비정상 종료로 알린다.
stack_up() { # 추가 인자(예: --scale 등)와 띄울 서비스 목록
    preflight || return 1
    ensure_images || return 1
    info "스택 기동: $COMPOSE ($*)"
    if ! compose up -d "$@" >/dev/null; then
        echo "스택 기동 실패: docker compose up 에러(포트 충돌?). 위 stderr 확인." >&2
        return 1
    fi
}

stack_down() {
    info "스택 정리"
    compose down --remove-orphans >/dev/null 2>&1 || true
}

# EXIT에서 항상 정리하고, INT/TERM은 exit로 바꾼다. bash 트랩은 핸들러가 끝나면 실행을 재개하므로
# 그냥 stack_down만 걸면 Ctrl-C 시 죽은 스택에 대고 나머지가 계속 돌며 거짓 FAIL이 잇따른다.
# exit로 바꾸면 단일 EXIT 트랩이 한 번만 정리한다.
arm_cleanup() {
    trap 'stack_down' EXIT
    trap 'exit 130' INT TERM
}

summary() {
    printf '\n%s===== 결과: %d pass, %d fail =====%s\n' "$C_B" "$_PASS" "$_FAIL" "$C_0"
    [ "$_FAIL" -eq 0 ]
}
