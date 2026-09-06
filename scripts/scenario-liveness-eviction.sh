#!/usr/bin/env bash
#
# 시나리오: liveness-eviction
#
# heartbeat가 끊긴 Agent를 Controller가 liveness로 축출하고, 재개하면 다시 접속하는지 검증한다.

# shellcheck source=scripts/scenario-lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

# shellcheck disable=SC2034 # scenario-lib.sh가 동적으로 읽는다.
SCENARIO_NAME=liveness-eviction
COMPOSE=docker-compose.yml
DEV=11111111-1111-1111-1111-111111111111 # agent-01
arm_cleanup

narrate "시나리오: liveness-eviction (침묵 축출 후 재접속)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1

wait_for "Agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
soak "${DDCS_SCENARIO_SOAK:-8}" "정상 운영"

pre_liveness_closed=$(metric_reason_int ddcs_connections_closed_total liveness_expired)
pre_reg=$(register_count "$DEV")
cid=$(compose ps -q agent-01)

narrate "agent-01 정지 (docker pause, heartbeat 중단)"
docker pause "$cid" >/dev/null
wait_for "Controller liveness 축출(연결 3으로 감소)" 15 metric_at_most ddcs_connections 3 || true
mid_liveness_closed=$(metric_reason_int ddcs_connections_closed_total liveness_expired)
mid_conn=$(metric_int ddcs_connections)
info "정지 중: connections=$mid_conn, liveness_closed_total=$mid_liveness_closed"

narrate "agent-01 재개 (docker unpause)"
docker unpause "$cid" >/dev/null
wait_for "재접속(연결 4 복구)" 25 metric_at_least ddcs_connections 4 || true
post_conn=$(metric_int ddcs_connections)
post_reg=$(register_count "$DEV")
info "재개 후: connections=$post_conn, register(${DEV:0:8})=$post_reg"

narrate "단언"
assert_ge "정지 중 liveness 축출 발생" "$mid_liveness_closed" $((pre_liveness_closed + 1))
assert_eq "정지 중 연결 수 감소" "$mid_conn" "3"
assert_ge "재개 후 연결 복구" "$post_conn" 4
assert_ge "Device 재등록(재접속)" "$post_reg" $((pre_reg + 1))

summary
