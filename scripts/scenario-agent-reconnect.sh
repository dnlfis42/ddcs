#!/usr/bin/env bash
#
# 시나리오: agent-reconnect
#
# 재시작한 Agent가 다시 접속하면 Device가 현재 목표 Mode를 다시 명령받는지 검증한다.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

COMPOSE=docker-compose.yml
DEV=11111111-1111-1111-1111-111111111111 # agent-01의 고정 DDCS_DEVICE_ID
arm_cleanup

narrate "시나리오: agent-reconnect (재접속 시 재명령)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1

wait_for "Agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
# 재시작 전에 첫 명령을 기다려 명령 기억을 만든다. 기억이 없으면 재명령과 첫 명령을 구별할 수 없다.
# 대기 상한은 SOAK 노브와 분리해 고정한다(SOAK을 줄였을 때 거짓 FAIL이 났던 지점).
wait_for "Device가 정책 Mode로 수렴(첫 명령 수신)" 60 dispatched_at_least "$DEV" 1 || true
soak 2 "명령 기억 정착"

pre_disp=$(dispatch_count "$DEV")
pre_reg=$(register_count "$DEV")
info "재시작 전: dispatch=$pre_disp, register=$pre_reg (Device ${DEV:0:8})"

cid=$(compose ps -q agent-01)
narrate "agent-01 재시작 (SimulatedDevice는 normal로 부팅)"
docker restart "$cid" >/dev/null

wait_for "재접속(재등록)" 25 registered_at_least "$DEV" $((pre_reg + 1)) || true
soak 4 "재명령 반영"

post_disp=$(dispatch_count "$DEV")
post_reg=$(register_count "$DEV")

narrate "Device ${DEV:0:8} 타임라인 (재접속 후 재명령):"
docker logs "$CTRL" 2>&1 | grep "$DEV" |
    grep -E '"event":"(session.connection.register.accept|command.dispatch)"|"reason":"kicked"' |
    tail -4 | sed "s/^/  ${C_D}/;s/$/${C_0}/"

narrate "단언"
assert_ge "재접속 시 재등록 발생" "$post_reg" $((pre_reg + 1))
assert_ge "재접속 후 재명령(command.dispatch 증가)" "$post_disp" $((pre_disp + 1))

summary
