#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d /tmp/ddcs-profile-test.XXXXXX)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/scripts/profiling" "$test_dir/scripts/performance" "$test_dir/scripts/measurement" "$test_dir/config"
cp "$REPO_ROOT/scripts/"*.sh "$test_dir/scripts/"
mkdir -p "$test_dir/scripts/lib"
cp "$REPO_ROOT/scripts/lib/"*.sh "$test_dir/scripts/lib/"
mkdir -p "$test_dir/scripts/scenario"
cp "$REPO_ROOT/scripts/scenario/"*.sh "$test_dir/scripts/scenario/"
cp "$REPO_ROOT/scripts/profiling/"*.sh "$test_dir/scripts/profiling/"
cp "$REPO_ROOT/scripts/performance/workload-config.py" "$test_dir/scripts/performance/"
mkdir -p "$test_dir/docker"
export DDCS_PERF_AGENTS_PER_FLEET=1
cp "$REPO_ROOT/config/"*.json "$test_dir/config/"
# 프로파일 분석 도구를 대체해 수집·검증 흐름을 검사한다.
# shellcheck disable=SC2016 # 테스트용 스크립트 실행 시 확장한다.
printf '%s\n' '#!/usr/bin/env bash' '[ "$MOCK_CASE" != verify-failure ]' >"$test_dir/verify"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\n" header "run,100,200,16384,3,0,true,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0"' >"$test_dir/report"
chmod +x "$test_dir/verify" "$test_dir/report"
export DDCS_PROFILE_VERIFY_BIN="$test_dir/verify" DDCS_PROFILE_REPORT_BIN="$test_dir/report"
# shellcheck disable=SC2016 # 테스트용 스크립트 실행 시 확장한다.
printf '%s\n' '#!/usr/bin/env bash' 'printf "[%s] mock preflight\n" "$MOCK_GATE"' >"$test_dir/scripts/measurement/verify-environment.sh"
chmod +x "$test_dir/scripts/measurement/verify-environment.sh"

