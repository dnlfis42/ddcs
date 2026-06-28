#!/usr/bin/env bash
# DDCS 데모: eviction -- 같은 DeviceId로 재접속한(리부트된) device를 컨트롤러가 재명령한다.
# 세션 종료 시 PolicyService가 그 device의 명령 belief를 폐기(DeviceReleaseSink)하므로,
# normal로 리부트된 agent도 다음 평가에서 반드시 현재 effective로 재명령된다(stale belief 고착 방지).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo-lib.sh"

COMPOSE=docker-compose.yml
DEV=11111111-1111-1111-1111-111111111111 # agent-01 의 고정 DDCS_DEVICE_ID
arm_cleanup

narrate "시나리오: eviction (재접속 시 재명령)"
stack_up controller agent-01 agent-02 agent-03 || exit 1

wait_for "agent 3대 연결" 40 '[ "$(metric_int "ddcs_connections ")" -ge 3 ]' || exit 1
# device가 정책 모드로 한 번이라도 명령받게 한다(belief 형성). 그래야 재접속 시
# "stale belief 때문에 재명령이 생략되던" 버그 시나리오를 비-vacuous하게 보인다.
wait_for "device가 정책 모드로 수렴(첫 명령 수신)" "${DDCS_DEMO_SOAK:-60}" \
    "[ \"\$(dispatch_count $DEV)\" -ge 1 ]" || true
soak 2 "belief 안정화"

pre_disp=$(dispatch_count "$DEV")
pre_reg=$(register_count "$DEV")
info "재시작 전: dispatch=$pre_disp, register=$pre_reg (device ${DEV:0:8})"

cid=$(compose ps -q agent-01)
narrate "agent-01 재시작 (SimulatedDevice가 normal로 리부트)"
docker restart "$cid" >/dev/null

wait_for "재접속(재등록)" 25 "[ \"\$(register_count $DEV)\" -ge $((pre_reg + 1)) ]" || true
soak 4 "재명령 반영"

post_disp=$(dispatch_count "$DEV")
post_reg=$(register_count "$DEV")

narrate "device ${DEV:0:8} 타임라인 (재접속 -> 재명령):"
docker logs "$CTRL" 2>&1 | grep "$DEV" |
    grep -E '"event":"(session.registered|session.kick_old|command.dispatch)"' |
    tail -4 | sed "s/^/  ${C_D}/;s/$/${C_0}/"

narrate "단언"
assert_ge "재접속 시 재등록 발생" "$post_reg" $((pre_reg + 1))
assert_ge "재접속 후 재명령(command.dispatch 증가)" "$post_disp" $((pre_disp + 1))

summary
