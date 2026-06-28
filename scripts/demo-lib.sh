# shellcheck shell=bash
# DDCS 데모 시나리오 공용 헬퍼. 실행하지 말고 source 한다.
# - 각 시나리오는 COMPOSE 와 (선택) SERVICES 를 정하고 stack_up/stack_down 으로 스택을 관리한다.
# - 단언은 assert_* 로 누적되고 summary 가 종료코드를 정한다(실패 1건이라도 있으면 비정상 종료).
# - 메트릭은 controller 의 raw 노출(:9000)을 직접 읽는다(Grafana 불필요, CI 친화적).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
METRICS_URL="${DDCS_METRICS_URL:-http://localhost:9000/metrics}"
CTRL="${DDCS_CONTROLLER_CONTAINER:-ddcs-controller}"
COMPOSE="${COMPOSE:-docker-compose.yml}"

if [ -t 1 ]; then
    C_G=$'\033[32m'; C_R=$'\033[31m'; C_Y=$'\033[33m'; C_B=$'\033[1m'; C_D=$'\033[2m'; C_0=$'\033[0m'
else
    C_G=; C_R=; C_Y=; C_B=; C_D=; C_0=
fi

_PASS=0
_FAIL=0

narrate() { printf '\n%s%s%s\n' "$C_B" "$*" "$C_0"; }
info()    { printf '  %s%s%s\n' "$C_D" "$*" "$C_0"; }

compose() { docker compose -f "$ROOT/docker/$COMPOSE" "$@"; }

# 라벨 포함 메트릭 1개의 값. 없으면 빈 문자열. 예: metric 'ddcs_connections '
# (# HELP / # TYPE 주석 줄은 제외해야 값 대신 "HELP"를 집지 않는다.)
metric() { curl -s --max-time 5 "$METRICS_URL" | grep -F "$1" | grep -v '^#' | awk '{print $2}' | head -1; }
# 정수 메트릭(없으면 0)
metric_int() { local v; v="$(metric "$1")"; printf '%s' "${v%%.*}"; [ -n "${v%%.*}" ] || printf '0'; }

# controller 로그에서 substring 발생 횟수
logcount() { docker logs "$CTRL" 2>&1 | grep -c "$1"; }
# policy.hot 을 한 번이라도 띄운 distinct device 수
hot_distinct() { docker logs "$CTRL" 2>&1 | grep '"event":"policy.hot"' | grep -oiE '"device":"[0-9a-f-]+"' | sort -u | wc -l; }
# 특정 device 로 나간 command.dispatch 횟수
dispatch_count() { docker logs "$CTRL" 2>&1 | grep '"event":"command.dispatch"' | grep -c "$1"; }
# 특정 device 의 session.registered 횟수(재접속이면 2 이상)
register_count() { docker logs "$CTRL" 2>&1 | grep '"event":"session.registered"' | grep -c "$1"; }

# 조건이 참이 될 때까지 1초 간격 폴링(최대 timeout 초). predicate 는 eval 되는 문자열.
wait_for() { # desc timeout predicate
    local desc="$1" timeout="$2" pred="$3" i=0
    printf '  %s대기: %s%s ' "$C_D" "$desc" "$C_0"
    while [ "$i" -lt "$timeout" ]; do
        if eval "$pred" >/dev/null 2>&1; then printf '%sok%s\n' "$C_G" "$C_0"; return 0; fi
        sleep 1; i=$((i + 1)); printf '.'
    done
    printf '%stimeout%s\n' "$C_R" "$C_0"; return 1
}

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

ensure_images() {
    if docker image inspect ddcs-controller:dev >/dev/null 2>&1 &&
        docker image inspect ddcs-agent:dev >/dev/null 2>&1; then
        return 0
    fi
    narrate "이미지 빌드 (최초 1회, 수 분 소요)..."
    compose build
}

# docker / compose v2 / curl 선행 확인. 없으면 명확히 실패한다.
preflight() {
    command -v docker >/dev/null 2>&1 || { echo "필요: docker" >&2; return 1; }
    docker compose version >/dev/null 2>&1 ||
        { echo "필요: docker compose v2 (구 docker-compose v1은 미지원)" >&2; return 1; }
    command -v curl >/dev/null 2>&1 || { echo "필요: curl" >&2; return 1; }
}

# 관측 스택(prometheus/grafana)은 데모 단언에 불필요해 controller+agent 만 띄운다.
# 실패(대개 호스트 포트 8080/9000 충돌)는 삼키지 않고 비정상 종료로 알린다.
stack_up() { # 추가 인자(예: --scale ...)와 띄울 서비스 목록
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

# EXIT 에서 항상 정리. INT/TERM 은 exit 로 바꾼다 -- bash 트랩은 핸들러 후 실행을 재개하므로,
# 그냥 stack_down 만 걸면 Ctrl-C 시 죽은 스택에 대고 나머지가 계속 돌며 거짓 FAIL을 쏟는다.
# exit 로 바꾸면 단일 EXIT 트랩이 한 번만 정리한다.
arm_cleanup() {
    trap 'stack_down' EXIT
    trap 'exit 130' INT TERM
}

summary() {
    printf '\n%s===== 결과: %d passed, %d failed =====%s\n' "$C_B" "$_PASS" "$_FAIL" "$C_0"
    [ "$_FAIL" -eq 0 ]
}
