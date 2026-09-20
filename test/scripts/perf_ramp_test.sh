#!/usr/bin/env bash
# 임시 저장소에서 측정 스크립트를 실행한다. Docker와 메트릭은 테스트용으로 대체한다.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d /tmp/ddcs-perf-ramp-test.XXXXXX)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/scripts/lib" "$test_dir/scripts/performance" "$test_dir/scripts/measurement" "$test_dir/config" "$test_dir/docker"
cp "$REPO_ROOT/scripts/lib/scenario.sh" "$REPO_ROOT/scripts/lib/results.sh" \
    "$REPO_ROOT/scripts/lib/workload.sh" "$test_dir/scripts/lib/"
cp "$REPO_ROOT/scripts/performance/"*.sh "$REPO_ROOT/scripts/performance/"*.py \
    "$test_dir/scripts/performance/"
cp "$REPO_ROOT/config/"*.json "$test_dir/config/"

docker() {
    local command="${1:-}" model=
    shift
    case "$command" in
    ps) return 0 ;;
    image)
        case "${*: -1}" in
        ddcs-controller:dev) printf 'sha256:controller\n' ;;
        ddcs-agent-fleet:dev) printf 'sha256:fleet\n' ;;
        *) printf 'sha256:legacy-agent\n' ;;
        esac
        ;;
    inspect) printf '0\n' ;;
    logs) printf '{"event":"policy.regime.update"}\n' ;;
    stats)
        [ "$MOCK_CASE" != stats-error ] || return 1
        printf '{"Name":"mock-fleet","MemUsage":"16MiB"}\n'
        ;;
    compose)
        while [ "$#" -gt 0 ]; do
            case "$1" in
            --project-name | -p) shift 2 ;;
            --file | -f) model="$2"; shift 2 ;;
            *) break ;;
            esac
        done
        case "${1:-}" in
        version) return 0 ;;
        config) if [ -f "$model" ]; then cat "$model"; else printf '{"services":{"controller":{}}}\n'; fi ;;
        build) return 0 ;;
        up)
            [ "$MOCK_CASE" != startup-error ] || return 1
            printf '%s count=%s\n' "$*" "${DDCS_PERF_AGENTS_PER_FLEET:-4}" >>"$MOCK_DIR/docker.txt"
            printf '0\n' >"$MOCK_DIR/curl-count"
            printf '%s\n' "${DDCS_PERF_AGENTS_PER_FLEET:-4}" >"$MOCK_DIR/count"
            case " $* " in
            *' fleet-zone-b '*) printf 'balance\n' >"$MOCK_DIR/layout" ;;
            *) printf 'single\n' >"$MOCK_DIR/layout" ;;
            esac
            if [ -f "$model" ]; then
                jq -r '[.services[] | select(.command) | select(.command[1] == "zone_a") | .command[0] | tonumber] | add' "$model" >"$MOCK_DIR/count"
                if jq -e 'any(.services[]; .command[1] == "zone_b")' "$model" >/dev/null; then
                    printf 'balance\n' >"$MOCK_DIR/layout"
                else
                    printf 'single\n' >"$MOCK_DIR/layout"
                fi
            fi
            ;;
        ps) printf 'mock-controller\n' ;;
        down) printf 'down\n' >>"$MOCK_DIR/docker.txt" ;;
        *) printf '예상하지 않은 Compose 명령: %s\n' "$*" >&2; return 1 ;;
        esac
        ;;
    *) printf '예상하지 않은 Docker 명령: %s\n' "$command" >&2; return 1 ;;
    esac
}

