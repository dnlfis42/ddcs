#!/usr/bin/env bash
# DDCS 데모: fault injection -- agent를 얼리면(docker pause) liveness 침묵으로 컨트롤러가 축출하고,
# 재개하면(docker unpause) agent가 hangup을 관측해 3-way로 재접속한다.
# liveness 상실은 컨트롤러가 구동한다(agent엔 자체 liveness 타이머가 없다).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo-lib.sh"

COMPOSE=docker-compose.yml
DEV=11111111-1111-1111-1111-111111111111 # agent-01
arm_cleanup

narrate "시나리오: fault injection (liveness 축출 -> 재접속)"
stack_up controller agent-01 agent-02 agent-03 || exit 1

wait_for "agent 3대 연결" 40 '[ "$(metric_int "ddcs_connections ")" -ge 3 ]' || exit 1
soak "${DDCS_DEMO_SOAK:-8}" "정상 운영"

pre_evict=$(metric_int "ddcs_agents_evicted_total ")
pre_reg=$(register_count "$DEV")
cid=$(compose ps -q agent-01)

narrate "agent-01 정지 (docker pause) -- heartbeat 침묵 유발"
docker pause "$cid" >/dev/null
wait_for "컨트롤러 liveness 축출(연결 2로 감소)" 15 '[ "$(metric_int "ddcs_connections ")" -le 2 ]' || true
mid_evict=$(metric_int "ddcs_agents_evicted_total ")
mid_conn=$(metric_int "ddcs_connections ")
info "정지 중: connections=$mid_conn, evicted_total=$mid_evict"

narrate "agent-01 재개 (docker unpause) -- 재접속 기대"
docker unpause "$cid" >/dev/null
wait_for "재접속(연결 3 복구)" 25 '[ "$(metric_int "ddcs_connections ")" -ge 3 ]' || true
post_conn=$(metric_int "ddcs_connections ")
post_reg=$(register_count "$DEV")
info "재개 후: connections=$post_conn, register(${DEV:0:8})=$post_reg"

narrate "단언"
assert_ge "정지 중 liveness 축출 발생" "$mid_evict" $((pre_evict + 1))
assert_eq "정지 중 연결 수 감소" "$mid_conn" "2"
assert_ge "재개 후 연결 복구" "$post_conn" 3
assert_ge "device 재등록(재접속)" "$post_reg" $((pre_reg + 1))

summary
