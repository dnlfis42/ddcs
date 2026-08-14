#!/usr/bin/env bash
#
# DDCS 검증 시나리오 러너
#
# 사용법: scripts/scenario.sh <thermal|agent-reconnect|regime-transition|liveness-eviction|policy-reload|all>
#
# 각 시나리오는 자기 스택을 띄웠다가 정리하고 종료 상태를 반환한다.
# 각 시나리오가 무엇을 검증하는지는 docs/SCENARIO.md에 작성하였다.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "${1:-}" in
thermal | agent-reconnect | regime-transition | liveness-eviction | policy-reload)
    exec "$here/scenario-$1.sh"
    ;;
all)
    rc=0
    for s in thermal agent-reconnect regime-transition liveness-eviction policy-reload; do
        "$here/scenario-$s.sh"
        st=$?
        # Ctrl-C는 러너와 시나리오 모두에게 SIGINT로 전달된다. 시나리오는 트랩으로
        # 스택을 정리한 뒤 종료 상태 130(128 + SIGINT 번호 2)을 남기는데, 러너를
        # 돌리는 bash는 이를 정상 종료로 보고 다음 시나리오를 실행한다. 따라서 러너가
        # 130을 받으면 자신도 종료 상태 130을 남긴다. 0이나 1을 남기면 호출자에게
        # 중단을 성공이나 실패로 잘못 알리는 셈이다.
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