curl() {
    local call count total reported group groups=zone_a
    [ "$MOCK_CASE" != snapshot-error ] || return 1
    call="$(<"$MOCK_DIR/curl-count")"
    call=$((call + 1))
    printf '%s\n' "$call" >"$MOCK_DIR/curl-count"
    count="$(<"$MOCK_DIR/count")"
    total=$count
    if [ "$(<"$MOCK_DIR/layout")" = balance ]; then
        groups='zone_a zone_b zone_c zone_d'
        total=$((count * 4))
    fi
    if [ "$MOCK_CASE" = connection-loss ]; then
        printf 'ddcs_connections 0\n'
    else
        printf 'ddcs_connections %s\n' "$total"
    fi
    printf 'ddcs_devices %s\n' "$total"
    for group in $groups; do
        reported=$count
        case "$MOCK_CASE" in
        timeout) reported=0 ;;
        settle-loss) if [ "$call" -ge 2 ]; then reported=0; fi ;;
        end-loss) if [ "$call" -ge 3 ]; then reported=0; fi ;;
        wrong-group) group=zone_d ;;
        esac
        printf 'ddcs_group_devices{group="%s",mode="normal"} %s\n' "$group" "$reported"
    done
    printf 'ddcs_ticks_total %s\n' "$call"
    printf 'ddcs_tick_duration_seconds_total 0.%06d\n' "$((call * 100))"
    printf 'ddcs_tick_duration_seconds_max 0.000100\n'
    printf 'ddcs_messages_received_total %s\n' "$((call * 30))"
    printf 'ddcs_command_rtt_seconds_sum 0.%06d\n' "$((call * 10))"
    printf 'ddcs_command_rtt_seconds_count %s\n' "$call"
    printf 'ddcs_commands_pending 0\n'
    printf 'ddcs_connections_closed_total{reason="liveness_expired"} 0\n'
}
sleep() { SECONDS=$((SECONDS + $1)); }
export -f docker curl sleep

export DDCS_PERF_AGENTS_PER_FLEET=1
failures=0
for test_case in single balance timeout settle-loss end-loss wrong-group multilevel periodic connection-loss snapshot-error stats-error startup-error; do
    export MOCK_DIR="$test_dir/$test_case" MOCK_CASE="$test_case" MOCK_LAYOUT=single
    mkdir "$MOCK_DIR"
    [ "$test_case" != balance ] || MOCK_LAYOUT=balance
    levels=4
    [ "$test_case" != multilevel ] || levels='4 8'
    soak=1
    [ "$test_case" != periodic ] || soak=3
    status=0
    env DDCS_PERF_LEVELS="$levels" DDCS_PERF_SETTLE=1 DDCS_PERF_SOAK="$soak" DDCS_PERF_SAMPLE_INTERVAL=1 \
        DDCS_PERF_READY_TIMEOUT=2 DDCS_PERF_SKIP_PREFLIGHT=1 \
        DDCS_PERF_RUN_ID=run DDCS_PERF_OUTPUT_ROOT="$MOCK_DIR" \
        bash "$test_dir/scripts/performance/measure-scalability.sh" "$MOCK_LAYOUT" >"$MOCK_DIR/output.txt" 2>&1 || status=$?
    expected=1
    case "$test_case" in single | balance | multilevel | periodic) expected=0 ;; esac
    if [ "$status" -ne "$expected" ] || ! jq -e \
        --argjson failed "$expected" \
        '.load_generator == "agent-fleet" and .level_lifecycle == "fresh_stack" and
         .agent_image_id == "sha256:fleet" and .failed_levels == $failed' \
        "$MOCK_DIR/run/manifest.json" >/dev/null 2>&1; then
        printf 'FAIL 단계별 성능 측정: %s (종료 코드=%s, 기대값=%s)\n' "$test_case" "$status" "$expected"
        tail -8 "$MOCK_DIR/output.txt"
        failures=$((failures + 1))
        continue
    fi
    if grep -q -- '--scale' "$MOCK_DIR/docker.txt"; then
        printf 'FAIL 단계별 성능 측정: %s: 컨테이너 수를 직접 늘리는 호출이 남아 있음\n' "$test_case"
        failures=$((failures + 1))
        continue
    fi
    if [ "$test_case" = multilevel ] &&
        [ "$(grep -c '^down$' "$MOCK_DIR/docker.txt")" -ne 2 ]; then
        printf 'FAIL 여러 단계 측정: 단계마다 새 스택을 사용해야 함\n'
        failures=$((failures + 1))
        continue
    fi
    if [ "$expected" -eq 0 ] && ! jq -e '.measurement.status == "valid" and .configured_slo.status == "unassessed"' "$MOCK_DIR/run/0004/assessment.json" >/dev/null; then
        printf 'FAIL 단계별 성능 측정: %s: 평가 결과가 없거나 성능 기준 통과로 잘못 판정함\n' "$test_case"
        failures=$((failures + 1))
        continue
    fi
    if [ "$test_case" = periodic ] && { [ ! -s "$MOCK_DIR/run/0004/samples/000001.docker-stats.jsonl" ] || ! jq -e '.measurement.sample_count >= 3' "$MOCK_DIR/run/0004/assessment.json" >/dev/null; }; then
        printf 'FAIL 주기적 수집: 측정 구간의 관측값이 없음\n'
        failures=$((failures + 1))
        continue
    fi
    printf 'PASS 단계별 성능 측정: %s\n' "$test_case"
