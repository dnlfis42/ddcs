#!/usr/bin/env bash

# 사용법: scripts/scenario/run.sh <thermal|agent-reconnect|regime-transition|liveness-eviction|policy-reload|all>
# 지정한 시나리오를 실행한다. all은 모든 시나리오를 차례로 실행한다.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

scenario_script() {
    if [ "$1" = thermal ]; then
        printf '%s\n' "$here/check-thermal-control.sh"
    else
        printf '%s\n' "$here/check-$1.sh"
    fi
}

case "${1:-}" in
thermal | agent-reconnect | regime-transition | liveness-eviction | policy-reload)
    exec "$(scenario_script "$1")"
    ;;
all)
    rc=0
    for s in thermal agent-reconnect regime-transition liveness-eviction policy-reload; do
        "$(scenario_script "$s")"
        st=$?
        # 중단 요청이 있으면 다음 시나리오를 실행하지 않는다.
        [ "$st" -eq 130 ] && exit 130
        [ "$st" -ne 0 ] && rc=1
    done
    exit "$rc"
    ;;
*)
    echo "사용법: $0 <thermal|agent-reconnect|regime-transition|liveness-eviction|policy-reload|all>"
    exit 2
    ;;
esac
