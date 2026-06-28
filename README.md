# DDCS -- Distributed Device Control System

분산 장치 제어 시스템. 단일 **Controller**가 다수의 **Agent**를 조율하고, 각 Agent는 하나의 **Device**를 호스팅한다. 상태/신원의 진실의 원천은 Agent 쪽 Device에 있고, Controller는 그 투영인 **DeviceShadow**만 보관하며 Group 단위 **Policy**로 각 Device의 Mode를 자동 제어한다(사람이 직접 내리는 명령 API는 없다).

> 용어는 [docs/GLOSSARY.md](docs/GLOSSARY.md), 구조/동작은 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), 와이어 프로토콜은 [docs/PROTOCOL.md](docs/PROTOCOL.md)가 권위다.

## 주요 기능

- 단일 스레드 **edge-triggered epoll 리액터**(Controller/Agent 양쪽), generation-token 핸들러 테이블
- 2계층 wire 프로토콜: `wire::frame`(프레이밍) + `wire::message`(메시지)
- 등록 **3-way 핸드셰이크** + 세션별 liveness + kick-old(new-wins)
- Agent **자동 재접속**(지수 backoff + jitter, cap 30s; 등록 성공 시 리셋)
- **양방향 명령 RPC**: `(DeviceId, command_id)` 상관 + supersede + 재전송/dedup
- **히스테리시스 정책 엔진**: Group 부하 밴드(집계) + device별 온도 트립 -> Mode 자동 전환, 플래핑 방지
- **Prometheus** 메트릭(Controller, HTTP `:9000`, group 라벨 포함) + **NDJSON 구조화 로그**
- 역할별 단일 JSON 런타임 설정, ObjectPool zero-copy 버퍼, RAII fd
- ASan/UBSan preset, GoogleTest 단위 + E2E

## 빠른 시작

요구: Ubuntu 24.04+ (x86_64), GCC 13+, CMake 3.25+, C++20.

```sh
# configure + build + test 한 번에
cmake --workflow --preset debug

# 또는 단계별
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

로컬 실행(기본 설정 파일 `config/controller.json`, `config/agent.json` 사용):

```sh
./build/debug/bin/ctrl &      # 8080 listen, 9000 메트릭
./build/debug/bin/agent       # 127.0.0.1:8080 에 연결
```

## 빌드 프로파일

| Preset      | 용도      | 비고                                            |
| ----------- | --------- | ----------------------------------------------- |
| `debug`     | 개발      | 디버그 심볼                                     |
| `coverage`  | 커버리지  | gcov 계측 (`scripts/coverage-report.sh` 참고)   |
| `asan`      | 위생 검사 | ASan + UBSan (`RelWithDebInfo`)                 |
| `release`   | 배포      | `-O2`, 경고 = 에러                              |
| `benchmark` | 벤치마크  | Release + 벤치 옵션 (벤치 타깃은 아직 스캐폴딩) |

```sh
cmake --workflow --preset asan
cmake --workflow --preset release
```

## 테스트

테스트는 두 계층이다: 모듈별 단위 테스트(`lib/**/test/unit`, 일부는 실제 소켓/Reactor를 쓰는 통합 성격)와 Controller<->Agent 왕복 E2E(`test/e2e`).

```sh
# 전체 실행
ctest --test-dir build/debug --output-on-failure

# 이름으로 필터 (예: wire 코덱만)
ctest --test-dir build/debug -R wire --output-on-failure

# 위생 검사로 전체
cmake --workflow --preset asan

