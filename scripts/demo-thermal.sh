#!/usr/bin/env bash
# DDCS 데모: per-device thermal -- 같은 Group 안에서 과열된 device만 safe로 빠진다(걔만 safe).
# zone당 다수 device를 띄우고, 한 스냅샷에 performance와 safe가 동시에 존재함을 보인다
# (그룹 단위 트립이면 한 zone은 전부 같은 모드여야 한다 -> 동시 혼재 = device 단위 트립).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo-lib.sh"

COMPOSE=docker-compose.scale.yml
PER_ZONE="${DDCS_DEMO_PER_ZONE:-5}"
arm_cleanup

narrate "시나리오: per-device thermal (걔만 safe)"
info "zone당 ${PER_ZONE}대씩 띄워 개별 과열 트립을 관측한다"
EXPECTED=$((PER_ZONE * 3))
stack_up --scale "agent-zone-a=$PER_ZONE" --scale "agent-zone-b=$PER_ZONE" \
    --scale "agent-zone-c=$PER_ZONE" controller agent-zone-a agent-zone-b agent-zone-c || exit 1

wait_for "agent ${EXPECTED}대 연결" 60 \
    "[ \"\$(metric_int \"ddcs_connections \")\" -ge $EXPECTED ]" || exit 1
soak 3 "연결 안정화"
info "연결된 device 수: $(metric_int "ddcs_connections ")"

soak "${DDCS_DEMO_SOAK:-70}" "mode-구동 load/temp가 high_temp(65)에 도달하며 트립 누적"

narrate "현재 모드 분포 (ddcs_group_devices):"
curl -s --max-time 5 "$METRICS_URL" | grep '^ddcs_group_devices' | sort | sed "s/^/  ${C_D}/;s/$/${C_0}/"

# 분포 샘플링: (1) 한 zone에 performance와 safe가 동시 존재(=per-device 분기), 그리고 (2) zone의
# safe 수가 직전 스냅샷보다 줄어드는 순간(=과열 device가 resume_temp 아래로 식어 base mode로 회복).
# policy.cool 같은 회복 로그 이벤트가 없으므로 safe 수 감소가 회복의 유일한 런타임 witness다.
info "약 28초간 분포 샘플링 (per-device 분기 + 과열 회복 관측)"
mix_seen=0
recover_seen=0
declare -A prev_safe
for i in $(seq 1 14); do
    snap=$(curl -s --max-time 5 "$METRICS_URL") # 동시성 주장이라 한 스냅샷에서 같이 읽는다
    for z in zone_a zone_b zone_c; do
        s=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"safe\"}" | awk '{print $2}')
        p=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"performance\"}" | awk '{print $2}')
        s=${s:-0}
        p=${p:-0}
        if [ "$s" -ge 1 ] && [ "$p" -ge 1 ]; then
            mix_seen=1
            info "샘플 $i: $z performance=$p safe=$s (동시 혼재)"
        fi
        # safe 수가 직전보다 감소 = 그 device가 safe -> base mode로 회복(resume_temp 아래로 식음)
        if [ -n "${prev_safe[$z]:-}" ] && [ "$s" -lt "${prev_safe[$z]}" ]; then
            recover_seen=1
            info "샘플 $i: $z safe ${prev_safe[$z]} -> $s (과열 회복)"
        fi
        prev_safe[$z]=$s
    done
    sleep 2
done
hot=$(hot_distinct)

narrate "단언"
assert_ge "모든 device가 한 번 이상 개별 과열 트립(policy.hot)" "$hot" "$EXPECTED"
assert_ge "한 Group 안에 performance와 safe가 동시 존재(과열된 device만 safe)" "$mix_seen" 1
assert_ge "과열 device가 resume_temp 아래로 식어 base mode로 회복(safe 수 감소 관측)" "$recover_seen" 1

summary
