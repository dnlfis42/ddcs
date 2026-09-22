# shellcheck shell=bash

# source 전용: 시나리오 실행·검증·결과 기록. 상세: docs/scenario.md
# 호출자는 COMPOSE, SCENARIO_NAME을 지정하고 arm_cleanup으로 EXIT 처리를 등록한다.
# 최종 결과는 스택 정리 후 build.json에 기록한다.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/results.sh
source "$ROOT/scripts/lib/results.sh"

METRICS_URL="${DDCS_METRICS_URL:-http://localhost:9000/metrics}"
CTRL="${DDCS_CONTROLLER_CONTAINER:-ddcs-controller}"
COMPOSE="${COMPOSE:-docker-compose.yml}"
COMPOSE_OVERLAY=

# 외부 COMPOSE_PROJECT_NAME과 무관하게 실행별 프로젝트를 사용한다.
_SCENARIO_PROJECT="ddcs-test-${BASHPID}-$(date -u +%s%N)"
readonly _SCENARIO_PROJECT
_SCENARIO_STACK_OWNED=false
_SCENARIO_STACK_COMPOSE=
_SCENARIO_STACK_OVERLAY=

# source 상태는 실행 산출물을 만들기 전에 고정한다.
SCENARIO_SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
    SCENARIO_SOURCE_DIRTY=true
else
    SCENARIO_SOURCE_DIRTY=false
fi
_SCENARIO_BUILD_DIR=
_SCENARIO_RESULT_RECORDED=false
_SCENARIO_RUN_DIR=
_SCENARIO_EVIDENCE_CAPTURED=false
_SCENARIO_EVIDENCE_FAILED=false
_SCENARIO_STARTED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
SCENARIO_TEMP_CONFIG_DIR=

if [ -t 1 ]; then
    C_G=$'\033[32m'; C_R=$'\033[31m'; C_B=$'\033[1m'; C_D=$'\033[2m'; C_0=$'\033[0m'
else
    C_G=; C_R=; C_B=; C_D=; C_0=
fi

_PASS=0
_FAIL=0

scenario_event() {
    [ -n "$_SCENARIO_RUN_DIR" ] || return 0
    jq -cn --arg at "$(date -u +%Y-%m-%dT%H:%M:%S.%NZ)" --arg kind "$1" --arg message "$2" \
        '{at:$at,kind:$kind,message:$message}' >>"$_SCENARIO_RUN_DIR/timeline.jsonl"
}
narrate() { printf '\n%s%s%s\n' "$C_B" "$*" "$C_0"; scenario_event phase "$*"; }
info()    { printf '  %s%s%s\n' "$C_D" "$*" "$C_0"; scenario_event info "$*"; }

# 판정에 사용한 응답을 그대로 보존한다. 실패 응답을 정상적인 빈 메트릭으로 바꾸지 않는다.
scenario_metrics() {
    local snapshot status
    if [ -z "$_SCENARIO_RUN_DIR" ]; then curl -fsS --max-time 5 "$METRICS_URL"; return; fi
    snapshot="$(mktemp "$_SCENARIO_RUN_DIR/metrics/$(date -u +%s%N)-XXXXXX.prom")" || return 1
    curl -fsS --max-time 5 "$METRICS_URL" >"$snapshot" 2>"$snapshot.stderr"
    status=$?
    printf '%s\n' "$status" >"$snapshot.exit"
    cat "$snapshot"
    return "$status"
}

scenario_snapshot_config() { # named fault-injection phase
    [ -n "$_SCENARIO_RUN_DIR" ] || return 0
    local label="$1"
    [[ "$label" =~ ^[a-z0-9-]+$ ]] || return 1
    mkdir -p "$_SCENARIO_RUN_DIR/config-$label" &&
        cp -RL "${DDCS_POLICY_CONFIG_DIR:-$ROOT/config}/." "$_SCENARIO_RUN_DIR/config-$label/" || return 1
    scenario_event config "$label"
}

