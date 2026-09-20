#!/usr/bin/env bash

# Agent 수를 늘려가며 성능을 측정한다. 각 단계는 새 Controller와 Fleet으로 실행한다.
# 사용법: scripts/performance/measure-scalability.sh <balance|single>
# balance: 4개 Group에 균등 배치. single: zone_a에 모두 배치
# 측정 성공과 성능 기준 통과는 별개다. 판정은 assessment.json에서 확인한다.

# shellcheck source=scripts/lib/scenario.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/scenario.sh"
# shellcheck source=scripts/lib/results.sh
source "$ROOT/scripts/lib/results.sh"
# shellcheck source=scripts/lib/workload.sh
source "$ROOT/scripts/lib/workload.sh"

# shellcheck disable=SC2034
COMPOSE=docker-compose.perf.yml
MODE="${1:-}"
LEVELS="${DDCS_PERF_LEVELS:-4000 12000 20000}"
SETTLE="${DDCS_PERF_SETTLE:-30}"
SOAK="${DDCS_PERF_SOAK:-120}"
READY_TIMEOUT="${DDCS_PERF_READY_TIMEOUT:-90}"
SAMPLE_INTERVAL="${DDCS_PERF_SAMPLE_INTERVAL:-5}"
EFFECTIVE_CONFIG_TMP=
OUTPUT_ROOT_OVERRIDE="${DDCS_PERF_OUTPUT_ROOT:-}"
SKIP_BUILD="${DDCS_PERF_SKIP_BUILD:-0}"
SOURCE_REVISION_OVERRIDE="${DDCS_PERF_SOURCE_REVISION:-}"
SOURCE_DIRTY_OVERRIDE="${DDCS_PERF_SOURCE_DIRTY:-}"

usage() {
    echo "사용법: $0 <balance|single>" >&2
    exit 2
}

[ "$#" -eq 1 ] || usage
case "$MODE" in
balance | single)
    ;;
*)
    usage
    ;;
esac

for duration in "$SETTLE" "$SOAK" "$READY_TIMEOUT" "$SAMPLE_INTERVAL"; do
    if ! [[ "$duration" =~ ^[1-9][0-9]{0,8}$ ]]; then
        echo "오류: 안정화·측정·준비 대기 시간은 1..999999999 정수여야 합니다." >&2
        exit 2
    fi
done
read -r -a REQUESTED_LEVELS <<< "${LEVELS//$'\n'/ }"
[ "${#REQUESTED_LEVELS[@]}" -gt 0 ] || usage
LEVELS="${REQUESTED_LEVELS[*]}"
declare -A seen_levels
for t in "${REQUESTED_LEVELS[@]}"; do
    if ! [[ "$t" =~ ^[1-9][0-9]{0,4}$ ]] || [ "$t" -gt 65504 ]; then
        echo "오류: Agent 수는 1..65504 정수여야 합니다(Compose nofile 65536)." >&2
        exit 2
    fi
    if [ "$MODE" = balance ] && [ $((t % 4)) -ne 0 ]; then
        echo "오류: balance 배치의 Agent 수은 4의 배수여야 합니다: $t" >&2
        exit 2
    fi
    [ -z "${seen_levels[$t]:-}" ] || {
        echo "오류: 중복된 Agent 수입니다: $t" >&2
        exit 2
    }
    seen_levels[$t]=1
done
workload_configure "$MODE" "${REQUESTED_LEVELS[@]}" || exit 2
case "$SKIP_BUILD" in
0 | 1)
    ;;
*)
    echo "오류: DDCS_PERF_SKIP_BUILD는 0 또는 1이어야 합니다: $SKIP_BUILD" >&2
    exit 2 ;;
esac
if [ -n "$SOURCE_REVISION_OVERRIDE" ] || [ -n "$SOURCE_DIRTY_OVERRIDE" ]; then
    if [ -z "$SOURCE_REVISION_OVERRIDE" ] || [ -z "$SOURCE_DIRTY_OVERRIDE" ]; then
        echo "오류: DDCS_PERF_SOURCE_REVISION과 DDCS_PERF_SOURCE_DIRTY는 함께 지정해야 합니다." >&2
        exit 2
    fi
    case "$SOURCE_DIRTY_OVERRIDE" in
    true | false)
        ;;
    *)
        echo "오류: DDCS_PERF_SOURCE_DIRTY는 true 또는 false여야 합니다: $SOURCE_DIRTY_OVERRIDE" >&2
        exit 2 ;;
    esac
    SOURCE_IDENTITY_OVERRIDDEN=true
