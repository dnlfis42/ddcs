#!/usr/bin/env bash

# 호스트 설정은 변경하지 않고 성능 측정 전제를 검사한다.
# 사용법: scripts/measurement/verify-environment.sh [containers|fleet] (기본 containers)
# fleet은 대량 컨테이너용 ARP·6GB 메모리 검사를 제외한다.
# 종료 코드: 0=통과(WARN 포함), 1=검사 실패, 2=사용법 오류

set -u

workload="${1:-containers}"
case "$workload" in
containers | fleet) ;;
*) echo "사용법: $0 [containers|fleet]" >&2; exit 2 ;;
esac

PASS=0; FAIL=0; WARN=0
ok()   { PASS=$((PASS + 1)); printf '[PASS]  %s\n' "$1"; }
note() { WARN=$((WARN + 1)); printf '[WARN]  %s\n' "$1"; }
bad()  { FAIL=$((FAIL + 1)); printf '[FAIL]  %s\n         확인/조치: %s\n' "$1" "$2"; }

echo "성능 측정 전제 검사"
echo

# CPU 샘플링 입력을 먼저 검증하고, 잘못된 경우에도 나머지 검사는 계속한다.
sample_seconds="${DDCS_PERF_CPU_SAMPLE_SECONDS:-3}"
case "$sample_seconds" in
'' | 0 | *[!0-9]*)
    bad "CPU sample 길이 = '$sample_seconds' (양의 정수 아님)" "DDCS_PERF_CPU_SAMPLE_SECONDS=3으로 지정"
    sample_seconds=0
    ;;
esac

# 데몬 접근 실패를 컨테이너 0개로 오인하지 않도록 먼저 확인한다.
if ! docker ps -q >/dev/null 2>&1; then
    bad "docker 데몬 접근 불가" "sudo systemctl start docker"
    note "containerd-shim 검사 생략 (실행 중 컨테이너 수 확인 불가)"
else
    running=$(docker ps -q | wc -l)
    if [ "$running" -eq 0 ]; then
        ok "실행 중 컨테이너 0"
    else
        bad "실행 중 컨테이너 $running개" \
            "docker ps -a --no-trunc와 docker compose ls로 소유 프로젝트를 확인한 뒤, 본인 테스트 스택만 종료하십시오."
    fi

    # 컨테이너 수를 확인한 경우에만 shim 수를 비교한다.
    # pgrep -c는 미검출 시에도 0을 출력하므로 || echo 0을 붙이지 않는다.
    shims=$(pgrep -fc 'containerd-shim-runc-v2 -namespace' 2>/dev/null)
    shims=${shims:-0}
    if [ "$shims" -le "$running" ]; then
        ok "고아 containerd-shim 없음 (shim $shims)"
    else
        bad "containerd-shim $shims개 vs 실행 컨테이너 $running개 (고아 의심)" \
            "pgrep -af containerd-shim-runc-v2와 docker ps -a --no-trunc로 PID·컨테이너 ID를 대조하십시오. 고아로 확인한 개별 대상만 정리하고, 소유 관계가 불명확하면 관리자에게 확인하십시오."
    fi
fi

# 가용 메모리
avail_kb=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
if [ "$workload" = fleet ]; then
    ok "Fleet 모드: 가용 메모리 $((avail_kb / 1024))MB (대량 컨테이너용 6GB 게이트 제외; 레벨별 메모리 관측 필요)"
elif [ "$avail_kb" -ge 8388608 ]; then
    ok "가용 메모리 $((avail_kb / 1048576))GB"
elif [ "$avail_kb" -ge 6291456 ]; then
    note "가용 메모리 $((avail_kb / 1048576))GB (1000대는 빠듯할 수 있음)"
else
    bad "가용 메모리 $((avail_kb / 1048576))GB (< 6GB)" "브라우저·IDE 등 대형 프로세스를 닫을 것"
fi

# 스왑 사용
swap_used_kb=$(awk '/SwapTotal/{t=$2} /SwapFree/{f=$2} END{print t-f}' /proc/meminfo)
if [ "$swap_used_kb" -lt 131072 ]; then
    ok "스왑 사용 $((swap_used_kb / 1024))MB"
elif [ "$swap_used_kb" -lt 1048576 ]; then
    note "스왑 사용 $((swap_used_kb / 1024))MB (이전 메모리 압박의 흔적. 원하면 sudo swapoff -a && sudo swapon -a)"
else
    bad "스왑 사용 $((swap_used_kb / 1024))MB (이전 메모리 압박의 흔적이 큼)" \
        "원인 프로세스 정리 후: sudo swapoff -a && sudo swapon -a"
fi

# ARP 상한 (컨테이너 방식만 검사)
th3=$(cat /proc/sys/net/ipv4/neigh/default/gc_thresh3 2>/dev/null || echo 0)
if [ "$workload" = fleet ]; then
    ok "Fleet 모드: 대량 컨테이너용 ARP 상한 검사 제외 (Agent별 컨테이너 대신 Fleet 사용)"
