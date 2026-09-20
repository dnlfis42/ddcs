#!/usr/bin/env bash

# 같은 이미지와 설정으로 balance·single 배치를 각각 측정한다.
# 사용법: scripts/performance/measure-all-layouts.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/scripts/lib/results.sh"
# shellcheck source=scripts/lib/workload.sh
source "$ROOT/scripts/lib/workload.sh"

LEVELS="${DDCS_PERF_SUITE_LEVELS:-4000 12000 20000}"
SETTLE="${DDCS_PERF_SUITE_SETTLE:-30}"
SOAK="${DDCS_PERF_SUITE_SOAK:-120}"
READY_TIMEOUT="${DDCS_PERF_READY_TIMEOUT:-90}"
EFFECTIVE_CONFIG_TMP=
trap '[ -z "${EFFECTIVE_CONFIG_TMP:-}" ] || rm -rf -- "$EFFECTIVE_CONFIG_TMP"' EXIT

fail() {
    echo "오류: $*" >&2
    exit 1
}

usage() {
    echo "사용법: $0" >&2
    exit 2
}

is_unsigned_integer() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

is_positive_integer() {
    [[ "$1" =~ ^[1-9][0-9]{0,8}$ ]]
}

validate_levels() {
    local level
    local -a levels
    local -A seen
    read -r -a levels <<< "${1//$'\n'/ }"
    [ "${#levels[@]}" -gt 0 ] || return 1
    for level in "${levels[@]}"; do
        [[ "$level" =~ ^[1-9][0-9]{0,4}$ ]] && [ "$level" -le 65504 ] || return 1
        [ $((level % 4)) -eq 0 ] || return 1
        [ -z "${seen[$level]:-}" ] || return 1
        seen[$level]=1
    done
    LEVELS="${levels[*]}"
}

validate_levels "$LEVELS" || usage
is_positive_integer "$SETTLE" || usage
is_positive_integer "$SOAK" || usage
is_positive_integer "$READY_TIMEOUT" || usage
read -r -a requested_levels <<<"$LEVELS"
workload_configure balance "${requested_levels[@]}" || exit 2
if [ "${DDCS_PERF_SUITE_REPETITIONS+x}" = x ]; then
    fail "DDCS_PERF_SUITE_REPETITIONS는 지원하지 않습니다. 각 배치는 한 번씩만 측정합니다."
fi

if [ -n "${DDCS_PERF_SUITE_ID:-}" ]; then
    SUITE_ID="$DDCS_PERF_SUITE_ID"
else
    SUITE_ID="$(date -u '+%Y%m%dT%H%M%SZ')-perf-suite-$$"
fi
case "$SUITE_ID" in
'' | . | .. | *[!A-Za-z0-9._-]*)
    usage
    ;;
esac

command -v docker >/dev/null 2>&1 || fail "'docker' 명령을 찾지 못했습니다."
docker compose version >/dev/null 2>&1 || fail "docker compose v2가 필요합니다."
result_require_jq || exit 1

if docker ps --format '{{.Names}}' | grep -Fxq ddcs-controller; then
    fail "ddcs-controller가 이미 실행 중입니다. 기존 DDCS 스택을 먼저 종료하십시오."
fi

# 결과 파일이 작업 트리 변경 여부에 영향을 주기 전에 소스 상태를 기록한다.
SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
    SOURCE_DIRTY=true
else
    SOURCE_DIRTY=false
fi

# 결과 파일이 이미지에 들어가지 않도록 빌드부터 한다.
echo "공통 이미지 빌드 (balance·single 측정에 재사용)"
docker compose -f "$ROOT/docker/docker-compose.perf.yml" build controller fleet-zone-a
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent-fleet:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || fail "공통 Controller 이미지 ID를 읽지 못했습니다."
[ -n "$AGENT_IMAGE_ID" ] || fail "공통 Fleet 이미지 ID를 읽지 못했습니다."
EFFECTIVE_CONFIG_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ddcs-perf-suite-config.XXXXXX")"
python3 "$ROOT/scripts/performance/workload-config.py" prepare "$ROOT/config" "$EFFECTIVE_CONFIG_TMP" "${policy_args[@]}"
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$EFFECTIVE_CONFIG_TMP")" || exit 1
POLICY_SHA256="$(python3 "$ROOT/scripts/performance/workload-config.py" policy-hash "$EFFECTIVE_CONFIG_TMP")"
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1

