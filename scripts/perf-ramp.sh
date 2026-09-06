#!/usr/bin/env bash
#
# DDCS 성능 램프: Agent 수를 늘려가며 Controller(싱글 스레드 리액터)의 포화 지표를 캡처한다.
#
# 핵심 지표는 Controller tick 한 번의 소요 시간(us)이다. 한 tick은 명령 재전송, liveness 검사, 정책 평가를
# 한 스레드에서 처리하므로, 이 시간이 tick 주기(기본 1s = 1,000,000us)에 근접하면 한 코어가 포화에 이른다.
# pending이 쌓이거나 liveness 종료가 늘어도 Controller가 따라가지 못하고 있다고 봐야 한다.
#
# 사용법: scripts/perf-ramp.sh <balance|single>
#
#   balance                         총 Agent를 4개 zone에 균등 분배한다(각 레벨은 4의 배수).
#   single                          전체 Agent를 zone_a 하나에 둔다.
#   DDCS_PERF_LEVELS="100 200 400 600 800 1000"  총 Agent 수 단계
#   DDCS_PERF_SETTLE=30                           목표 연결 뒤 안정화 시간 (초)
#   DDCS_PERF_SOAK=120                            단계별 측정 창 (초)
#   DDCS_PERF_SKIP_PREFLIGHT=1      전제 검사를 건너뛴다(비권장. 그렇게 잰 수치는 표에 싣지 않는다)
#   DDCS_PERF_SKIP_BUILD=1          이미 준비한 Controller/Agent image를 재사용한다(perf-suite 내부용)
#   DDCS_PERF_SOURCE_REVISION=...   evidence source identity를 외부 suite가 고정할 때 둘 다 함께 지정한다
#   DDCS_PERF_SOURCE_DIRTY=true     (output 디렉터리 생성이 clean source 판정을 오염시키지 않게 한다)
#   DDCS_PERF_OUTPUT_ROOT=...       결과 루트 override (perf-suite 내부용)
#
# 본 측정 예: DDCS_PERF_LEVELS="100 200 400 600 800 1000" DDCS_PERF_SETTLE=30 DDCS_PERF_SOAK=120 scripts/perf-ramp.sh balance
# 단일 Group 측정 예: DDCS_PERF_LEVELS="1000" DDCS_PERF_SETTLE=30 DDCS_PERF_SOAK=120 scripts/perf-ramp.sh single
#
# 종료 상태:
#   0   모든 레벨의 측정이 정상
#   1   실행 실패 (전제 검사 실패, 스택 기동 실패, 측정값을 수집하지 못한 레벨 존재)
#   2   사용법 오류 (DDCS_PERF_LEVELS, DDCS_PERF_SETTLE, DDCS_PERF_SOAK가 정수가 아님)
#
# 주의: Agent 프로세스도 같은 호스트 CPU를 쓰므로 cpu_pct는 호스트 경합에 오염될 수 있다.
#       tick_avg_us는 Controller가 tick당 실제로 일한 시간이라 호스트 경합에 오염되지 않는다.
#       Agent 1대 = 컨테이너 1개라 메모리도 대수에 비례한다. 상위 레벨(1000)은 호스트 가용 메모리를
#       먼저 확인할 것(레벨이 점진 상승하므로 포화·실패 지점은 표에서 드러난다).
#
# 사후 점검:
#       수천 컨테이너를 만들었다 부순 뒤에는 containerd-shim 고아가 남아 메모리를 잡을 수 있다.
#       (컨테이너는 0인데 shim 프로세스 수천 개가 잔존, 개당 6MB 안팎)
#
#   pgrep -fc containerd-shim-runc-v2   # docker ps -q 가 0인데 이 수가 크면 전부 고아
#   sudo pkill -f containerd-shim-runc-v2 && sudo systemctl restart docker
#
# 호스트 전제(500대 이상):
#       컨테이너 수가 리눅스 ARP 이웃 테이블 상한(net.ipv4.neigh.default.gc_thresh3, 기본 1024)에
#       접근하면 커널이 신규 연결의 SYN을 응답 없이 버린다. 기존 연결은 유지되어 Controller는
#       멀쩡해 보이는데 메트릭 curl과 신규 등록만 실패하며, 이때 병목은 Controller가 아니라
#       측정 하네스에  있다(커널 로그에 "neighbour: arp_cache: neighbor table overflow!"가 찍힌다).
#       따라서 측정 전에 상한을 올려야 한다.
#
#   sudo sysctl -w net.ipv4.neigh.default.gc_thresh1=2048 \
#                  net.ipv4.neigh.default.gc_thresh2=4096 \
#                  net.ipv4.neigh.default.gc_thresh3=8192

