#!/usr/bin/env bash
set -u
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [ "${1:-}" = --archive ]; then
    source "$REPO_ROOT/scripts/lib/scenario.sh"
    SCENARIO_NAME=thermal
    _SCENARIO_BUILD_DIR="$2"
    TEST_CASE="$3"
    # shellcheck disable=SC2317 # 수집 함수가 간접 호출하는 명령 대역.
    docker() {
        case "$1" in
        logs) printf '{"event":"command.complete","device":"fixture","command_id":1}\n' ;;
        inspect) printf '[{"Id":"fixture-controller","Image":"sha256:fixture"}]\n' ;;
        *) return 1 ;;
        esac
    }
    compose() {
        case "$1" in
        config) printf '{"services":{"controller":{"container_name":"fixture-controller"}}}\n' ;;
        logs)
            printf 'logs\n' >>"$_SCENARIO_BUILD_DIR/order"
            printf 'fixture raw compose log\n'
            [ "$TEST_CASE" != evidence-failure ]
            ;;
        down)
            printf 'down\n' >>"$_SCENARIO_BUILD_DIR/order"
            [ "$TEST_CASE" != cleanup-failure ]
            ;;
        *) return 1 ;;
        esac
    }
    curl() { printf 'ddcs_connections 4\n'; }
    arm_cleanup
    scenario_initialize_evidence || exit 1
    _SCENARIO_STACK_OWNED=true
    scenario_metrics >/dev/null || exit 1
    _pass 'fixture assertion'
    [ "$TEST_CASE" != interrupt ] || exit 130
    exit 0
fi
if [ "${1:-}" = --finalize ]; then
    source "$REPO_ROOT/scripts/lib/scenario.sh"
    SCENARIO_NAME=thermal
    _SCENARIO_BUILD_DIR="$2"
    TEST_CASE="$3"
    # shellcheck disable=SC2317 # EXIT 트랩이 간접 호출한다.
    stack_down() { [ "$TEST_CASE" != cleanup-failure ]; }
    arm_cleanup
    case "$TEST_CASE" in assertion-failure) _FAIL=1 ;; esac
    summary || true
    case "$TEST_CASE" in interrupt) exit 130 ;; late-failure) exit 1 ;; esac
    exit 0
fi

test_dir="$(mktemp -d /tmp/ddcs-scenario-evidence.XXXXXX)" || exit 1
trap 'rm -rf -- "$test_dir"' EXIT
failures=0
for test_case in normal cleanup-failure assertion-failure interrupt late-failure; do
    mkdir "$test_dir/$test_case"
    printf '{}\n' >"$test_dir/$test_case/build.json"
    bash "$0" --finalize "$test_dir/$test_case" "$test_case" >"$test_dir/$test_case/output" 2>&1
    status=$?
    expected=1; verdict=fail
    case "$test_case" in normal) expected=0; verdict=pass ;; interrupt) expected=130 ;; esac
    if [ "$status" -eq "$expected" ] &&
        jq -e --arg verdict "$verdict" '.scenario.thermal == $verdict' "$test_dir/$test_case/build.json" >/dev/null; then
        printf 'PASS 최종 결과 기록: %s\n' "$test_case"
    else
        printf 'FAIL 최종 결과 기록: %s (종료 코드=%s)\n' "$test_case" "$status"
        failures=$((failures + 1))
    fi
done

# 스택 삭제 전에 근거를 수집하고, 정리 후 최종 결과를 기록해야 한다.
for test_case in normal cleanup-failure evidence-failure interrupt; do
    evidence_dir="$test_dir/archive-$test_case"
    mkdir "$evidence_dir"
    printf '{}\n' >"$evidence_dir/build.json"
    bash "$0" --archive "$evidence_dir" "$test_case" >"$evidence_dir/output" 2>&1
    status=$?
    expected=1; verdict=fail
    case "$test_case" in normal) expected=0; verdict=pass ;; interrupt) expected=130 ;; esac
    manifest="$(find "$evidence_dir/scenario" -name manifest.json -type f)"
    run_dir="$(dirname "$manifest")"
    if [ "$status" -eq "$expected" ] &&
        [ "$(cat "$evidence_dir/order")" = $'logs\ndown' ] &&
        [ -s "$run_dir/compose.log" ] && [ -s "$run_dir/controller.log" ] &&
        [ -s "$run_dir/controller-final.json" ] && [ -s "$run_dir/config-start/controller.json" ] &&
        [ -s "$run_dir/config-final/controller.json" ] &&
        [ "$(find "$run_dir/metrics" -name '*.prom' | wc -l)" -ge 2 ] &&
        jq -e --arg verdict "$verdict" --argjson status "$expected" \
            '.status == $verdict and .exit_status == $status and .assertions.passed == 1' "$manifest" >/dev/null &&
        jq -e --arg verdict "$verdict" '.scenario.thermal == $verdict and (.scenario_runs.thermal|length) == 1' \
            "$evidence_dir/build.json" >/dev/null &&
        jq -se 'any(.[]; .kind == "pass")' "$run_dir/timeline.jsonl" >/dev/null; then
        printf 'PASS 원문 근거 수집: %s\n' "$test_case"
    else
        printf 'FAIL 원문 근거 수집: %s (종료 코드=%s)\n' "$test_case" "$status"
        cat "$evidence_dir/output"
        failures=$((failures + 1))
    fi
