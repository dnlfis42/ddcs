#!/usr/bin/env bash

# 프로파일러를 끈 상태와 켠 상태를 각각 3번 측정해 추가 비용을 비교한다.
# 사용법: scripts/profiling/measure-profiler-overhead.sh <balance|single> <총 Agent 수>
# 환경 검사를 생략하거나 경고가 있으면 대표 결과로 저장하지 않는다.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/results.sh
source "$ROOT/scripts/lib/results.sh"
# shellcheck source=scripts/lib/workload.sh
source "$ROOT/scripts/lib/workload.sh"

MODE="${1:-}"
AGENT_COUNT="${2:-}"
DURATION_SECONDS=120
REPETITIONS=3
SKIP_PREFLIGHT="${DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT:-0}"

fail() {
    echo "오류: $*" >&2
    exit 1
}

usage() {
    echo "사용법: $0 <balance|single> <총 Agent 수>" >&2
    exit 2
}

is_positive_integer() { [[ "$1" =~ ^[1-9][0-9]{0,4}$ ]] && [ "$1" -le 65504 ]; }

[ "$#" -eq 2 ] || usage
case "$MODE" in balance | single) ;; *) usage ;; esac
is_positive_integer "$AGENT_COUNT" || usage
workload_configure "$MODE" "$AGENT_COUNT" || exit 2
case "$SKIP_PREFLIGHT" in 0 | 1) ;; *) fail "DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT는 0 또는 1이어야 합니다." ;; esac
if [ "$MODE" = balance ] && [ $((AGENT_COUNT % 4)) -ne 0 ]; then
    fail "balance의 총 Agent 수는 4의 배수여야 합니다: $AGENT_COUNT"
fi

command -v docker >/dev/null 2>&1 || fail "'docker' 명령을 찾을 수 없습니다."
docker compose version >/dev/null 2>&1 || fail "docker compose v2가 필요합니다."
command -v jq >/dev/null 2>&1 || fail "'jq' 명령을 찾을 수 없습니다."

# 모든 측정에 같은 소스 상태를 기록하도록 결과 생성 전에 확인한다.
SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
    SOURCE_DIRTY=true
else
    SOURCE_DIRTY=false
fi

PREFLIGHT_SKIPPED=true
PREFLIGHT_WARNING_COUNT=0
if [ "$SKIP_PREFLIGHT" = 0 ]; then
    if ! PREFLIGHT_OUTPUT="$("$ROOT/scripts/measurement/verify-environment.sh" fleet)"; then
        printf '%s\n' "$PREFLIGHT_OUTPUT"
        fail "성능 사전 검사에 실패했습니다."
    fi
    printf '%s\n' "$PREFLIGHT_OUTPUT"
    PREFLIGHT_SKIPPED=false
    PREFLIGHT_WARNING_COUNT="$(printf '%s\n' "$PREFLIGHT_OUTPUT" | grep -c '^\[WARN\]' || true)"
fi
REPRESENTATIVE_ELIGIBLE=false
if [ "$PREFLIGHT_SKIPPED" = false ] && [ "$PREFLIGHT_WARNING_COUNT" -eq 0 ]; then
    REPRESENTATIVE_ELIGIBLE=true
fi

echo "공통 이미지 빌드"
docker compose -f "$ROOT/docker/docker-compose.perf.yml" build controller fleet-zone-a
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent-fleet:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || fail "Controller image ID를 읽지 못했습니다."
[ -n "$AGENT_IMAGE_ID" ] || fail "Agent image ID를 읽지 못했습니다."
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ddcs-profile-overhead.XXXXXX")" ||
    fail "profile overhead 임시 디렉터리를 만들지 못했습니다."
