#!/usr/bin/env bash
#
# Controller tick profile을 한 조건에서 수집하고 검증한다.
#
# 사용법: scripts/profile-capture.sh <balance|single> <총 Agent 수> <측정 초>
#
# raw profile과 metrics 원문은 /tmp의 단일 실행 디렉터리에서만 쓰고, 검증·요약 뒤 제거한다.
# 영구 결과는 var/result/<build-key>/build.json의 profile.capture에 대표값 하나만 남긴다.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/result-lib.sh
source "$ROOT/scripts/result-lib.sh"

MODE="${1:-}"
AGENT_COUNT="${2:-}"
DURATION_SECONDS="${3:-}"
WARMUP_SECONDS="${DDCS_PROFILE_WARMUP_SECONDS:-10}"
CAPACITY="${DDCS_PROFILE_CAPACITY:-16384}"
STOP_TIMEOUT="${DDCS_PROFILE_STOP_TIMEOUT:-15}"
METRICS_URL="${DDCS_PROFILE_METRICS_URL:-${DDCS_METRICS_URL:-http://localhost:9000/metrics}}"
PROFILE_ENABLED="${DDCS_PROFILE_ENABLED:-true}"
RECORD_CAPTURE="${DDCS_PROFILE_RECORD_CAPTURE:-true}"
SKIP_BUILD="${DDCS_PROFILE_SKIP_BUILD:-0}"
SKIP_PREFLIGHT="${DDCS_PROFILE_SKIP_PREFLIGHT:-0}"
SOURCE_REVISION_OVERRIDE="${DDCS_PROFILE_SOURCE_REVISION:-}"
SOURCE_DIRTY_OVERRIDE="${DDCS_PROFILE_SOURCE_DIRTY:-}"
RESULT_JSON="${DDCS_PROFILE_RESULT_JSON:-}"
PROFILE_REPORT_BIN="${DDCS_PROFILE_REPORT_BIN:-$ROOT/build/release/bin/profile-report}"
PROFILE_VERIFY_BIN="${DDCS_PROFILE_VERIFY_BIN:-$ROOT/build/release/bin/profile-verify}"

fail() {
    echo "오류: $*" >&2
    exit 1
}

usage() {
    echo "사용법: $0 <balance|single> <총 Agent 수> <측정 초>" >&2
    exit 2
}

is_unsigned_integer() { [[ "$1" =~ ^[0-9]+$ ]]; }
is_positive_integer() { is_unsigned_integer "$1" && [ "$1" -gt 0 ]; }
is_nonnegative_integer() { is_unsigned_integer "$1"; }

normalize_boolean() {
    case "$1" in
    true | 1) printf '%s' true ;;
    false | 0) printf '%s' false ;;
    *) return 1 ;;
    esac
}

capture_stamp() {
    date -u '+%Y-%m-%dT%H:%M:%S.%NZ %s%N'
}

set_stamp() { # ISO 변수명, epoch-ns 변수명
    local stamp
    stamp="$(capture_stamp)"
    printf -v "$1" '%s' "${stamp%% *}"
    printf -v "$2" '%s' "${stamp##* }"
}

capture_metrics_snapshot() { # output path
    curl -fsS --max-time 5 "$METRICS_URL" >"$1" ||
        fail "metrics snapshot을 읽지 못했습니다: $METRICS_URL"
    [ -s "$1" ] || fail "metrics snapshot이 비어 있습니다: $1"
}

metric_connections() {
    curl -fsS --max-time 5 "$METRICS_URL" 2>/dev/null |
        awk '$1 == "ddcs_connections" {printf "%d", $2; exit}' || true
}

wait_for_connections() { # expected agents
    local expected="$1" elapsed=0 actual
    printf '  대기: Agent %s대 연결 ' "$expected"
    while [ "$elapsed" -lt 90 ]; do
        actual="$(metric_connections)"
        if [ "${actual:-0}" -ge "$expected" ]; then
            printf 'ok\n'
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
        printf '.'
    done
    printf 'timeout\n' >&2
    return 1
}

