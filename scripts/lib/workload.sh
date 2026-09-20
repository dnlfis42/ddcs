# shellcheck shell=bash

# performance/profile 공통: 단일 snapshot의 연결 수와 그룹별 Status 보고 수를 검증한다.
workload_ready() { # snapshot, 총 Agent 수, single|balance
    printf '%s\n' "$1" | awk -v total="$2" -v layout="$3" '
        $1 == "ddcs_connections" {
            if ($2 !~ /^[0-9]+$/ || connections_seen++) bad=1
            connections=$2
        }
        /^ddcs_group_devices/ {
            if ($1 !~ /^ddcs_group_devices\{group="zone_[a-d]",mode="(normal|performance|safe)"\}$/ ||
                $2 !~ /^[0-9]+$/ || seen[$1]++) { bad=1; next }
            split($1, labels, "\"")
            counts[labels[2]] += $2
        }
        END {
            if (layout != "single" && layout != "balance") exit 1
            if (bad || connections_seen != 1 || connections != total) exit 1
            per_group = (layout == "single" ? total : total/4)
            if (counts["zone_a"] != per_group) exit 1
            for (i=1; i<=3; i++) {
                group="zone_" substr("bcd", i, 1)
                if (counts[group]+0 != (layout == "single" ? 0 : per_group)) exit 1
            }
        }
    '
}

# 성능 측정과 프로파일링에 같은 Fleet 크기와 정책 옵션을 적용한다.
# 작은 점검 실행은 DDCS_PERF_AGENTS_PER_FLEET을 명시적으로 낮춰 사용한다.
workload_configure() { # layout, 총 Agent 수 목록
    local layout="$1"
    shift
    FIXED_FLEET_SIZE="${DDCS_PERF_AGENTS_PER_FLEET-1000}"
    UNIFORM_POLICY="${DDCS_PERF_UNIFORM_POLICY-1}"
    if ! [[ "$FIXED_FLEET_SIZE" =~ ^[1-9][0-9]{0,4}$ ]]; then
        echo "오류: DDCS_PERF_AGENTS_PER_FLEET은 양의 정수여야 합니다." >&2
        return 2
    fi
    case "$UNIFORM_POLICY" in
    0 | 1) ;;
    *) echo "오류: DDCS_PERF_UNIFORM_POLICY는 0 또는 1이어야 합니다." >&2; return 2 ;;
    esac
    command -v python3 >/dev/null || { echo "오류: python3이 필요합니다." >&2; return 2; }
    layout_args=(--size "$FIXED_FLEET_SIZE")
    policy_args=()
    # shellcheck disable=SC2034 # 호출자가 설정 복사와 Compose 생성에 사용한다.
    [ "$UNIFORM_POLICY" != 1 ] || policy_args=(--uniform)
    python3 "$ROOT/scripts/performance/workload-config.py" validate --layout "$layout" "${layout_args[@]}" "$@" || return 2
    export DDCS_PERF_AGENTS_PER_FLEET="$FIXED_FLEET_SIZE"
    export DDCS_PERF_UNIFORM_POLICY="$UNIFORM_POLICY"
}
