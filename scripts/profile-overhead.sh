#!/usr/bin/env bash
#
# 같은 workload에서 profiler off/on을 3쌍 교차 실행해 대표 overhead를 남긴다.
#
# 사용법: scripts/profile-overhead.sh <balance|single> <총 Agent 수>
#
# 각 물리 run의 raw profile과 metrics snapshot은 /tmp에서 검증한 뒤 폐기한다. build.json에는
# off/on 중앙값과 그 차이만 남기므로, run ID나 raw 경로가 결과 계약에 섞이지 않는다.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/result-lib.sh
source "$ROOT/scripts/result-lib.sh"

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

is_positive_integer() { [[ "$1" =~ ^[1-9][0-9]*$ ]]; }

[ "$#" -eq 2 ] || usage
case "$MODE" in balance | single) ;; *) usage ;; esac
is_positive_integer "$AGENT_COUNT" || usage
case "$SKIP_PREFLIGHT" in 0 | 1) ;; *) fail "DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT는 0 또는 1이어야 합니다." ;; esac
if [ "$MODE" = balance ] && [ $((AGENT_COUNT % 4)) -ne 0 ]; then
    fail "balance의 총 Agent 수는 4의 배수여야 합니다: $AGENT_COUNT"
fi

command -v docker >/dev/null 2>&1 || fail "'docker' 명령을 찾을 수 없습니다."
docker compose version >/dev/null 2>&1 || fail "docker compose v2가 필요합니다."
command -v jq >/dev/null 2>&1 || fail "'jq' 명령을 찾을 수 없습니다."

# var/ 출력물을 만들기 전에 source 상태를 고정해 여섯 run이 하나의 build identity를 공유하게 한다.
SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
    SOURCE_DIRTY=true
else
    SOURCE_DIRTY=false
fi

if [ "$SKIP_PREFLIGHT" = 0 ]; then
    "$ROOT/scripts/perf-preflight.sh"
fi

echo "공통 이미지 빌드"
docker compose -f "$ROOT/docker/docker-compose.scale.yml" build
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || fail "Controller image ID를 읽지 못했습니다."
[ -n "$AGENT_IMAGE_ID" ] || fail "Agent image ID를 읽지 못했습니다."
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$ROOT/config")" || exit 1
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1

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
            DDCS_PROFILE_ENABLED="$enabled" \
            DDCS_PROFILE_RECORD_CAPTURE=false \
            DDCS_PROFILE_RESULT_JSON="$run_json" \
            DDCS_PROFILE_SKIP_BUILD=1 \
            DDCS_PROFILE_SKIP_PREFLIGHT=1 \
            DDCS_PROFILE_SOURCE_REVISION="$SOURCE_REVISION" \
            DDCS_PROFILE_SOURCE_DIRTY="$SOURCE_DIRTY" \
            "$ROOT/scripts/profile-capture.sh" "$MODE" "$AGENT_COUNT" "$DURATION_SECONDS"
        jq -e \
            --arg build_key "$DDCS_RESULT_BUILD_KEY" \
            --arg condition "$(printf '%s-%04d' "$MODE" "$AGENT_COUNT")" \
            --argjson profile_enabled "$enabled" '
                .build_key == $build_key and
                .condition == $condition and
                .profile_enabled == $profile_enabled and
                (if $profile_enabled then .verified == true else .verified == null end)
            ' "$run_json" >/dev/null ||
            fail "overhead run 조건 또는 검증 상태가 다릅니다: ${side}-${padded_repeat}"
    done
done

CONDITION="$(printf '%s-%04d' "$MODE" "$AGENT_COUNT")"
OVERHEAD_SUMMARY="$(jq -s \
    --argjson duration_seconds "$DURATION_SECONDS" \
    --argjson repetitions "$REPETITIONS" '
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
            duration_seconds: $duration_seconds,
            repetitions: $repetitions,
            statistic: "median",
            tick_average_us: comparison("tick_average_us"),
            controller_cpu_percent: comparison("controller_cpu_percent"),
            rtt_average_ms: comparison("rtt_average_ms")
        }
    ' "$TEMP_DIR"/*.json)" || fail "profile overhead 대표값을 계산하지 못했습니다."

result_set_profile_overhead "$DDCS_RESULT_BUILD_DIR" "$CONDITION" "$OVERHEAD_SUMMARY" ||
    fail "build.json에 profile overhead 결과를 기록하지 못했습니다."

echo
echo "완료: $DDCS_RESULT_BUILD_DIR/build.json"
echo "  조건: $CONDITION"
echo "  off/on 원시 산출물은 검증 뒤 제거했습니다."