scenario_initialize_evidence() {
    local effective_config_sha256
    _SCENARIO_RUN_DIR="$_SCENARIO_BUILD_DIR/scenario/$SCENARIO_NAME/$_SCENARIO_PROJECT"
    mkdir -p "$_SCENARIO_RUN_DIR/metrics" "$_SCENARIO_RUN_DIR/config-start" || return 1
    cp -RL "${DDCS_POLICY_CONFIG_DIR:-$ROOT/config}/." "$_SCENARIO_RUN_DIR/config-start/" || return 1
    effective_config_sha256="$(result_directory_sha256 "$_SCENARIO_RUN_DIR/config-start")" || return 1
    compose config --format json >"$_SCENARIO_RUN_DIR/compose.json" || return 1
    cp "$_SCENARIO_BUILD_DIR/build.json" "$_SCENARIO_RUN_DIR/build-start.json" || return 1
    jq -n --arg scenario "$SCENARIO_NAME" --arg project "$_SCENARIO_PROJECT" \
        --arg started_at "$_SCENARIO_STARTED_AT" --arg revision "$SCENARIO_SOURCE_REVISION" \
        --argjson dirty "$SCENARIO_SOURCE_DIRTY" --arg metrics_url "$METRICS_URL" \
        --arg controller "$CTRL" --arg compose "$COMPOSE" --arg overlay "$COMPOSE_OVERLAY" \
        --arg per_zone "${DDCS_SCENARIO_PER_ZONE:-}" --arg soak "${DDCS_SCENARIO_SOAK:-}" \
        --arg effective_config_sha256 "$effective_config_sha256" \
        '{scenario:$scenario,project:$project,started_at:$started_at,source_revision:$revision,
          source_dirty:$dirty,metrics_url:$metrics_url,controller:$controller,compose:$compose,
          overlay:$overlay,effective_config_sha256:$effective_config_sha256,
          overrides:{per_zone:$per_zone,soak:$soak},status:"running"}' \
        >"$_SCENARIO_RUN_DIR/manifest.json" || return 1
    info "실행 근거: $_SCENARIO_RUN_DIR"
}

# 스택 삭제 전에 실행 인스턴스와 전체 로그를 저장한다. 실패해도 호출자는 정리를 계속한다.
scenario_capture_evidence() {
    [ -n "$_SCENARIO_RUN_DIR" ] || return 0
    if [ "$_SCENARIO_EVIDENCE_CAPTURED" = true ]; then
        [ "$_SCENARIO_EVIDENCE_FAILED" != true ]; return
    fi
    _SCENARIO_EVIDENCE_CAPTURED=true
    local failed=0
    compose logs --no-color --timestamps >"$_SCENARIO_RUN_DIR/compose.log" 2>&1 || failed=1
    docker logs --timestamps "$CTRL" >"$_SCENARIO_RUN_DIR/controller.log" 2>&1 || failed=1
    docker inspect "$CTRL" >"$_SCENARIO_RUN_DIR/controller-final.json" 2>"$_SCENARIO_RUN_DIR/inspect.stderr" || failed=1
    # HTTP 요청 자체의 실패도 증거로 남긴다. 중단·부분 기동에서는 기대 가능한 실패다.
    scenario_metrics >"$_SCENARIO_RUN_DIR/metrics-final.prom" || true
    mkdir -p "$_SCENARIO_RUN_DIR/config-final" || failed=1
    cp -RL "${DDCS_POLICY_CONFIG_DIR:-$ROOT/config}/." "$_SCENARIO_RUN_DIR/config-final/" || failed=1
    [ "$failed" -eq 0 ] || _SCENARIO_EVIDENCE_FAILED=true
    return "$failed"
}

scenario_finish_evidence() { # final exit status, after cleanup
    [ -n "$_SCENARIO_RUN_DIR" ] || return 0
    local status="$1" temporary="$_SCENARIO_RUN_DIR/manifest.final.json"
    jq --arg finished_at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" --argjson exit_status "$status" \
        --argjson passed "$_PASS" --argjson failed "$_FAIL" \
        --argjson evidence_failed "$_SCENARIO_EVIDENCE_FAILED" \
        '. + {finished_at:$finished_at,exit_status:$exit_status,
          status:(if $exit_status == 0 then "pass" else "fail" end),
          assertions:{passed:$passed,failed:$failed},evidence_failed:$evidence_failed}' \
        "$_SCENARIO_RUN_DIR/manifest.json" >"$temporary" &&
        mv "$temporary" "$_SCENARIO_RUN_DIR/manifest.json" || return 1
    # shellcheck disable=SC2016 # jq 변수다.
    result_update_build_json "$_SCENARIO_BUILD_DIR" \
        '(.scenario_runs //= {}) | (.scenario_runs[$name] //= []) | .scenario_runs[$name] += [$run]' \
        --arg name "$SCENARIO_NAME" --arg run "scenario/$SCENARIO_NAME/$_SCENARIO_PROJECT/manifest.json"
}