done

# 같은 빌드를 다시 실행해도 이전 실행 기록을 보존해야 한다.
if bash "$0" --archive "$test_dir/archive-normal" normal >"$test_dir/archive-normal/output-repeat" 2>&1 &&
    jq -e '(.scenario_runs.thermal|length) == 2' \
    "$test_dir/archive-normal/build.json" >/dev/null; then
    printf 'PASS 재실행 시 이전 근거 보존\n'
else
    printf 'FAIL 재실행 시 이전 근거 보존\n'
    failures=$((failures + 1))
fi

# 재등록 이전 완료나 다른 명령의 완료는 재접속 복구의 증거로 사용하지 않는다.
source "$REPO_ROOT/scripts/lib/scenario.sh"
for test_case in valid old-completion wrong-id other-device no-dispatch; do
    printf '%s\n' \
        '{"event":"session.connection.register.accept","device":"target"}' \
        '{"event":"command.dispatch","device":"target","command_id":1}' \
        '{"event":"command.complete","device":"target","command_id":1}' \
        '{"event":"session.connection.register.accept","device":"target"}' >"$test_dir/chain.jsonl"
    case "$test_case" in
    valid | wrong-id | other-device)
        printf '%s\n' '{"event":"command.dispatch","device":"target","command_id":2}' >>"$test_dir/chain.jsonl" ;;
    esac
    case "$test_case" in
    valid | no-dispatch) printf '%s\n' '{"event":"command.complete","device":"target","command_id":2}' >>"$test_dir/chain.jsonl" ;;
    wrong-id) printf '%s\n' '{"event":"command.complete","device":"target","command_id":3}' >>"$test_dir/chain.jsonl" ;;
    other-device) printf '%s\n' '{"event":"command.complete","device":"other","command_id":2}' >>"$test_dir/chain.jsonl" ;;
    esac
    scenario_reconnect_chain target 2 <"$test_dir/chain.jsonl" >/dev/null 2>&1
    status=$?
    if { [ "$test_case" = valid ] && [ "$status" -eq 0 ]; } ||
        { [ "$test_case" != valid ] && [ "$status" -ne 0 ]; }; then
        printf 'PASS 재접속 후 명령 완료 검증: %s\n' "$test_case"
    else
        printf 'FAIL 재접속 후 명령 완료 검증: %s\n' "$test_case"
        failures=$((failures + 1))
    fi
done

mkdir -p "$test_dir/scripts"
cp "$REPO_ROOT/scripts/"*.sh "$test_dir/scripts/"
mkdir -p "$test_dir/scripts/lib"
cp "$REPO_ROOT/scripts/lib/"*.sh "$test_dir/scripts/lib/"
mkdir -p "$test_dir/scripts/scenario"
cp "$REPO_ROOT/scripts/scenario/"*.sh "$test_dir/scripts/scenario/"
# 과열 제어 검증에 미리 정한 메트릭을 제공한다.
# shellcheck disable=SC2016 # 테스트용 스크립트 실행 시 확장한다.
printf '%s\n' \
    'stack_up() { _SCENARIO_BUILD_DIR="$MOCK_DIR"; }' \
    'stack_down() { :; }' \
    'wait_for() { :; }' \
    'soak() { :; }' \
    'metric_int() { printf 20; }' \
    'hot_distinct() { printf 20; }' >>"$test_dir/scripts/lib/scenario.sh"
curl() {
    local call group safe=2 performance=3
    call="$(<"$MOCK_DIR/calls")"; call=$((call + 1))
    printf '%s\n' "$call" >"$MOCK_DIR/calls"
    # 첫 요청은 화면 출력용, 이후 요청은 판정용이다.
    if [ "$call" -ge 3 ]; then
        case "$MOCK_CASE" in recovery) safe=1; performance=4 ;; missing) safe= ;; malformed) safe=oops ;; esac
    fi
    printf 'ddcs_connections 20\n'
    for group in zone_a zone_b zone_c zone_d; do
        printf 'ddcs_group_devices{group="%s",mode="normal"} 0\n' "$group"
        [ -z "$safe" ] || printf 'ddcs_group_devices{group="%s",mode="safe"} %s\n' "$group" "$safe"
        printf 'ddcs_group_devices{group="%s",mode="performance"} %s\n' "$group" "$performance"
    done
}
sleep() { :; }
export -f curl sleep
for test_case in recovery missing malformed; do
    export MOCK_CASE="$test_case" MOCK_DIR="$test_dir/$test_case"
    mkdir "$MOCK_DIR"
    printf '{}\n' >"$MOCK_DIR/build.json"
    printf '0\n' >"$MOCK_DIR/calls"
    bash "$test_dir/scripts/scenario/check-thermal-control.sh" >"$MOCK_DIR/output" 2>&1
    status=$?
    expected=1
    [ "$test_case" != recovery ] || expected=0
    if [ "$status" -eq "$expected" ]; then
        printf 'PASS 과열 제어 검증: %s\n' "$test_case"
    else
        printf 'FAIL 과열 제어 검증: %s (종료 코드=%s)\n' "$test_case" "$status"
        failures=$((failures + 1))
    fi
done
[ "$failures" -eq 0 ]