elif [ "$th3" -ge 4096 ]; then
    ok "ARP gc_thresh3 = $th3"
else
    bad "ARP gc_thresh3 = $th3 (기본 1024는 컨테이너 600대쯤부터 신규 SYN을 응답 없이 버린다)" \
        "sudo sysctl -w net.ipv4.neigh.default.gc_thresh1=2048 net.ipv4.neigh.default.gc_thresh2=4096 net.ipv4.neigh.default.gc_thresh3=8192"
fi

# CPU governor
governors=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort -u)
if [ "$governors" = "performance" ]; then
    ok "CPU governor = performance (전 코어)"
else
    bad "CPU governor = [$(echo "$governors" | tr '\n' ' ')] (performance 아님)" \
        "echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
fi

# Turbo: intel_pstate가 없는 플랫폼은 별도 확인이 필요하다.
if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    if [ "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" = "1" ]; then
        ok "turbo 비활성 (최대 동작 주파수 제한)"
    else
        bad "turbo 활성 상태 (열 상태 따라 클럭 변동)" \
            "echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
    fi
else
    note "intel_pstate 없음: turbo 비활성 검사 생략 (플랫폼에 맞게 별도 확인 필요)"
fi

# 호스트 부하
load1=$(awk '{print $1}' /proc/loadavg)
if awk "BEGIN{exit !($load1 < 2.0)}"; then
    ok "loadavg(1m) = $load1"
else
    note "loadavg(1m) = $load1 (유휴가 아님. 다른 작업이 돌고 있으면 수치가 오염된다)"
fi

# ps의 생애 평균 대신 짧은 구간의 CPU jiffies 차이로 점유율을 판정한다.
clock_ticks=$(getconf CLK_TCK 2>/dev/null || true)
if [ "$sample_seconds" -gt 0 ] && [ -n "$clock_ticks" ] && [ "$clock_ticks" -gt 0 ] 2>/dev/null; then
    declare -A cpu_start_jiffies
    declare -A cpu_start_comm
    while read -r pid comm; do
        case "$pid" in
        '' | *[!0-9]*) continue ;;
        esac
        [ -r "/proc/$pid/stat" ] || continue
        jiffies=$(awk '{print $14 + $15}' "/proc/$pid/stat" 2>/dev/null || true)
        case "$jiffies" in
        '' | *[!0-9]*) continue ;;
        esac
        cpu_start_jiffies["$pid"]="$jiffies"
        cpu_start_comm["$pid"]="$comm"
    done < <(ps -eo pid=,comm=)

    sleep "$sample_seconds"

    hogs=
    for pid in "${!cpu_start_jiffies[@]}"; do
        [ -r "/proc/$pid/stat" ] || continue
        jiffies=$(awk '{print $14 + $15}' "/proc/$pid/stat" 2>/dev/null || true)
        case "$jiffies" in
        '' | *[!0-9]*) continue ;;
        esac
        delta=$((jiffies - cpu_start_jiffies[$pid]))
        [ "$delta" -ge 0 ] || continue
        pct=$(awk -v d="$delta" -v t="$clock_ticks" -v s="$sample_seconds" 'BEGIN { printf "%.1f", 100 * d / t / s }')
        if awk -v p="$pct" 'BEGIN { exit !(p > 10.0) }'; then
            hogs+="${cpu_start_comm[$pid]}(${pct}%) "
        fi
    done
    if [ -z "$hogs" ]; then
        ok "${sample_seconds}s 구간 CPU 10%+ 점유 프로세스 없음"
    else
        note "${sample_seconds}s 구간 CPU 점유 큰 프로세스: $hogs(측정 전 종료 또는 유휴 대기 권장)"
    fi
else
    note "CPU 구간 점유율 검사 생략 (CLK_TCK 또는 sample 길이 확인 필요)"
fi

echo
echo "환경 요약"
echo
printf 'kernel  %s\n' "$(uname -r)"
printf 'cpu     %s\n' "$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
printf 'clock   governor=%s no_turbo=%s cur_avg=%sMHz\n' \
    "$(echo "$governors" | head -1)" \
    "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo '-')" \
    "$(grep MHz /proc/cpuinfo | awk '{s+=$4; n++} END{printf "%.0f", s/n}')"
printf 'memory  available %sGB, swap used %sMB\n' "$((avail_kb / 1048576))" "$((swap_used_kb / 1024))"
printf 'docker  %s\n' "$(docker --version 2>/dev/null || echo unknown)"
printf 'arp     gc_thresh3=%s\n' "$th3"

printf '\n결과: %d pass, %d warn, %d fail\n' "$PASS" "$WARN" "$FAIL"
[ "$FAIL" -eq 0 ]