done

for levels in ' ' '0' '04' '4 4' '65505' '999999999999999999999'; do
    status=0
    env DDCS_PERF_LEVELS="$levels" bash "$test_dir/scripts/performance/measure-scalability.sh" single \
        >"$test_dir/invalid-output.txt" 2>&1 || status=$?
    if [ "$status" -ne 2 ]; then
        printf 'FAIL 잘못된 Agent 수 거부: %s (종료 코드=%s)\n' "$levels" "$status"
        failures=$((failures + 1))
    else
        printf 'PASS 잘못된 Agent 수 거부: %s\n' "$levels"
    fi
done

# 두 배치의 측정 결과가 공통 실행 조건과 일치하는지 확인한다.
# shellcheck disable=SC2016 # 인자는 테스트용 스크립트 실행 시 확장한다.
printf '#!/usr/bin/env bash\n[ "$1" = fleet ] || exit 1\nprintf "[PASS] mock preflight\\n"\n' \
    >"$test_dir/scripts/measurement/verify-environment.sh"
chmod +x "$test_dir/scripts/measurement/verify-environment.sh"
export MOCK_DIR="$test_dir/suite" MOCK_CASE=suite
mkdir "$MOCK_DIR"
status=0
env DDCS_PERF_SUITE_LEVELS=' 4  8 ' DDCS_PERF_SUITE_SETTLE=1 DDCS_PERF_SUITE_SOAK=1 \
    DDCS_PERF_SUITE_ID=suite DDCS_PERF_SKIP_PREFLIGHT=0 \
    bash "$test_dir/scripts/performance/measure-all-layouts.sh" >"$MOCK_DIR/output.txt" 2>&1 || status=$?
suite_manifests=("$test_dir"/var/result/*/performance/suite/manifest.json)
if [ "$status" -ne 0 ] || ! jq -e \
    '.load_generator == "agent-fleet" and .requested_levels == "4 8" and (.runs | length) == 2' \
    "${suite_manifests[0]}" >/dev/null 2>&1; then
    printf 'FAIL 전체 배치 측정 (종료 코드=%s)\n' "$status"
    tail -10 "$MOCK_DIR/output.txt"
    failures=$((failures + 1))
else
    printf 'PASS 전체 배치 측정\n'
fi
# 두 배치 모두 Fleet 20개로 실행하고 원본 정책을 보존해야 한다.
export MOCK_DIR="$test_dir/fixed-suite" MOCK_CASE=suite
mkdir "$MOCK_DIR"
status=0
before="$(sha256sum "$test_dir/config/controller.json")"
env -u DDCS_PERF_AGENTS_PER_FLEET -u DDCS_PERF_UNIFORM_POLICY -u DDCS_PERF_SUITE_LEVELS DDCS_PERF_SUITE_SETTLE=1 DDCS_PERF_SUITE_SOAK=1 \
    DDCS_PERF_SUITE_ID=fixed-suite DDCS_PERF_SKIP_PREFLIGHT=0 \
    bash "$test_dir/scripts/performance/measure-all-layouts.sh" >"$MOCK_DIR/output.txt" 2>&1 || status=$?
fixed_manifests=("$test_dir"/var/result/*/performance/fixed-suite/manifest.json)
if [ "$status" -ne 0 ] || [ "$before" != "$(sha256sum "$test_dir/config/controller.json")" ] || ! jq -e '
    .fixed_agents_per_fleet == 1000 and .uniform_policy == true and
    (.workloads.balance | map(.fleet_count)) == [4,12,20] and
    (.workloads.single | map(.fleet_count)) == [4,12,20] and
    all(.workloads[][]; .agents_per_fleet == 1000) and
    .workloads.balance[2].policy_sha256 == .workloads.single[2].policy_sha256' \
    "${fixed_manifests[0]}" >/dev/null 2>&1; then
    printf 'FAIL 전체 배치 측정의 Fleet 구성 (종료 코드=%s)\n' "$status"
    tail -15 "$MOCK_DIR/output.txt"
    failures=$((failures + 1))
else
    printf 'PASS 전체 배치 측정: Fleet 크기·개수·정책 일치 및 원본 설정 보존\n'
fi
[ "$failures" -eq 0 ]