else
    SOURCE_IDENTITY_OVERRIDDEN=false
fi

if [ -n "${DDCS_PERF_RUN_ID:-}" ]; then
    RUN_ID="$DDCS_PERF_RUN_ID"
else
    RUN_ID="$(date -u '+%Y%m%dT%H%M%SZ')-perf-${MODE}-$$"
fi
case "$RUN_ID" in
'' | . | .. | *[!A-Za-z0-9._-]*)
    echo "오류: DDCS_PERF_RUN_ID는 비어 있지 않은 [A-Za-z0-9._-] 문자열이어야 합니다." >&2
    exit 2 ;;
esac

# 결과 파일이 작업 트리 변경 여부에 영향을 주기 전에 소스 상태를 기록한다.
if [ "$SOURCE_IDENTITY_OVERRIDDEN" = true ]; then
    SOURCE_REVISION="$SOURCE_REVISION_OVERRIDE"
    SOURCE_DIRTY="$SOURCE_DIRTY_OVERRIDE"
else
    SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
    if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
        SOURCE_DIRTY=true
    else
        SOURCE_DIRTY=false
    fi
fi

cleanup_preflight_tmp() {
    [ -z "${PREFLIGHT_TMP:-}" ] || rm -f -- "$PREFLIGHT_TMP"
    [ -z "${EFFECTIVE_CONFIG_TMP:-}" ] || rm -rf -- "$EFFECTIVE_CONFIG_TMP"
}
PREFLIGHT_TMP="$(mktemp "${TMPDIR:-/tmp}/ddcs-perf-preflight.XXXXXX")" || {
    echo "오류: 측정 환경 검사 결과를 저장할 임시 파일을 만들지 못했습니다." >&2
    exit 1
}
trap cleanup_preflight_tmp EXIT

preflight || exit 1
result_require_jq || exit 1
command -v python3 >/dev/null || { echo "오류: 측정 결과 평가에 python3이 필요합니다." >&2; exit 1; }
python3 "$ROOT/scripts/performance/evaluate-results.py" --validate-config || exit 2

if [ "${DDCS_PERF_SKIP_PREFLIGHT:-0}" != "1" ]; then
    PREFLIGHT_SKIPPED=false
    "$ROOT/scripts/measurement/verify-environment.sh" fleet >"$PREFLIGHT_TMP" 2>&1 || {
        sed -n '1,240p' "$PREFLIGHT_TMP"
        echo "오류: 측정 환경 검사에 실패했습니다. 위 검사 결과와 조치 안내를 확인하십시오. 진단 목적으로 생략하려면 DDCS_PERF_SKIP_PREFLIGHT=1을 지정하십시오." >&2
        exit 1
    }
    sed -n '1,240p' "$PREFLIGHT_TMP"
else
    PREFLIGHT_SKIPPED=true
    printf '%s\n' 'DDCS_PERF_SKIP_PREFLIGHT=1로 측정 환경 검사를 생략했습니다. 진단용 결과입니다.' >"$PREFLIGHT_TMP"
fi

if [ "$MODE" = single ]; then
    narrate "성능 램프(single): 레벨 = [$LEVELS] (총 Agent 수, 전부 zone_a), 레벨당 연결 안정화 ${SETTLE}s + ${SOAK}s 측정"
else
    narrate "성능 램프(balance): 레벨 = [$LEVELS] (총 Agent 수, zone 4개 균등 분배), 레벨당 연결 안정화 ${SETTLE}s + ${SOAK}s 측정"
fi
if [ "$SKIP_BUILD" = 1 ]; then
    narrate "이미지 재사용"
    docker image inspect ddcs-controller:dev >/dev/null 2>&1 || {
        echo "오류: 재사용할 Controller 이미지를 찾지 못했습니다: ddcs-controller:dev" >&2
        exit 1
    }
    docker image inspect ddcs-agent-fleet:dev >/dev/null 2>&1 || {
        echo "오류: 재사용할 Fleet 이미지를 찾지 못했습니다: ddcs-agent-fleet:dev" >&2
        exit 1
    }
    IMAGE_BUILD_SKIPPED=true