# shellcheck source=scripts/scenario-lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"
# shellcheck source=scripts/result-lib.sh
source "$ROOT/scripts/result-lib.sh"

# scenario-lib.sh의 compose()가 source한 쪽의 COMPOSE를 읽는다.
# shellcheck disable=SC2034
COMPOSE=docker-compose.scale.yml
MODE="${1:-}"
LEVELS="${DDCS_PERF_LEVELS:-100 200 400 600 800 1000}"
SETTLE="${DDCS_PERF_SETTLE:-30}"
SOAK="${DDCS_PERF_SOAK:-120}"
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

# 환경 변수 검증. 정수가 아니면 산술 확장에서 코드 실행이나 즉사로 이어지므로 먼저 거른다.
case "$SETTLE" in
'' | 0 | *[!0-9]*)
    echo "오류: DDCS_PERF_SETTLE은 양의 정수여야 합니다: $SETTLE" >&2
    exit 2 ;;
esac
case "$SOAK" in
'' | 0 | *[!0-9]*)
    echo "오류: DDCS_PERF_SOAK는 양의 정수여야 합니다: $SOAK" >&2
    exit 2 ;;
esac
for t in $LEVELS; do
    case "$t" in
    '' | *[!0-9]*)
        echo "오류: DDCS_PERF_LEVELS는 정수 목록이어야 합니다: $t" >&2
        exit 2 ;;
    esac
    if [ "$MODE" = balance ] && [ $((t % 4)) -ne 0 ]; then
        echo "오류: balance 레벨은 4의 배수여야 합니다: $t" >&2
        exit 2
    fi
done
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

# output directory를 만들기 전에 source 상태를 읽어야, 새 local 결과가 clean source를 dirty로
# 보이게 만들지 않는다. untracked file도 재현 불가능한 입력이므로 dirty로 취급한다.
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
}
PREFLIGHT_TMP="$(mktemp "${TMPDIR:-/tmp}/ddcs-perf-preflight.XXXXXX")" || {
    echo "오류: preflight 임시 파일을 만들지 못했습니다." >&2
    exit 1
}
trap cleanup_preflight_tmp EXIT

preflight || exit 1
result_require_jq || exit 1

# 측정 환경 게이트: 클럭 고정과 깨끗한 호스트가 아니면 시작하지 않는다. 실패한 gate 결과는
# 실행 산출물로 만들지 않고 호출자에게만 출력한다.
if [ "${DDCS_PERF_SKIP_PREFLIGHT:-0}" != "1" ]; then
    PREFLIGHT_SKIPPED=false
    "$ROOT/scripts/perf-preflight.sh" >"$PREFLIGHT_TMP" 2>&1 || {
        sed -n '1,240p' "$PREFLIGHT_TMP"
        echo "오류: 전제 검사에 실패했습니다. 위 출력의 수정 명령을 적용하거나 DDCS_PERF_SKIP_PREFLIGHT=1로 우회하십시오(비권장)." >&2
        exit 1
    }
    sed -n '1,240p' "$PREFLIGHT_TMP"
else
    PREFLIGHT_SKIPPED=true
    printf '%s\n' 'preflight skipped by DDCS_PERF_SKIP_PREFLIGHT=1; diagnostic result only' >"$PREFLIGHT_TMP"
fi

if [ "$MODE" = single ]; then
    narrate "성능 램프(single): 레벨 = [$LEVELS] (총 Agent 수, 전부 zone_a), 레벨당 연결 안정화 ${SETTLE}s + ${SOAK}s 측정"
else
    narrate "성능 램프(balance): 레벨 = [$LEVELS] (총 Agent 수, zone 4개 균등 분배), 레벨당 연결 안정화 ${SETTLE}s + ${SOAK}s 측정"
fi
if [ "$SKIP_BUILD" = 1 ]; then
    narrate "이미지 재사용"
    docker image inspect ddcs-controller:dev >/dev/null 2>&1 || {
        echo "오류: 재사용할 Controller image를 찾지 못했습니다: ddcs-controller:dev" >&2
        exit 1
    }
    docker image inspect ddcs-agent:dev >/dev/null 2>&1 || {
        echo "오류: 재사용할 Agent image를 찾지 못했습니다: ddcs-agent:dev" >&2
        exit 1
    }
    IMAGE_BUILD_SKIPPED=true
else
    ensure_images || exit 1
    IMAGE_BUILD_SKIPPED=false
