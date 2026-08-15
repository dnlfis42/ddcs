#!/usr/bin/env bash
#
# DDCS 로컬 실행. Controller와 Agent, 관측 스택을 컨테이너로 띄우고 Ctrl-C까지 유지한다.
#
# 사용법: scripts/run.sh [Agent 수]   (기본 4)
#
#   4
#       docker-compose.yml       고정 DeviceId, zone당 1대
#   그 외
#       docker-compose.scale.yml zone 4개에 균등 분배 (4의 배수)
#
# 종료 상태:
#   0    정상 종료
#   1    실행 실패 (기동 실패, 연결 미달)
#   2    사용법 오류
#   130  Ctrl-C

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

count="${1:-4}"
case "$count" in
'' | *[!0-9]*)
    echo "사용법: $0 [Agent 수]" >&2
    exit 2
    ;;
esac
if [ "$count" -lt 1 ]; then
    echo "사용법: $0 [Agent 수]" >&2
    exit 2
fi

if [ "$count" -eq 4 ]; then
    COMPOSE=docker-compose.yml
    up_args=(controller agent-01 agent-02 agent-03 agent-04 prometheus grafana)
else
    if [ $((count % 4)) -ne 0 ]; then
        echo "오류: Agent 수는 4의 배수여야 합니다(zone 4개에 균등 분배)." >&2
        echo "사용법: $0 [Agent 수]" >&2
        exit 2
    fi
    COMPOSE=docker-compose.scale.yml
    per_zone=$((count / 4))
    up_args=(
        --scale "agent-zone-a=${per_zone}" --scale "agent-zone-b=${per_zone}"
        --scale "agent-zone-c=${per_zone}" --scale "agent-zone-d=${per_zone}"
        controller agent-zone-a agent-zone-b agent-zone-c agent-zone-d prometheus grafana
    )
fi

arm_cleanup

narrate "DDCS 실행: Agent ${count}대 (${COMPOSE})"
stack_up "${up_args[@]}" || exit 1
wait_for "Agent ${count}대 연결" 90 metric_at_least ddcs_connections "$count" || exit 1

narrate "기동 완료. Ctrl-C로 종료합니다."
info "메트릭       http://localhost:9000/metrics"
info "Prometheus   http://localhost:9090"
info "Grafana      http://localhost:3000"
info "장애 주입    docker pause <컨테이너> / docker unpause <컨테이너>"
info "정책 리로드  config/controller.json 편집 후 docker kill --signal=HUP ${CTRL}"
printf '\n'

# 백그라운드로 두고 wait으로 막는다. 포그라운드로 두면 스크립트만 신호를 받았을 때
# logs가 셸을 붙들어 EXIT 트랩이 늦고 스택이 남는다.
compose logs -f &
wait $!
