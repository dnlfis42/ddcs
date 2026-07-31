#!/usr/bin/env bash
# DDCS 데모: load regime. 그룹 평균 부하가 히스테리시스 밴드를 넘나들며 busy/idle을 전환한다.
# mode-구동 폐루프(performance가 부하를 빼고 normal이 쌓는다)라 부하가 limit cycle로 진동하고,
# high_load를 넘으면 busy(performance)로, low_load 아래로 내려가면 idle(normal)로 자연히 전환된다.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/demo-lib.sh"

COMPOSE=docker-compose.yml # zone당 1대라 그룹 평균 = 그 device의 부하, 진동이 선명하다
arm_cleanup

narrate "시나리오: load regime 전환 (busy <-> idle)"
stack_up controller agent-01 agent-02 agent-03 || exit 1

wait_for "agent 3대 연결" 40 '[ "$(metric_int "ddcs_connections ")" -ge 3 ]' || exit 1
soak "${DDCS_DEMO_SOAK:-90}" "부하 limit cycle 진동(밴드 양끝 교차)"

busy=$(logcount '"regime":"busy"')
idle=$(logcount '"regime":"idle"')

narrate "관측된 regime 전환 (policy.regime.update):"
docker logs "$CTRL" 2>&1 | grep '"event":"policy.regime.update"' |
    tail -8 | sed "s/^/  ${C_D}/;s/$/${C_0}/"
narrate "현재 그룹 평균 부하:"
curl -s --max-time 5 "$METRICS_URL" | grep '^ddcs_group_load_avg' | sort | sed "s/^/  ${C_D}/;s/$/${C_0}/"

narrate "단언"
assert_ge "busy 전환 발생(평균 부하가 high_load 초과)" "$busy" 1
assert_ge "idle 전환 발생(평균 부하가 low_load 미만)" "$idle" 1

summary
