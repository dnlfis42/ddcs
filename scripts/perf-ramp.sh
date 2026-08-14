#!/usr/bin/env bash
#
# DDCS 성능 램프: agent 수를 늘려가며 컨트롤러(단일 스레드 리액터)의 포화 지표를 캡처한다.
#
# 핵심 지표 = sweep tick 작업 소요(us). 한 tick은 명령 재전송 + monitor sweep + 정책 평가를
# 한 스레드에서 처리하므로, 이 시간이 sweep 주기(기본 1s = 1,000,000us)에 근접하면 한 코어가 포화에 이른다.
# pending이 쌓이거나 evicted가 급증하는 것도 컨트롤러가 못 따라간다는 신호이다.
#
# 사용법: scripts/perf-ramp.sh
#
#   DDCS_PERF_LEVELS="100 200 400"  총 agent 수 단계(4개 zone에 균등 분배; 4의 배수 권장, 100 단위면 딱 떨어진다)
#   DDCS_PERF_SOAK=25               단계별 측정 창(초)
#   DDCS_PERF_SINGLE_GROUP=1        전체를 zone_a 하나에 몰아넣는다(최악 케이스: 밴드 교차 한 번이
#                                   그룹 전체를 한 tick에 재명령하는 burst가 그룹 크기만큼 커진다)
#   DDCS_PERF_SKIP_PREFLIGHT=1      전제 검사를 건너뛴다(비권장. 그렇게 잰 수치는 표에 싣지 않는다)
#
# 본 측정 예: DDCS_PERF_LEVELS="100 200 400 600 800 1000" DDCS_PERF_SOAK=30 scripts/perf-ramp.sh
# burst 측정 예: DDCS_PERF_SINGLE_GROUP=1 DDCS_PERF_LEVELS="1000" DDCS_PERF_SOAK=120 scripts/perf-ramp.sh
#
# 종료 상태:
#
#   0   모든 레벨의 측정이 정상
#   1   실행 실패 (전제 검사 실패, 스택 기동 실패, 측정값을 수집하지 못한 레벨 존재)
#   2   사용법 오류 (DDCS_PERF_LEVELS나 DDCS_PERF_SOAK가 정수가 아님)
#
# 주의: agent 프로세스도 같은 호스트 CPU를 먹으므로 cpu_pct는 호스트 경합에 오염될 수 있다.
#       오염되지 않는 신호는 컨트롤러가 tick당 실제로 일한 시간인 sweep_avg_us이다.
#       agent 1대 = 컨테이너 1개라 메모리도 대수에 비례한다. 상위 레벨(1000)은 호스트 가용 메모리를
#       먼저 확인할 것(레벨이 점진 상승하므로 포화·실패 지점은 표에서 드러난다).
#
# 사후 점검: 수천 컨테이너를 만들었다 부순 뒤에는 containerd-shim 고아가 남아 메모리를 잡을 수 있다
# (컨테이너는 0인데 shim 프로세스 수천 개가 잔존, 개당 6MB 안팎). 확인과 정리:
#   pgrep -fc containerd-shim-runc-v2   # docker ps -q 가 0인데 이 수가 크면 전부 고아
#   sudo pkill -f containerd-shim-runc-v2 && sudo systemctl restart docker
#
# 호스트 전제(500대 이상): 컨테이너 수가 리눅스 ARP 이웃 테이블 상한(net.ipv4.neigh.default.gc_thresh3,
# 기본 1024)에 접근하면 커널이 신규 연결의 SYN을 응답 없이 버린다. 기존 연결은 살아 있어서 컨트롤러는
# 멀쩡해 보이는데 메트릭 curl과 신규 등록만 실패하는, 병목이 하네스인 전형적 패턴이다(커널 로그에
# "neighbour: arp_cache: neighbor table overflow!"가 찍힌다). 측정 전에 상한을 올릴 것:
#
#   sudo sysctl -w net.ipv4.neigh.default.gc_thresh1=2048 \
#                  net.ipv4.neigh.default.gc_thresh2=4096 \
#                  net.ipv4.neigh.default.gc_thresh3=8192
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

COMPOSE=docker-compose.scale.yml
LEVELS="${DDCS_PERF_LEVELS:-100 200 400}"
SOAK="${DDCS_PERF_SOAK:-25}"
SINGLE="${DDCS_PERF_SINGLE_GROUP:-0}"

