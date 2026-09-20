#!/usr/bin/env bash

# 고정된 safe 정책에서 재등록→재명령→성공과 보고된 실제 Mode를 검증한다.

# shellcheck source=scripts/lib/scenario.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/scenario.sh"

# shellcheck disable=SC2034 # lib/scenario.sh가 동적으로 읽는다.
SCENARIO_NAME=agent-reconnect
COMPOSE=docker-compose.yml
COMPOSE_OVERLAY=docker-compose.policy-reload.yml
DEV=11111111-1111-1111-1111-111111111111 # zone_a 유일 Device, agent-01
arm_cleanup
# 부하·과열 변화가 같은 시점에 명령을 유발하는 경우와 구분한다. 원본 설정은 보존한다.
SCENARIO_TEMP_CONFIG_DIR="$(mktemp -d /tmp/ddcs-reconnect.XXXXXX)" || exit 1
export DDCS_POLICY_CONFIG_DIR="$SCENARIO_TEMP_CONFIG_DIR"
cp -RL "$ROOT/config/." "$SCENARIO_TEMP_CONFIG_DIR/" || exit 1
jq '.policy.groups.zone_a |= (.busy_mode="safe" | .idle_mode="safe" | .hot_mode="safe")' \
    "$ROOT/config/controller.json" >"$SCENARIO_TEMP_CONFIG_DIR/controller.json" || exit 1

completed_after_registration() {
    docker logs "$CTRL" 2>&1 | scenario_reconnect_chain "$DEV" "$1"
}
zone_a_is_safe() {
    scenario_metrics | awk '
        $1 == "ddcs_group_devices{group=\"zone_a\",mode=\"safe\"}" { safe=$2; ns++ }
        $1 == "ddcs_group_devices{group=\"zone_a\",mode=\"normal\"}" { normal=$2; nn++ }
        $1 == "ddcs_group_devices{group=\"zone_a\",mode=\"performance\"}" { performance=$2; np++ }
        END { exit !(ns == 1 && nn == 1 && np == 1 && safe == "1" && normal == "0" && performance == "0") }'
}

narrate "시나리오: agent-reconnect (고정 safe 정책의 재접속 복구)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1
wait_for "Agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
wait_for "첫 등록 뒤 명령 성공" 40 completed_after_registration 1 || exit 1
wait_for "zone_a Device가 safe를 보고" 15 zone_a_is_safe || exit 1
pre_reg=$(register_count "$DEV")
info "재시작 전: register=$pre_reg, zone_a는 safe 고정 정책"
if [ -n "$_SCENARIO_RUN_DIR" ]; then
    completed_after_registration 1 >"$_SCENARIO_RUN_DIR/reconnect-before.json" || exit 1
fi
cid=$(compose ps -q agent-01) || exit 1
[ -n "$cid" ] || exit 1
narrate "agent-01 재시작 (같은 Device ID, normal로 부팅)"
docker restart "$cid" >/dev/null || exit 1

reconnected=0
completed=0
safe_reported=0
wait_for "재등록" 25 registered_at_least "$DEV" $((pre_reg + 1)) && reconnected=1
wait_for "재등록 이후 dispatch와 같은 command_id의 complete" 25 \
    completed_after_registration $((pre_reg + 1)) && completed=1
wait_for "재명령 성공 후 zone_a의 실제 safe 보고" 15 zone_a_is_safe && safe_reported=1
if [ "$completed" -eq 1 ] && [ -n "$_SCENARIO_RUN_DIR" ]; then
    completed_after_registration $((pre_reg + 1)) >"$_SCENARIO_RUN_DIR/reconnect-after.json" || exit 1
fi

narrate "검증"
assert_eq "동일 Device의 재등록" "$reconnected" 1
assert_eq "재등록 뒤 발행한 명령이 동일 command_id로 성공" "$completed" 1
assert_eq "유일한 zone_a Device가 목표 safe Mode를 보고" "$safe_reported" 1
summary
