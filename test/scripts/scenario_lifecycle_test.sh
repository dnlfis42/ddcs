#!/usr/bin/env bash
# Docker를 대체해 스택 기동과 종료 시 정리를 검사한다.
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "${1:-}" = --case ]; then
    TEST_CASE="$2"
    exec 3>&1
    # shellcheck source=scripts/lib/scenario.sh
    source "$REPO_ROOT/scripts/lib/scenario.sh"
    SCENARIO_NAME=thermal
    COMPOSE=docker-compose.scale.yml
    if [ "$TEST_CASE" = overlay ]; then COMPOSE_OVERLAY=docker-compose.profile.yml; fi
    MOCK_EXISTING=false

    docker() {
        local project=default subcommand
        local -a files=()
        if [ "$1" = ps ]; then
            [ "$2" = -a ] || return 1 # 정지된 컨테이너도 검사해야 한다.
            [ "$TEST_CASE" != daemon-failure ] || return 1
            if [ "$TEST_CASE" = existing ] || [ "$MOCK_EXISTING" = true ]; then
                printf 'ddcs-controller\n'
            fi
            return 0
        fi
        [ "$1" = compose ] || return 1
        shift
        while [ "$#" -gt 0 ]; do
            case "$1" in
            --project-name | -p) project="$2"; shift 2 ;;
            --file | -f) files+=("$2"); shift 2 ;;
            *) break ;;
            esac
        done
        subcommand="${1:-}"
        case "$subcommand" in
        version) [ "$TEST_CASE" != preflight-failure ] ;;
        config)
            [ "$TEST_CASE" != config-failure ] || return 1
            printf '%s\n' '{"services":{"controller":{"container_name":"ddcs-controller"}}}'
            ;;
        build)
            printf 'BUILD project=%s\n' "$project" >&3
            if [ "$TEST_CASE" = name-race ]; then MOCK_EXISTING=true; fi
            [ "$TEST_CASE" != build-failure ]
            ;;
        up)
            printf 'UP project=%s\n' "$project" >&3
            [ "$TEST_CASE" != partial-start ]
            ;;
        down)
            if [ "$TEST_CASE" = overlay ]; then
                [ "${#files[@]}" -eq 2 ] &&
                    [ "${files[0]}" = "$ROOT/docker/docker-compose.scale.yml" ] &&
                    [ "${files[1]}" = "$ROOT/docker/docker-compose.profile.yml" ] || return 1
            fi
            printf 'DOWN project=%s\n' "$project" >&3
            [ "$TEST_CASE" != cleanup-failure ]
            ;;
        *) return 1 ;;
        esac
    }

    # 결과 파일을 쓰지 않고, 해당 단계의 실패만 주입한다.
    scenario_initialize_result() { [ "$TEST_CASE" != result-failure ]; }
    arm_cleanup
    if [ "$TEST_CASE" = ramp ]; then
        compose up -d --scale agent-zone-a=4 controller agent-zone-a || exit 1
        compose up -d --scale agent-zone-a=8 controller agent-zone-a || exit 1
    else
        stack_up controller agent-zone-a || exit 1
    fi
    case "$TEST_CASE" in
    overlay) COMPOSE=docker-compose.yml; COMPOSE_OVERLAY=; stack_down ;;
    interrupt) kill -TERM "$BASHPID" ;;
    normal) stack_down; stack_down ;;
    esac
    exit 0
fi

failures=0
for test_case in preflight-failure daemon-failure config-failure build-failure result-failure existing name-race partial-start normal overlay interrupt ramp cleanup-failure; do
    output="$(bash "$0" --case "$test_case" 2>&1)"
    status=$?
    expected_status=1
    expected_up=0
    expected_down=0
    case "$test_case" in
    partial-start | cleanup-failure) expected_up=1; expected_down=1 ;;
    normal | overlay) expected_status=0; expected_up=1; expected_down=1 ;;
    interrupt) expected_status=130; expected_up=1; expected_down=1 ;;
    ramp) expected_status=0; expected_up=2; expected_down=1 ;;
    esac
    up_count="$(printf '%s\n' "$output" | grep -c '^UP ' || true)"
    down_count="$(printf '%s\n' "$output" | grep -c '^DOWN ' || true)"
    projects="$(printf '%s\n' "$output" | sed -n -E 's/^(BUILD|UP|DOWN) project=//p' | sort -u)"
    if [ "$status" -eq "$expected_status" ] &&
        [ "$up_count" -eq "$expected_up" ] && [ "$down_count" -eq "$expected_down" ] &&
        { [ -z "$projects" ] || [[ "$projects" =~ ^ddcs-test-[a-z0-9-]+$ ]]; }; then
        printf 'PASS 스택 기동·정리: %s\n' "$test_case"
    else
        printf 'FAIL 스택 기동·정리: %s (종료 코드=%s, 기동 수=%s, 정리 수=%s)\n%s\n' \
            "$test_case" "$status" "$up_count" "$down_count" "$output"
        failures=$((failures + 1))
    fi
done

# 같은 COMPOSE_PROJECT_NAME을 물려받아도 실행마다 별도 프로젝트를 사용한다.
first="$(COMPOSE_PROJECT_NAME=existing-stack bash "$0" --case normal 2>&1 | sed -n 's/^UP project=//p')"
second="$(COMPOSE_PROJECT_NAME=existing-stack bash "$0" --case normal 2>&1 | sed -n 's/^UP project=//p')"
if [ -n "$first" ] && [ -n "$second" ] && [ "$first" != "$second" ]; then
    printf 'PASS 실행별 Docker 프로젝트 분리\n'
else
    printf 'FAIL 실행별 Docker 프로젝트 분리 (%s, %s)\n' "$first" "$second"
    failures=$((failures + 1))
fi
[ "$failures" -eq 0 ]