controller_cpu_snapshot() {
    local pid jiffies
    pid="$(docker inspect --format '{{.State.Pid}}' ddcs-controller 2>/dev/null || true)"
    if ! is_positive_integer "$pid" || [ ! -r "/proc/$pid/stat" ]; then
        return 1
    fi
    jiffies="$(awk '{printf "%.0f", $14 + $15}' "/proc/$pid/stat" 2>/dev/null || true)"
    is_unsigned_integer "$jiffies" || return 1
    printf '%s %s\n' "$pid" "$jiffies"
}

set_controller_cpu_snapshot() { # pid 변수명, jiffies 변수명
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

# 라벨 없는 정수 / seconds 계열 값을 snapshot 하나에서 읽는다.
snap_int() { # text metric-name
    printf '%s\n' "$1" | awk -v m="$2" '$1 == m {print $2; exit}'
}

snap_seconds_us() { # text metric-name
    printf '%s\n' "$1" | awk -v m="$2" '
        function to_us(value, parts, fraction) {
            if (value !~ /^[0-9]+(\.[0-9]+)?$/) exit 2
            split(value, parts, ".")
            fraction = (length(parts) > 1 ? parts[2] : "") "000000"
            if (length(parts) > 1 && length(parts[2]) > 6) exit 2
            printf "%.0f", parts[1] * 1000000 + substr(fraction, 1, 6)
        }
        $1 == m { to_us($2); exit }
    '
}

controller_cpu_percent() { # start PID,jiffies,time end PID,jiffies,time CLK_TCK
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

profile_summary_from_report() { # raw profile start unix-ns end unix-ns
    local raw_profile="$1" from_unix_ns="$2" to_unix_ns="$3" csv line
    csv="$("$PROFILE_REPORT_BIN" "$raw_profile" \
        --from-unix-ns "$from_unix_ns" --to-unix-ns "$to_unix_ns")" || return 1
    line="$(printf '%s\n' "$csv" | sed -n '2p')"
    [ -n "$line" ] || return 1
    printf '%s\n' "$line" | jq -R '
        def optional_number: if . == "" then null else tonumber end;
        split(",") as $row |
        if ($row | length) != 33 then
            error("profile-report CSV column count")
        else {
            recording: {
                capacity: ($row[3] | tonumber),
                captured: ($row[4] | tonumber),
                dropped: ($row[5] | tonumber),
                complete: ($row[6] == "true")
            },
            selected_ticks: ($row[7] | tonumber),
            completed_ticks: ($row[8] | tonumber),
            failures: {
                command_sweep: ($row[9] | tonumber),
                session_sweep: ($row[10] | tonumber),
                policy_evaluate: ($row[11] | tonumber)
            },
            tick: {
                count: ($row[12] | tonumber),
                mean_ns: ($row[13] | optional_number),
                p95_ns: ($row[14] | optional_number),
                max_ns: ($row[15] | optional_number)
            },
            command_sweep: {
                count: ($row[16] | tonumber),
                mean_ns: ($row[17] | optional_number),
                p95_ns: ($row[18] | optional_number),
                max_ns: ($row[19] | optional_number)
            },
            session_sweep: {
                count: ($row[20] | tonumber),
                mean_ns: ($row[21] | optional_number),
                p95_ns: ($row[22] | optional_number),
                max_ns: ($row[23] | optional_number)
            },
            policy_evaluate: {
                count: ($row[24] | tonumber),
                mean_ns: ($row[25] | optional_number),
                p95_ns: ($row[26] | optional_number),
                max_ns: ($row[27] | optional_number)
            },
            tick_start_interval: {
                count: ($row[28] | tonumber),
                mean_ns: ($row[29] | optional_number),
                p95_ns: ($row[30] | optional_number),
                max_ns: ($row[31] | optional_number),
                gaps: ($row[32] | tonumber)
            }
        }
        end
    '
}

metrics_summary_json() { # metrics-start metrics-end start cpu end cpu, timestamps, clock ticks
    local metrics_start="$1" metrics_end="$2"
    local start_pid="$3" start_jiffies="$4" start_ns="$5"
    local end_pid="$6" end_jiffies="$7" end_ns="$8" clock_ticks="$9"
    local ticks_start ticks_end duration_start_us duration_end_us rtt_count_start rtt_count_end
    local rtt_sum_start_us rtt_sum_end_us tick_average_us rtt_average_ms cpu_percent

    ticks_start="$(snap_int "$(<"$metrics_start")" ddcs_ticks_total)"
    ticks_end="$(snap_int "$(<"$metrics_end")" ddcs_ticks_total)"
    duration_start_us="$(snap_seconds_us "$(<"$metrics_start")" ddcs_tick_duration_seconds_total)"
    duration_end_us="$(snap_seconds_us "$(<"$metrics_end")" ddcs_tick_duration_seconds_total)"
    for value in "$ticks_start" "$ticks_end" "$duration_start_us" "$duration_end_us"; do
        is_unsigned_integer "$value" || return 1
    done
    [ "$ticks_end" -gt "$ticks_start" ] || return 1
    [ "$duration_end_us" -ge "$duration_start_us" ] || return 1
    tick_average_us=$(((duration_end_us - duration_start_us) / (ticks_end - ticks_start)))

    rtt_count_start="$(snap_int "$(<"$metrics_start")" ddcs_command_rtt_seconds_count)"
    rtt_count_end="$(snap_int "$(<"$metrics_end")" ddcs_command_rtt_seconds_count)"
    rtt_sum_start_us="$(snap_seconds_us "$(<"$metrics_start")" ddcs_command_rtt_seconds_sum)"
    rtt_sum_end_us="$(snap_seconds_us "$(<"$metrics_end")" ddcs_command_rtt_seconds_sum)"
    if is_unsigned_integer "$rtt_count_start" && is_unsigned_integer "$rtt_count_end" &&
        is_unsigned_integer "$rtt_sum_start_us" && is_unsigned_integer "$rtt_sum_end_us" &&
        [ "$rtt_count_end" -gt "$rtt_count_start" ] && [ "$rtt_sum_end_us" -ge "$rtt_sum_start_us" ]; then
        rtt_average_ms="$(awk -v total_us="$((rtt_sum_end_us - rtt_sum_start_us))" \
            -v count="$((rtt_count_end - rtt_count_start))" 'BEGIN { printf "%.3f", total_us / count / 1000 }')"
    else
        rtt_average_ms=null
    fi
    cpu_percent="$(controller_cpu_percent \
        "$start_pid" "$start_jiffies" "$start_ns" \
        "$end_pid" "$end_jiffies" "$end_ns" "$clock_ticks")"

    jq -n \
        --argjson tick_average_us "$tick_average_us" \
        --arg cpu_percent "$cpu_percent" \
        --arg rtt_average_ms "$rtt_average_ms" '
            {
                tick_average_us: $tick_average_us,
                controller_cpu_percent: (
                    if $cpu_percent == "N/A" then null else ($cpu_percent | tonumber) end
                ),
                rtt_average_ms: (
                    if $rtt_average_ms == "null" then null else ($rtt_average_ms | tonumber) end
                )
            }
        '
}

[ "$#" -eq 3 ] || usage
case "$MODE" in
balance | single) ;;
*) usage ;;
esac
is_positive_integer "$AGENT_COUNT" || usage
is_positive_integer "$DURATION_SECONDS" || usage
if [ "$MODE" = balance ] && [ $((AGENT_COUNT % 4)) -ne 0 ]; then
    fail "balance의 총 Agent 수는 4의 배수여야 합니다: $AGENT_COUNT"