fi
CONTROLLER_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-controller:dev 2>/dev/null || true)"
[ -n "$CONTROLLER_IMAGE_ID" ] || {
    echo "오류: Controller image ID를 읽지 못했습니다." >&2
    exit 1
}
AGENT_IMAGE_ID="$(docker image inspect --format '{{.Id}}' ddcs-agent:dev 2>/dev/null || true)"
[ -n "$AGENT_IMAGE_ID" ] || {
    echo "오류: Agent image ID를 읽지 못했습니다." >&2
    exit 1
}
RUNTIME_CONFIG_SHA256="$(result_directory_sha256 "$ROOT/config")" || exit 1
result_initialize_build \
    "$ROOT" "$SOURCE_REVISION" "$SOURCE_DIRTY" \
    "$CONTROLLER_IMAGE_ID" "$AGENT_IMAGE_ID" "$RUNTIME_CONFIG_SHA256" || exit 1

if [ -n "$OUTPUT_ROOT_OVERRIDE" ]; then
    OUTPUT_ROOT="$OUTPUT_ROOT_OVERRIDE"
else
    OUTPUT_ROOT="$DDCS_RESULT_BUILD_DIR/performance"
fi
mkdir -p "$OUTPUT_ROOT" || {
    echo "오류: 성능 결과 루트를 만들지 못했습니다: $OUTPUT_ROOT" >&2
    exit 1
}
RUN_DIR="$OUTPUT_ROOT/$RUN_ID"
[ ! -e "$RUN_DIR" ] || {
    echo "오류: 기존 성능 run을 덮어쓰지 않습니다: $RUN_DIR" >&2
    exit 1
}
mkdir "$RUN_DIR" || {
    echo "오류: 성능 run 디렉터리를 만들지 못했습니다: $RUN_DIR" >&2
    exit 1
}
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
PREFLIGHT_ARTIFACT="$RUN_DIR/preflight.txt"
cp "$PREFLIGHT_TMP" "$PREFLIGHT_ARTIFACT" || {
    echo "오류: preflight 결과를 보관하지 못했습니다: $PREFLIGHT_ARTIFACT" >&2
    exit 1
}
chmod 644 "$PREFLIGHT_ARTIFACT"
rm -f -- "$PREFLIGHT_TMP"
PREFLIGHT_TMP=
RUN_STARTED_UTC="$(date -u '+%Y-%m-%dT%H:%M:%S.%NZ')"
arm_cleanup

# 메트릭 스냅샷을 한 번 뜬다. 실패하면 빈 문자열이다.
# 측정 창 양끝을 각각 한 번의 응답에서 파싱해야 값들 사이에 시차가 없고,
# 수집 실패가 0으로 위장해 델타 표를 오염시키는 일도 없다.
snapshot() { curl -sf --max-time 5 "$METRICS_URL"; }

is_unsigned_integer() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

is_positive_integer() {
    is_unsigned_integer "$1" && [ "$1" -gt 0 ]
}

capture_stamp() {
    # GNU date 한 호출에서 UTC와 epoch ns를 함께 가져온다. 두 snapshot의 실제 간격과
    # Controller CPU jiffies delta의 분모를 같은 artifact에 남긴다.
    date -u '+%Y-%m-%dT%H:%M:%S.%NZ %s%N'
}