# 커버리지 HTML (gcovr 필요) -> build/coverage/html/index.html
./scripts/coverage-report.sh
```

## 성능

컨트롤러는 단일 스레드 리액터다(한 코어). 주기 **sweep tick** 하나가 명령 재전송 + monitor sweep + 정책 평가를 모두 처리하므로, **tick 작업 시간이 sweep 주기(기본 1s)에 근접하면 그 코어가 포화**다. 이 시간은 `ddcs_sweep_duration_us`(직전 / `_max` 피크)와 `ddcs_sweep_duration_us_sum`/`ddcs_sweep_ticks_total`(평균)로 노출된다.

`scripts/perf-ramp.sh`가 agent 수를 늘려가며 레벨별로 이 지표를 캡처한다:

```sh
DDCS_PERF_LEVELS="30 60 120" DDCS_PERF_SOAK=25 scripts/perf-ramp.sh
```

로컬 측정 예(컨테이너 agent, 단일 호스트):

| agents | sweep_avg_us | sweep_max_us | cpu_pct | pending | rtt_ms |
| ------ | ------------ | ------------ | ------- | ------- | ------ |
| 30     | 189          | 343          | 0.6     | 0       | 3      |
| 60     | 472          | 770          | 1.4     | 0       | 3      |
| 120    | 902          | 1596         | 2.3     | 0       | 4      |

sweep 작업은 agent 수에 거의 선형(약 8us/agent)이고, 120대에서도 1s 주기의 **0.1% 미만**(CPU ~2%)이다. 선형 외삽하면 sweep 포화는 코어당 10^4~10^5 대 규모라, 실제로는 그 전에 다른 한계(소켓 fd, 메모리, 네트워크)가 먼저 묶인다. 모든 레벨에서 `pending`/`evicted`가 0이고 RTT는 수 ms로, backpressure 없이 건강하다. RTT는 평균뿐 아니라 `ddcs_command_rtt_ms` **히스토그램**으로도 노출돼 `histogram_quantile`로 p99 꼬리까지 본다(평균만으론 꼬리가 안 드러남).

> localhost loopback이라 RTT는 과소평가다(sub-ms~ms). latency를 사실적으로 보려면 netem 등으로 지연을 주입한다. CPU-bound 확장성은 이 측정으로 충분하다.

## Docker / 배포

Controller 1 + Agent 3 + 관측 스택(Prometheus/Grafana)을 한 번에 띄운다. 각 Agent는 고정 `DDCS_DEVICE_ID`(= DeviceId)로 식별되어 kick-old 검증에 쓸 수 있다.

```sh
docker compose -f docker/docker-compose.yml up --build -d
docker compose -f docker/docker-compose.yml logs -f
docker compose -f docker/docker-compose.yml down
```

관측: Grafana `http://localhost:3000`(익명 Viewer), Prometheus `http://localhost:9090`, 원시 메트릭 `http://localhost:9000/metrics`. Group별 부하/온도/모드 분포를 보는 멀티존 스케일 테스트는 별도 파일이다: `docker compose -f docker/docker-compose.scale.yml up -d`(zone별 N대씩, `DDCS_SIM_*` 환경변수로 device 거동 조절).

`docker/Dockerfile`은 멀티스테이지/멀티타깃이다: `builder`가 release로 두 바이너리를 빌드하고, `controller`/`agent` 타깃이 각 바이너리만 담은 런타임 이미지를 만든다. `controller` 이미지는 `config/`를 번들하고 `8080`(wire)/`9000`(메트릭)을 노출한다.

```sh
# 단일 이미지만 빌드
docker build -f docker/Dockerfile --target controller -t ddcs-controller .
```

### 데모 시나리오

`scripts/demo.sh`가 네 가지 핵심 동작을 각각 격리해 띄우고, 컨트롤러의 원시 출력(메트릭 `:9000` + 이벤트 로그)으로 단언한 뒤 PASS/FAIL과 종료코드를 낸다(관측 스택 없이 돌아 CI 친화적). 각 시나리오는 자기 스택을 띄웠다 정리한다.

```sh
scripts/demo.sh thermal    # per-device thermal: 같은 zone에서 과열된 device만 safe로 빠진다
scripts/demo.sh eviction   # 재접속(리부트)한 device를 재명령 (stale 명령 belief 고착 방지)
scripts/demo.sh regime     # 부하 밴드 busy/idle 전환 (히스테리시스)
scripts/demo.sh fault      # docker pause로 liveness 축출 -> 재개 시 재접속
scripts/demo.sh all        # 네 시나리오 순차 실행
```

