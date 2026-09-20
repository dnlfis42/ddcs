#!/usr/bin/env bash

# Group 부하가 히스테리시스 밴드를 넘을 때 busy/idle 전환을 검증한다.

# shellcheck source=scripts/lib/scenario.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/scenario.sh"

# shellcheck disable=SC2034 # lib/scenario.sh가 동적으로 읽는다.
SCENARIO_NAME=regime-transition
COMPOSE=docker-compose.yml # zone당 1대라 Group 평균이 곧 그 Device의 부하, 진동이 선명하다
arm_cleanup

narrate "시나리오: regime-transition (부하 밴드 전환)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1

wait_for "Agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
soak "${DDCS_SCENARIO_SOAK:-90}" "부하 진동 관측: 밴드 양끝 교차"

busy=$(logcount '"regime":"busy"')
idle=$(logcount '"regime":"idle"')

narrate "관측된 Regime 전환 (policy.regime.update):"
docker logs "$CTRL" 2>&1 | grep '"event":"policy.regime.update"' |
    tail -8 | sed "s/^/  ${C_D}/;s/$/${C_0}/"
narrate "현재 Group 평균 load ratio (0–1):"
scenario_metrics | grep '^ddcs_group_load_ratio' | sort | sed "s/^/  ${C_D}/;s/$/${C_0}/"

narrate "검증"
assert_ge "busy 전환 발생(평균 부하가 busy_load 초과)" "$busy" 1
assert_ge "idle 전환 발생(평균 부하가 idle_load 미만)" "$idle" 1

summary