compose() {
    local overlay="$COMPOSE_OVERLAY"
    local -a files
    if [ "${1:-}" = up ] && [ "$_SCENARIO_STACK_OWNED" != true ]; then
        scenario_require_available_stack || return 1
        _SCENARIO_STACK_COMPOSE="$COMPOSE"
        _SCENARIO_STACK_OVERLAY="$COMPOSE_OVERLAY"
        # 부분 기동 실패에도 정리할 수 있도록 up 전에 소유권을 기록한다.
        _SCENARIO_STACK_OWNED=true
    fi
    if [ "$_SCENARIO_STACK_OWNED" = true ]; then overlay="$_SCENARIO_STACK_OVERLAY"; fi
    files=(-f "$ROOT/docker/${_SCENARIO_STACK_COMPOSE:-$COMPOSE}")
    [ -z "$overlay" ] || files+=(-f "$ROOT/docker/$overlay")
    docker compose --project-name "$_SCENARIO_PROJECT" "${files[@]}" "$@"
}

scenario_require_available_stack() {
    local model fixed_names existing_names name
    [ "$_SCENARIO_STACK_OWNED" != true ] || return 0
    model="$(compose config --format json)" || return 1
    fixed_names="$(printf '%s\n' "$model" | jq -r '.services[] | .container_name // empty')" || return 1
    existing_names="$(docker ps -a --format '{{.Names}}')" || {
        echo "오류: 기존 컨테이너를 확인하지 못했습니다. 스택을 기동하지 않습니다." >&2
        return 1
    }
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        if grep -Fxq -- "$name" <<<"$existing_names"; then
            echo "오류: 컨테이너 '$name'이 이미 존재합니다. 기존 스택을 먼저 확인하십시오." >&2
            return 1
        fi
    done <<<"$fixed_names"
}

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
    scenario_initialize_evidence
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

# 라벨 없는 메트릭: 이름 전체 일치, 없으면 빈 문자열.
metric() { scenario_metrics | awk -v m="$1" '$1 == m {print $2; exit}'; }
# 정수 메트릭(없으면 0)
metric_int() { local v; v="$(metric "$1")"; printf '%s' "${v%%.*}"; [ -n "${v%%.*}" ] || printf '0'; }
# Exporter의 고정된 reason 라벨 형식을 사용한다.
metric_reason() {
    scenario_metrics |
        awk -v m="$1" -v r="$2" '$1 == m "{reason=\"" r "\"}" {print $2; exit}'
}
metric_reason_int() {
    local v
    v="$(metric_reason "$1" "$2")"
    printf '%s' "${v%%.*}"
    [ -n "${v%%.*}" ] || printf '0'
}

logcount() { docker logs "$CTRL" 2>&1 | grep -c "$1"; }
# 한 번이라도 hot으로 전환된 distinct Device 수
hot_distinct() { docker logs "$CTRL" 2>&1 | grep '"event":"policy.thermal.update"' | grep '"thermal":"hot"' | grep -oiE '"device":"[0-9a-f-]+"' | sort -u | wc -l; }
dispatch_count() { docker logs "$CTRL" 2>&1 | grep '"event":"command.dispatch"' | grep -c "$1"; }
# 특정 device의 등록 확정 횟수(재접속이면 2 이상)
register_count() { docker logs "$CTRL" 2>&1 | grep '"event":"session.connection.register.accept"' | grep -c "$1"; }

# stdin Controller JSON 로그에서 지정 등록 이후 같은 command_id의 dispatch→complete를 찾는다.
# 등록 이전의 완료, 다른 Device의 완료, dispatch 없는 완료는 성공 근거가 아니다.
scenario_reconnect_chain() { # device minimum-registration-count
    jq -Rsec --arg device "$1" --argjson minimum "$2" '
        [split("\n")[] | fromjson? | select(.device == $device)] as $events |
        [$events | to_entries[] |
          select(.value.event == "session.connection.register.accept")] as $registrations |
        select(($registrations | length) >= $minimum) |
        $registrations[-1] as $registration |
        [$events | to_entries[] | select(.key > $registration.key)] as $after |
        first($after[] | select(.value.event == "command.dispatch") as $dispatch |
          $after[] | select(.key > $dispatch.key and .value.event == "command.complete" and
            .value.command_id == $dispatch.value.command_id) |
          {registration:$registration.value,dispatch:$dispatch.value,complete:.value})'
}

