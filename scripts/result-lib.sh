#!/usr/bin/env bash
# shellcheck shell=bash
#
# DDCS 로컬 결과 공용 헬퍼. 실행하지 말고 source 한다.
#
# 하나의 build 결과는 release 설정, source 상태, 두 runtime image, runtime config 전체로 식별한다.
# 같은 build-key 디렉터리를 다시 사용할 때는 build.json의 식별값이 정확히 같은지만 확인하고,
# 이미 기록한 profile/scenario 결과는 건드리지 않는다.

result_require_jq() {
    command -v jq >/dev/null 2>&1 || {
        echo "오류: 결과 build.json을 만들고 검증하려면 'jq'가 필요합니다." >&2
        return 1
    }
}

result_is_boolean() {
    case "$1" in
    true | false) return 0 ;;
    *) return 1 ;;
    esac
}

result_has_newline() {
    case "$1" in
    *$'\n'* | *$'\r'*) return 0 ;;
    *) return 1 ;;
    esac
}

result_directory_sha256() { # directory; file path와 내용 hash를 함께 고정한다.
    local directory="$1"

    [ -d "$directory" ] || {
        echo "오류: hash할 runtime config 디렉터리가 없습니다: $directory" >&2
        return 1
    }
    (
        cd "$directory" || exit 1
        while IFS= read -r -d '' file; do
            printf '%s\0' "$file"
            sha256sum -- "$file" | awk '{print $1}' | tr -d '\n'
            printf '\0'
        done < <(LC_ALL=C find . -type f -printf '%P\0' | LC_ALL=C sort -z)
    ) | sha256sum | awk '{print $1}'
}

result_build_key() { # configuration source-revision source-dirty controller-image-id agent-image-id runtime-config-sha256
    local configuration="$1" source_revision="$2" source_dirty="$3"
    local controller_image_id="$4" agent_image_id="$5" runtime_config_sha256="$6"
    local value

    result_is_boolean "$source_dirty" || {
        echo "오류: source dirty 값은 true 또는 false여야 합니다: $source_dirty" >&2
        return 1
    }
    [[ "$runtime_config_sha256" =~ ^[0-9a-f]{64}$ ]] || {
        echo "오류: runtime config SHA-256 형식이 아닙니다: $runtime_config_sha256" >&2
        return 1
    }
    for value in "$configuration" "$source_revision" "$controller_image_id" "$agent_image_id" "$runtime_config_sha256"; do
        if [ -z "$value" ] || result_has_newline "$value"; then
            echo "오류: build-key 입력이 비어 있거나 줄바꿈을 포함합니다." >&2
            return 1
        fi
    done

    printf 'configuration=%s\nsource_revision=%s\nsource_dirty=%s\ncontroller_image_id=%s\nagent_image_id=%s\nruntime_config_sha256=%s\n' \
        "$configuration" "$source_revision" "$source_dirty" "$controller_image_id" "$agent_image_id" "$runtime_config_sha256" |
        sha256sum | awk '{print "build-" $1}'
}

result_initialize_build() { # repository-root source-revision source-dirty controller-image-id agent-image-id runtime-config-sha256
    local repository_root="$1" source_revision="$2" source_dirty="$3"
    local controller_image_id="$4" agent_image_id="$5" runtime_config_sha256="$6"
    local configuration=release build_json temporary

    result_require_jq || return 1
    DDCS_RESULT_BUILD_KEY="$(result_build_key \
        "$configuration" "$source_revision" "$source_dirty" \
        "$controller_image_id" "$agent_image_id" "$runtime_config_sha256")" || return 1
    DDCS_RESULT_BUILD_DIR="$repository_root/var/result/$DDCS_RESULT_BUILD_KEY"
    build_json="$DDCS_RESULT_BUILD_DIR/build.json"

    mkdir -p "$DDCS_RESULT_BUILD_DIR" || {
        echo "오류: build 결과 디렉터리를 만들지 못했습니다: $DDCS_RESULT_BUILD_DIR" >&2
        return 1
    }

    if [ -e "$build_json" ]; then
        [ -f "$build_json" ] || {
            echo "오류: build.json이 일반 파일이 아닙니다: $build_json" >&2
            return 1
        }
        jq -e \
            --arg key "$DDCS_RESULT_BUILD_KEY" \
            --arg configuration "$configuration" \
            --arg source_revision "$source_revision" \
            --argjson source_dirty "$source_dirty" \
            --arg controller_image_id "$controller_image_id" \
            --arg agent_image_id "$agent_image_id" \
            --arg runtime_config_sha256 "$runtime_config_sha256" '
                (.build | type == "object") and
                .build.key == $key and
                .build.configuration == $configuration and
                .build.source_revision == $source_revision and
                .build.source_dirty == $source_dirty and
                .build.controller_image_id == $controller_image_id and
                .build.agent_image_id == $agent_image_id and
                .build.runtime_config_sha256 == $runtime_config_sha256
            ' "$build_json" >/dev/null || {
            echo "오류: 기존 build.json의 build 식별값이 현재 build-key와 다릅니다: $build_json" >&2
            return 1
        }
        return 0
    fi

    temporary="$(mktemp "$DDCS_RESULT_BUILD_DIR/.build.json.XXXXXX")" || {
        echo "오류: build.json 임시 파일을 만들지 못했습니다: $DDCS_RESULT_BUILD_DIR" >&2
        return 1
    }
    if ! jq -n \
        --arg key "$DDCS_RESULT_BUILD_KEY" \
        --arg configuration "$configuration" \
        --arg source_revision "$source_revision" \
        --argjson source_dirty "$source_dirty" \
        --arg controller_image_id "$controller_image_id" \
        --arg agent_image_id "$agent_image_id" \
        --arg runtime_config_sha256 "$runtime_config_sha256" '
            {
                build: {
                    key: $key,
                    configuration: $configuration,
                    source_revision: $source_revision,
                    source_dirty: $source_dirty,
                    controller_image_id: $controller_image_id,
                    agent_image_id: $agent_image_id,
                    runtime_config_sha256: $runtime_config_sha256
                },
                profile: {
                    overhead: {},
                    capture: {}
                },
                scenario: {
                    thermal: "not_run",
                    "agent-reconnect": "not_run",
                    "regime-transition": "not_run",
                    "liveness-eviction": "not_run",
                    "policy-reload": "not_run"
                }
            }
        ' >"$temporary"; then
        rm -f "$temporary"
        echo "오류: build.json을 만들지 못했습니다: $build_json" >&2
        return 1
    fi
    chmod 644 "$temporary" || {
        rm -f "$temporary"
        return 1
    }
    mv "$temporary" "$build_json" || {
        rm -f "$temporary"
        echo "오류: build.json을 확정하지 못했습니다: $build_json" >&2
        return 1
    }
}