cleanup() {
    local status=$?
    trap - EXIT INT TERM
    rm -rf -- "$TEMP_DIR"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

python3 "$ROOT/scripts/performance/workload-config.py" prepare "$ROOT/config" "$TEMP_DIR/config" "${policy_args[@]}"
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$TEMP_DIR/config")" || exit 1
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1
python3 "$ROOT/scripts/performance/workload-config.py" generate "$TEMP_DIR/config" "$TEMP_DIR" \
    --total "$AGENT_COUNT" --layout "$MODE" "${layout_args[@]}" "${policy_args[@]}"

echo "Profiler overhead 교차 수집: ${MODE}, Agent ${AGENT_COUNT}대, ${DURATION_SECONDS}s × ${REPETITIONS}쌍"
for repeat in $(seq 1 "$REPETITIONS"); do
    padded_repeat="$(printf '%02d' "$repeat")"
    for enabled in false true; do
        if [ "$enabled" = false ]; then
            side=off
        else
            side=on
        fi
        run_json="$TEMP_DIR/${side}-${padded_repeat}.json"
        echo "  ${side}-${padded_repeat} (profile.enabled=${enabled})"
        env \
            DDCS_PERF_CONFIG_SOURCE="$TEMP_DIR/config" \
            DDCS_PROFILE_ENABLED="$enabled" \
            DDCS_PROFILE_RECORD_CAPTURE=false \
            DDCS_PROFILE_RESULT_JSON="$run_json" \
            DDCS_PROFILE_SKIP_BUILD=1 \
            DDCS_PROFILE_SKIP_PREFLIGHT=1 \
            DDCS_PROFILE_SOURCE_REVISION="$SOURCE_REVISION" \
            DDCS_PROFILE_SOURCE_DIRTY="$SOURCE_DIRTY" \
            "$ROOT/scripts/profiling/capture-controller-profile.sh" "$MODE" "$AGENT_COUNT" "$DURATION_SECONDS"
        jq -e \
            --slurpfile workload "$TEMP_DIR/workload.json" \
            --arg build_key "$DDCS_RESULT_BUILD_KEY" \
            --arg condition "$(printf '%s-%04d' "$MODE" "$AGENT_COUNT")" \
            --argjson profile_enabled "$enabled" '
                .build_key == $build_key and
                .load_generator == "agent-fleet" and
                .level_lifecycle == "fresh_stack" and
                (.workload_configuration | del(.compose_sha256)) == ($workload[0] | del(.compose_sha256)) and
                .condition == $condition and
                .profile_enabled == $profile_enabled and
                (if $profile_enabled then .verified == true else .verified == null end)
            ' "$run_json" >/dev/null ||
            fail "overhead run 조건 또는 검증 상태가 다릅니다: ${side}-${padded_repeat}"
    done
done

CONDITION="$(printf '%s-%04d' "$MODE" "$AGENT_COUNT")"
OVERHEAD_SUMMARY="$(jq -s \
    --slurpfile workload "$TEMP_DIR/workload.json" \
    --argjson duration_seconds "$DURATION_SECONDS" \
    --argjson repetitions "$REPETITIONS" \
    --argjson preflight_skipped "$PREFLIGHT_SKIPPED" \
    --argjson preflight_warning_count "$PREFLIGHT_WARNING_COUNT" \
    --argjson representative_eligible "$REPRESENTATIVE_ELIGIBLE" '
        def median:
            sort | length as $n |
            if $n == 0 then null
            elif ($n % 2) == 1 then .[$n / 2]
            else ((.[$n / 2 - 1] + .[$n / 2]) / 2)
            end;
        def value($enabled; $field):
            [ .[] | select(.profile_enabled == $enabled) | .metrics[$field] | select(. != null) ] | median;
        def comparison($field):
            (value(false; $field)) as $off |
            (value(true; $field)) as $on |
            {
                off: $off,
                on: $on,
                delta: (if $off == null or $on == null then null else $on - $off end),
                ratio: (if $off == null or $on == null or $off == 0 then null else $on / $off end)
            };
        {
            preflight_skipped: $preflight_skipped,
            preflight_warning_count: $preflight_warning_count,
            representative_eligible: $representative_eligible,
            load_generator: "agent-fleet",
            workload_configuration: $workload[0],
            duration_seconds: $duration_seconds,
            repetitions: $repetitions,
            statistic: "median",
            tick_average_us: comparison("tick_average_us"),
            controller_cpu_percent: comparison("controller_cpu_percent"),
            rtt_average_ms: comparison("rtt_average_ms")
        }
    ' "$TEMP_DIR"/off-*.json "$TEMP_DIR"/on-*.json)" || fail "profile overhead 대표값을 계산하지 못했습니다."

if [ "$REPRESENTATIVE_ELIGIBLE" = true ]; then
    result_set_profile_overhead "$DDCS_RESULT_BUILD_DIR" "$CONDITION" "$OVERHEAD_SUMMARY" ||
        fail "build.json에 profile overhead 결과를 기록하지 못했습니다."
else
    echo "진단 실행: 사전 검사 생략 또는 경고가 있어 대표 결과는 저장하지 않습니다."
fi

echo
printf '%s\n' "$OVERHEAD_SUMMARY"
if [ "$REPRESENTATIVE_ELIGIBLE" = true ]; then echo "완료: $DDCS_RESULT_BUILD_DIR/build.json"; fi
echo "  조건: $CONDITION"
echo "  off/on 원시 산출물은 검증 뒤 제거했습니다."