set_stamp() { # ISO 변수명, epoch-ns 변수명
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

set_controller_cpu_snapshot() { # PID 변수명, jiffies 변수명
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

# 스냅샷 텍스트에서 라벨 없는 정수 메트릭 하나의 값. 이름 전체 일치라 접두어가 같은 메트릭과 섞이지 않는다.
snap_int() { # text name
    printf '%s\n' "$1" | awk -v m="$2" '$1 == m {print $2; exit}'
}

# seconds 계열 Prometheus 값을 정수 µs로 환산한다. exporter가 고정 소수점 최대 6자리로 정확히 내므로,
# 측정 창 delta를 계산하는 이 경계에서만 쉘 표의 단위(us)로 되돌린다.
snap_seconds_us() { # text name
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

# reason 라벨 counter 하나의 정수값. exporter의 reason label 출력 순서는 고정이다.
snap_reason_int() { # text name reason
    printf '%s\n' "$1" |
        awk -v m="$2" -v r="$3" '$1 == m "{reason=\"" r "\"}" {print $2; exit}'
}

format_us_as_ms() {
    awk -v us="$1" 'BEGIN { printf "%.3f", us / 1000 }'
}

format_ns_as_seconds() {
    awk -v ns="$1" 'BEGIN { printf "%.6f", ns / 1000000000 }'
}

format_per_second() { # count elapsed ns
    awk -v count="$1" -v elapsed_ns="$2" 'BEGIN {
        if (elapsed_ns <= 0) {
            exit 1
        }
        printf "%.1f", count * 1000000000 / elapsed_ns
    }'
}

controller_cpu_percent() { # start PID, jiffies, timestamp; end PID, jiffies, timestamp; CLK_TCK
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

write_level_measurement() { # output path, captured snapshot 여부는 전역 snap0/snap1과 경계 변수를 사용한다.
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
            '  "schema_version": 5,' \
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
    chmod 644 "$RUN_DIR/manifest.json"
}

# 누적치(sum/count류)는 전부 측정 창 양끝의 델타로 계산해 레벨별 값을 낸다.
# 예외는 tick_max_cum 하나: 시작 후 누적 최대라 리셋이 없어 이전 레벨과 레벨 전환(접속 폭풍)을
# 포함한다. 과대 방향(보수적)이라 그대로 싣되 열 이름에 cum을 박아 오독을 막는다.
printf '\n%-8s %-7s %-10s %-13s %-14s %-9s %-10s %-8s %-8s %-8s\n' \
    agents conns window_s tick_avg_us tick_max_cum_us cpu_pct in_msgs_s pending rtt_ms liveness_closed
printf -- '---------------------------------------------------------------------------------------------------------\n'

bad_levels=0
for total in $LEVELS; do
    if [ "$MODE" = single ]; then
        want=$total
        up_services="controller agent-zone-a"
        up_flags="--scale agent-zone-a=$total"
    else
        pz=$((total / 4))
        want=$total
        up_services="controller agent-zone-a agent-zone-b agent-zone-c agent-zone-d"
        up_flags="--scale agent-zone-a=$pz --scale agent-zone-b=$pz --scale agent-zone-c=$pz --scale agent-zone-d=$pz"
    fi
    printf -v LEVEL_NAME '%04d' "$want"
    LEVEL_DIR="$RUN_DIR/$LEVEL_NAME"
    [ ! -e "$LEVEL_DIR" ] || {
        echo "오류: 같은 실제 Agent 수의 level artifact가 이미 있습니다: $want" >&2
        exit 2
    }
    mkdir "$LEVEL_DIR" || {
        echo "오류: level artifact 디렉터리를 만들지 못했습니다: $LEVEL_DIR" >&2
        exit 1
    }
    # stdout(생성 로그)만 버리고 stderr는 남긴다. 기동 실패 뒤의 레벨은 전부 오염이므로 즉시 끝낸다.
    # shellcheck disable=SC2086  # up_flags는 이 스크립트가 만든 옵션 나열이라 단어 분할이 의도다
    if ! compose up -d $up_flags $up_services >/dev/null; then
        echo "레벨 $total: 스택 기동 실패. 측정을 중단합니다." >&2
        exit 1
    fi

    # 목표 연결 수 도달 대기(최대 90s) 뒤 접속 폭풍이 측정 창에 새지 않게 명시적으로 안정화한다.
    i=0
    while [ "$i" -lt 90 ]; do
        [ "$(metric_int ddcs_connections)" -ge "$want" ] && break
        sleep 1; i=$((i + 1))
    done
    sleep "$SETTLE"

    set_stamp SNAP0_STARTED_UTC SNAP0_STARTED_UNIX_NS
    set_controller_cpu_snapshot SNAP0_CONTROLLER_PID SNAP0_CPU_JIFFIES
    snap0=$(snapshot)
    set_stamp SNAP0_ENDED_UTC SNAP0_ENDED_UNIX_NS
    if [ -n "$snap0" ]; then
        printf '%s\n' "$snap0" >"$LEVEL_DIR/metrics-start.prom"
        chmod 644 "$LEVEL_DIR/metrics-start.prom"
    fi
    sleep "$SOAK"
    set_stamp SNAP1_STARTED_UTC SNAP1_STARTED_UNIX_NS
    set_controller_cpu_snapshot SNAP1_CONTROLLER_PID SNAP1_CPU_JIFFIES
    snap1=$(snapshot)
    set_stamp SNAP1_ENDED_UTC SNAP1_ENDED_UNIX_NS
    if [ -n "$snap1" ]; then
        printf '%s\n' "$snap1" >"$LEVEL_DIR/metrics-end.prom"
        chmod 644 "$LEVEL_DIR/metrics-end.prom"
    fi
    write_level_measurement "$LEVEL_DIR/measurement.json" || {
        echo "레벨 $total: 측정 경계 metadata를 쓰지 못했습니다. 이 레벨의 행을 건너뜁니다." >&2
        bad_levels=$((bad_levels + 1))
        continue
    }
    if [ -z "$snap0" ] || [ -z "$snap1" ]; then
        echo "레벨 $total: 메트릭 수집 실패(스냅샷 누락). 이 레벨의 행을 건너뜁니다." >&2
        bad_levels=$((bad_levels + 1))
        continue
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
        echo "레벨 $total: 새 metric contract 값을 파싱하지 못했습니다. 이미지와 endpoint를 확인하십시오." >&2
        bad_levels=$((bad_levels + 1))
        continue
    fi

    # 카운터가 역행했거나(Controller 재시작) tick이 하나도 없으면 측정 창이 오염된 것으로 보고 행에 싣지 않는다.
    dtk=$((tk1 - tk0))
    if [ "$dtk" -le 0 ] || [ $((sum1 - sum0)) -lt 0 ] || [ $((rsum1 - rsum0)) -lt 0 ] ||
        [ $((rcnt1 - rcnt0)) -lt 0 ] || [ $((recv1 - recv0)) -lt 0 ] ||
        [ $((liveness1 - liveness0)) -lt 0 ]; then
        echo "레벨 $total: 측정 창 오염(카운터 역행 또는 tick 없음). 이 레벨의 행을 건너뜁니다." >&2
        bad_levels=$((bad_levels + 1))
        continue
    fi

    elapsed_ns=$((SNAP1_STARTED_UNIX_NS - SNAP0_STARTED_UNIX_NS))
    if [ "$elapsed_ns" -le 0 ]; then
        echo "레벨 $total: 측정 창 시간이 역행했습니다. 이 레벨의 행을 건너뜁니다." >&2
        bad_levels=$((bad_levels + 1))
        continue
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

    printf '%-8s %-7s %-10s %-13s %-14s %-9s %-10s %-8s %-8s %-8s\n' \
        "$want" "$conns" "$window_seconds" "$avg" "$smax" "$cpu" "$inps" "$pending" "$rtt" "$((liveness1 - liveness0))"

    if [ "$conns" -lt "$want" ]; then
        echo "레벨 $total: 목표 연결 미달($conns/$want). 이 레벨의 값은 신뢰할 수 없습니다." >&2
        bad_levels=$((bad_levels + 1))
    fi
done

# rtt 표의 열은 평균이라 burst 꼬리를 가린다. 분포와 전환(=burst 발생) 횟수를 함께 남긴다.
narrate "rtt 분포 (누적 히스토그램, 마지막 레벨까지 합산; seconds)"
curl -s --max-time 5 "$METRICS_URL" | grep '^ddcs_command_rtt_seconds_bucket' | sed 's/^/  /'
info "Regime 전환(Group 전체 재명령 burst) 횟수: $(logcount '"event":"policy.regime.update"')"

narrate "해석"
info "표의 누적 지표(tick_avg_us, in_msgs_s, rtt_ms, liveness_closed)는 양끝 snapshot의 실제 측정 창에서 늘어난 양으로 계산한다."
info "tick_max_cum_us만 시작부터의 최댓값이라, 레벨을 올릴 때 수백 대가 한꺼번에 접속하는 구간까지 포함한다."
info "tick_avg_us가 tick 주기(1초)에 근접하면 코어 하나로는 더 감당하지 못하며, pending이나 liveness_closed가 0이 아닌 행도 같은 신호로 본다."
info "in_msgs_s가 기대 유입(약 3 x Agent 수: heartbeat 2/s + status 1/s + 명령 응답)에 못 미치면 병목이 Controller가 아니라 부하 생성 쪽이므로, 같은 행의 다른 값도 재해석해야 한다."
info "자세한 기준과 본 측정 결과는 README의 성능 절에서 다룬다."

if docker logs "$CTRL" >"$RUN_DIR/controller.jsonl" 2>&1; then
    chmod 644 "$RUN_DIR/controller.jsonl"
else
    echo "경고: Controller JSONL을 수집하지 못했습니다." >&2
    bad_levels=$((bad_levels + 1))
fi
chmod 644 "$PREFLIGHT_ARTIFACT"
write_manifest || {
    echo "오류: 성능 evidence manifest를 쓰지 못했습니다: $RUN_DIR" >&2
    exit 1
}
info "성능 결과: $RUN_DIR"

[ "$bad_levels" -eq 0 ] || {
    echo "측정 실패 레벨 $bad_levels개. 표를 그대로 쓰지 마십시오." >&2
    exit 1
}