docker() {
    local cmd="$1" project=default model=
    shift
    case "$cmd" in
    ps) return 0 ;;
    image) printf 'sha256:%s\n' "${*: -1}" ;;
    inspect) printf '0\n' ;;
    logs) printf '"event":"command.dispatch"\n%.0s' {1..4} ;;
    kill)
        # 정책 재적용 전후로 원본 설정과 사용자 편집을 보존해야 한다.
        cmp "$MOCK_DIR/original.json" "$MOCK_ROOT/config/controller.json" || return 9
        printf '\n' >>"$MOCK_ROOT/config/controller.json"
        cp "$MOCK_ROOT/config/controller.json" "$MOCK_DIR/edited.json"
        kill -TERM "$BASHPID"
        ;;
    compose)
        while [ "$#" -gt 0 ]; do
            case "$1" in
            --project-name | -p) project="$2"; shift 2 ;;
            --file | -f) if [[ "$2" == *.json ]]; then model="$2"; fi; shift 2 ;;
            *) break ;;
            esac
        done
        case "${1:-}" in
        version | build) return 0 ;;
        config) printf '{"services":{"controller":{}}}\n' ;;
        logs) printf 'fixture compose logs\n' ;;
        up)
            printf 'up %s %s\n' "$project" "$*" >>"$MOCK_DIR/commands"
            printf '0\n' >"$MOCK_DIR/calls"
            if [ -n "$model" ]; then
                cp "$model" "$MOCK_DIR/compose.json"
                cp "$(dirname "$model")/workload.json" "$MOCK_DIR/workload.json"
                cp "$(dirname "$model")/config/controller.json" "$MOCK_DIR/effective-controller.json"
                if jq -e 'any(.services[]; .command[1] == "zone_b")' "$model" >/dev/null; then
                    printf 'balance\n' >"$MOCK_DIR/layout"
                else
                    printf 'single\n' >"$MOCK_DIR/layout"
                fi
            else
                printf 'single\n' >"$MOCK_DIR/layout"
            fi
            [ "$MOCK_CASE" != interrupt ] || kill -TERM "$BASHPID"
            [ "$MOCK_CASE" != partial-start ]
            ;;
        ps) printf 'owned-controller\n' ;;
        stop)
            [ "$MOCK_CASE" != stop-failure ] || return 1
            if [ "${DDCS_PROFILE_ENABLED:-false}" = true ] && [ "$MOCK_CASE" != missing-raw ]; then
                printf '{}\n' >"$DDCS_PROFILE_OUTPUT_DIR/tick-profile.json"
            fi
            ;;
        down)
            printf 'down %s\n' "$project" >>"$MOCK_DIR/commands"
            [ "$MOCK_CASE" != cleanup-failure ]
            ;;
        *) return 1 ;;
        esac
        ;;
    *) return 1 ;;
    esac
}
curl() {
    local call reported=4
    call="$(<"$MOCK_DIR/calls")"
    call=$((call + 1))
    printf '%s\n' "$call" >"$MOCK_DIR/calls"
    case "$MOCK_CASE" in
    unreported) reported=0 ;;
    start-loss) [ "$call" -lt 2 ] || reported=0 ;;
    end-loss) [ "$call" -lt 3 ] || reported=0 ;;
    esac
    local total=4 per_group=1
    if [ -s "$MOCK_DIR/workload.json" ]; then
        total="$(jq -r .requested_agents "$MOCK_DIR/workload.json")"
        per_group=$((total / 4))
        [ "$reported" = 0 ] || reported="$total"
    fi
    printf 'ddcs_connections %s\n' "$total"
    if [ "$(<"$MOCK_DIR/layout")" = balance ]; then
        for group in zone_a zone_b zone_c zone_d; do
            printf 'ddcs_group_devices{group="%s",mode="normal"} %s\n' "$group" "$per_group"
        done
    else
        printf 'ddcs_group_devices{group="zone_a",mode="normal"} %s\n' "$reported"
    fi
    printf 'ddcs_ticks_total %s\nddcs_tick_duration_seconds_total 0.%06d\n' "$call" "$call"
    printf 'ddcs_command_rtt_seconds_count %s\nddcs_command_rtt_seconds_sum 0.%06d\n' "$call" "$call"
}
sleep() { SECONDS=$((SECONDS + $1)); }
export -f docker curl sleep
export MOCK_ROOT="$test_dir"
failures=0
for test_case in normal balance enabled missing-raw verify-failure unreported start-loss end-loss partial-start stop-failure cleanup-failure interrupt; do
    export MOCK_CASE="$test_case" MOCK_DIR="$test_dir/$test_case"
    mkdir "$MOCK_DIR"
    status=0
    enabled=false
    mode=single
    case "$test_case" in enabled | missing-raw | verify-failure) enabled=true ;; balance) mode=balance ;; esac
    env TMPDIR="$MOCK_DIR" DDCS_PROFILE_ENABLED="$enabled" DDCS_PROFILE_RECORD_CAPTURE=false \
        DDCS_PROFILE_SKIP_BUILD=1 DDCS_PROFILE_SKIP_PREFLIGHT=1 \
        DDCS_PROFILE_WARMUP_SECONDS=1 DDCS_PROFILE_READY_TIMEOUT=2 \
        DDCS_PROFILE_RESULT_JSON="$MOCK_DIR/result.json" \
        bash "$test_dir/scripts/profiling/capture-controller-profile.sh" "$mode" 4 1 >"$MOCK_DIR/output" 2>&1 || status=$?
    expected=1
    case "$test_case" in normal | balance | enabled) expected=0 ;; interrupt) expected=130 ;; esac
    if [ "$status" -ne "$expected" ] ||
        grep -Eq '^up default|--scale' "$MOCK_DIR/commands" ||
        { [ "$expected" != 0 ] && [ -e "$MOCK_DIR/result.json" ]; }; then
        printf 'FAIL 프로파일 수집 %s (종료 코드=%s)\n' "$test_case" "$status"
        failures=$((failures + 1))
    else
        printf 'PASS 프로파일 수집 %s\n' "$test_case"
    fi
done