else
    ensure_images || exit 1
    IMAGE_BUILD_SKIPPED=false
fi
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || {
    echo "오류: Controller 이미지 ID를 읽지 못했습니다." >&2
    exit 1
}
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent-fleet:dev 2>/dev/null || true)"
[ -n "$AGENT_IMAGE_ID" ] || {
    echo "오류: Fleet 이미지 ID를 읽지 못했습니다." >&2
    exit 1
}
EFFECTIVE_CONFIG_TMP="$(mktemp -d "${TMPDIR:-/tmp}/ddcs-perf-config.XXXXXX")" || exit 1
python3 "$ROOT/scripts/performance/workload-config.py" prepare "${DDCS_PERF_CONFIG_SOURCE:-$ROOT/config}" "$EFFECTIVE_CONFIG_TMP" "${policy_args[@]}" || exit 1
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$EFFECTIVE_CONFIG_TMP")" || exit 1
POLICY_SHA256="$(python3 "$ROOT/scripts/performance/workload-config.py" policy-hash "$EFFECTIVE_CONFIG_TMP")" || exit 1
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1

if [ -n "$OUTPUT_ROOT_OVERRIDE" ]; then
    OUTPUT_ROOT="$OUTPUT_ROOT_OVERRIDE"
else
    OUTPUT_ROOT="$DDCS_RESULT_BUILD_DIR/performance"
fi
mkdir -p "$OUTPUT_ROOT" || {
    echo "오류: 성능 측정 결과의 상위 디렉터리를 만들지 못했습니다: $OUTPUT_ROOT" >&2
    exit 1
}
RUN_DIR="$OUTPUT_ROOT/$RUN_ID"
[ ! -e "$RUN_DIR" ] || {
    echo "오류: 같은 실행 ID의 성능 측정 결과가 이미 있습니다: $RUN_DIR" >&2
    exit 1
}
mkdir "$RUN_DIR" || {
    echo "오류: 성능 측정 실행 디렉터리를 만들지 못했습니다: $RUN_DIR" >&2
    exit 1
}
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
cp -R "$EFFECTIVE_CONFIG_TMP" "$RUN_DIR/config" || exit 1
rm -rf -- "$EFFECTIVE_CONFIG_TMP"
EFFECTIVE_CONFIG_TMP=
PREFLIGHT_ARTIFACT="$RUN_DIR/preflight.txt"
cp "$PREFLIGHT_TMP" "$PREFLIGHT_ARTIFACT" || {
    echo "오류: 측정 환경 검사 결과를 보관하지 못했습니다: $PREFLIGHT_ARTIFACT" >&2
    exit 1
}
chmod 644 "$PREFLIGHT_ARTIFACT"
rm -f -- "$PREFLIGHT_TMP"
PREFLIGHT_TMP=
RUN_STARTED_UTC="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
arm_cleanup

snapshot() { curl -sf --max-time 5 "$METRICS_URL"; }

is_unsigned_integer() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

is_positive_integer() {
    is_unsigned_integer "$1" && [ "$1" -gt 0 ]
}

capture_stamp() {
    date -u '+%Y-%m-%dT%H:%M:%S.%NZ %s%N'
}

set_stamp() {
    local stamp
    stamp="$(capture_stamp)"
    printf -v "$1" '%s' "${stamp%% *}"
    printf -v "$2" '%s' "${stamp##* }"
}

controller_cpu_snapshot() {
    local pid jiffies
    pid="$(docker inspect --format '{{.State.Pid}}' "$CTRL" 2>/dev/null || true)"
    if ! is_positive_integer "$pid" || [ ! -r "/proc/$pid/stat" ]; then
        return 1
    fi
    jiffies="$(awk '{printf "%.0f", $14 + $15}' "/proc/$pid/stat" 2>/dev/null || true)"
    is_unsigned_integer "$jiffies" || return 1
    printf '%s %s\n' "$pid" "$jiffies"
}

set_controller_cpu_snapshot() {
    local pid_variable="$1" jiffies_variable="$2" snapshot pid jiffies
    if snapshot="$(controller_cpu_snapshot)"; then
        read -r pid jiffies <<<"$snapshot"
        printf -v "$pid_variable" '%s' "$pid"
        printf -v "$jiffies_variable" '%s' "$jiffies"
    else
        printf -v "$pid_variable" '%s' null
        printf -v "$jiffies_variable" '%s' null
    fi
}