각 시나리오가 검증하는 동작은 ARCHITECTURE의 "정책 엔진"/"세션 생명주기" 절과 1:1이다. 같은 동작을 Grafana로 눈으로 보려면 위 compose 스택을 직접 띄운다.

## 설정

런타임 설정은 역할별 **단일 JSON 파일**이다: controller는 `config/controller.json`, agent는 `config/agent.json`. 키는 점 경로로 중첩 object를 가리키고(`session.heartbeat_interval_ms`), 값 우선순위는 **환경변수 > 파일 > 코드 기본값**이다. 시간(ms) 키는 환경변수 override가 없다(아래 표의 env 빈칸). 메커니즘 설명은 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)의 "관측성과 설정" 절 참고.

| 키                                  | 역할       | 기본값      | 환경변수               | 설명                                   |
| ----------------------------------- | ---------- | ----------- | ---------------------- | -------------------------------------- |
| `transport.host`                    | agent      | `127.0.0.1` | `DDCS_TRANSPORT_HOST`  | 연결할 Controller 호스트/IP            |
| `transport.port`                    | both       | `8080`      | `DDCS_TRANSPORT_PORT`  | Controller listen / Agent connect 포트 |
| `transport.bind_address`            | controller | `0.0.0.0`   | --                     | wire listen 바인드 주소                |
| `transport.accept_backlog`          | controller | `128`       | --                     | listen backlog                         |
| `transport.reconnect_base_delay_ms` | agent      | `1000`      | --                     | 재연결 backoff 시작값(지수 증가)       |
| `transport.reconnect_max_delay_ms`  | agent      | `30000`     | --                     | 재연결 backoff 상한(cap)               |
| `prometheus.port`                   | controller | `9000`      | `DDCS_PROMETHEUS_PORT` | 메트릭 포트                            |
| `prometheus.bind_address`           | controller | `0.0.0.0`   | --                     | 메트릭 listen 바인드 주소              |
| `controller.sweep_interval_ms`      | controller | `1000`      | --                     | 주기 sweep(재전송/축출/정책 평가) 간격 |
| `session.handshake_timeout_ms`      | controller | `3000`      | --                     | 핸드셰이크 단계별 시한                 |
| `session.liveness_timeout_ms`       | controller | `3000`      | --                     | active 세션 liveness 시한              |
| `command.timeout_ms`                | controller | `5000`      | --                     | 명령 응답 대기 시한                    |
| `command.max_attempts`              | controller | `3`         | --                     | 명령 전송 시도 횟수(1이면 무재전송)    |
| `command.backoff_base_ms`           | controller | `500`       | --                     | 명령 재전송 backoff base               |
| `session.heartbeat_interval_ms`     | agent      | `1000`      | --                     | heartbeat 주기                         |
| `session.status_report_interval_ms` | agent      | `5000`      | --                     | status 보고 주기                       |
| `session.registration_timeout_ms`   | agent      | `2000`      | --                     | register_outcome 대기 시한             |
| `log.level`                         | both       | `info`      | `DDCS_LOG_LEVEL`       | debug / info / warn / error            |

> `transport.bind_address` / `prometheus.bind_address`는 **현재 미배선**이다 -- 두 리스너 모두 `0.0.0.0`(INADDR_ANY)로 고정 바인드하며 파일 값은 읽지 않는다(향후 배선 대비 placeholder 키).

설정 파일 경로는 `DDCS_CONFIG_PATH`(기본 controller `config/controller.json`, agent `config/agent.json`)로 바꾼다. Group은 agent의 `DDCS_DEVICE_GROUP`(기본 `zone_a`)로 선언한다. Agent 신원은 `DDCS_DEVICE_ID` > `DDCS_DEVICE_ID_FILE`(기본 `data/agent.uuid`) 읽기 > 생성/기록 순으로 해석된다.

> 신원은 Device의 것(**DeviceId**)이다. `DDCS_DEVICE_ID`는 그 DeviceId를 고정하는 수단일 뿐, agent 프로세스의 신원이 아니다.

