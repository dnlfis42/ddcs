#!/usr/bin/env bash

# SIGHUP 정책 교체와 잘못된 JSON 거부를 검증한다. 원본 설정은 변경하지 않는다.

# shellcheck source=scripts/lib/scenario.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/scenario.sh"

# shellcheck disable=SC2034 # lib/scenario.sh가 동적으로 읽는다.
SCENARIO_NAME=policy-reload
COMPOSE=docker-compose.yml
COMPOSE_OVERLAY=docker-compose.policy-reload.yml
POLICY_CONFIG_DIR="$(mktemp -d /tmp/ddcs-policy-reload.XXXXXX)" || exit 1
export DDCS_POLICY_CONFIG_DIR="$POLICY_CONFIG_DIR"
CFG="$POLICY_CONFIG_DIR/controller.json"

# 마운트된 설정 사본은 스택 정리 성공 뒤에만 제거한다.
arm_cleanup
policy_reload_cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if stack_down; then
        rm -rf -- "$POLICY_CONFIG_DIR" || status=1
    else
        echo "스택 정리 실패: 임시 설정을 보존합니다: $POLICY_CONFIG_DIR" >&2
        status=1
    fi
    scenario_finalize_exit "$status"
}
trap 'policy_reload_cleanup' EXIT
cp -RL "$ROOT/config/." "$POLICY_CONFIG_DIR/" || exit 1

dispatch_total() { logcount '"event":"command.dispatch"'; }
load_total() { logcount '"event":"policy.load"'; } # 성공만 센다. 끝의 " 때문에 policy.load.fail은 안 잡힌다
zone_a_safe() {
    scenario_metrics |
        grep -F 'ddcs_group_devices{group="zone_a",mode="safe"}' | awk '{print $2}'
}
reload_seen() { [ "$(logcount '"trigger":"reload"')" -ge "${1:-1}" ]; }
parsefail_seen() { [ "$(logcount '"reason":"parse"')" -ge "${1:-1}" ]; }
dispatch_total_at_least() { [ "$(dispatch_total)" -ge "$1" ]; }
load_total_at_least() { [ "$(load_total)" -ge "$1" ]; }
zone_a_safe_at_least() { [ "$(zone_a_safe)" -ge "$1" ]; }

show_modes() {
    scenario_metrics | grep '^ddcs_group_devices' | sort |
        sed "s/^/  ${C_D}/;s/$/${C_0}/"
}

narrate "시나리오: policy-reload (SIGHUP 정책 교체와 잘못된 형식 거부)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1

wait_for "Agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
# 첫 명령 이후 reload해야 명령 기억을 비우고 재명령했는지 구별할 수 있다.
wait_for "Device가 정책 Mode로 수렴(첫 명령)" 40 dispatch_total_at_least 4 || exit 1
soak 2 "명령 기억 정착"

pre_disp=$(dispatch_total)
pre_load=$(load_total) # 부팅 시 1
info "reload 전: command.dispatch=$pre_disp, policy.load=$pre_load, zone_a.safe=$(zone_a_safe)"
narrate "reload 전 Mode 분포:"
show_modes

# zone_a의 목표를 safe로 고정해 reload 효과를 구별한다.
narrate "PHASE 1: zone_a를 safe로 강제하는 정책으로 편집 후 SIGHUP"
cat >"$CFG" <<'JSON'
{
  "policy": {
    "groups": {
      "zone_a": {"busy_load": 70, "idle_load": 30, "busy_mode": "safe", "idle_mode": "safe", "hot_temp": 65, "cool_temp": 50, "hot_mode": "safe"},
      "zone_b": {"busy_load": 60, "idle_load": 45, "busy_mode": "performance", "idle_mode": "normal", "hot_temp": 65, "cool_temp": 50, "hot_mode": "safe"},
      "zone_c": {"busy_load": 80, "idle_load": 20, "busy_mode": "performance", "idle_mode": "normal", "hot_temp": 65, "cool_temp": 50, "hot_mode": "safe"},
      "zone_d": {"busy_load": 75, "idle_load": 40, "busy_mode": "performance", "idle_mode": "normal", "hot_temp": 65, "cool_temp": 50, "hot_mode": "safe"}
    }
  }
}
JSON
scenario_snapshot_config valid-reload || exit 1
docker kill --signal=HUP "$CTRL" >/dev/null

wait_for "SIGHUP 처리(trigger=reload)" 10 reload_seen 1 || true
wait_for "새 정책 재적용(policy.load 재발생)" 10 load_total_at_least $((pre_load + 1)) || true
wait_for "zone_a가 reload로 safe 재명령" 20 zone_a_safe_at_least 1 || true
# 일시적 과열과 구별하기 위해 3회 연속 safe를 확인한다.
za_persist=0
for _ in 1 2 3; do
    [ "$(zone_a_safe)" -ge 1 ] && za_persist=$((za_persist + 1))
    sleep 2
done

post_load=$(load_total)
info "reload 후: command.dispatch=$(dispatch_total)(전 $pre_disp), policy.load=$post_load, zone_a safe 연속=$za_persist/3"
narrate "reload 후 Mode 분포 (zone_a 전부 safe 기대):"
show_modes

# 잘못된 JSON은 거부하고 기존 정책을 유지해야 한다.
narrate "PHASE 2: 깨진 JSON으로 편집 후 SIGHUP (거부 기대)"
mid_load=$(load_total)
printf '{ this is not valid json\n' >"$CFG"
scenario_snapshot_config invalid-reload || exit 1
docker kill --signal=HUP "$CTRL" >/dev/null

wait_for "잘못된 형식 거부(reason=parse)" 10 parsefail_seen 1 || true
soak 2 "옛 정책 유지 확인"
after_bad_load=$(load_total)
after_bad_conn=$(metric_int ddcs_connections)
info "잘못된 편집 후: policy.load(성공)=$after_bad_load, connections=$after_bad_conn"

narrate "검증"
assert_ge "SIGHUP이 reload를 트리거(trigger=reload)" "$(logcount '"trigger":"reload"')" 1
assert_ge "유효 reload가 새 정책을 재적용(policy.load 재발생)" "$post_load" $((pre_load + 1))
assert_ge "재적용이 동작 중 fleet을 재명령(zone_a가 강제된 safe로 정착)" "$za_persist" 3
assert_ge "잘못된 형식의 편집을 거부(policy.load.fail reason=parse)" "$(logcount '"reason":"parse"')" 1
assert_eq "거부 후 옛 정책 유지(성공 load 미증가)" "$after_bad_load" "$mid_load"
assert_ge "잘못된 편집에도 fleet 생존(연결 유지)" "$after_bad_conn" 4

summary
