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

# 10초간 샘플링: 한 zone 안에 performance와 safe가 동시 존재하는 순간을 잡는다.
info "10초간 분포 샘플링 (per-device 분기 관측)"
mix_seen=0
for i in 1 2 3 4 5; do
    snap=$(curl -s --max-time 5 "$METRICS_URL") # 동시성 주장이라 한 스냅샷에서 둘 다 읽는다
    for z in zone_a zone_b zone_c; do
        s=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"safe\"}" | awk '{print $2}')
        p=$(printf '%s\n' "$snap" | grep -F "ddcs_group_devices{group=\"$z\",mode=\"performance\"}" | awk '{print $2}')
        if [ "${s:-0}" -ge 1 ] && [ "${p:-0}" -ge 1 ]; then
            mix_seen=1
            info "샘플 $i: $z performance=$p safe=$s (동시 혼재)"
        fi
    done
    sleep 2
done
hot=$(hot_distinct)

narrate "단언"
assert_ge "모든 device가 한 번 이상 개별 과열 트립(policy.hot)" "$hot" "$EXPECTED"
assert_ge "한 Group 안에 performance와 safe가 동시 존재(과열된 device만 safe)" "$mix_seen" 1

summary
