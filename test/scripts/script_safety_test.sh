#!/usr/bin/env bash
set -u
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d /tmp/ddcs-script-safety.XXXXXX)" || exit 1
trap 'rm -rf -- "$test_dir"' EXIT
export SAFETY_CALLS="$test_dir/calls"
failures=0

# shellcheck disable=SC2317 # export한 함수를 자식 Bash가 호출한다.
docker() { printf '%s\n' "$*" >>"$SAFETY_CALLS"; return 1; }
export -f docker
for count in unset 1 5 25 '' 0 00 01 08 26 1000 65536 999999999999999999999 -1 +1 1.5 ' 5' '5 ' '1+1'; do
    : >"$SAFETY_CALLS"
    status=0
    if [ "$count" = unset ]; then
        env -u DDCS_SCENARIO_PER_ZONE bash "$REPO_ROOT/scripts/scenario/check-thermal-control.sh" \
            >"$test_dir/thermal-output" 2>&1 || status=$?
    else
        env DDCS_SCENARIO_PER_ZONE="$count" bash "$REPO_ROOT/scripts/scenario/check-thermal-control.sh" \
            >"$test_dir/thermal-output" 2>&1 || status=$?
    fi
    expected=2; calls=0
    case "$count" in unset | 1 | 5 | 25) expected=1; calls=1 ;; esac
    if [ "$status" -eq "$expected" ] && [ "$(wc -l <"$SAFETY_CALLS")" -eq "$calls" ]; then
        printf 'PASS 과열 제어의 Agent 수 입력 검증: <%s>\n' "$count"
    else
        printf 'FAIL 과열 제어의 Agent 수 입력 검증: <%s> (종료 코드=%s, 호출 수=%s)\n' "$count" "$status" "$(wc -l <"$SAFETY_CALLS")"
        failures=$((failures + 1))
    fi
done

docker() {
    case "$*" in
    'ps -q') if [ "$SAFETY_CASE" = running ]; then printf 'user-container\n'; fi ;;
    --version) printf 'Docker mock\n' ;;
    *) printf '예상하지 않은 Docker 호출: %s\n' "$*" >>"$SAFETY_CALLS"; return 1 ;;
    esac
}
pgrep() { printf '2\n'; }
getconf() { printf '0\n'; }
sudo() { printf '예상하지 않은 sudo 호출: %s\n' "$*" >>"$SAFETY_CALLS"; return 1; }
export -f docker pgrep getconf sudo
for test_case in orphan running; do
    export SAFETY_CASE="$test_case"
    : >"$SAFETY_CALLS"
    status=0
    bash "$REPO_ROOT/scripts/measurement/verify-environment.sh" fleet >"$test_dir/preflight-output" 2>&1 || status=$?
    if [ "$status" -eq 1 ] && [ ! -s "$SAFETY_CALLS" ] &&
        ! grep -Eq 'pkill|killall|systemctl restart|docker compose .* down' "$test_dir/preflight-output" &&
        grep -Fq 'docker ps -a --no-trunc' "$test_dir/preflight-output" &&
        grep -Fq 'pgrep -af' "$test_dir/preflight-output"; then
        printf 'PASS 환경 검사 조치 안내: %s\n' "$test_case"
    else
        printf 'FAIL 환경 검사 조치 안내: %s\n' "$test_case"
        failures=$((failures + 1))
    fi
done
[ "$failures" -eq 0 ]
