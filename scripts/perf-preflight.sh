#!/usr/bin/env bash
#
# DDCS 성능 측정 전제 검사. 검사만 하고 아무것도 바꾸지 않으며, FAIL이 있으면 수정 명령을
# 출력하고 비정상 종료한다. perf-ramp.sh가 시작 시 이 스크립트를 게이트로 실행한다
# (DDCS_PERF_SKIP_PREFLIGHT=1 로 우회할 수 있지만, 그렇게 잰 수치는 표에 싣지 않는다).
#
# 종료 상태:
#   0   모든 검사 통과 (WARN은 통과로 본다)
#   1   FAIL 있음
#
# 고정하는 환경:
# - CPU governor = performance, turbo 비활성. 이 둘로 전 코어를 기저 주파수에 고정한다.
#   turbo를 켠 채 재면 노트북 열 상태에 따라 클럭이 오르내려 레벨 간 비교가 흔들린다.
# - 실행 중 컨테이너 0, 고아 containerd-shim 0, 스왑 사용 최소, 가용 메모리 확보, 유휴 load.
# - ARP 이웃 테이블 상한(500대 이상 컨테이너 전제).
# - CPU 큰 프로세스는 `ps`의 누적 평균이 아니라 짧은 구간의 CPU jiffies delta로 판정한다.
# 끝에 측정 기록에 함께 남길 환경 요약을 출력한다.

set -u

PASS=0; FAIL=0; WARN=0
ok()   { PASS=$((PASS + 1)); printf '[PASS]  %s\n' "$1"; }
note() { WARN=$((WARN + 1)); printf '[WARN]  %s\n' "$1"; }
bad()  { FAIL=$((FAIL + 1)); printf '[FAIL]  %s\n         수정 명령: %s\n' "$1" "$2"; }

echo "성능 측정 전제 검사"
echo

# 1. CPU governor
governors=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort -u)
if [ "$governors" = "performance" ]; then
    ok "CPU governor = performance (전 코어)"
else
    bad "CPU governor = [$(echo "$governors" | tr '\n' ' ')] (performance 아님)" \
        "echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
fi

# 2. turbo 비활성 (intel_pstate 기준. 없는 플랫폼이면 건너뛴다)
if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    if [ "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" = "1" ]; then
        ok "turbo 비활성 (기저 주파수 고정)"
    else
        bad "turbo 활성 상태 (열 상태 따라 클럭 변동)" \
            "echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
    fi
else
    note "intel_pstate 없음: turbo 고정 검사 생략 (플랫폼에 맞게 별도 고정 권장)"
fi

# 3. ARP 이웃 테이블 상한 (500+ 컨테이너 전제)
th3=$(cat /proc/sys/net/ipv4/neigh/default/gc_thresh3 2>/dev/null || echo 0)
if [ "$th3" -ge 4096 ]; then
    ok "ARP gc_thresh3 = $th3"
else
    bad "ARP gc_thresh3 = $th3 (기본 1024는 컨테이너 600대쯤부터 신규 SYN을 응답 없이 버린다)" \
        "sudo sysctl -w net.ipv4.neigh.default.gc_thresh1=2048 net.ipv4.neigh.default.gc_thresh2=4096 net.ipv4.neigh.default.gc_thresh3=8192"
fi

# 4. 실행 중 컨테이너 0 (데몬이 죽어 있으면 개수가 0으로 위장하므로 접근부터 확인한다)
if ! docker ps -q >/dev/null 2>&1; then
    bad "docker 데몬 접근 불가" "sudo systemctl start docker"
    running=0
else
    running=$(docker ps -q | wc -l)
    if [ "$running" -eq 0 ]; then
        ok "실행 중 컨테이너 0"
    else
        bad "실행 중 컨테이너 $running개" "docker compose -f docker/docker-compose.yml down; docker compose -f docker/docker-compose.scale.yml down"
    fi
fi

# 5. 고아 containerd-shim 0 (컨테이너를 대량으로 만들었다 부순 뒤 수천 개가 남는 사례 있음)
# pgrep -c는 매치 0이어도 "0"을 출력하며 실패 코드를 주므로 || 폴백을 붙이면 안 된다.
shims=$(pgrep -fc 'containerd-shim-runc-v2 -namespace' 2>/dev/null)
shims=${shims:-0}
if [ "$shims" -le "$running" ]; then
    ok "고아 containerd-shim 없음 (shim $shims)"
else
    bad "containerd-shim $shims개 vs 실행 컨테이너 $running개 (고아 잔존)" \
        "sudo pkill -f containerd-shim-runc-v2 && sudo systemctl restart docker"
fi

# 6. 스왑 사용
swap_used_kb=$(awk '/SwapTotal/{t=$2} /SwapFree/{f=$2} END{print t-f}' /proc/meminfo)
if [ "$swap_used_kb" -lt 131072 ]; then
    ok "스왑 사용 $((swap_used_kb / 1024))MB"
elif [ "$swap_used_kb" -lt 1048576 ]; then
    note "스왑 사용 $((swap_used_kb / 1024))MB (이전 메모리 압박의 흔적. 원하면 sudo swapoff -a && sudo swapon -a)"
else
    bad "스왑 사용 $((swap_used_kb / 1024))MB (이전 메모리 압박의 흔적이 큼)" \
        "원인 프로세스 정리 후: sudo swapoff -a && sudo swapon -a"
fi

# 7. 가용 메모리 (1000대 ~= 컨테이너 몫 수 GB + 여유)
avail_kb=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
if [ "$avail_kb" -ge 8388608 ]; then
    ok "가용 메모리 $((avail_kb / 1048576))GB"
elif [ "$avail_kb" -ge 6291456 ]; then
    note "가용 메모리 $((avail_kb / 1048576))GB (1000대는 빠듯할 수 있음)"
else
    bad "가용 메모리 $((avail_kb / 1048576))GB (< 6GB)" "브라우저·IDE 등 대형 프로세스를 닫을 것"
fi

# 8. 유휴 load
load1=$(awk '{print $1}' /proc/loadavg)
if awk "BEGIN{exit !($load1 < 2.0)}"; then
    ok "loadavg(1m) = $load1"
else
    note "loadavg(1m) = $load1 (유휴가 아님. 다른 작업이 돌고 있으면 수치가 오염된다)"
fi

# 9. CPU를 크게 먹는 다른 프로세스. ps의 %CPU는 생애 평균이라 오래 켜 둔 desktop shell을
# 현재도 점유 중인 것처럼 오인할 수 있다. 같은 PID의 utime+stime을 짧은 구간에 두 번 읽어
# 실제 한 CPU 기준 점유율을 계산한다.
sample_seconds="${DDCS_PERF_CPU_SAMPLE_SECONDS:-3}"
case "$sample_seconds" in
'' | 0 | *[!0-9]*)
    bad "CPU sample 길이 = '$sample_seconds' (양의 정수 아님)" "DDCS_PERF_CPU_SAMPLE_SECONDS=3으로 지정"
    sample_seconds=0
    ;;
esac
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
