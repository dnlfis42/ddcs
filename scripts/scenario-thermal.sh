#!/usr/bin/env bash
#
# 시나리오: thermal
#
# 같은 zone에서 과열된 Device만 safe로 빠지는지(Device마다 따로 판정) 검증한다.

# shellcheck source=scripts/scenario-lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

# shellcheck disable=SC2034 # scenario-lib.sh가 동적으로 읽는다.
SCENARIO_NAME=thermal
COMPOSE=docker-compose.scale.yml
PER_ZONE="${DDCS_SCENARIO_PER_ZONE:-5}"
case "$PER_ZONE" in
'' | 0 | *[!0-9]*)
    echo "오류: DDCS_SCENARIO_PER_ZONE는 양의 정수여야 합니다: $PER_ZONE" >&2
    exit 2 ;;
esac
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
curl -s --max-time 5 "$METRICS_URL" | grep '^ddcs_group_devices' | sort | sed "s/^/  ${C_D}/;s/$/${C_0}/"

# 한 스냅샷 안의 공존(Device 단위 분기)과 safe 수 감소(회복)를 찾는다.
info "약 28초간 분포 샘플링 (Device 단위 분기와 과열 회복 관측)"
mix_seen=0
recover_seen=0
declare -A prev_safe
for i in $(seq 1 14); do
    snap=$(curl -s --max-time 5 "$METRICS_URL") # 공존 단언에는 같은 순간의 두 값이 필요하므로 한 스냅샷에서 같이 읽는다
    # 빈 스냅샷을 0으로 읽으면 safe 수 감소로 오판해 회복 단언이 거짓 통과한다. 샘플을 버린다.
    if [ -z "$snap" ]; then
        info "샘플 $i: 스냅샷 수집 실패, 건너뜀"
        sleep 2
        continue
    fi
    for z in zone_a zone_b zone_c zone_d; do
        s=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"safe\"}" | awk '{print $2}')
        p=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"performance\"}" | awk '{print $2}')
        s=${s:-0}
        p=${p:-0}
        if [ "$s" -ge 1 ] && [ "$p" -ge 1 ]; then
            mix_seen=1
            info "샘플 $i: $z performance=$p safe=$s (공존)"
        fi
        # safe 수가 직전보다 줄면 그 Device가 cool_temp 아래로 식어 Base Mode로 복귀했다고 본다
        if [ -n "${prev_safe[$z]:-}" ] && [ "$s" -lt "${prev_safe[$z]}" ]; then
            recover_seen=1
            info "샘플 $i: $z safe ${prev_safe[$z]}에서 $s로 감소 (과열 회복)"
        fi
        prev_safe[$z]=$s
    done
    sleep 2
done
hot=$(hot_distinct)

narrate "단언"
assert_ge "모든 Device가 한 번 이상 과열 트립(thermal=hot)" "$hot" "$EXPECTED"
assert_ge "한 Group 안에 performance와 safe가 공존(과열 Device만 safe)" "$mix_seen" 1
assert_ge "과열 Device가 cool_temp 아래로 식어 Base Mode로 회복(safe 수 감소 관측)" "$recover_seen" 1

summary
