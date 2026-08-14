#!/usr/bin/env bash
#
# 시나리오: regime-transition
#
# 그룹 평균 부하가 히스테리시스 밴드를 넘나들며 busy와 idle을 오가는지 검증한다.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

COMPOSE=docker-compose.yml # zone당 1대라 그룹 평균 = 그 device의 부하, 진동이 선명하다
arm_cleanup

narrate "시나리오: regime-transition (busy와 idle을 오간다)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1

wait_for "agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
soak "${DDCS_SCENARIO_SOAK:-90}" "부하 limit cycle 진동(밴드 양끝 교차)"

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