snap_int() {
    printf '%s\n' "$1" | awk -v m="$2" '$1 == m {print $2; exit}'
}

snap_seconds_us() {
    printf '%s\n' "$1" | awk -v m="$2" '
        function to_us(value, parts, fraction) {
            if (value !~ /^[0-9]+(\.[0-9]+)?$/) {
                exit 2
            }
            split(value, parts, ".")
            fraction = (length(parts) > 1 ? parts[2] : "") "000000"
            if (length(parts) > 1 && length(parts[2]) > 6) {
                exit 2
            }
            printf "%.0f", parts[1] * 1000000 + substr(fraction, 1, 6)
        }
        $1 == m { to_us($2); exit }
    '
}

snap_reason_int() {
    printf '%s\n' "$1" |
        awk -v m="$2" -v r="$3" '$1 == m "{reason=\"" r "\"}" {print $2; exit}'
}

format_us_as_ms() {
    awk -v us="$1" 'BEGIN { printf "%.3f", us / 1000 }'
}

format_ns_as_seconds() {
    awk -v ns="$1" 'BEGIN { printf "%.6f", ns / 1000000000 }'
}

format_per_second() {
    awk -v count="$1" -v elapsed_ns="$2" 'BEGIN {
        if (elapsed_ns <= 0) {
            exit 1
        }
        printf "%.1f", count * 1000000000 / elapsed_ns
    }'
}

controller_cpu_percent() {
    local start_pid="$1" start_jiffies="$2" start_ns="$3"
    local end_pid="$4" end_jiffies="$5" end_ns="$6" clock_ticks="$7"
    local delta_jiffies elapsed_ns
    if ! is_positive_integer "$start_pid" || ! is_unsigned_integer "$start_jiffies" ||
        ! is_positive_integer "$end_pid" || ! is_unsigned_integer "$end_jiffies" ||
        ! is_unsigned_integer "$start_ns" || ! is_unsigned_integer "$end_ns" ||
        ! is_positive_integer "$clock_ticks" || [ "$start_pid" != "$end_pid" ]; then
        printf 'N/A'
        return 0
    fi
    delta_jiffies=$((end_jiffies - start_jiffies))
    elapsed_ns=$((end_ns - start_ns))
    if [ "$delta_jiffies" -lt 0 ] || [ "$elapsed_ns" -le 0 ]; then
        printf 'N/A'
        return 0
    fi
    awk -v jiffies="$delta_jiffies" -v ticks="$clock_ticks" -v elapsed_ns="$elapsed_ns" 'BEGIN {
        printf "%.3f", 100 * jiffies * 1000000000 / ticks / elapsed_ns
    }'
}

write_level_measurement() {
    local output="$1" start_captured=false end_captured=false
    [ -n "$snap0" ] && start_captured=true
    [ -n "$snap1" ] && end_captured=true
    {
        printf '%s\n' \
            '{' \
            "  \"requested_agents\": ${want}," \
            "  \"measurement_window\": {\"started_utc\": \"${SNAP0_STARTED_UTC}\", \"started_unix_ns\": ${SNAP0_STARTED_UNIX_NS}, \"ended_utc\": \"${SNAP1_STARTED_UTC}\", \"ended_unix_ns\": ${SNAP1_STARTED_UNIX_NS}}," \
            "  \"metrics_start\": {\"path\": \"metrics-start.prom\", \"captured\": ${start_captured}, \"started_utc\": \"${SNAP0_STARTED_UTC}\", \"started_unix_ns\": ${SNAP0_STARTED_UNIX_NS}, \"ended_utc\": \"${SNAP0_ENDED_UTC}\", \"ended_unix_ns\": ${SNAP0_ENDED_UNIX_NS}, \"controller_cpu\": {\"pid\": ${SNAP0_CONTROLLER_PID}, \"jiffies\": ${SNAP0_CPU_JIFFIES}, \"clock_ticks_per_second\": ${CONTROLLER_CLOCK_TICKS_PER_SECOND}}}," \
            "  \"metrics_end\": {\"path\": \"metrics-end.prom\", \"captured\": ${end_captured}, \"started_utc\": \"${SNAP1_STARTED_UTC}\", \"started_unix_ns\": ${SNAP1_STARTED_UNIX_NS}, \"ended_utc\": \"${SNAP1_ENDED_UTC}\", \"ended_unix_ns\": ${SNAP1_ENDED_UNIX_NS}, \"controller_cpu\": {\"pid\": ${SNAP1_CONTROLLER_PID}, \"jiffies\": ${SNAP1_CPU_JIFFIES}, \"clock_ticks_per_second\": ${CONTROLLER_CLOCK_TICKS_PER_SECOND}}}" \
            '}'
    } >"$output" || return 1
    chmod 644 "$output"
}