OUTPUT_ROOT="$DDCS_RESULT_BUILD_DIR/performance"
mkdir -p "$OUTPUT_ROOT" || fail "성능 측정 결과의 상위 디렉터리를 만들지 못했습니다: $OUTPUT_ROOT"
SUITE_DIR="$OUTPUT_ROOT/$SUITE_ID"
[ ! -e "$SUITE_DIR" ] || fail "같은 실행 ID의 전체 배치 측정 결과가 이미 있습니다: $SUITE_DIR"
mkdir "$SUITE_DIR"
SUITE_DIR="$(cd "$SUITE_DIR" && pwd -P)"
cp -R "$EFFECTIVE_CONFIG_TMP" "$SUITE_DIR/config"
rm -rf -- "$EFFECTIVE_CONFIG_TMP"
EFFECTIVE_CONFIG_TMP=
SUITE_STARTED_UTC="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"

declare -a RUN_LAYOUTS RUN_IDS RUN_LEVELS RUN_SETTLE RUN_SOAK RUN_PREFLIGHT_WARNINGS

run_ramp() {
    local layout="$1" run_id="$2" run_dir manifest warnings

    echo
    echo "== ${run_id} (${layout}) =="
    env \
        DDCS_PERF_OUTPUT_ROOT="$SUITE_DIR" \
        DDCS_PERF_CONFIG_SOURCE="$SUITE_DIR/config" \
        DDCS_PERF_RUN_ID="$run_id" \
        DDCS_PERF_LEVELS="$LEVELS" \
        DDCS_PERF_SETTLE="$SETTLE" \
        DDCS_PERF_SOAK="$SOAK" \
        DDCS_PERF_READY_TIMEOUT="$READY_TIMEOUT" \
        DDCS_PERF_SKIP_BUILD=1 \
        DDCS_PERF_SOURCE_REVISION="$SOURCE_REVISION" \
        DDCS_PERF_SOURCE_DIRTY="$SOURCE_DIRTY" \
        "$ROOT/scripts/performance/measure-scalability.sh" "$layout"

    run_dir="$SUITE_DIR/$run_id"
    manifest="$run_dir/manifest.json"
    [ -f "$manifest" ] || fail "배치별 측정 기록(manifest.json)을 찾지 못했습니다: $manifest"
    jq -e \
        --arg build_key "$DDCS_RESULT_BUILD_KEY" \
        --arg revision "$SOURCE_REVISION" \
        --arg controller_image "$CONTROLLER_IMAGE_ID" \
        --arg agent_image "$AGENT_IMAGE_ID" \
        --arg runtime_config_sha256 "$RUNTIME_CONFIG_SHA256" \
        --arg policy_sha256 "$POLICY_SHA256" \
        --argjson fixed_fleet_size "${FIXED_FLEET_SIZE:-null}" \
        --argjson uniform_policy "$UNIFORM_POLICY" \
        --arg layout "$layout" \
        --arg levels "$LEVELS" \
        --argjson source_dirty "$SOURCE_DIRTY" \
        --argjson settle_seconds "$SETTLE" \
        --argjson readiness_timeout "$READY_TIMEOUT" \
        --argjson seconds "$SOAK" '
            .build_key == $build_key and
            .schema_version == 6 and
            .load_generator == "agent-fleet" and
            .level_lifecycle == "fresh_stack" and
            .source_revision == $revision and
            .source_dirty == $source_dirty and
            .source_identity_overridden == true and
            .controller_image_id == $controller_image and
            .agent_image_id == $agent_image and
            .runtime_config_sha256 == $runtime_config_sha256 and
            .policy_sha256 == $policy_sha256 and
            .fixed_agents_per_fleet == $fixed_fleet_size and
            .uniform_policy == ($uniform_policy == 1) and
            (.workloads | length) == ($levels | split(" ") | length) and
            all(.workloads[];
                .policy_sha256 == $policy_sha256 and
                .uniform_policy == ($uniform_policy == 1) and
                (if $fixed_fleet_size == null then true else
                   .agents_per_fleet == $fixed_fleet_size and
                   .fleet_count * .agents_per_fleet == .requested_agents end)) and
            .layout == $layout and
            .requested_levels == $levels and
            .readiness_timeout_seconds == $readiness_timeout and
            .settle_seconds_per_level == $settle_seconds and
            .measurement_seconds_per_level == $seconds and
            .preflight_skipped == false and
            .image_build_skipped == true and
            .failed_levels == 0
        ' "$manifest" >/dev/null || fail "배치별 측정 조건이 공통 조건과 다르거나 측정이 실패했습니다: $run_id"

    warnings="$(grep -c '^\[WARN\]' "$run_dir/preflight.txt" || true)"
    is_unsigned_integer "$warnings" || fail "측정 환경 검사 경고 수를 읽지 못했습니다: $run_id"
    RUN_LAYOUTS+=("$layout")
    RUN_IDS+=("$run_id")
    RUN_LEVELS+=("$LEVELS")
    RUN_SETTLE+=("$SETTLE")
    RUN_SOAK+=("$SOAK")
    RUN_PREFLIGHT_WARNINGS+=("$warnings")
}