# 두 배치 모두 기본 크기의 Fleet 20개를 사용해야 한다.
for mode in single balance; do
    export MOCK_CASE=topology MOCK_DIR="$test_dir/topology-$mode"
    mkdir "$MOCK_DIR"
    before="$(sha256sum "$test_dir/config/controller.json")"
    status=0
    env -u DDCS_PERF_AGENTS_PER_FLEET -u DDCS_PERF_UNIFORM_POLICY \
        DDCS_PROFILE_ENABLED=false DDCS_PROFILE_RECORD_CAPTURE=false \
        DDCS_PROFILE_SKIP_BUILD=1 DDCS_PROFILE_SKIP_PREFLIGHT=1 \
        DDCS_PROFILE_WARMUP_SECONDS=0 DDCS_PROFILE_RESULT_JSON="$MOCK_DIR/result.json" \
        bash "$test_dir/scripts/profiling/capture-controller-profile.sh" "$mode" 20000 1 >"$MOCK_DIR/output" 2>&1 || status=$?
    if [ "$status" -eq 0 ] && [ "$before" = "$(sha256sum "$test_dir/config/controller.json")" ] &&
        jq -e --arg mode "$mode" '
            .workload_configuration | .fleet_count == 20 and .agents_per_fleet == 1000 and
            .uniform_policy == true and .layout == $mode and
            ([.fleets[] | select(.group == "zone_a")] | length) == (if $mode == "single" then 20 else 5 end)' \
            "$MOCK_DIR/result.json" >/dev/null &&
        jq -e '.policy.groups | .zone_a as $rule | all(.[]; . == $rule)' "$MOCK_DIR/effective-controller.json" >/dev/null; then
        printf 'PASS 기본 Fleet 구성 %s\n' "$mode"
    else
        printf 'FAIL 기본 Fleet 구성 %s (종료 코드=%s)\n' "$mode" "$status"
        tail -8 "$MOCK_DIR/output"
        failures=$((failures + 1))
    fi
done

