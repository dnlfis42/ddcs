#!/usr/bin/env bash
#
# 같은 Release build로 balance와 single 성능 램프를 각각 반복 수집한다.
#
# perf-ramp.sh 하나는 한 layout의 여러 Agent 수를 한 번 측정한다. 이 wrapper는 이미지를 한 번만
# 빌드하고, child가 같은 source·image·config·build-key를 실제로 사용했는지 manifest로 확인한다.
#
# 사용법: scripts/perf-suite.sh
#
#   DDCS_PERF_SUITE_LEVELS="100 200 400"  balance/single 모두에 적용할 Agent 수 단계(4의 배수)
#   DDCS_PERF_SUITE_SOAK=30                각 레벨의 측정 창(초)
#   DDCS_PERF_SUITE_REPETITIONS=3          layout별 반복 수
#   DDCS_PERF_SUITE_ID=...                 내부/재현용 suite ID override; 기본은 UTC-perf-suite-PID

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/result-lib.sh"

LEVELS="${DDCS_PERF_SUITE_LEVELS:-100 200 400}"
SOAK="${DDCS_PERF_SUITE_SOAK:-30}"
REPETITIONS="${DDCS_PERF_SUITE_REPETITIONS:-3}"

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
    is_unsigned_integer "$1" && [ "$1" -gt 0 ]
}

validate_levels() { # 공백으로 구분한 4의 배수 양의 정수 목록
    local levels="$1" level
    [ -n "$levels" ] || return 1
    for level in $levels; do
        is_positive_integer "$level" || return 1
        [ $((level % 4)) -eq 0 ] || return 1
    done
}

validate_levels "$LEVELS" || usage
is_positive_integer "$SOAK" || usage
is_positive_integer "$REPETITIONS" || usage

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

# 결과 directory를 만들기 전에 source identity를 고정한다. child에도 이 값을 넘겨 local 결과가
# clean source 판정을 dirty로 오인하지 않게 한다.
SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
    SOURCE_DIRTY=true
else
    SOURCE_DIRTY=false
fi

# Dockerfile의 COPY . 입력에 새 결과가 섞이지 않도록, output은 공통 image build 후에만 만든다.
echo "공통 이미지 빌드 (suite 전체에 재사용)"
docker compose -f "$ROOT/docker/docker-compose.scale.yml" build
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || fail "공통 Controller image ID를 읽지 못했습니다."
[ -n "$AGENT_IMAGE_ID" ] || fail "공통 Agent image ID를 읽지 못했습니다."
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$ROOT/config")" || exit 1
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1

OUTPUT_ROOT="$DDCS_RESULT_BUILD_DIR/performance"
mkdir -p "$OUTPUT_ROOT" || fail "성능 결과 루트를 만들지 못했습니다: $OUTPUT_ROOT"
SUITE_DIR="$OUTPUT_ROOT/$SUITE_ID"
[ ! -e "$SUITE_DIR" ] || fail "기존 suite 결과를 덮어쓰지 않습니다: $SUITE_DIR"
mkdir "$SUITE_DIR"
SUITE_DIR="$(cd "$SUITE_DIR" && pwd -P)"
SUITE_STARTED_UTC="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"

declare -a RUN_LAYOUTS RUN_IDS RUN_LEVELS RUN_SOAK RUN_PREFLIGHT_WARNINGS

