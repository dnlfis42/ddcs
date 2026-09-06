#!/usr/bin/env bash
#
# 도는 스택에 장애를 주입한다. run.sh나 docker compose로 이미 띄운 스택을 대상으로 하며,
# 스택을 세우지도 정리하지도 않는다.
#
# 사용법: scripts/fault.sh [동작] [대상]   (기본: cycle agent-01)
#
# 동작:
#   cycle    pause 후 축출을, resume 후 복구를 확인한다. 캡처용 시간 범위를 출력한다
#   pause    무응답 유발. 소켓은 열린 채 침묵하므로 liveness 축출 경로를 지난다
#   resume   pause 해제
#   restart  재시작. 재접속과 재명령 경로를 지난다
#   kill     SIGKILL. 연결이 닫혀 즉시 감지되므로 liveness 경로가 아니다
#
# 대상은 compose 파일에 적힌 이름이다. 컨테이너 이름(docker-agent-01-1)이 아니다.
#   docker-compose.yml         agent-01 agent-02 agent-03 agent-04
#   docker-compose.scale.yml   agent-zone-a agent-zone-b agent-zone-c agent-zone-d
#
# 환경 변수:
#   COMPOSE=docker-compose.scale.yml   scale 스택을 대상으로 할 때
#
# 종료 상태:
#   0    정상
#   1    실행 실패 (스택이 없거나 서비스를 찾지 못함)
#   2    사용법 오류

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenario-lib.sh"

action="${1:-cycle}"
target="${2:-agent-01}"

case "$action" in
cycle | pause | resume | restart | kill) ;;
*)
    echo "사용법: $0 [cycle|pause|resume|restart|kill] [대상]" >&2
    exit 2
    ;;
esac

if ! curl -s --max-time 3 "$METRICS_URL" >/dev/null 2>&1; then
    echo "오류: Controller 메트릭($METRICS_URL)에 닿지 않습니다. 스택이 떠 있는지 확인하십시오." >&2
    echo "기동: scripts/run.sh" >&2
    exit 1
fi

mapfile -t cids < <(compose ps -q "$target" 2>/dev/null)
if [ "${#cids[@]}" -eq 0 ] || [ -z "${cids[0]}" ]; then
    echo "오류: 대상 '${target}'의 컨테이너를 찾을 수 없습니다(${COMPOSE})." >&2
    echo "확인: docker compose -f docker/${COMPOSE} ps --services" >&2
    exit 1
fi
cid="${cids[0]}"
name="$(docker inspect -f '{{.Name}}' "$cid" | sed 's|^/||')"
[ "${#cids[@]}" -gt 1 ] && info "복제본 ${#cids[@]}개 중 첫 번째를 고릅니다: ${name}"

conn() { metric_int ddcs_connections; }
liveness_closed() { metric_reason_int ddcs_connections_closed_total liveness_expired; }

case "$action" in
pause)
    narrate "pause ${name}"
    docker pause "$cid" >/dev/null
    info "heartbeat가 끊깁니다. 해제는 $0 resume ${target}"
    ;;
resume)
    narrate "resume ${name}"
    docker unpause "$cid" >/dev/null
    ;;
restart)
    narrate "restart ${name}"
    docker restart "$cid" >/dev/null
    ;;
kill)
    narrate "kill ${name} (SIGKILL)"
    docker kill "$cid" >/dev/null
    info "컨테이너가 종료됩니다. 되살리려면 docker compose -f docker/${COMPOSE} up -d ${target}"
    ;;
cycle)
    hold="${DDCS_FAULT_HOLD:-45}"
    tail_s="${DDCS_FAULT_TAIL:-15}"
    start_epoch="$(date +%s)"
    pre_conn="$(conn)"
    pre_liveness_closed="$(liveness_closed)"
    narrate "장애 주입 사이클: ${name}"
    info "시작 $(date '+%H:%M:%S') — connections=${pre_conn}, liveness_closed=${pre_liveness_closed}"

    docker pause "$cid" >/dev/null
    t0="$SECONDS"
    wait_for "축출 (connections ${pre_conn} 아래로)" 20 metric_at_most ddcs_connections "$((pre_conn - 1))" || true
    detect=$((SECONDS - t0))
    info "정지 중 — connections=$(conn), liveness_closed=$(liveness_closed), 감지까지 ${detect}초"
    # 그래프에서 계단이 보이려면 정지 구간이 충분히 넓어야 한다. 감지에 쓴 시간을 뺀 나머지를 채운다.
    [ "$hold" -gt "$detect" ] && soak $((hold - detect)) "정지 구간 유지"

    docker unpause "$cid" >/dev/null
    t1="$SECONDS"
    wait_for "복구 (connections ${pre_conn})" 30 metric_at_least ddcs_connections "$pre_conn" || true
    info "복구 후 — connections=$(conn), devices=$(metric_int ddcs_devices), $((SECONDS - t1))초"
    [ "$tail_s" -gt 0 ] && soak "$tail_s" "복구 구간 유지"

    end_epoch="$(date +%s)"
    narrate "Grafana 시간 범위"
    info "사이클 $(date -d "@${start_epoch}" '+%H:%M:%S') ~ $(date -d "@${end_epoch}" '+%H:%M:%S')"
    info "붙여넣을 범위 $(date -d "@$((start_epoch - 30))" '+%Y-%m-%d %H:%M:%S') ~ $(date -d "@$((end_epoch + 15))" '+%Y-%m-%d %H:%M:%S')"
    info "유지 시간은 DDCS_FAULT_HOLD(현재 ${hold}초), DDCS_FAULT_TAIL(현재 ${tail_s}초)로 바꿉니다"
    ;;
esac
