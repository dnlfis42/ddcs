<div align="center">
<h1>DDCS</h1>
<p><b>C++20 기반 정책 중심 분산 장치 제어 시스템</b></p>
</div>

## 목차

- **[개요](#개요)**
- **[동작 방식](#동작-방식)**
- **[문서 안내](#문서-안내)**
- **[빠른 시작](#빠른-시작)**
- **[빌드와 테스트](#빌드와-테스트)**
- **[문제 해결](#문제-해결)**
- **[라이선스](#라이선스)**

## 개요

작업 구역의 상황은 수시로 달라집니다.
부하가 늘면 장치의 처리 성능을 높여야 하고, 과열된 장치는 보호해야 합니다.</br>
관리자가 상태를 확인하고 조치할 수 있지만, 장치가 늘어날수록 모든 변화에 일일이 대응하기는 어렵습니다.

**DDCS**는 이러한 판단과 제어를 자동화하기 위해 만든 **C++20/Linux 기반 분산 장치 제어 시스템**입니다.</br>
장치가 보고한 상태와 구역별 정책으로 동작 모드를 결정하고, 통신이 끊겼을 때 다시 제어를 이어가도록 구현했습니다.

현재 실행 예제와 검증에는 부하·온도 변화를 모사하는 장치를 사용합니다.

## 동작 방식

각 Agent는 장치 하나를 맡아 Controller에 상태를 주기적으로 보고합니다.</br>
Controller는 구역별 평균 부하율로 기본 동작 모드를 결정하되, 과열된 장치에는 보호 모드를 우선적으로 적용합니다.</br>
결정한 사항은 각 Agent에 명령으로 전달되고, 적용 이후 상태는 다음 보고에 반영되도록 설계했습니다.

그림 1은 상태 보고와 명령 전달이 이어지는 제어 흐름을 보여줍니다.

![상태 보고 → 정책 판단 → 명령 전달 → 장치 적용으로 이어지는 DDCS 제어 흐름](assets/architecture-control-loop.svg)

*그림 1. 장치 상태 보고와 정책에 따른 제어 흐름*

장치 상태에 맞춰 명령을 보내는 것만으로 제어가 끝나지는 않습니다.</br>
작은 부하 변화에도 모드가 계속 바뀔 수 있으므로, DDCS는 모드 전환과 복귀의 기준을 나눠 잦은 전환을 억제합니다.

명령을 전달하는 도중 연결이 끊기거나 응답이 돌아오지 않을 수도 있습니다.</br>
Agent는 연결이 끊기면 재접속하고, Controller는 연결이 복구되면 현재 정책에 맞는 명령을 다시 보냅니다.</br>
응답이 지연되면 같은 명령을 재전송하며, Agent는 직전에 처리한 명령을 다시 받으면 재적용하는 대신 저장한 결과를 반환합니다.

이러한 통신과 제어는 직접 구현한 싱글 스레드 epoll 리액터에서 처리하며, Controller와 Agent는 자체 TCP 프로토콜로 상태와 명령을 주고받습니다.

장치가 정책에 맞게 동작하고 장애 후에도 제어가 이어지는지 확인하기 위해 메트릭과 로그로 상태 변화를 추적합니다. 실행 화면은 [대시보드](docs/metrics.md#대시보드)에 정리했습니다.

부하 전환과 과열 보호, 연결 복구는 자동화 시나리오로 검증해 코드 변경으로 기존 동작이 깨지는 문제를 발견할 수 있도록 했습니다.

## 문서 안내

|문서|내용|
|---|---|
|[아키텍처](docs/architecture.md)|시스템 구성, 내부 처리와 설계 이유|
|[프로토콜](docs/protocol.md)|통신 메시지 형식과 교환 규칙|
|[정책 엔진](docs/policy.md)|부하·온도 판단과 동작 모드 제어|
|[설정](docs/config.md)|실행 설정과 정책 변경 방법|
|[로그](docs/log.md)|로그 형식, 이벤트와 조회 방법|
|[메트릭](docs/metrics.md)|수집 지표, 조회식과 해석|
|[성능 측정](docs/performance.md)|AgentFleet 부하 생성과 성능 측정·판정|
|[프로파일링](docs/profiling.md)|Controller 내부 처리 시간과 계측 비용 분석|
|[검증 시나리오](docs/scenario.md)|제어 동작과 장애 복구 검증|

## 빠른 시작

### 요구 사항

**Docker로 실행**하기 위해서는 다음 환경과 도구가 필요합니다.

|항목|요구 사항|
|---|---|
|컨테이너 실행|Linux 컨테이너를 실행할 수 있는 Docker, Docker Compose v2|
|연결 수 확인|curl, grep|
|대시보드 접속|웹 브라우저|
|사용 가능한 호스트 포트|8080(장치 통신), 9000(메트릭), 9090(Prometheus), 3000(Grafana)|

**로컬 빌드와 검증**에는 수행할 작업에 따라 다음 도구가 필요합니다.

|항목|요구 사항|
|---|---|
|로컬 빌드 환경|Linux|
|C++ 도구 모음|C++20 지원 컴파일러와 표준 라이브러리|
|빌드 도구|CMake 3.25 이상, Make 등 선택한 CMake 생성기에 맞는 도구|
|스크립트 회귀 테스트|로컬 빌드 환경·도구 + Bash, Python 3, jq|
|ASan·UBSan 검사|로컬 빌드·테스트 환경 + ASan·UBSan 지원 컴파일러와 런타임|
|커버리지 측정|로컬 빌드·테스트 환경 + GCC, 버전이 맞는 gcov, gcovr|
|기능 시나리오 검증|Docker 실행 환경 + Linux, Bash, Git, jq, 기본 명령 도구|
|성능 측정|기능 시나리오 검증 환경 + Python 3|
|프로파일 수집|성능 측정 환경 + 로컬 빌드 도구, Release로 빌드한 `profile-report`·`profile-verify`|

스크립트 회귀 테스트는 기본 구성에 포함되며, `DDCS_ENABLE_SCRIPT_TEST=OFF`로 제외할 수 있습니다.

`gcovr`와 `gcov`는 커버리지 측정에 필요하며, 일반 Docker 실행에는 필요하지 않습니다.</br>
Sanitizer와 커버리지 실행 방법은 [빌드와 테스트](#빌드와-테스트)에 정리했습니다.

측정 시 호스트 조건은 [성능 측정](docs/performance.md)에 정리했습니다.

### 빠른 실행

아래 명령을 저장소 루트에서 실행하면 Controller 1대, Agent 4대와 Prometheus·Grafana가 함께 기동됩니다.

```sh
docker compose -f docker/docker-compose.yml up --build -d
```

Agent 4대가 Controller에 접속했는지 연결 수를 확인합니다. 등록 중인 연결도 포함되므로, 이 값만으로 상태 보고 완료까지 확인할 수는 없습니다.

```sh
curl -fsS http://localhost:9000/metrics | grep '^ddcs_connections '
```

```text
ddcs_connections 4
```

기동 직후 조회가 실패하거나 연결 수가 작으면 잠시 기다린 뒤 다시 확인합니다.</br>
[Grafana](http://localhost:3000)에서는 로그인 없이 상태를 볼 수 있고, [Prometheus](http://localhost:9090)에서는 메트릭을 직접 조회할 수 있습니다.

<details>
<summary>로그 확인과 실행 중 정책 변경</summary>

다음 명령으로 서비스 로그를 확인합니다.

```sh
docker compose -f docker/docker-compose.yml logs -f
```

`Ctrl+C`는 로그 보기만 종료하며 스택은 계속 실행됩니다.

`config/controller.json`의 `policy.groups`에서 임계값이나 목표 모드를 수정한 뒤, SIGHUP으로 새 정책을 적용할 수 있습니다.

```sh
docker compose -f docker/docker-compose.yml kill -s SIGHUP controller
```

정책 외 설정을 바꾸려면 해당 프로세스를 재시작해야 합니다.</br>
각 설정의 의미는 [설정 가이드](docs/config.md#정책)를 참고하시기 바랍니다.

</details>

실행을 마치고 모든 서비스를 종료하려면 다음 명령을 사용합니다.

```sh
docker compose -f docker/docker-compose.yml down
```

### Docker 구성

기본 시연 외에도 여러 구역의 제어를 확인하거나, 한 구역에 많은 Agent를 연결하는 구성을 선택할 수 있습니다.</br>
모든 구성은 Controller 1대를 사용합니다.

그룹별 대수를 직접 지정하려면 실행 스크립트를 사용하면 됩니다.</br>
`-m`은 Prometheus·Grafana를 함께 실행하며, 그룹마다 Fleet 컨테이너 하나를 만듭니다.

```sh
# 그룹별 직접 지정
scripts/run/stack.sh up -m -g zone_a,2000 -g zone_b,3000

# 네 그룹에 2,500대씩 균등 배치
scripts/run/stack.sh up -m --group-count 4 --agents-per-group 2500

# 종료
scripts/run/stack.sh down
```

자동 배치는 `zone_a`부터 이름을 부여하며, 정책이 없는 그룹은 경고를 출력합니다.

실행 구성은 `var/run/stack/compose.json`에 저장합니다. 기존 Compose 스택과 포트가 겹치므로 먼저 종료해야 합니다.

|구성|Agent 배치|관측 도구|Compose 파일|
|---|---|---|---|
|기본 시연|4개 구역 × 1대, 고정 DeviceId|Prometheus·Grafana|[docker-compose.yml](docker/docker-compose.yml)|
|다중 구역 검증|4개 구역 × 25대, 총 100대|Prometheus·Grafana|[docker-compose.scale.yml](docker/docker-compose.scale.yml)|
|단일 구역 대규모 부하|Fleet 1개에서 N대 실행, 기본 1,000대|Controller 메트릭|[docker-compose.fleet.yml](docker/docker-compose.fleet.yml)|
|단일 구역 대규모 부하 관찰|Fleet 1개에서 N대 실행, 기본 1,000대|Prometheus·Grafana|[docker-compose.fleet-monitoring.yml](docker/docker-compose.fleet-monitoring.yml)|

<details>
<summary>구성별 실행과 종료</summary>

저장소 루트에서 실행하며, `-f` 뒤에 선택한 Compose 파일을 지정합니다.</br>
구성 간 호스트 포트가 겹치므로 기존 스택을 종료한 뒤 다른 구성을 실행합니다.

예를 들어 다중 구역 검증 구성은 다음과 같이 실행합니다.

```sh
docker compose -f docker/docker-compose.scale.yml up --build -d
docker compose -f docker/docker-compose.scale.yml ps
docker compose -f docker/docker-compose.scale.yml logs -f
```

`Ctrl+C`는 로그 보기만 종료합니다. 서비스를 종료할 때는 실행 시 선택한 파일과 같은 파일을 지정합니다.

```sh
docker compose -f docker/docker-compose.scale.yml down
```

기본 시연 구성은 DeviceId가 고정되어 컨테이너를 재시작해도 같은 Device로 등록됩니다.</br>
장애 주입 절차는 [수동 장애 주입](docs/scenario.md#수동-장애-주입), Fleet의 대수·구역 설정은 [AgentFleet](docs/performance.md#부하-생성기-agentfleet)에 정리했습니다.

</details>

<details>
<summary>이미지 빌드와 환경변수 설정</summary>

`docker/Dockerfile`은 멀티스테이지 빌드로 `ctrl`, `agent`, `agent-fleet`을 Release 설정으로 빌드합니다.</br>
`controller`·`agent`·`agent-fleet` 타깃은 각각 해당 바이너리를 담은 런타임 이미지를 만듭니다.

Device 동작을 바꾸는 `DDCS_SIM_NOISE`·`DDCS_SIM_JITTER`는 Compose 파일의 Agent `environment`에 지정합니다.</br>
셸에서 `export`하는 것만으로는 컨테이너에 전달되지 않습니다.

</details>

## 빌드와 테스트

[요구 사항](#요구-사항)의 로컬 빌드·테스트 도구를 준비한 뒤 저장소 루트에서 다음 명령을 실행합니다.
설정 생성, 컴파일, 테스트를 차례로 수행합니다.

```sh
cmake --workflow --preset debug
```

결과는 `build/debug/`에 생성됩니다.

테스트에는 모듈별 단위 테스트, 실제 TCP 소켓을 사용하는 E2E, Docker 명령을 모의 실행하는 스크립트 회귀 테스트가 포함됩니다.

<details>
<summary>단계별 실행과 테스트 범위 선택</summary>

각 단계를 따로 실행하려면 다음 명령을 사용합니다.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

코드를 수정한 경우 테스트 전에 다시 빌드해야 합니다.
`ctest`는 빌드를 수행하지 않습니다.

|확인할 범위|명령|
|---|---|
|wire 프로토콜|`ctest --preset debug -R wire`|
|E2E|`ctest --preset debug -R e2e`|
|직전에 실패한 테스트|`ctest --preset debug --rerun-failed`|

실패한 테스트의 출력은 자동으로 표시됩니다.

C++ 빌드와 테스트만 필요하면 아래 옵션으로 스크립트 테스트를 제외할 수 있습니다.

```sh
cmake --preset debug -DDDCS_ENABLE_SCRIPT_TEST=OFF
```

다시 포함하려면 옵션을 `ON`으로 지정합니다.
Docker 이미지 빌드에서는 `OFF`를 사용합니다.

</details>

<details>
<summary>최적화 빌드, Sanitizer와 커버리지</summary>

|목적|명령|
|---|---|
|배포용 최적화 빌드·테스트|`cmake --workflow --preset release`|
|ASan·UBSan 검사|`cmake --workflow --preset asan`|
|코드 커버리지|`scripts/coverage-report.sh`|

각 빌드 결과는 `build/<preset>/`에 생성됩니다.</br>
테스트만 다시 실행할 때는 `ctest --preset` 뒤에 `release`, `asan`, `coverage`를 지정합니다.

커버리지 측정에는 `gcovr`가 추가로 필요하며, HTML 리포트는 `build/coverage/html/index.html`에 생성됩니다.

</details>

<details>
<summary>실제 스택의 장애 복구 검증과 성능 측정</summary>

자동 테스트 외에 Docker 스택을 기동해 과열 보호, 부하 전환, 재접속과 정책 변경을 확인할 수 있습니다.</br>
검증 절차와 판정 기준은 [시나리오](docs/scenario.md)에 정리했습니다.

대규모 부하 생성에는 [AgentFleet](docs/performance.md#부하-생성기-agentfleet)을 사용합니다.</br>
[성능 측정](docs/performance.md) 스크립트는 부하 구성과 결과 분석에 Python 3를 추가로 사용하며, 빌드의 `DDCS_ENABLE_SCRIPT_TEST` 설정과 무관하게 호스트에 설치되어 있어야 합니다.

</details>

## 문제 해결

먼저 컨테이너 상태와 최근 로그를 확인합니다.</br>
다른 구성을 사용 중이면 `-f` 뒤의 경로를 해당 Compose 파일로 바꿉니다.

```sh
docker compose -f docker/docker-compose.yml ps -a
docker compose -f docker/docker-compose.yml logs --tail 50
```

|증상·로그|확인할 항목|조치|
|---|---|---|
|기동 실패</br>`Address already in use`|포트 점유 확인</br>`lsof -i :8080`</br>`lsof -i :9000`|본인 테스트 스택인지 확인한 뒤 해당 스택 종료</br>다른 프로세스는 임의로 종료하지 않음|
|설정 오류</br>`config: malformed JSON`|오류에 표시된 JSON 파일의 문법</br>예: `jq . config/controller.json`|문법 수정 후 기동 재시도</br>정책 리로드 중 오류라면 이전 정책 유지|
|Agent가 연결을 반복 시도함|Controller 실행 상태</br>Agent의 `transport.host`·`transport.port`</br>네트워크 연결|접속 대상과 네트워크 확인</br>Docker에서는 컨테이너에 전달된 환경변수도 확인|
|연결이 끊긴 뒤 재접속함|종료 로그의 `reason`</br>`liveness_expired`이면 상태 보고·heartbeat 지연 여부|Agent 정지·CPU 부하·네트워크 확인</br>heartbeat 주기·liveness 제한 시간 확인|
|정책 명령이 적용되지 않음</br>`device.group.unknown`|Agent의 Group이</br>`policy.groups`에 정의돼 있는지 확인|해당 Group의 정책 추가</br>SIGHUP으로 리로드|
|신원 저장 실패</br>`device.id.not_persisted`|`DDCS_DEVICE_ID_FILE` 경로</br>해당 경로의 쓰기 권한|저장 경로·권한 수정</br>또는 Agent별 고유 `DDCS_DEVICE_ID` 지정|
|Fleet 기동 실패</br>`nofile limit too low`|로컬 실행 셸의 `ulimit -n`</br>또는 Compose의 `ulimits.nofile`|파일 디스크립터 한도를</br>Agent 수 N + 32 이상으로 설정|

연결 종료 원인은 `ddcs_connections_closed_total`의 `reason` 라벨과 로그를 함께 확인합니다.</br>
설정 키는 [설정 가이드](docs/config.md), 지표와 이벤트는 [메트릭](docs/metrics.md)·[로그](docs/log.md)를 참고합니다.

## 라이선스

[MIT License](LICENSE)