# 진단 실행이 기존 대표 결과를 덮어쓰면 안 된다.
build_files=("$test_dir"/var/result/*/build.json)
build_file="${build_files[0]}"
jq '.profile.capture["single-0004"] = {sentinel:true} | .profile.overhead["balance-0004"] = {sentinel:true}' \
    "$build_file" >"$test_dir/seed.json"
mv "$test_dir/seed.json" "$build_file"
export MOCK_CASE=diagnostic MOCK_DIR="$test_dir/diagnostic"
mkdir "$MOCK_DIR"
status=0
env DDCS_PROFILE_ENABLED=true DDCS_PROFILE_RECORD_CAPTURE=true DDCS_PROFILE_SKIP_BUILD=1 \
    DDCS_PROFILE_SKIP_PREFLIGHT=1 DDCS_PROFILE_WARMUP_SECONDS=1 \
    DDCS_PROFILE_RESULT_JSON="$MOCK_DIR/result.json" \
    bash "$test_dir/scripts/profiling/capture-controller-profile.sh" single 4 1 >"$MOCK_DIR/output" 2>&1 || status=$?
if [ "$status" -eq 0 ] && jq -e '.profile.capture["single-0004"].sentinel == true' "$build_file" >/dev/null &&
    jq -e '.preflight_skipped == true and .representative_eligible == false' "$MOCK_DIR/result.json" >/dev/null; then
    printf 'PASS 진단 수집 시 기존 대표 결과 보존\n'
else
    printf 'FAIL 진단 수집이 대표 결과를 덮어썼거나 실행 근거를 누락함\n'
    failures=$((failures + 1))
fi

export MOCK_CASE=overhead MOCK_DIR="$test_dir/overhead"
mkdir "$MOCK_DIR"
status=0
env DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT=1 DDCS_PROFILE_WARMUP_SECONDS=1 \
    bash "$test_dir/scripts/profiling/measure-profiler-overhead.sh" balance 4 >"$MOCK_DIR/output" 2>&1 || status=$?
if [ "$status" -eq 0 ] && jq -e '.profile.overhead["balance-0004"].sentinel == true' "$build_file" >/dev/null &&
    [ "$(grep -c '^up ' "$MOCK_DIR/commands")" -eq 6 ] &&
    [ "$(grep '^up ' "$MOCK_DIR/commands" | awk '{print $2}' | sort -u | wc -l)" -eq 6 ]; then
    printf 'PASS 프로파일러 비용 측정: 독립된 스택으로 6회 실행\n'
else
    printf 'FAIL 프로파일러 비용 측정 (종료 코드=%s)\n' "$status"
    tail -8 "$MOCK_DIR/output"
    failures=$((failures + 1))
fi

# 환경 검사에 경고가 있으면 기존 대표 결과를 보존한다.
for gate in WARN PASS; do
    export MOCK_GATE="$gate" MOCK_CASE=gate MOCK_DIR="$test_dir/gate-$gate"
    mkdir "$MOCK_DIR"
    status=0
    env DDCS_PROFILE_ENABLED=true DDCS_PROFILE_RECORD_CAPTURE=true DDCS_PROFILE_SKIP_BUILD=1 \
        DDCS_PROFILE_SKIP_PREFLIGHT=0 DDCS_PROFILE_WARMUP_SECONDS=1 \
        DDCS_PROFILE_RESULT_JSON="$MOCK_DIR/result.json" \
        bash "$test_dir/scripts/profiling/capture-controller-profile.sh" single 4 1 >"$MOCK_DIR/output" 2>&1 || status=$?
    eligible=false; warnings=1; saved='.profile.capture["single-0004"].sentinel == true'
    if [ "$gate" = PASS ]; then
        eligible=true; warnings=0; saved='.profile.capture["single-0004"].duration_seconds == 1'
    fi
    if [ "$status" -eq 0 ] && jq -e "$saved" "$build_file" >/dev/null &&
        jq -e --argjson eligible "$eligible" --argjson warnings "$warnings" \
            '.preflight_skipped == false and .representative_eligible == $eligible and .preflight_warning_count == $warnings' \
            "$MOCK_DIR/result.json" >/dev/null; then
        printf 'PASS 프로파일 수집의 환경 검사 결과 처리: %s\n' "$gate"
    else
        printf 'FAIL 프로파일 수집의 환경 검사 결과 처리: %s\n' "$gate"
        failures=$((failures + 1))
    fi
    status=0
    env DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT=0 DDCS_PROFILE_WARMUP_SECONDS=1 \
        bash "$test_dir/scripts/profiling/measure-profiler-overhead.sh" balance 4 >"$MOCK_DIR/overhead-output" 2>&1 || status=$?
    saved='.profile.overhead["balance-0004"].sentinel == true'
    if [ "$gate" = PASS ]; then saved='.profile.overhead["balance-0004"].representative_eligible == true'; fi
    if [ "$status" -eq 0 ] && jq -e "$saved" "$build_file" >/dev/null; then
        printf 'PASS 프로파일러 비용 측정의 환경 검사 결과 처리: %s\n' "$gate"
    else
        printf 'FAIL 프로파일러 비용 측정의 환경 검사 결과 처리: %s\n' "$gate"
        failures=$((failures + 1))
    fi
done

# Agent 수가 다섯 자리여도 결과를 저장할 수 있어야 한다.
source "$REPO_ROOT/scripts/lib/results.sh"
mkdir "$test_dir/result"
printf '{}\n' >"$test_dir/result/build.json"
if result_set_profile_capture "$test_dir/result" single-10000 1 true '{}' &&
    result_set_profile_overhead "$test_dir/result" balance-10000 '{}' &&
    jq -e '.profile.capture["single-10000"].verified == true and .profile.overhead["balance-10000"] == {}' \
        "$test_dir/result/build.json" >/dev/null; then
    printf 'PASS 다섯 자리 Agent 수의 결과 저장\n'
else
    printf 'FAIL 다섯 자리 Agent 수의 결과 저장\n'
    failures=$((failures + 1))
fi

for condition in single-0000 single-00004 single-65505 balance-0005 single-999999999999; do
    if result_valid_profile_condition "$condition"; then
        printf 'FAIL 잘못된 측정 조건 거부: %s\n' "$condition"
        failures=$((failures + 1))
    else
        printf 'PASS 잘못된 측정 조건 거부: %s\n' "$condition"
    fi
done

export MOCK_CASE=policy MOCK_DIR="$test_dir/policy"
mkdir "$MOCK_DIR"
cp "$test_dir/config/controller.json" "$MOCK_DIR/original.json"
status=0
env TMPDIR="$MOCK_DIR" bash "$test_dir/scripts/scenario/check-policy-reload.sh" \
    >"$MOCK_DIR/output" 2>&1 || status=$?
if [ "$status" -eq 130 ] && [ -f "$MOCK_DIR/edited.json" ] &&
    cmp -s "$MOCK_DIR/edited.json" "$test_dir/config/controller.json"; then
    printf 'PASS 원본 정책과 사용자 편집 보존\n'
else
    printf 'FAIL 원본 정책 보존 (종료 코드=%s)\n' "$status"
    failures=$((failures + 1))
fi
[ "$failures" -eq 0 ]