fi
is_nonnegative_integer "$WARMUP_SECONDS" ||
    fail "DDCS_PROFILE_WARMUP_SECONDS는 0 이상의 정수여야 합니다."
is_positive_integer "$CAPACITY" || fail "DDCS_PROFILE_CAPACITY는 양의 정수여야 합니다."
is_positive_integer "$STOP_TIMEOUT" || fail "DDCS_PROFILE_STOP_TIMEOUT은 양의 정수여야 합니다."
PROFILE_ENABLED="$(normalize_boolean "$PROFILE_ENABLED")" ||
    fail "DDCS_PROFILE_ENABLED는 true, false, 1, 0 중 하나여야 합니다."
RECORD_CAPTURE="$(normalize_boolean "$RECORD_CAPTURE")" ||
    fail "DDCS_PROFILE_RECORD_CAPTURE는 true, false, 1, 0 중 하나여야 합니다."
[ "$PROFILE_ENABLED" = true ] || [ "$RECORD_CAPTURE" = false ] ||
    fail "profile capture 대표값은 profiler enabled run에서만 기록합니다."
case "$SKIP_BUILD" in 0 | 1) ;; *) fail "DDCS_PROFILE_SKIP_BUILD는 0 또는 1이어야 합니다." ;; esac
case "$SKIP_PREFLIGHT" in 0 | 1) ;; *) fail "DDCS_PROFILE_SKIP_PREFLIGHT는 0 또는 1이어야 합니다." ;; esac