# 환경 변수 검증. 정수가 아니면 산술 확장에서 코드 실행이나 즉사로 이어지므로 먼저 거른다.
case "$SOAK" in
'' | *[!0-9]*)
    echo "오류: DDCS_PERF_SOAK는 양의 정수여야 합니다: $SOAK" >&2
    exit 2 ;;
esac
for t in $LEVELS; do
    case "$t" in
    '' | *[!0-9]*)
        echo "오류: DDCS_PERF_LEVELS는 정수 목록이어야 합니다: $t" >&2
        exit 2 ;;
    esac
done

# 측정 환경 게이트: 클럭 고정과 깨끗한 호스트가 아니면 시작하지 않는다.
if [ "${DDCS_PERF_SKIP_PREFLIGHT:-0}" != "1" ]; then
    "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/perf-preflight.sh" || {
        echo "전제 검사 실패. 위에 출력된 고치는 명령을 적용하거나 DDCS_PERF_SKIP_PREFLIGHT=1 로 우회하십시오(비권장)." >&2
        exit 1
    }
fi
arm_cleanup

if [ "$SINGLE" = "1" ]; then
    narrate "성능 램프(단일 그룹 burst): 레벨 = [$LEVELS] 전부 zone_a, 레벨당 ${SOAK}s 측정"
else
    narrate "성능 램프: 레벨 = [$LEVELS] (총 agent 수, zone 4개 균등 분배), 레벨당 ${SOAK}s 측정"
fi
preflight || exit 1
ensure_images || exit 1

# 메트릭 스냅샷을 한 번 뜬다. 실패하면 빈 문자열이다.
# 측정 창 양끝을 각각 한 번의 응답에서 파싱해야 값들 사이에 시차가 없고,
# 수집 실패가 0으로 위장해 델타 표를 오염시키는 일도 없다.
snapshot() { curl -sf --max-time 5 "$METRICS_URL"; }

# 스냅샷 텍스트에서 메트릭 하나의 정수값. 이름 전체 일치라 접두어가 같은 메트릭과 섞이지 않는다.
snap_int() { # text name
    printf '%s\n' "$1" | awk -v m="$2" '$1 == m {printf "%d", $2; exit}'
}

# 누적치(sum/count류)는 전부 측정 창 양끝의 델타로 계산해 레벨별 값을 낸다.
# 예외는 sweep_max_cum 하나: 시작 후 누적 최대라 리셋이 없어 이전 레벨과 램프 전이(접속 폭풍)를
# 포함한다. 과대 방향(보수적)이라 그대로 싣되 열 이름에 cum을 박아 오독을 막는다.
printf '\n%-8s %-7s %-13s %-14s %-8s %-9s %-8s %-8s %-8s\n' \
    agents conns sweep_avg_us sweep_max_cum cpu_pct in_msgs_s pending rtt_ms evicted
printf -- '---------------------------------------------------------------------------------------------\n'

