#!/usr/bin/env bash

# DDCS 데모 시나리오 러너
#
# 사용법: scripts/demo.sh <scenario>
#
# scenario 값:
# - thermal  : per-device thermal (걔만 safe)
# - eviction : 재접속 시 재명령 (DeviceReleaseSink)
# - regime   : 부하 밴드 busy <-> idle 전환
# - fault    : liveness 축출 -> 재접속 (docker pause/unpause)
# - all      : 위 4개를 순차 실행
#
# 각 시나리오는 자기 스택을 띄웠다 정리하며, 끝에 PASS/FAIL과 종료코드를 낸다.
# 관측 스택 없이 raw 메트릭 :9000으로 단언한다. Grafana로 보려면 docker/README의 compose를 직접 띄울 것.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "${1:-}" in
thermal | eviction | regime | fault)
    exec "$here/demo-$1.sh"
    ;;
all)
    rc=0
    for s in thermal eviction regime fault; do
        "$here/demo-$s.sh" || rc=1
    done
    exit "$rc"
    ;;
*)
    echo "사용법: $0 <thermal|eviction|regime|fault|all>"
    exit 2
    ;;
esac