for layout in balance single; do
    run_ramp "$layout" "$layout"
done

write_suite_manifest() {
    local ended_utc index manifest_path checksum bytes separator
    ended_utc="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
    {
        printf '%s\n' \
            '{' \
            '  "schema_name": "ddcs.perf_suite_evidence",' \
            '  "schema_version": 4,' \
            '  "load_generator": "agent-fleet",' \
            '  "level_lifecycle": "fresh_stack",' \
            "  \"suite_id\": \"${SUITE_ID}\", " \
            "  \"build_key\": \"${DDCS_RESULT_BUILD_KEY}\", " \
            "  \"started_utc\": \"${SUITE_STARTED_UTC}\", " \
            "  \"ended_utc\": \"${ended_utc}\", " \
            "  \"source_revision\": \"${SOURCE_REVISION}\", " \
            "  \"source_dirty\": ${SOURCE_DIRTY}," \
            "  \"controller_image_id\": \"${CONTROLLER_IMAGE_ID}\", " \
            "  \"agent_image_id\": \"${AGENT_IMAGE_ID}\", " \
            "  \"runtime_config_sha256\": \"${RUNTIME_CONFIG_SHA256}\", " \
            "  \"requested_levels\": \"${LEVELS}\", " \
            "  \"readiness_timeout_seconds\": ${READY_TIMEOUT}," \
            "  \"settle_seconds_per_level\": ${SETTLE}," \
            "  \"measurement_seconds_per_level\": ${SOAK}," \
            '  "runs": ['
        for index in "${!RUN_IDS[@]}"; do
            manifest_path="${RUN_IDS[$index]}/manifest.json"
            checksum="$(sha256sum "$SUITE_DIR/$manifest_path" | awk '{print $1}')"
            bytes="$(wc -c <"$SUITE_DIR/$manifest_path" | tr -d '[:space:]')"
            separator=,
            [ "$index" -eq $(( ${#RUN_IDS[@]} - 1 )) ] && separator=
            printf '    {"layout":"%s","run_id":"%s","requested_levels":"%s","settle_seconds_per_level":%s,"measurement_seconds_per_level":%s,"preflight_warning_count":%s,"manifest":"%s","sha256":"%s","bytes":%s}%s\n' \
                "${RUN_LAYOUTS[$index]}" "${RUN_IDS[$index]}" "${RUN_LEVELS[$index]}" \
                "${RUN_SETTLE[$index]}" "${RUN_SOAK[$index]}" "${RUN_PREFLIGHT_WARNINGS[$index]}" \
                "$manifest_path" "$checksum" "$bytes" "$separator"
        done
        printf '%s\n' '  ]' '}'
    } >"$SUITE_DIR/manifest.json"
    jq --argjson fixed_fleet_size "${FIXED_FLEET_SIZE:-null}" \
        --argjson uniform_policy "$UNIFORM_POLICY" --arg policy_sha256 "$POLICY_SHA256" \
        --slurpfile balance "$SUITE_DIR/balance/manifest.json" \
        --slurpfile single "$SUITE_DIR/single/manifest.json" \
        '. + {fixed_agents_per_fleet:$fixed_fleet_size,uniform_policy:($uniform_policy == 1),
              policy_sha256:$policy_sha256,effective_config:"config",
              workloads:{balance:$balance[0].workloads,single:$single[0].workloads}}' \
        "$SUITE_DIR/manifest.json" >"$SUITE_DIR/manifest.enriched.json"
    mv "$SUITE_DIR/manifest.enriched.json" "$SUITE_DIR/manifest.json"
    chmod 644 "$SUITE_DIR/manifest.json"
}

write_suite_manifest
echo
echo "전체 배치 측정 완료: $SUITE_DIR"
echo "빌드 식별자: $DDCS_RESULT_BUILD_KEY"
echo "공통 Controller 이미지: $CONTROLLER_IMAGE_ID"
echo "공통 Fleet 이미지:      $AGENT_IMAGE_ID"
echo "전체 manifest.json에서 배치별 측정 조건과 각 manifest.json의 SHA-256을 확인하십시오."