run_ramp() { # layout run-id
    local layout="$1" run_id="$2" run_dir manifest warnings

    echo
    echo "== ${run_id} (${layout}) =="
    env \
        DDCS_PERF_OUTPUT_ROOT="$SUITE_DIR" \
        DDCS_PERF_RUN_ID="$run_id" \
        DDCS_PERF_LEVELS="$LEVELS" \
        DDCS_PERF_SOAK="$SOAK" \
        DDCS_PERF_SKIP_BUILD=1 \
        DDCS_PERF_SOURCE_REVISION="$SOURCE_REVISION" \
        DDCS_PERF_SOURCE_DIRTY="$SOURCE_DIRTY" \
        "$ROOT/scripts/perf-ramp.sh" "$layout"

    run_dir="$SUITE_DIR/$run_id"
    manifest="$run_dir/manifest.json"
    [ -f "$manifest" ] || fail "child manifest를 찾지 못했습니다: $manifest"
    jq -e \
        --arg build_key "$DDCS_RESULT_BUILD_KEY" \
        --arg revision "$SOURCE_REVISION" \
        --arg controller_image "$CONTROLLER_IMAGE_ID" \
        --arg agent_image "$AGENT_IMAGE_ID" \
        --arg runtime_config_sha256 "$RUNTIME_CONFIG_SHA256" \
        --arg layout "$layout" \
        --arg levels "$LEVELS" \
        --argjson source_dirty "$SOURCE_DIRTY" \
        --argjson seconds "$SOAK" '
            .build_key == $build_key and
            .source_revision == $revision and
            .source_dirty == $source_dirty and
            .source_identity_overridden == true and
            .controller_image_id == $controller_image and
            .agent_image_id == $agent_image and
            .runtime_config_sha256 == $runtime_config_sha256 and
            .layout == $layout and
            .requested_levels == $levels and
            .measurement_seconds_per_level == $seconds and
            .preflight_skipped == false and
            .image_build_skipped == true and
            .failed_levels == 0
        ' "$manifest" >/dev/null || fail "child run 조건이 suite 공통 조건과 다릅니다: $run_id"

    warnings="$(grep -c '^\[WARN\]' "$run_dir/preflight.txt" || true)"
    is_unsigned_integer "$warnings" || fail "preflight warning 수를 읽지 못했습니다: $run_id"
    RUN_LAYOUTS+=("$layout")
    RUN_IDS+=("$run_id")
    RUN_LEVELS+=("$LEVELS")
    RUN_SOAK+=("$SOAK")
    RUN_PREFLIGHT_WARNINGS+=("$warnings")
}

for layout in balance single; do
    for repetition in $(seq 1 "$REPETITIONS"); do
        printf -v run_suffix '%02d' "$repetition"
        run_ramp "$layout" "${layout}-${run_suffix}"
    done
done

write_suite_manifest() {
    local ended_utc index manifest_path checksum bytes separator
    ended_utc="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
    {
        printf '%s\n' \
            '{' \
            '  "schema_name": "ddcs.perf_suite_evidence",' \
            '  "schema_version": 2,' \
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
            "  \"measurement_seconds_per_level\": ${SOAK}," \
            "  \"repetitions_per_layout\": ${REPETITIONS}," \
            '  "runs": ['
        for index in "${!RUN_IDS[@]}"; do
            manifest_path="${RUN_IDS[$index]}/manifest.json"
            checksum="$(sha256sum "$SUITE_DIR/$manifest_path" | awk '{print $1}')"
            bytes="$(wc -c <"$SUITE_DIR/$manifest_path" | tr -d '[:space:]')"
            separator=,
            [ "$index" -eq $(( ${#RUN_IDS[@]} - 1 )) ] && separator=
            printf '    {"layout":"%s","run_id":"%s","requested_levels":"%s","measurement_seconds_per_level":%s,"preflight_warning_count":%s,"manifest":"%s","sha256":"%s","bytes":%s}%s\n' \
                "${RUN_LAYOUTS[$index]}" "${RUN_IDS[$index]}" "${RUN_LEVELS[$index]}" \
                "${RUN_SOAK[$index]}" "${RUN_PREFLIGHT_WARNINGS[$index]}" \
                "$manifest_path" "$checksum" "$bytes" "$separator"
        done
        printf '%s\n' '  ]' '}'
    } >"$SUITE_DIR/manifest.json"
    chmod 644 "$SUITE_DIR/manifest.json"
}

write_suite_manifest
echo
echo "완료: $SUITE_DIR"
echo "build key: $DDCS_RESULT_BUILD_KEY"
echo "공통 Controller image: $CONTROLLER_IMAGE_ID"
echo "공통 Agent image:      $AGENT_IMAGE_ID"
echo "child manifest 조건과 SHA-256을 suite manifest에서 확인하십시오."