if [ -n "$SOURCE_REVISION_OVERRIDE" ] || [ -n "$SOURCE_DIRTY_OVERRIDE" ]; then
    if [ -z "$SOURCE_REVISION_OVERRIDE" ] || [ -z "$SOURCE_DIRTY_OVERRIDE" ]; then
        fail "DDCS_PROFILE_SOURCE_REVISION과 DDCS_PROFILE_SOURCE_DIRTY는 함께 지정해야 합니다."
    fi
    result_is_boolean "$SOURCE_DIRTY_OVERRIDE" ||
        fail "DDCS_PROFILE_SOURCE_DIRTY는 true 또는 false여야 합니다."
    SOURCE_REVISION="$SOURCE_REVISION_OVERRIDE"
    SOURCE_DIRTY="$SOURCE_DIRTY_OVERRIDE"
else
    # var/ 출력물을 만들기 전 source 상태를 확정한다.
    SOURCE_REVISION="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unknown')"
    if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal)" ]; then
        SOURCE_DIRTY=true
    else
        SOURCE_DIRTY=false
    fi
fi

command -v docker >/dev/null 2>&1 || fail "'docker' 명령을 찾을 수 없습니다."
docker compose version >/dev/null 2>&1 || fail "docker compose v2가 필요합니다."
command -v curl >/dev/null 2>&1 || fail "'curl' 명령을 찾을 수 없습니다."
result_require_jq || exit 1
if [ "$PROFILE_ENABLED" = true ]; then
    [ -x "$PROFILE_REPORT_BIN" ] ||
        fail "profile-report를 찾지 못했습니다: $PROFILE_REPORT_BIN (release target을 build하거나 DDCS_PROFILE_REPORT_BIN을 지정하십시오.)"
    [ -x "$PROFILE_VERIFY_BIN" ] ||
        fail "profile-verify를 찾지 못했습니다: $PROFILE_VERIFY_BIN (release target을 build하거나 DDCS_PROFILE_VERIFY_BIN을 지정하십시오.)"
fi

if [ "$SKIP_PREFLIGHT" = 0 ]; then
    "$ROOT/scripts/perf-preflight.sh"
fi

if ! running_names="$(docker ps --format '{{.Names}}')"; then
    fail "docker daemon에 접근할 수 없습니다."
fi
if printf '%s\n' "$running_names" | grep -Fxq 'ddcs-controller'; then
    fail "ddcs-controller가 이미 실행 중입니다. 기존 DDCS 스택을 먼저 종료한 뒤 다시 시도하십시오."
fi