wait_for() { # desc timeout command [arg...]
    local desc="$1" timeout="$2" i=0
    shift 2
    scenario_event wait "$desc (timeout=${timeout}s)"
    printf '  %s대기: %s%s ' "$C_D" "$desc" "$C_0"
    while [ "$i" -lt "$timeout" ]; do
        if "$@" >/dev/null 2>&1; then printf '%sok%s\n' "$C_G" "$C_0"; scenario_event ready "$desc"; return 0; fi
        sleep 1; i=$((i + 1)); printf '.'
    done
    printf '%stimeout%s\n' "$C_R" "$C_0"; scenario_event timeout "$desc"; return 1
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

_pass() { _PASS=$((_PASS + 1)); printf '  %s[PASS]%s %s\n' "$C_G" "$C_0" "$1"; scenario_event pass "$1"; }
_fail() { _FAIL=$((_FAIL + 1)); printf '  %s[FAIL]%s %s\n' "$C_R" "$C_0" "$1"; scenario_event fail "$1"; }

assert_ge() { # desc actual min
    if [ "${2:-0}" -ge "$3" ] 2>/dev/null; then _pass "$1 (=${2}, want >=$3)"; else _fail "$1 (=${2:-?}, want >=$3)"; fi
}
assert_eq() { # desc actual expected
    if [ "${2:-x}" = "$3" ]; then _pass "$1 (=$2)"; else _fail "$1 (got '${2:-}', want '$3')"; fi
}

# 이전 소스의 이미지 재사용을 막기 위해 항상 빌드한다.
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
    scenario_require_available_stack || return 1
}

# 검증에 필요한 controller와 agent만 기동한다.
stack_up() { # 추가 인자(예: --scale 등)와 띄울 서비스 목록
    preflight || return 1
    ensure_images || return 1
    scenario_initialize_result || return 1
    info "스택 기동: $COMPOSE ($*)"
    if ! compose up -d "$@" >/dev/null; then
        echo "오류: docker compose up이 실패했습니다(대개 호스트 포트 8080/9000 충돌). 위 출력에서 원인을 확인하십시오." >&2
        return 1
    fi
    if [ -n "$_SCENARIO_RUN_DIR" ]; then
        docker inspect "$CTRL" >"$_SCENARIO_RUN_DIR/controller-start.json" || return 1
        printf '%s\n' "$@" >"$_SCENARIO_RUN_DIR/up-arguments.txt"
    fi
}

stack_down() {
    [ "$_SCENARIO_STACK_OWNED" = true ] || return 0
    local evidence_status=0
    scenario_capture_evidence || evidence_status=1
    narrate "스택 정리: $_SCENARIO_PROJECT"
    if ! compose down --remove-orphans >/dev/null; then
        echo "오류: 스택 정리에 실패했습니다. 프로젝트 '$_SCENARIO_PROJECT'를 확인하십시오." >&2
        return 1
    fi
    _SCENARIO_STACK_OWNED=false
    _SCENARIO_STACK_COMPOSE=
    _SCENARIO_STACK_OVERLAY=
    return "$evidence_status"
}

scenario_finalize_exit() {
    local exit_status=$?
    [ "$#" -eq 0 ] || exit_status="$1"
    trap - EXIT INT TERM
    if ! stack_down; then
        [ "$exit_status" -ne 0 ] || exit_status=1
    fi
    if [ "$_FAIL" -gt 0 ] && [ "$exit_status" -eq 0 ]; then exit_status=1; fi
    if [ -n "$SCENARIO_TEMP_CONFIG_DIR" ] && [ "$_SCENARIO_STACK_OWNED" != true ]; then
        rm -rf -- "$SCENARIO_TEMP_CONFIG_DIR" || exit_status=1
    fi
    scenario_finish_evidence "$exit_status" || exit_status=1
    if [ -n "$_SCENARIO_BUILD_DIR" ] && [ "$_SCENARIO_RESULT_RECORDED" != true ]; then
        if [ "$exit_status" -eq 0 ]; then
            scenario_record_result pass || exit_status=1
        else
            scenario_record_result fail || exit_status=1
        fi
    fi
    exit "$exit_status"
}

# 중단도 EXIT 경로에서 정리하며 종료 코드 130을 보존한다.
arm_cleanup() {
    trap 'scenario_finalize_exit' EXIT
    trap 'exit 130' INT TERM
}

summary() {
    # 저장은 정리·종료 상태가 확정되는 EXIT 처리에 맡긴다.
    printf '\n%s===== 검증: %d pass, %d fail (최종 결과는 정리 후 기록) =====%s\n' "$C_B" "$_PASS" "$_FAIL" "$C_0"
    [ "$_FAIL" -eq 0 ]
}
