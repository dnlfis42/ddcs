#!/usr/bin/env bash
#
# 시나리오: policy-reload
#
# SIGHUP으로 정책을 재시작 없이 교체하고, 형식이 깨진 편집은 거부하는지 검증한다.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

COMPOSE=docker-compose.yml
CFG="$ROOT/config/controller.json" # 컨테이너가 /config로 bind-mount해 읽는 바로 그 파일
BAK="$(mktemp)"
cp "$CFG" "$BAK" || {
    echo "오류: 설정 백업에 실패했습니다: $CFG" >&2
    exit 1
}

# arm_cleanup의 EXIT 트랩(stack_down)에 더해 config 원복까지 한다. INT/TERM은 arm_cleanup이
# exit 130으로 바꿔 이 EXIT 트랩을 한 번 태운다(SIGKILL이 아닌 한 repo를 더럽히지 않는다).
arm_cleanup
trap 'cp "$BAK" "$CFG" && rm -f "$BAK"; stack_down' EXIT

# wait_for와 단언에 쓸 조건 함수다.
dispatch_total() { logcount '"event":"command.dispatch"'; }
load_total() { logcount '"event":"policy.load"'; } # 성공만 센다. 끝의 " 때문에 policy.load.fail은 안 잡힌다
zone_a_safe() {
    curl -s --max-time 5 "$METRICS_URL" |
        grep -F 'ddcs_group_devices{group="zone_a",mode="safe"}' | awk '{print $2}'
}
reload_seen() { [ "$(logcount '"trigger":"reload"')" -ge "${1:-1}" ]; }
parsefail_seen() { [ "$(logcount '"reason":"parse"')" -ge "${1:-1}" ]; }
dispatch_total_at_least() { [ "$(dispatch_total)" -ge "$1" ]; }
load_total_at_least() { [ "$(load_total)" -ge "$1" ]; }
zone_a_safe_at_least() { [ "$(zone_a_safe)" -ge "$1" ]; }

show_modes() {
    curl -s --max-time 5 "$METRICS_URL" | grep '^ddcs_group_devices' | sort |
        sed "s/^/  ${C_D}/;s/$/${C_0}/"
}

narrate "시나리오: policy-reload (SIGHUP 재명령 + malformed 거부)"
stack_up controller agent-01 agent-02 agent-03 agent-04 || exit 1

wait_for "agent 4대 연결" 40 metric_at_least ddcs_connections 4 || exit 1
# device들이 부팅 정책으로 한 번이라도 명령받아 명령 기억을 갖게 한다. 그래야 reload가
# 기억을 비우고 다시 명령하는 과정을 단언이 실제로 짚는다.
# 대기 상한은 soak 노브와 분리한다(eviction과 같은 이유).
wait_for "device가 정책 모드로 수렴(첫 명령)" 40 dispatch_total_at_least 4 || true
soak 2 "명령 기억 정착"

pre_disp=$(dispatch_total)
pre_load=$(load_total) # 부팅 시 1
info "reload 전: command.dispatch=$pre_disp, policy.load=$pre_load, zone_a.safe=$(zone_a_safe)"
narrate "reload 전 모드 분포:"
show_modes

# PHASE 1: 유효한 편집 후 SIGHUP. zone_a를 busy/idle 모두 safe로 강제해 결과를 결정적으로 만든다.
narrate "PHASE 1: zone_a를 safe로 강제하는 정책으로 편집 후 SIGHUP"
cat >"$CFG" <<'JSON'
{
  "policy": {
    "groups": {
      "zone_a": {"high_load": 70, "low_load": 30, "high_load_mode": "safe", "low_load_mode": "safe", "high_temp": 65, "resume_temp": 50, "high_temp_mode": "safe"},
      "zone_b": {"high_load": 60, "low_load": 45, "high_load_mode": "performance", "low_load_mode": "normal", "high_temp": 65, "resume_temp": 50, "high_temp_mode": "safe"},
      "zone_c": {"high_load": 80, "low_load": 20, "high_load_mode": "performance", "low_load_mode": "normal", "high_temp": 65, "resume_temp": 50, "high_temp_mode": "safe"},
      "zone_d": {"high_load": 75, "low_load": 40, "high_load_mode": "performance", "low_load_mode": "normal", "high_temp": 65, "resume_temp": 50, "high_temp_mode": "safe"}
    }
  }
}
JSON
docker kill --signal=HUP "$CTRL" >/dev/null

wait_for "SIGHUP 처리(trigger=reload)" 10 reload_seen 1 || true
wait_for "새 정책 재적용(policy.load 재발생)" 10 load_total_at_least $((pre_load + 1)) || true
wait_for "zone_a가 reload로 safe 재명령" 20 zone_a_safe_at_least 1 || true
# 단발 safe는 우연일 수 있다. 3회 연속이면 정책이 강제한 것으로 본다.
za_persist=0
for _ in 1 2 3; do
    [ "$(zone_a_safe)" -ge 1 ] && za_persist=$((za_persist + 1))
    sleep 2
done

post_load=$(load_total)
info "reload 후: command.dispatch=$(dispatch_total)(전 $pre_disp), policy.load=$post_load, zone_a safe 연속=$za_persist/3"
narrate "reload 후 모드 분포 (zone_a 전부 safe 기대):"
show_modes

# PHASE 2: malformed 편집 후 SIGHUP을 보내 거부와 옛 정책 유지를 확인한다.
narrate "PHASE 2: 깨진 JSON으로 편집 후 SIGHUP (거부 기대)"
mid_load=$(load_total)
printf '{ this is not valid json\n' >"$CFG"
docker kill --signal=HUP "$CTRL" >/dev/null

wait_for "malformed 거부(reason=parse)" 10 parsefail_seen 1 || true
soak 2 "옛 정책 유지 확인"
after_bad_load=$(load_total)
after_bad_conn=$(metric_int ddcs_connections)
info "malformed 후: policy.load(성공)=$after_bad_load, connections=$after_bad_conn"

narrate "단언"
assert_ge "SIGHUP이 reload를 트리거(trigger=reload)" "$(logcount '"trigger":"reload"')" 1
assert_ge "유효 reload가 새 정책을 재적용(policy.load 재발생)" "$post_load" $((pre_load + 1))
assert_ge "재적용이 동작 중 fleet을 재명령(zone_a가 강제된 safe로 정착, e2e)" "$za_persist" 3
assert_ge "malformed 편집을 거부(policy.load.fail reason=parse)" "$(logcount '"reason":"parse"')" 1
assert_eq "malformed는 옛 정책 유지(성공 load 미증가)" "$after_bad_load" "$mid_load"
assert_ge "malformed에도 fleet 생존(연결 유지)" "$after_bad_conn" 4

summary