# build.json을 원자적으로 갱신한다. 호출자는 jq filter 뒤에 필요한 --arg/--argjson을 넘긴다.
# result 경로는 모두 var/ 아래라 source identity를 계산한 뒤에만 이 함수를 호출한다.
result_update_build_json() { # build-directory jq-filter [jq-arguments...]
    local build_directory="$1" filter="$2" build_json temporary
    shift 2

    result_require_jq || return 1
    build_json="$build_directory/build.json"
    [ -f "$build_json" ] || {
        echo "오류: 갱신할 build.json이 없습니다: $build_json" >&2
        return 1
    }
    temporary="$(mktemp "$build_directory/.build.json.update.XXXXXX")" || {
        echo "오류: build.json 임시 파일을 만들지 못했습니다: $build_directory" >&2
        return 1
    }
    if ! jq "$@" "$filter" "$build_json" >"$temporary"; then
        rm -f "$temporary"
        echo "오류: build.json 갱신 값을 만들지 못했습니다: $build_json" >&2
        return 1
    fi
    chmod 644 "$temporary" || {
        rm -f "$temporary"
        return 1
    }
    mv "$temporary" "$build_json" || {
        rm -f "$temporary"
        echo "오류: build.json 갱신을 확정하지 못했습니다: $build_json" >&2
        return 1
    }
}

result_set_profile_capture() { # build-directory condition duration-seconds verified summary-json
    local build_directory="$1" condition="$2" duration_seconds="$3" verified="$4" summary_json="$5"

    [[ "$condition" =~ ^(balance|single)-[0-9]{4}$ ]] || {
        echo "오류: profile capture 조건 형식이 아닙니다: $condition" >&2
        return 1
    }
    [[ "$duration_seconds" =~ ^[1-9][0-9]*$ ]] || {
        echo "오류: profile capture duration은 양의 정수여야 합니다: $duration_seconds" >&2
        return 1
    }
    result_is_boolean "$verified" || {
        echo "오류: profile capture verified 값은 boolean이어야 합니다: $verified" >&2
        return 1
    }
    # shellcheck disable=SC2016 # jq filter에서 $condition 등을 jq 변수로 해석한다.
    result_update_build_json "$build_directory" '
        (.profile //= {}) |
        (.profile.capture //= {}) |
        .profile.capture[$condition] = {
            duration_seconds: $duration_seconds,
            verified: $verified,
            summary: $summary
        }
    ' \
        --arg condition "$condition" \
        --argjson duration_seconds "$duration_seconds" \
        --argjson verified "$verified" \
        --argjson summary "$summary_json"
}

result_set_profile_overhead() { # build-directory condition summary-json
    local build_directory="$1" condition="$2" summary_json="$3"

    [[ "$condition" =~ ^(balance|single)-[0-9]{4}$ ]] || {
        echo "오류: profile overhead 조건 형식이 아닙니다: $condition" >&2
        return 1
    }
    # shellcheck disable=SC2016 # jq filter에서 $condition 등을 jq 변수로 해석한다.
    result_update_build_json "$build_directory" '
        (.profile //= {}) |
        (.profile.overhead //= {}) |
        .profile.overhead[$condition] = $summary
    ' --arg condition "$condition" --argjson summary "$summary_json"
}

result_set_scenario() { # build-directory scenario-name pass|fail
    local build_directory="$1" scenario="$2" scenario_status="$3"

    case "$scenario" in
    thermal | agent-reconnect | regime-transition | liveness-eviction | policy-reload)
        ;;
    *)
        echo "오류: 알 수 없는 scenario 이름입니다: $scenario" >&2
        return 1
        ;;
    esac
    case "$scenario_status" in
    pass | fail)
        ;;
    *)
        echo "오류: scenario 상태는 pass 또는 fail이어야 합니다: $scenario_status" >&2
        return 1
        ;;
    esac
    # shellcheck disable=SC2016 # jq filter에서 $scenario 등을 jq 변수로 해석한다.
    result_update_build_json "$build_directory" '
        (.scenario //= {}) |
        .scenario[$scenario] = $status
    ' --arg scenario "$scenario" --arg status "$scenario_status"
}