BASE_COMPOSE=docker-compose.scale.yml
if [ "$MODE" = balance ]; then
    PER_ZONE=$((AGENT_COUNT / 4))
    SERVICES=(controller agent-zone-a agent-zone-b agent-zone-c agent-zone-d)
    SCALE_ARGS=(
        --scale "agent-zone-a=${PER_ZONE}"
        --scale "agent-zone-b=${PER_ZONE}"
        --scale "agent-zone-c=${PER_ZONE}"
        --scale "agent-zone-d=${PER_ZONE}"
    )
else
    SERVICES=(controller agent-zone-a)
    SCALE_ARGS=(--scale "agent-zone-a=${AGENT_COUNT}")
fi
CONDITION="$(printf '%s-%04d' "$MODE" "$AGENT_COUNT")"
RUN_ID="profile-${CONDITION}-$$"

compose() {
    docker compose \
        -f "$ROOT/docker/$BASE_COMPOSE" \
        -f "$ROOT/docker/docker-compose.profile.yml" \
        "$@"
}

if [ "$SKIP_BUILD" = 1 ]; then
    echo "이미지 재사용"
    docker image inspect ddcs-controller:dev >/dev/null 2>&1 ||
        fail "재사용할 Controller image를 찾지 못했습니다: ddcs-controller:dev"
    docker image inspect ddcs-agent:dev >/dev/null 2>&1 ||
        fail "재사용할 Agent image를 찾지 못했습니다: ddcs-agent:dev"
else
    echo "이미지 빌드"
    docker compose -f "$ROOT/docker/$BASE_COMPOSE" build
fi
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || fail "Controller image ID를 읽지 못했습니다."
[ -n "$AGENT_IMAGE_ID" ] || fail "Agent image ID를 읽지 못했습니다."
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$ROOT/config")" || exit 1
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1

TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ddcs-profile-capture.XXXXXX")" ||
    fail "profile 임시 디렉터리를 만들지 못했습니다."
PROFILE_OUTPUT_DIR="$TEMP_DIR/profile-output"
mkdir "$PROFILE_OUTPUT_DIR"
RAW_PROFILE="$PROFILE_OUTPUT_DIR/tick-profile.json"
METRICS_START="$TEMP_DIR/metrics-start.prom"
METRICS_END="$TEMP_DIR/metrics-end.prom"

export DDCS_PROFILE_OUTPUT_DIR="$PROFILE_OUTPUT_DIR"
export DDCS_PROFILE_ENABLED="$PROFILE_ENABLED"
export DDCS_PROFILE_RUN_ID="$RUN_ID"
export DDCS_PROFILE_CAPACITY="$CAPACITY"

stack_started=0
controller_stopped=0
cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [ "$stack_started" -eq 1 ]; then
        if [ "$controller_stopped" -eq 0 ]; then
            compose stop -t "$STOP_TIMEOUT" controller >/dev/null 2>&1 || true
        fi
        compose down --remove-orphans >/dev/null 2>&1 || true
    fi
    rm -rf -- "$TEMP_DIR"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

echo "스택 기동: ${MODE}, Agent ${AGENT_COUNT}대"
stack_started=1
compose up -d "${SCALE_ARGS[@]}" "${SERVICES[@]}"
wait_for_connections "$AGENT_COUNT" || fail "목표 연결 수에 도달하지 못했습니다."

echo "예열: ${WARMUP_SECONDS}s"
sleep "$WARMUP_SECONDS"
set_stamp MEASUREMENT_STARTED_UTC MEASUREMENT_STARTED_UNIX_NS
set_controller_cpu_snapshot START_CONTROLLER_PID START_CPU_JIFFIES
set_stamp START_CPU_SAMPLED_UTC START_CPU_SAMPLED_UNIX_NS
capture_metrics_snapshot "$METRICS_START"
set_stamp METRICS_START_ENDED_UTC METRICS_START_ENDED_UNIX_NS
echo "측정: ${DURATION_SECONDS}s"
sleep "$DURATION_SECONDS"
set_stamp MEASUREMENT_ENDED_UTC MEASUREMENT_ENDED_UNIX_NS
set_controller_cpu_snapshot END_CONTROLLER_PID END_CPU_JIFFIES
set_stamp END_CPU_SAMPLED_UTC END_CPU_SAMPLED_UNIX_NS
capture_metrics_snapshot "$METRICS_END"
set_stamp METRICS_END_ENDED_UTC METRICS_END_ENDED_UNIX_NS