정책은 `controller.json`의 `policy.groups`에 인라인된다. Group별 규칙은 load 히스테리시스(`high_load`/`low_load` + `high_load_mode`/`low_load_mode`)와 선택적 **온도 override**(`high_temp`/`resume_temp`/`high_temp_mode`)다. busy/idle은 부하 밴드의 regime이고 각각이 Group의 목표 Mode로 매핑되며, **device별** 온도가 `high_temp`를 넘으면 그 device만 load와 무관하게 `high_temp_mode`로 빠진다 -- 자세한 동작은 ARCHITECTURE "정책 엔진" 절 참고.

## 운영 / 문제 해결

| 증상                                  | 원인                                             | 동작                                                                     |
| ------------------------------------- | ------------------------------------------------ | ------------------------------------------------------------------------ |
| Controller 즉시 종료                  | listen/메트릭 포트 점유(EADDRINUSE) 등 기동 실패 | stderr에 `fatal` 한 줄 + `exit 1` (SIGABRT 아님 -- `main`이 예외를 잡음) |
| 프로세스 `exit 1`                     | 설정 JSON malformed                              | 로드 중 throw -> `main`이 잡아 종료                                      |
| `session.register.unknown_group` 경고 | Agent가 정책에 없는 Group으로 등록               | 등록은 허용되나 정책 명령 대상에서 제외(soft)                            |
| `agent.device_id_ephemeral` 경고      | UUID가 env/파일로 고정되지 않음                  | 동작하나 재시작 시 새 DeviceId(kick-old 검증 깨짐)                       |
| Agent 연결 반복 실패                  | Controller 부재/미기동                           | 지수 backoff로 재시도(1->30s), 등록 성공 시 리셋                         |
| Agent가 갑자기 끊김                   | Controller liveness timeout 축출                 | Agent가 hangup으로 관측 -> 재접속                                        |

관측 지점: 메트릭 `ddcs_agents_evicted_total`/`ddcs_handshake_expired_total`/`ddcs_commands_gave_up_total`/`ddcs_commands_stale_total`, group별 `ddcs_group_load_avg`/`ddcs_group_temp_avg`/`ddcs_group_devices`, 로그 `event` 키(`session.kick_old`, `policy.hot`, `device.status.non_finite` 등). 메트릭 전체 목록은 ARCHITECTURE "관측성과 설정" 절 참고.

## 문서

| 문서                                         | 내용                                                                              |
| -------------------------------------------- | --------------------------------------------------------------------------------- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 시스템 개관, 모듈 지도, 런타임 / 세션 / 정책 / 명령 / 재접속 / 관측성 + 설계 근거 |
| [docs/PROTOCOL.md](docs/PROTOCOL.md)         | wire 프로토콜(frame/message 레이아웃, 메시지 타입, 의미론) -- 권위                |
| [docs/GLOSSARY.md](docs/GLOSSARY.md)         | 도메인 어휘(ubiquitous language) -- 권위                                          |

## 디렉토리 구조

```
apps/            # 실행 파일 (ctrl, agent)
lib/
  common/        # 순수 C++ 값 타입 (strong id, uuid, 버퍼, object pool, clock, endian)
  io/            # epoll 리액터, timerfd/signalfd, Fd RAII
  net/           # 소켓 프리미티브 (stream_io, socket)
  json/          # JSON 파싱/쓰기
  logger/        # NDJSON 구조화 로거
  config/        # 단일 파일 JSON 설정 로더
  device/        # 공유 어휘 커널 (Mode)
  wire/          # wire 프로토콜 (frame / message)
  ctrl/          # controller 측 (domain / app / infra + facade)
  agent/         # agent 측 (domain / app / infra + facade)
cmake/           # 빌드 옵션 / sanitizer / coverage / testing 모듈
config/          # 런타임 JSON 설정 (controller / agent, 정책 인라인)
scripts/         # coverage-report.sh, demo.sh (데모), perf-ramp.sh (성능 램프)
docker/          # Dockerfile + compose (core / scale / 관측 스택)
docs/            # ARCHITECTURE / PROTOCOL / GLOSSARY
test/e2e/        # Controller <-> Agent E2E
data/            # agent UUID persist (런타임 생성, git 미추적)
```

## 라이선스

[MIT License](LICENSE)