CONTROLLER_CLOCK_TICKS_PER_SECOND="$(getconf CLK_TCK 2>/dev/null || true)"
if ! is_positive_integer "$CONTROLLER_CLOCK_TICKS_PER_SECOND"; then
    CONTROLLER_CLOCK_TICKS_PER_SECOND=null
fi

write_manifest() {
    local ended_utc artifact checksum bytes index separator
    local -a artifacts
    ended_utc="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
    mapfile -t artifacts < <(
        cd "$RUN_DIR" || exit 1
        find . -type f ! -name manifest.json -printf '%P\n' | sort
    )
    {
        printf '%s\n' \
            '{' \
            '  "schema_name": "ddcs.perf_ramp_evidence",' \
            '  "schema_version": 6,' \
            '  "load_generator": "agent-fleet",' \
            '  "level_lifecycle": "fresh_stack",' \
            "  \"run_id\": \"${RUN_ID}\", " \
            "  \"build_key\": \"${DDCS_RESULT_BUILD_KEY}\", " \
            "  \"started_utc\": \"${RUN_STARTED_UTC}\", " \
            "  \"ended_utc\": \"${ended_utc}\", " \
            "  \"source_revision\": \"${SOURCE_REVISION}\", " \
            "  \"source_dirty\": ${SOURCE_DIRTY}," \
            "  \"source_identity_overridden\": ${SOURCE_IDENTITY_OVERRIDDEN}," \
            "  \"controller_image_id\": \"${CONTROLLER_IMAGE_ID}\", " \
            "  \"agent_image_id\": \"${AGENT_IMAGE_ID}\", " \
            "  \"runtime_config_sha256\": \"${RUNTIME_CONFIG_SHA256}\", " \
            "  \"layout\": \"${MODE}\", " \
            "  \"requested_levels\": \"${LEVELS}\", " \
            "  \"readiness_timeout_seconds\": ${READY_TIMEOUT}," \
            "  \"settle_seconds_per_level\": ${SETTLE}," \
            "  \"measurement_seconds_per_level\": ${SOAK}," \
            "  \"preflight_skipped\": ${PREFLIGHT_SKIPPED}," \
            "  \"image_build_skipped\": ${IMAGE_BUILD_SKIPPED}," \
            "  \"failed_levels\": ${bad_levels}," \
            '  "artifacts": ['
        for index in "${!artifacts[@]}"; do
            artifact="${artifacts[$index]}"
            checksum="$(sha256sum "$RUN_DIR/$artifact" | awk '{print $1}')" || return 1
            bytes="$(wc -c <"$RUN_DIR/$artifact" | tr -d '[:space:]')" || return 1
            separator=,
            [ "$index" -eq $((${#artifacts[@]} - 1)) ] && separator=
            printf '    {"name":"%s","sha256":"%s","bytes":%s}%s\n' \
                "$artifact" "$checksum" "$bytes" "$separator"
        done
        printf '%s\n' '  ]' '}'
    } >"$RUN_DIR/manifest.json" || return 1
    jq --argjson fixed_fleet_size "${FIXED_FLEET_SIZE:-null}" \
        --argjson uniform_policy "$UNIFORM_POLICY" --arg policy_sha256 "$POLICY_SHA256" \
        --slurpfile workloads <(cat "$RUN_DIR"/*/workload.json) \
        '. + {fixed_agents_per_fleet:$fixed_fleet_size,uniform_policy:($uniform_policy == 1),
              policy_sha256:$policy_sha256,effective_config:"config",workloads:($workloads | sort_by(.requested_agents))}' \
        "$RUN_DIR/manifest.json" >"$RUN_DIR/manifest.enriched.json" || return 1
    mv "$RUN_DIR/manifest.enriched.json" "$RUN_DIR/manifest.json" || return 1
    chmod 644 "$RUN_DIR/manifest.json"
}

level_fail() {
    printf '%s\n' "$*" >&2
    printf '%s\n' "$*" >>"$LEVEL_DIR/failure.txt"
}

wait_for_workload() {
    local current deadline=$((SECONDS + READY_TIMEOUT))
    while [ "$SECONDS" -lt "$deadline" ]; do
        current="$(snapshot)" || current=
        if [ -n "$current" ]; then
            printf '%s\n' "$current" >"$LEVEL_DIR/readiness.prom"
            if workload_ready "$current" "$want" "$MODE"; then return 0; fi
        fi
        sleep 1
    done
    level_fail "Agent ${want}대: 등록·Status 보고 목표 미달 또는 메트릭 수집 실패 (제한 ${READY_TIMEOUT}s)."
    return 1
}

measure_level() {
    if ! compose up -d "${up_services[@]}" >/dev/null; then
        level_fail "Agent ${want}대: 스택 기동 실패."
        return 1
    fi
    CTRL="$(compose ps -q controller)" || return 1
    [ -n "$CTRL" ] || { level_fail "Controller ID를 찾지 못했습니다."; return 1; }
    wait_for_workload || return 1
    sleep "$SETTLE"

    set_stamp SNAP0_STARTED_UTC SNAP0_STARTED_UNIX_NS
    set_controller_cpu_snapshot SNAP0_CONTROLLER_PID SNAP0_CPU_JIFFIES
    snap0="$(snapshot)" || snap0=
    set_stamp SNAP0_ENDED_UTC SNAP0_ENDED_UNIX_NS
    printf '%s\n' "$snap0" >"$LEVEL_DIR/metrics-start.prom"
    if ! workload_ready "$snap0" "$want" "$MODE"; then
        level_fail "Agent ${want}대: 안정화 후 등록·Status 보고 수가 목표와 다릅니다. 측정하지 않습니다."
        return 1
    fi

    # 수집에 걸린 시간도 측정 시간에 포함한다.
    local remaining="$SOAK" deadline=$((SECONDS + SOAK)) sample_index=0 delay sample_name sample_started sample_ended ids
    local -a sample_containers
    mkdir "$LEVEL_DIR/samples" || return 1
    ids="$(compose ps -q)" || return 1
    [ -n "$ids" ] || return 1
    mapfile -t sample_containers <<<"$ids"
    while [ "$remaining" -gt 0 ]; do
        delay="$SAMPLE_INTERVAL"
        [ "$remaining" -ge "$delay" ] || delay="$remaining"
        sleep "$delay"
        remaining=$((deadline - SECONDS))
        [ "$remaining" -gt 0 ] || break
        sample_index=$((sample_index + 1))
        printf -v sample_name '%06d' "$sample_index"
        sample_started="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
        if ! snapshot >"$LEVEL_DIR/samples/$sample_name.prom" ||
            ! docker stats --no-stream --format '{{json .}}' "${sample_containers[@]}" >"$LEVEL_DIR/samples/$sample_name.docker-stats.jsonl"; then
            level_fail "Agent ${want}대: 측정 구간 내 메트릭 또는 자원 관측 실패."
            return 1
        fi
        sample_ended="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
        printf '{"started_utc":"%s","ended_utc":"%s"}\n' "$sample_started" "$sample_ended" >"$LEVEL_DIR/samples/$sample_name.json"
        remaining=$((deadline - SECONDS))
    done
    set_stamp SNAP1_STARTED_UTC SNAP1_STARTED_UNIX_NS
    set_controller_cpu_snapshot SNAP1_CONTROLLER_PID SNAP1_CPU_JIFFIES
    snap1="$(snapshot)" || snap1=
    set_stamp SNAP1_ENDED_UTC SNAP1_ENDED_UNIX_NS
    printf '%s\n' "$snap1" >"$LEVEL_DIR/metrics-end.prom"
    write_level_measurement "$LEVEL_DIR/measurement.json" || {
        level_fail "Agent ${want}대: 측정 메타데이터 기록 실패."
        return 1
    }
    if ! workload_ready "$snap1" "$want" "$MODE"; then
        level_fail "Agent ${want}대: 측정 종료 시 등록·Status 보고 수가 목표와 다릅니다."
        return 1
    fi
    if [ "$SNAP0_CONTROLLER_PID" != null ] && [ "$SNAP1_CONTROLLER_PID" != null ] &&
        [ "$SNAP0_CONTROLLER_PID" != "$SNAP1_CONTROLLER_PID" ]; then
        level_fail "Agent ${want}대: 측정 중 Controller 프로세스가 바뀌었습니다."
        return 1
    fi

    sum0=$(snap_seconds_us "$snap0" ddcs_tick_duration_seconds_total)
    tk0=$(snap_int "$snap0" ddcs_ticks_total)
    rsum0=$(snap_seconds_us "$snap0" ddcs_command_rtt_seconds_sum)
    rcnt0=$(snap_int "$snap0" ddcs_command_rtt_seconds_count)
    recv0=$(snap_int "$snap0" ddcs_messages_received_total)
    liveness0=$(snap_reason_int "$snap0" ddcs_connections_closed_total liveness_expired)
    sum1=$(snap_seconds_us "$snap1" ddcs_tick_duration_seconds_total)
    tk1=$(snap_int "$snap1" ddcs_ticks_total)
    rsum1=$(snap_seconds_us "$snap1" ddcs_command_rtt_seconds_sum)
    rcnt1=$(snap_int "$snap1" ddcs_command_rtt_seconds_count)
    recv1=$(snap_int "$snap1" ddcs_messages_received_total)
    liveness1=$(snap_reason_int "$snap1" ddcs_connections_closed_total liveness_expired)
    smax=$(snap_seconds_us "$snap1" ddcs_tick_duration_seconds_max)
    conns=$(snap_int "$snap1" ddcs_connections)
    pending=$(snap_int "$snap1" ddcs_commands_pending)

    if ! [[ "$sum0 $tk0 $rsum0 $rcnt0 $recv0 $liveness0 $sum1 $tk1 $rsum1 $rcnt1 $recv1 $liveness1 $smax $conns $pending" =~ ^[0-9]+(\ [0-9]+)*$ ]]; then
        level_fail "Agent ${total}대: 필수 메트릭 값을 읽지 못했습니다. 실행 이미지와 메트릭 엔드포인트를 확인하십시오."
        return 1
    fi

    dtk=$((tk1 - tk0))
    if [ "$dtk" -le 0 ] || [ $((sum1 - sum0)) -lt 0 ] || [ $((rsum1 - rsum0)) -lt 0 ] ||
        [ $((rcnt1 - rcnt0)) -lt 0 ] || [ $((recv1 - recv0)) -lt 0 ] ||
        [ $((liveness1 - liveness0)) -lt 0 ]; then
        level_fail "Agent ${total}대: 측정 구간에 카운터 감소 또는 tick 미발생이 확인되어 이 단계의 요약 행을 출력하지 않습니다."
        return 1
    fi

    elapsed_ns=$((SNAP1_STARTED_UNIX_NS - SNAP0_STARTED_UNIX_NS))
    if [ "$elapsed_ns" -le 0 ]; then
        level_fail "Agent ${total}대: 측정 종료 시간이 시작 시간보다 이르므로 이 단계의 요약 행을 출력하지 않습니다."
        return 1
    fi
    avg=$(((sum1 - sum0) / dtk))
    drc=$((rcnt1 - rcnt0))
    rtt=N/A
    [ "$drc" -gt 0 ] && rtt=$(format_us_as_ms "$(((rsum1 - rsum0) / drc))")
    window_seconds=$(format_ns_as_seconds "$elapsed_ns")
    inps=$(format_per_second "$((recv1 - recv0))" "$elapsed_ns")
    cpu=$(controller_cpu_percent \
        "$SNAP0_CONTROLLER_PID" "$SNAP0_CPU_JIFFIES" "$SNAP0_STARTED_UNIX_NS" \
        "$SNAP1_CONTROLLER_PID" "$SNAP1_CPU_JIFFIES" "$SNAP1_STARTED_UNIX_NS" \
        "$CONTROLLER_CLOCK_TICKS_PER_SECOND")

    python3 "$ROOT/scripts/performance/evaluate-results.py" "$LEVEL_DIR" --layout "$MODE" || {
        level_fail "Agent ${want}대: 측정 데이터 또는 부하 검증 실패. assessment.json과 오류 출력을 확인하십시오."
        return 1
    }
    printf '%-8s %-7s %-10s %-13s %-14s %-9s %-10s %-8s %-8s %-8s\n' \
        "$want" "$conns" "$window_seconds" "$avg" "$smax" "$cpu" "$inps" "$pending" "$rtt" "$((liveness1 - liveness0))"

}

capture_level_runtime() {
    local ids controller_id status=0
    local -a containers
    ids="$(compose ps -q)" || return 1
    [ -n "$ids" ] || return 1
    mapfile -t containers <<<"$ids"
    docker stats --no-stream --format '{{json .}}' "${containers[@]}" >"$LEVEL_DIR/docker-stats.jsonl" || status=1
    controller_id="$(compose ps -q controller)" || return 1
    [ -n "$controller_id" ] || return 1
    docker logs "$controller_id" >"$LEVEL_DIR/controller.jsonl" 2>&1 || status=1
    return "$status"
}

printf '\n%-8s %-7s %-10s %-13s %-14s %-9s %-10s %-8s %-8s %-8s\n' \
    agents conns window_s tick_avg_us tick_max_cum_us cpu_pct in_msgs_s pending rtt_ms liveness_closed

bad_levels=0
for total in "${REQUESTED_LEVELS[@]}"; do
    want=$total
    printf -v LEVEL_NAME '%04d' "$want"
    LEVEL_DIR="$RUN_DIR/$LEVEL_NAME"
    mkdir "$LEVEL_DIR" || exit 1
    python3 "$ROOT/scripts/performance/workload-config.py" generate "$RUN_DIR/config" "$LEVEL_DIR" \
        --total "$want" --layout "$MODE" "${layout_args[@]}" "${policy_args[@]}" || exit 1
    COMPOSE="$(realpath --relative-to="$ROOT/docker" "$LEVEL_DIR/compose.json")" || exit 1
    mapfile -t up_services < <(jq -r '.services | keys[]' "$LEVEL_DIR/compose.json")
    DDCS_PERF_AGENTS_PER_FLEET="$(jq -r '.agents_per_fleet' "$LEVEL_DIR/workload.json")" || exit 1
    export DDCS_PERF_AGENTS_PER_FLEET
    printf '{"schema_name":"ddcs.perf_assessment","schema_version":1,"measurement":{"status":"not_completed"},"configured_slo":{"status":"unassessed"}}\n' >"$LEVEL_DIR/assessment.json"
    level_status=passed
    if ! measure_level; then level_status=failed; fi
    if ! capture_level_runtime; then
        level_fail "Agent ${want}대: Controller 로그 또는 컨테이너 자원 관측 실패."
        level_status=failed
    fi
    if ! stack_down; then
        level_fail "Agent ${want}대: 스택 정리 실패. 다음 단계을 실행하지 않습니다."
        level_status=failed
        bad_levels=$((bad_levels + 1))
        printf '{"status":"failed"}\n' >"$LEVEL_DIR/result.json"
        write_manifest || true
        exit 1
    fi
    printf '{"status":"%s"}\n' "$level_status" >"$LEVEL_DIR/result.json"
    if [ "$level_status" = failed ]; then bad_levels=$((bad_levels + 1)); fi
done

narrate "해석"
info "tick_avg_us, in_msgs_s, rtt_ms, liveness_closed는 측정 시작·종료 시점의 누적 메트릭 차이로 계산합니다."
info "각 단계은 새 Controller와 Fleet으로 실행합니다. tick_max_cum_us에는 해당 단계의 등록 구간이 포함됩니다."
info "Fleet CPU 포화와 메모리 사용은 단계별 docker-stats.jsonl에서 확인하십시오."
write_manifest || { echo "오류: 성능 측정 기록(manifest.json)을 저장하지 못했습니다." >&2; exit 1; }
info "성능 결과: $RUN_DIR"
info "측정 완료는 성능 기준 통과를 뜻하지 않습니다. 단계별 assessment.json의 configured_slo를 확인하십시오."
[ "$bad_levels" -eq 0 ] || {
    echo "측정에 실패한 단계: ${bad_levels}개. 실패 이유는 단계별 failure.txt를 확인하십시오." >&2
    exit 1
}