bad_levels=0
for total in $LEVELS; do
    if [ "$SINGLE" = "1" ]; then
        want=$total
        up_services="controller agent-zone-a"
        up_flags="--scale agent-zone-a=$total"
    else
        pz=$((total / 4))
        want=$((pz * 4))
        up_services="controller agent-zone-a agent-zone-b agent-zone-c agent-zone-d"
        up_flags="--scale agent-zone-a=$pz --scale agent-zone-b=$pz --scale agent-zone-c=$pz --scale agent-zone-d=$pz"
    fi
    # stdout(생성 로그)만 버리고 stderr는 남긴다. 기동 실패 뒤의 레벨은 전부 오염이므로 즉시 끝낸다.
    # shellcheck disable=SC2086  # up_flags는 이 스크립트가 만든 옵션 나열이라 단어 분할이 의도다
    if ! compose up -d $up_flags $up_services >/dev/null; then
        echo "레벨 $total: 스택 기동 실패. 측정을 중단합니다." >&2
        exit 1
    fi

    # 목표 연결 수 도달 대기(최대 90s) + 접속 폭풍이 측정 창에 새지 않게 짧은 안정화
    i=0
    while [ "$i" -lt 90 ]; do
        [ "$(metric_int ddcs_connections)" -ge "$want" ] && break
        sleep 1; i=$((i + 1))
    done
    sleep 3

    snap0=$(snapshot)
    sleep "$SOAK"
    snap1=$(snapshot)
    if [ -z "$snap0" ] || [ -z "$snap1" ]; then
        echo "레벨 $total: 메트릭 수집 실패(스냅샷 누락). 이 레벨의 행을 건너뜁니다." >&2
        bad_levels=$((bad_levels + 1))
        continue
    fi

    sum0=$(snap_int "$snap0" ddcs_sweep_duration_us_sum)
    tk0=$(snap_int "$snap0" ddcs_sweep_ticks_total)
    rsum0=$(snap_int "$snap0" ddcs_command_rtt_ms_sum)
    rcnt0=$(snap_int "$snap0" ddcs_commands_completed_total)
    recv0=$(snap_int "$snap0" ddcs_messages_received_total)
    ev0=$(snap_int "$snap0" ddcs_agents_evicted_total)
    sum1=$(snap_int "$snap1" ddcs_sweep_duration_us_sum)
    tk1=$(snap_int "$snap1" ddcs_sweep_ticks_total)
    rsum1=$(snap_int "$snap1" ddcs_command_rtt_ms_sum)
    rcnt1=$(snap_int "$snap1" ddcs_commands_completed_total)
    recv1=$(snap_int "$snap1" ddcs_messages_received_total)
    ev1=$(snap_int "$snap1" ddcs_agents_evicted_total)

    # 카운터 역행(컨트롤러 재시작)이나 tick 없음은 측정이 아니라 사고다. 행에 싣지 않는다.
    dtk=$((tk1 - tk0))
    if [ "$dtk" -le 0 ] || [ $((recv1 - recv0)) -lt 0 ]; then
        echo "레벨 $total: 측정 창 오염(카운터 역행 또는 tick 없음). 이 레벨의 행을 건너뜁니다." >&2
        bad_levels=$((bad_levels + 1))
        continue
    fi

    avg=$(((sum1 - sum0) / dtk))
    drc=$((rcnt1 - rcnt0))
    rtt=-
    [ "$drc" -gt 0 ] && rtt=$(((rsum1 - rsum0) / drc))
    inps=$(((recv1 - recv0) / SOAK))
    smax=$(snap_int "$snap1" ddcs_sweep_duration_us_max)
    conns=$(snap_int "$snap1" ddcs_connections)
    pending=$(snap_int "$snap1" ddcs_commands_pending)
    cpu=$(docker stats --no-stream --format '{{.CPUPerc}}' "$CTRL" 2>/dev/null | tr -d '%')

    printf '%-8s %-7s %-13s %-14s %-8s %-9s %-8s %-8s %-8s\n' \
        "$want" "$conns" "$avg" "$smax" "${cpu:-?}" "$inps" "$pending" "$rtt" "$((ev1 - ev0))"

    if [ "$conns" -lt "$want" ]; then
        echo "레벨 $total: 목표 연결 미달($conns/$want). 이 레벨의 값은 신뢰할 수 없습니다." >&2
        bad_levels=$((bad_levels + 1))
    fi
done

# rtt 표의 열은 평균이라 burst 꼬리를 가린다. 분포와 전환(=burst 발생) 횟수를 함께 남긴다.
narrate "rtt 분포 (누적 히스토그램, 마지막 레벨까지 합산)"
curl -s --max-time 5 "$METRICS_URL" | grep '^ddcs_command_rtt_ms_bucket' | sed 's/^/  /'
info "regime 전환(그룹 전체 재명령 burst) 횟수: $(logcount '"event":"policy.regime.update"')"

narrate "해석"
info "sweep_avg_us(창 델타)가 sweep 주기(1,000,000us=1s)에 근접하면 단일 코어가 포화에 이른 것이다."
info "in_msgs_s는 기대 유입(대략 3 x agent 수: heartbeat 2/s + status 1/s + 명령 응답)과 비교한다."
info "기대보다 낮으면 시뮬레이터/호스트가 못 따라온 것이니 그 레벨의 다른 열도 의심해야 한다."
info "pending이 단조 증가하거나 evicted(창 델타)가 튀는 레벨에서 컨트롤러가 더는 따라가지 못한다."
info "rtt_ms는 창 안에서 완료된 명령의 평균이고, sweep_max_cum만 시작 후 누적 최대(전이 폭풍 포함)다."
info "단일 그룹 모드에서는 burst(그룹 전체 동시 재명령)의 비용이 rtt 분포의 긴 꼬리로 나타난다."

[ "$bad_levels" -eq 0 ] || {
    echo "측정 실패 레벨 $bad_levels개. 표를 그대로 쓰지 마십시오." >&2
    exit 1
}
