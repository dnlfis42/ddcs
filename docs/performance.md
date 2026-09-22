# 성능 측정

연결된 장치가 늘어나면 상태 보고와 명령 처리가 Controller에 함께 몰립니다.
연결 수만 확보해서는 이 부하를 계속 처리할 수 있는지 알 수 없으므로, DDCS는 AgentFleet으로 실제 TCP 경로에 부하를 공급하고 보고량·tick 지연·명령 처리·자원 사용을 함께 측정합니다.

이 문서는 부하 생성부터 결과 판정까지의 재현 절차를 다룹니다. 내부 처리 단계의 시간을 나누어 분석하는 방법은 [프로파일링](profiling.md)에 정리했습니다.

## 목차

- [부하 생성기 AgentFleet](#부하-생성기-agentfleet)
- [실행과 조건](#실행과-조건)
- [저장되는 근거](#저장되는-근거)
- [계산과 판정](#계산과-판정)
- [해석의 경계](#해석의-경계)

## 부하 생성기 AgentFleet

장치마다 프로세스와 컨테이너를 만들면 부하 생성 자체가 많은 자원을 차지합니다.
AgentFleet은 여러 Agent의 이벤트 루프와 버퍼 풀을 공유하면서 세션별 TCP 연결과 장치 상태는 독립적으로 유지합니다.

`agent-fleet N group`은 한 프로세스에서 N개 Agent 세션을 실행하는 부하 테스트 앱입니다.
전체 Agent가 인자로 지정한 하나의 Group에 등록됩니다.

```sh
cmake --preset release
cmake --build --preset release --target ctrl agent-fleet

# 터미널 1
DDCS_LOG_LEVEL=warn build/release/bin/ctrl

# 터미널 2
ulimit -n 65536
DDCS_LOG_LEVEL=warn build/release/bin/agent-fleet 2000 zone_a
```

N은 양의 정수, group은 비어 있지 않은 이름이며 두 인자 모두 필수입니다. `--help`로 사용법을 확인합니다.
파일 디스크립터 soft limit이 N + 32보다 작으면 실행 전에 오류를 출력합니다.
프로그램이 호스트 설정을 변경하지 않으므로 실행 셸이나 컨테이너에서 limit을 설정합니다.

### 연결과 상태

Reactor, TimerScheduler, SignalSource, 메시지 버퍼 풀을 프로세스당 하나씩 사용합니다.
각 Agent는 고유 Device UUID, SimulatedDevice, SessionService, Connector, TCP 소켓,
수신 버퍼, 송신 큐, 재접속 backoff를 유지합니다.
기존 Agent의 등록·Heartbeat·Status·명령 처리 로직을 사용하며, Controller의 프로토콜은 바뀌지 않습니다.

시작할 때 첫 연결을 개시하고 이후 1ms 간격으로 연결을 추가합니다.
실제 등록 완료 시간은 이벤트 루프 지연, 연결 실패, 재접속에 따라 달라집니다.
SIGINT 또는 SIGTERM을 받으면 이벤트 루프를 멈추고 전체 연결과 타이머를 정리합니다.

### 설정

`config/agent.json`과 기존 Agent의 설정 우선순위(환경변수 > 파일 > 기본값)를 공유합니다.
`DDCS_CONFIG_PATH`, `DDCS_TRANSPORT_HOST`, `DDCS_TRANSPORT_PORT`, `DDCS_LOG_LEVEL`,
`DDCS_SIM_NOISE`, `DDCS_SIM_JITTER`를 사용할 수 있습니다.
Group은 명령행 인자를 Fleet 전체에 적용하며, `DDCS_DEVICE_GROUP`과 파일의 `device.group`보다 우선합니다.

각 Device UUID는 기동 시 발급하며 프로세스가 살아 있는 동안 재접속에도 유지합니다.
`DDCS_DEVICE_ID`와 `DDCS_DEVICE_ID_FILE`은 Fleet에 적용하지 않습니다.
프로세스를 다시 실행하면 새 UUID를 발급합니다.
Controller 호스트명은 Fleet 기동 시 IPv4 주소 하나로 해석합니다.
해석 실패 시 기동을 중단하며, 실행 도중 DNS 주소 변경은 반영하지 않습니다.

### Docker 실행

```sh
DDCS_FLEET_COUNT=1000 DDCS_FLEET_GROUP=zone_a docker compose -f docker/docker-compose.fleet.yml up --build
```

Controller 컨테이너 하나와 Fleet 컨테이너 하나를 실행합니다. `DDCS_FLEET_COUNT`는
Compose가 N 인자로 전달하는 값이며 기본값은 1,000입니다.
`DDCS_FLEET_GROUP`은 group 인자로 전달하며 Compose 기본값은 `zone_a`입니다.
기존 스택과 호스트 포트 8080, 9000이 겹치므로 동시에 실행할 때는 포트를 조정해야 합니다.
이 Compose에는 Prometheus와 Grafana를 포함하지 않습니다. Controller의 메트릭은 직접 조회할 수 있습니다.

대시보드로 관찰하려면 모니터링을 포함한 구성을 사용합니다. 아래 예시는 `zone_a`에 10,000대를 실행합니다.

```sh
DDCS_FLEET_COUNT=10000 DDCS_FLEET_GROUP=zone_a \
docker compose -f docker/docker-compose.fleet-monitoring.yml up --build -d
```

[Grafana](http://localhost:3000)에서 상태를 확인하고, 종료할 때는 같은 Compose 파일을 지정합니다.

```sh
docker compose -f docker/docker-compose.fleet-monitoring.yml down
```

```sh
curl -s localhost:9000/metrics | grep -E '^ddcs_(connections |group_devices|messages_received_total|commands_succeeded_total)'
```

`ddcs_connections`에는 등록 중인 연결도 포함됩니다.
등록 후 Status 보고까지 확인하려면 `ddcs_group_devices{group="zone_a",...}`의 동작 모드별 값을
합산해 N인지 확인합니다. 안정화 후 유입 메시지와 명령 성공 수가 증가하는지도 확인합니다.

### 생성기와 Controller의 부하 구분

Fleet은 Agent별 프로세스·컨테이너와 이벤트 루프 자원의 중복을 줄입니다.
TCP 소켓과 수신 버퍼는 여전히 Agent 수만큼 필요하고, 송신 정체 시 버퍼 풀이 증가할 수 있습니다.
단일 출발지 주소에서 같은 Controller로 연결하므로 출발지 포트 범위도 최대 연결 수에 영향을 줍니다.

측정 결과에는 N, 단일 Group, 보고 주기, Fleet/Controller CPU와 메모리, 안정화·관측 시간을 기록합니다.
Fleet 자체가 한 코어를 포화시키면 Controller에 공급하는 부하가 제한됩니다.
Group과 Fleet 프로세스는 서로 다른 단위입니다. 같은 Group에 여러 Fleet을 연결할 수 있습니다. 예를 들어 20개 Fleet이 각각 1,000대를 같은 zone_a에 등록하면 하나의 Group에 총 20,000대가 됩니다.

Controller의 구역 배치를 비교할 때는 양쪽 Fleet 수·Fleet당 Agent 수·정책 임계값을 맞춰야 생성 조건의 차이를 줄일 수 있습니다.

## 실행과 조건

저장소 루트에서 실행합니다. Linux에서 Docker Compose v2, Bash, Git, curl, jq, Python 3가 필요합니다.

```sh
DDCS_PERF_AGENTS_PER_FLEET=1000 DDCS_PERF_UNIFORM_POLICY=1 \
DDCS_PERF_SUITE_LEVELS="4000 12000 20000" \
DDCS_PERF_SUITE_SETTLE=30 DDCS_PERF_SUITE_SOAK=120 \
DDCS_PERF_READY_TIMEOUT=120 \
DDCS_PERF_SLO_MAX_TICK_SKIPPED=0 \
DDCS_PERF_SLO_MAX_LIVENESS_CLOSED=0 \
DDCS_PERF_SLO_MAX_TERMINAL_FAILURES=0 \
DDCS_PERF_SLO_MAX_DISPATCH_FAILURES=0 \
scripts/performance/measure-all-layouts.sh
```

suite가 이미지를 한 번 빌드하고 balance/single에 재사용합니다. 각 레벨은 Controller와 Fleet을 새로 실행하며, 연결 수와 그룹별 Status 보유 active 장치 수가 목표에 도달한 뒤 안정화합니다. 측정 시작·종료에도 대수를 검사합니다. CPU governor·turbo·스왑·다른 프로세스 점유 등 환경 검사는 `scripts/measurement/verify-environment.sh fleet`로 확인하며, 이 스크립트는 호스트 설정을 변경하지 않습니다.

표 1은 총 N대에 적용하는 두 배치를 비교합니다.

|구성|Fleet 프로세스 수|구역별 Agent 배치|
|---|---|---|
|`balance`|N ÷ 1,000개|`zone_a`~`zone_d`에 N ÷ 4대씩|
|`single`|N ÷ 1,000개|`zone_a`에 N대|

*표 1. Fleet당 1,000대일 때의 구역 배치*

위 명령은 Fleet당 정확히 1,000대로 고정하고, 실행용 설정 사본에서 각 구역 정책을 zone_a와 동일하게 맞춥니다. 4개 그룹 균등 배치와 비교하려면 총 대수는 4,000의 배수여야 합니다. 따라서 위 예시의 비교 지점은 4,000·12,000·20,000대입니다. 두 배치의 생성기 수·프로세스당 부하·정책 임계값을 맞춰도 초기 무작위 상태와 OS 스케줄링까지 동일하지는 않습니다. 관측 차이를 모든 환경의 우열로 일반화하지 않습니다.

한 배치만 확인하려면 다음처럼 실행합니다. 각 레벨은 기본 Fleet 크기인 1,000의 배수이며, `balance`는 네 구역에 같은 수의 Fleet을 배치할 수 있도록 4,000의 배수여야 합니다. 실행 스크립트의 총 Agent 수 입력 범위는 1~65,504입니다. 작은 점검에서는 `DDCS_PERF_AGENTS_PER_FLEET`도 함께 낮춥니다.

```sh
DDCS_PERF_LEVELS="1000 2000" scripts/performance/measure-scalability.sh single
```

단일 배치의 안정화·측정 시간은 `DDCS_PERF_SETTLE`과 `DDCS_PERF_SOAK`으로 지정합니다. 기본값은 각각 30초와 120초이며, 준비 제한 `DDCS_PERF_READY_TIMEOUT`의 기본값은 90초입니다. 위 suite 예시는 준비 제한을 120초로 늘렸습니다.

각 실행은 필요한 Compose 구성을 생성합니다. suite는 두 배치에 같은 이미지를 재사용하며, 프로파일링도 같은 Fleet 부하 구성을 사용합니다. 개별 Agent 장애를 검증하는 시나리오는 Agent별 컨테이너를 사용합니다.

## 저장되는 근거

소스나 설정이 다른 결과를 섞지 않도록 성능 측정·프로파일링·시나리오는 공통 build identity에 귀속됩니다. identity는 Release 구성, source revision과 dirty 상태, Controller·Agent 이미지 ID, 실행에 사용한 설정 전체의 SHA-256으로 결정합니다.
성능 측정과 프로파일링의 `agent_image_id`는 Fleet 이미지이고 시나리오는 개별 Agent 이미지이므로 서로 다른 identity가 됩니다. 식별 정보와 대표 결과는 `var/result/build-<sha256>/build.json`에 기록합니다.

`var/result/<build-key>/performance/<suite-id>/<balance|single>/<level>/`에 표 2의 근거를 남깁니다.

|파일|의미|
|---|---|
|`readiness.prom`|준비 단계 마지막 관측|
|`metrics-start.prom`, `metrics-end.prom`|측정 양끝 원본 메트릭|
|`measurement.json`|실제 snapshot 시각, Controller PID·CPU jiffies·CLK_TCK|
|`samples/*.prom`|측정 창 안의 메트릭 표본|
|`samples/*.docker-stats.jsonl`, `samples/*.json`|자원 표본과 수집 시작·종료 시각|
|`assessment.json`|측정 유효성, 표본 부하 조건, 명령 계수, 설정된 기준의 판정|
|`controller.jsonl`, `docker-stats.jsonl`|Controller 원문 로그와 종료 시점 자원 snapshot|
|`result.json`|runner 실행·측정 절차의 성공/실패|

*표 2. 성능 측정 산출물*

`DDCS_PERF_SAMPLE_INTERVAL`은 표본 수집 사이 대기 간격이며 기본 5초입니다. Docker 자원 수집 시간도 실제 측정 창에 포함되므로 정확히 5초마다 수집되는 것은 아닙니다. sample 사이의 짧은 폭증이나 단절은 놓칠 수 있습니다. 종료 시점 `docker-stats.jsonl` 한 파일만으로 전체 구간의 자원 사용을 대표하지 않습니다.

각 실행은 실제 사용한 Compose와 설정 사본·Fleet 배치를 보존합니다. manifest는 실행 조건, source revision·dirty, 이미지 ID, 실제 설정 hash 및 산출물 checksum을 기록합니다. `source_dirty=true`나 preflight warning을 숨기지 않고 해당 조건의 로컬 관측 자료로 표시합니다. 커밋되지 않은 소스는 revision 하나만으로 재현되지 않으므로 별도 소스 snapshot/hash가 필요합니다.

## 계산과 판정

### 유입 부하와 tick 처리

현재 배포 설정의 heartbeat 500ms·Status 1,000ms 주기에서는 약 3N message/s가 유입되고 명령 ack/outcome이 추가됩니다. 총 수신량이 3N을 넘는 것만으로 두 보고 종류가 각각 주기를 지켰다고 판단할 수는 없습니다. 부족하면 생성기와 Controller의 지연·전송 정체를 함께 조사합니다.

tick 평균은 `Δduration_total / Δticks`로 계산합니다. 실제 작업 시간과 예약 대비 시작 지연·건너뛴 주기는 별도로 확인합니다. `_max`는 등록 구간을 포함한 프로세스 시작 이후의 누적 최대입니다.

Controller CPU 평균은 동일 PID의 jiffies 차이를 실제 측정 시간과 CLK_TCK로 환산합니다. **100%는 논리 CPU 한 개**의 사용에 해당하며, 읽을 수 없으면 N/A입니다.

### 명령 지연과 처리 계수

명령 평균 RTT는 `Δrtt_sum / Δrtt_count`입니다. 성공 outcome에 한정되므로 실패·대체·미결을 함께 보고해야 합니다. 이 값은 Status 관측부터 장치의 물리 반응까지의 종단 지연과 다릅니다.

p99는 같은 측정 창의 histogram bucket 차이로 판단합니다. bucket 상한은 정확한 p99 값이 아니며, 측정 레벨보다 긴 쿼리 창을 사용하면 다른 레벨의 관측이 섞일 수 있습니다.

명령 계수는 각 snapshot에서 `dispatched = succeeded + failed + superseded + pending`을 검사합니다. 초기 dispatch 실패는 dispatched에 들어가기 전의 실패이므로 별도로 기록합니다.

### 판정 결과

runner의 `passed`는 실행·측정 절차 성공입니다. `assessment.json`에서 표 3의 판정을 각각 확인합니다.

|판정|범위|
|---|---|
|`measurement`|필수 메트릭·증가하는 카운터·PID·시간 구간|
|`workload.sampled_connections_and_groups`|양끝과 구간 내 표본에서 목표 연결·그룹 대수|
|`command_accounting`|논리 명령 계수 불변식|
|`configured_slo`|명시적으로 설정한 기준만 비교|

*표 3. 실행 성공과 구분해 확인할 판정*

선택 기준은 `DDCS_PERF_SLO_MEAN_RTT_MS`, `DDCS_PERF_SLO_MAX_TICK_SKIPPED`, `DDCS_PERF_SLO_MAX_LIVENESS_CLOSED`, `DDCS_PERF_SLO_MAX_TERMINAL_FAILURES`, `DDCS_PERF_SLO_MAX_DISPATCH_FAILURES`입니다. 미설정 항목은 합격으로 간주하지 않습니다. 위 실행 예시는 네 가지 실패 계수의 증가 0을 요구하며, 지연은 별도 요구치 없이 관측값을 보고합니다.

## 해석의 경계

측정할 때에는 Agent 수와 Group 배치, Fleet 크기, 보고 주기, 안정화·관측 시간, 소스·설정·이미지와 호스트 환경을 결과에 함께 남깁니다. Fleet과 Controller의 CPU·메모리를 같이 확인해야 생성기의 한계와 Controller의 한계를 구분할 수 있습니다.

단일 개발 호스트의 모의 장치 시험은 실제 다중 호스트 네트워크나 물리 장치의 성능을 보장하지 않습니다. 조건별 한 번의 관측으로 장기 안정성이나 최대 수용량을 단정할 수도 없습니다. 기능 장애 시험은 별도 소규모 구성의 [시나리오](scenario.md)에서 다루며, 대규모 연결 시험과 합쳐 모든 장애를 해당 규모에서 검증했다고 표현하지 않습니다.

프로파일을 켠 결과를 비교할 때는 [계측 비용](profiling.md#계측-비용)도 확인합니다. 지표 정의와 조회식은 [메트릭](metrics.md)에 정리했습니다.
