#!/usr/bin/env bash

# 같은 Group에서 과열된 Device만 safe로 전환되고 회복하는지 검증한다.

# shellcheck source=scripts/lib/scenario.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/scenario.sh"

# shellcheck disable=SC2034 # lib/scenario.sh가 동적으로 읽는다.
SCENARIO_NAME=thermal
COMPOSE=docker-compose.scale.yml
PER_ZONE="${DDCS_SCENARIO_PER_ZONE-5}"
# 기능 검증은 총 100대까지만 사용한다. 대규모 부하는 Fleet으로 측정한다.
if ! [[ "$PER_ZONE" =~ ^[1-9][0-9]?$ ]] || [ "$PER_ZONE" -gt 25 ]; then
    echo "오류: DDCS_SCENARIO_PER_ZONE는 선행 0 없는 1..25 정수여야 합니다: $PER_ZONE" >&2
    exit 2
fi
arm_cleanup

narrate "시나리오: thermal (과열 Device만 safe)"
info "zone당 ${PER_ZONE}대 기동, Device별 과열 트립 관측"
EXPECTED=$((PER_ZONE * 4))
stack_up --scale "agent-zone-a=$PER_ZONE" --scale "agent-zone-b=$PER_ZONE" \
    --scale "agent-zone-c=$PER_ZONE" --scale "agent-zone-d=$PER_ZONE" \
    controller agent-zone-a agent-zone-b agent-zone-c agent-zone-d || exit 1

wait_for "Agent ${EXPECTED}대 연결" 60 metric_at_least ddcs_connections "$EXPECTED" || exit 1
soak 3 "연결 안정화"
info "연결된 Device 수: $(metric_int ddcs_connections)"

soak "${DDCS_SCENARIO_SOAK:-70}" "발열 누적: Device가 저마다의 시점에 hot_temp(65)를 넘김"

narrate "현재 Mode 분포 (ddcs_group_devices):"
scenario_metrics | grep '^ddcs_group_devices' | sort | sed "s/^/  ${C_D}/;s/$/${C_0}/"

info "약 28초간 분포 샘플링 (Device 단위 분기와 과열 회복 관측)"
mix_seen=0
recover_seen=0
declare -A prev_safe
for i in $(seq 1 14); do
    snap=$(scenario_metrics) || snap=
    # HTTP 오류·빈 응답은 인접 샘플 비교도 끊는다.
    if [ -z "$snap" ]; then
        prev_safe=()
        info "샘플 $i: 스냅샷 수집 실패, 건너뜀"
        sleep 2
        continue
    fi
    for z in zone_a zone_b zone_c zone_d; do
        s=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"safe\"}" | awk '{print $2}')
        p=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"performance\"}" | awk '{print $2}')
        # 잘못된 값을 0으로 읽으면 회복으로 오판하므로 연속 비교를 끊는다.
        if ! [[ "$s" =~ ^[0-9]+$ && "$p" =~ ^[0-9]+$ ]] ||
            [ "${#s}" -gt 5 ] || [ "${#p}" -gt 5 ]; then
            unset 'prev_safe[$z]'
            info "샘플 $i: $z Mode 메트릭 누락 또는 형식 오류, 건너뜀"
            continue
        fi
        if [ "$s" -ge 1 ] && [ "$p" -ge 1 ]; then
            mix_seen=1
            info "샘플 $i: $z performance=$p safe=$s (공존)"
        fi
        # 유효한 연속 샘플에서 safe 수 감소를 회복으로 판단한다.
        if [ -n "${prev_safe[$z]:-}" ] && [ "$s" -lt "${prev_safe[$z]}" ]; then
            recover_seen=1
            info "샘플 $i: $z safe ${prev_safe[$z]}에서 $s로 감소 (과열 회복)"
        fi
        prev_safe[$z]=$s
    done
    sleep 2
done
hot=$(hot_distinct)

narrate "검증"
assert_ge "모든 Device가 한 번 이상 과열 트립(thermal=hot)" "$hot" "$EXPECTED"
assert_ge "한 Group 안에 performance와 safe가 공존(과열 Device만 safe)" "$mix_seen" 1
assert_ge "과열 Device가 cool_temp 아래로 식어 Base Mode로 회복(safe 수 감소 관측)" "$recover_seen" 1

summary
