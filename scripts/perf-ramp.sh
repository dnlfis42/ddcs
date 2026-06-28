#!/usr/bin/env bash
# DDCS 성능 램프: agent 수를 늘려가며 컨트롤러(단일 스레드 리액터)의 포화 지표를 캡처한다.
#
# 핵심 지표 = sweep tick 작업 소요(us). 한 tick은 명령 재전송 + monitor sweep + 정책 평가를
# 한 스레드에서 처리하므로, 이 시간이 sweep 주기(기본 1s = 1,000,000us)에 근접하면 한 코어가 포화다.
# pending 누적 증가나 evicted 급증도 "컨트롤러가 못 따라감"의 신호다.
#
# 사용법: scripts/perf-ramp.sh
#   DDCS_PERF_LEVELS="30 60 120"  총 agent 수 단계(3으로 나뉘어 zone별 분배; 3의 배수 권장)
#   DDCS_PERF_SOAK=25             단계별 측정 창(초)
#
# 주의: agent 프로세스도 같은 호스트 CPU를 먹으므로 cpu_pct는 호스트 경합에 오염될 수 있다.
#       오염 없는 순수 신호는 sweep_avg_us(컨트롤러가 tick당 실제로 일한 시간)다.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo-lib.sh"

COMPOSE=docker-compose.scale.yml
LEVELS="${DDCS_PERF_LEVELS:-30 60 120}"
SOAK="${DDCS_PERF_SOAK:-25}"
arm_cleanup

narrate "성능 램프: 레벨 = [$LEVELS] (총 agent 수), 레벨당 ${SOAK}s 측정"
preflight || exit 1
ensure_images || exit 1

printf '\n%-8s %-7s %-13s %-13s %-8s %-8s %-7s %-8s\n' \
    agents conns sweep_avg_us sweep_max_us cpu_pct pending rtt_ms evicted
printf -- '------------------------------------------------------------------------------\n'

for total in $LEVELS; do
    pz=$((total / 3))
    want=$((pz * 3))
    compose up -d --scale "agent-zone-a=$pz" --scale "agent-zone-b=$pz" \
        --scale "agent-zone-c=$pz" controller agent-zone-a agent-zone-b agent-zone-c >/dev/null 2>&1

    # 목표 연결 수 도달 대기(최대 90s)
    i=0
    while [ "$i" -lt 90 ]; do
        [ "$(metric_int "ddcs_connections ")" -ge "$want" ] && break
        sleep 1; i=$((i + 1))
    done

    # 측정 창: sweep 누적(sum/ticks)의 델타로 이 레벨의 평균 tick 시간을 낸다
    sum0=$(metric_int "ddcs_sweep_duration_us_sum")
    tk0=$(metric_int "ddcs_sweep_ticks_total")
    sleep "$SOAK"
    sum1=$(metric_int "ddcs_sweep_duration_us_sum")
    tk1=$(metric_int "ddcs_sweep_ticks_total")

    dtk=$((tk1 - tk0))
    avg=0
    [ "$dtk" -gt 0 ] && avg=$(((sum1 - sum0) / dtk))
    smax=$(metric_int "ddcs_sweep_duration_us_max")
    conns=$(metric_int "ddcs_connections ")
    pending=$(metric_int "ddcs_commands_pending ")
    evicted=$(metric_int "ddcs_agents_evicted_total ")
    rsum=$(metric_int "ddcs_command_rtt_ms_sum")
    rcnt=$(metric_int "ddcs_commands_completed_total")
    rtt=0
    [ "$rcnt" -gt 0 ] && rtt=$((rsum / rcnt))
    cpu=$(docker stats --no-stream --format '{{.CPUPerc}}' "$CTRL" 2>/dev/null | tr -d '%')

    printf '%-8s %-7s %-13s %-13s %-8s %-8s %-7s %-8s\n' \
        "$want" "$conns" "$avg" "$smax" "${cpu:-?}" "$pending" "$rtt" "$evicted"
done

narrate "해석"
info "sweep_avg_us/max 가 sweep 주기(1,000,000us=1s)에 근접 -> 단일 코어 포화."
info "여유가 크면 이론 한계 ~= 1,000,000 / (agent당 sweep us 증가분) 으로 외삽."
info "pending이 단조 증가하거나 evicted가 튀는 레벨이 실질 한계(컨트롤러가 못 따라감)."