echo "Controller 정상 종료 및 profile dump 대기"
compose stop -t "$STOP_TIMEOUT" controller
controller_stopped=1
if [ "$PROFILE_ENABLED" = true ]; then
    [ -s "$RAW_PROFILE" ] || fail "Controller 종료 뒤 profile 결과를 찾지 못했습니다."
    "$PROFILE_VERIFY_BIN" "$RAW_PROFILE" "$METRICS_END" >/dev/null ||
        fail "profile raw와 종료 직전 metrics 교차 검증에 실패했습니다."
    PROFILE_SUMMARY="$(profile_summary_from_report \
        "$RAW_PROFILE" "$MEASUREMENT_STARTED_UNIX_NS" "$MEASUREMENT_ENDED_UNIX_NS")" ||
        fail "profile 대표 요약을 만들지 못했습니다."
    PROFILE_VERIFIED=true
else
    [ ! -e "$RAW_PROFILE" ] || fail "disabled baseline에 profile 결과가 생겼습니다."
    PROFILE_SUMMARY=null
    PROFILE_VERIFIED=null
fi

CONTROLLER_CLOCK_TICKS_PER_SECOND="$(getconf CLK_TCK 2>/dev/null || true)"
is_positive_integer "$CONTROLLER_CLOCK_TICKS_PER_SECOND" || CONTROLLER_CLOCK_TICKS_PER_SECOND=0
METRICS_SUMMARY="$(metrics_summary_json \
    "$METRICS_START" "$METRICS_END" \
    "$START_CONTROLLER_PID" "$START_CPU_JIFFIES" "$START_CPU_SAMPLED_UNIX_NS" \
    "$END_CONTROLLER_PID" "$END_CPU_JIFFIES" "$END_CPU_SAMPLED_UNIX_NS" \
    "$CONTROLLER_CLOCK_TICKS_PER_SECOND")" ||
    fail "측정 metrics delta를 계산하지 못했습니다."

RUN_RESULT="$(jq -n \
    --arg build_key "$DDCS_RESULT_BUILD_KEY" \
    --arg condition "$CONDITION" \
    --argjson duration_seconds "$DURATION_SECONDS" \
    --argjson profile_enabled "$PROFILE_ENABLED" \
    --argjson verified "$PROFILE_VERIFIED" \
    --argjson summary "$PROFILE_SUMMARY" \
    --argjson metrics "$METRICS_SUMMARY" '
        {
            build_key: $build_key,
            condition: $condition,
            duration_seconds: $duration_seconds,
            profile_enabled: $profile_enabled,
            verified: $verified,
            summary: $summary,
            metrics: $metrics
        }
    ')" || fail "profile 실행 요약을 만들지 못했습니다."

if [ "$RECORD_CAPTURE" = true ]; then
    result_set_profile_capture \
        "$DDCS_RESULT_BUILD_DIR" "$CONDITION" "$DURATION_SECONDS" true "$PROFILE_SUMMARY" ||
        fail "build.json에 profile capture 결과를 기록하지 못했습니다."
fi
if [ -n "$RESULT_JSON" ]; then
    RESULT_JSON_TMP="$(mktemp "${TMPDIR:-/tmp}/ddcs-profile-result.XXXXXX")" ||
        fail "profile 실행 결과 임시 파일을 만들지 못했습니다."
    printf '%s\n' "$RUN_RESULT" >"$RESULT_JSON_TMP"
    mv "$RESULT_JSON_TMP" "$RESULT_JSON"
fi

echo
echo "완료: $DDCS_RESULT_BUILD_DIR/build.json"
echo "  조건: $CONDITION"
if [ "$PROFILE_ENABLED" = true ]; then
    echo "  raw profile과 metrics 원문은 검증 후 제거했습니다."
fi
