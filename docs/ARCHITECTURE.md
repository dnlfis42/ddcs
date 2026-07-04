# DDCS 아키텍처

단일 **Controller**가 다수의 **Agent**를 조율하고, 각 Agent는 정확히 하나의 **Device**를 호스팅한다(1 agent : 1 device : 1 session).
상태와 신원의 진실의 원천은 Agent 쪽 Device에 있고, Controller는 그 투영인 **DeviceShadow**만 보관한다.
Controller는 각 Device를 Group 단위 **Policy**로 자동 조율한다. 사람이 직접 명령을 내리는 API는 없으며, 명령은 오직 정책 엔진이 발행한다.

## 목차

1. [시스템 컨텍스트](#1-시스템-컨텍스트)
2. [코드 구조 (모듈 지도)](#2-코드-구조-모듈-지도)
3. [런타임 모델: 단일 스레드 리액터](#3-런타임-모델-단일-스레드-리액터)
4. [전송과 프로토콜](#4-전송과-프로토콜)
5. [세션 생명주기](#5-세션-생명주기)
6. [정책 엔진](#6-정책-엔진)
7. [명령 RPC와 상관](#7-명령-rpc와-상관)
8. [에이전트 재접속](#8-에이전트-재접속)
9. [관측성](#9-관측성)
10. [설정](#10-설정)

## 1. 시스템 컨텍스트

시스템은 단일 Controller와 다수의 Agent(각각 Device 하나), 그리고 외부 행위자인 operator와 Prometheus로 구성된다.

전체 관계는 다음 다이어그램과 같다:

```mermaid
graph LR
  oper["operator"]
  prom["Prometheus"]
  subgraph ctrl["Controller (단일 서버)"]
    subgraph core["Core"]
      session["세션 관리"]
      policy["정책 엔진"]
    end
    shadows["DeviceShadow 저장소<br/>key = DeviceId"]
    metrics["HTTP metrics :9000"]
  end
  subgraph agents["Agent x N (클라이언트)"]
    device["Device<br/>DeviceId, 진실의 원천"]
  end

  oper -. "controller.json 정책<br/>부팅 로드 + SIGHUP 리로드" .-> core
  agents -- "Register / Status / Heartbeat" --> core
  core -- "Command (정책이 발행)" --> agents
  prom -- "scrape" --> metrics
  core -. "보고된 Status를 캐시" .-> shadows
  session <--> policy
```

각 행위자의 역할은 다음과 같다:

| 행위자           | 역할                                                                                                                             |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| **Controller**   | 다수 Device의 집합적 Mode를 Group 단위 Policy로 조율하는 단일 서버. Agent를 통해 각 Device를 관측(Status)하고 구동(Command)한다. |
| **Agent**        | 하나의 Device를 호스팅해 Controller에 등록하고 Status를 보고하며 명령을 적용하는 클라이언트.                                     |
| **Device**       | Agent가 제어하는 실제 장치. 상태와 신원의 진실의 원천.                                                                           |
| **DeviceShadow** | Controller가 DeviceId로 보관하는 Device의 캐시 투영. 재접속을 가로질러 지속된다(Session보다 오래 산다).                          |
| operator         | `config/controller.json`의 인라인 정책을 주입한다. 런타임 명령 API는 없다. (어휘 밖 외부 행위자)                                 |
| Prometheus       | Controller의 HTTP 메트릭 엔드포인트(`:9000`)를 긁는다. (어휘 밖 외부 행위자)                                                     |

Controller 쪽에는 Agent라는 엔티티가 따로 없다. Controller는 상대를 **Session**(휘발성 연결 관계)과 **DeviceShadow**(DeviceId로 지속되는 캐시)로만 모델링하고, "Agent"라는 이름은 클라이언트 액터와 agent-side 코드에만 존재한다.

## 2. 코드 구조 (모듈 지도)

코드는 `lib/`의 라이브러리 모듈과 `apps/`의 두 실행 파일(`ctrl`, `agent`)로 나뉘고, 의존은 위에서 아래로 한 방향으로만 흐른다.

모듈별 레이어와 책임은 다음과 같다:

| 모듈                      | 레이어      | 책임                                                                                                                    |
| ------------------------- | ----------- | ----------------------------------------------------------------------------------------------------------------------- |
| `common`                  | 기반        | 순수 C++ 값 타입 (strong id, uuid, 버퍼, object pool, 시계, endian)                                                     |
| `json`                    | 직렬화      | JSON 파싱/쓰기                                                                                                          |
| `logger`                  | 관측        | JSON Lines(JSONL) 구조화 로깅 (싱글턴)                                                                                  |
| `config`                  | 설정        | 단일 파일 JSON 설정 로더                                                                                                |
| `io`                      | OS          | epoll 리액터, timerfd/signalfd, `Fd` RAII(Resource Acquisition Is Initialization, 자원 획득이 곧 초기화), `throw_errno` |
| `net`                     | OS          | 소켓 프리미티브 (`stream_io`, `socket`)                                                                                 |
| `device`                  | 도메인 커널 | 공유 어휘 `Mode`(safe/normal/performance)와 그 wire 매핑                                                                |
| `wire::frame`             | 프로토콜    | 프레이밍(4B 헤더: magic+length). payload는 opaque                                                                       |
| `wire::message`           | 프로토콜    | message(`[type][body]`)와 command 코덱                                                                                  |
| `ctrl::domain`            | controller  | `DeviceShadow`/`DeviceRegistry`/`GroupPolicy` 등 순수 도메인                                                            |
| `ctrl::app`               | controller  | Session/Command/Policy/Status/Registration/Metrics 서비스 (use-case)                                                    |
| `ctrl::infra`             | controller  | Acceptor/Server, Prometheus HTTP 서버 (어댑터)                                                                          |
| `ctrl`                    | controller  | 컨트롤러 공개 파사드                                                                                                    |
| `agent::domain`           | agent       | `Device` 구현 (시뮬레이션/더미)                                                                                         |
| `agent::app`              | agent       | 세션 로직, 메시지 버퍼 관리 (use-case)                                                                                  |
| `agent::infra`            | agent       | Connector(연결/백오프) 어댑터                                                                                           |
| `agent`                   | agent       | 에이전트 공개 파사드                                                                                                    |
| `apps/ctrl`, `apps/agent` | 실행        | 설정 로드 + 조립 + run()                                                                                                |

```mermaid
graph TD
  apps["apps (실행)<br/>apps/ctrl, apps/agent"]
  subgraph side["controller / agent -- 양측 동일 계층"]
    facade["facade (공개 API)<br/>ctrl, agent"]
    infra["infra (전송 어댑터)<br/>ctrl::infra, agent::infra"]
    app["app (use-case)<br/>ctrl::app, agent::app"]
    domain["domain (순수 도메인)<br/>ctrl::domain, agent::domain"]
    facade --> infra
    facade --> app
    facade --> domain
    infra --> app
    app --> domain
  end
  shared["공유 하부 (한 방향 의존)<br/>프로토콜: wire::frame, wire::message<br/>커널: device<br/>기반/OS: io, net, json, logger, config, common"]

  apps --> facade
  side --> shared
```

각 측(`ctrl`/`agent`)은 동일하게 **domain -> app -> infra -> facade**로 계층화되고, 의존은 한 방향(아래로)으로만 흐른다.

- **domain**: 순수 도메인 상태/규칙(`common`/`device`에만 의존).
- **app**: use-case 서비스. `<side>::app::transport::port`(계약)와 `<side>::app::session`(세션 로직)을 포함한다.
- **infra**: 계약을 충족하는 어댑터(`<side>::infra::transport`). `io`/`net`/`wire::frame`을 알지만 상대(peer)는 모른다.
- **facade**: 공개 API. infra를 PRIVATE로 숨긴다.

의존의 루트는 port 계약("피어를 Connection 단위로 잇고 그 위에서 크기-유한한 typed Message를 주고받는다")이고, wire 프로토콜은 이 계약을 충족하는 메커니즘이라 계약 안의 내부 변경은 port와 app에 닿지 않는다. namespace도 메커니즘(`sfp`)이 아니라 계약 concern(`transport`)으로 명명한다.

wire 코덱은 `device`에 의존하지 않는다. `wire::message`는 `Mode`를 raw `u8`로만 싣고, 어휘<->바이트 매핑(`device::encode_mode`/`decode_mode`)은 `device` 커널이 소유하며, 번역은 app 어댑터 경계에서 일어난다.

## 3. 런타임 모델: 단일 스레드 리액터

각 프로세스(`ctrl`, `agent`)는 **단일 스레드 edge-triggered epoll 리액터** 하나로 동작하며, `lib/`/`apps/` 어디에도 스레드/뮤텍스/atomic이 없다. 동시성은 한 OS 스레드 위의 협력적 I/O 멀티플렉싱이고, 모든 콜백이 그 스레드에서 실행된다.

`io::Reactor`가 `epoll_wait`로 블록하고, 준비된 fd마다 소유 `Channel`의 핸들러를 호출한다.

리액터에 등록되는 여러 **게스트**는 다음과 같다:

- `io::TimerScheduler`: 하나의 timerfd에 다수 논리 타이머를 deadline 최소힙으로 멀티플렉싱
- `io::SignalSource`: signalfd로 SIGINT/SIGTERM(=`stop()`)을 reactor 콜백으로 변환. Agent는 SIGINT/SIGTERM을 등록하고, Controller는 정책 핫리로드를 위해 SIGHUP을 추가로 등록한다.
- 전송 서버/커넥터: Controller는 `Acceptor` + N개 Connection, Agent는 단일 Connection
- Prometheus HTTP 서버: 같은 리액터의 "두 번째 게스트"

Controller의 주기 도메인 작업은 단일 **sweep 타이머**가 구동한다: 매 tick마다 명령 재전송 sweep, 핸드셰이크/liveness 만료 축출, 정책 평가를 순서대로 수행하고 다음 tick을 다시 예약한다.

```mermaid
sequenceDiagram
  participant K as epoll
  participant R as Reactor (단일 스레드)
  participant H as Channel 핸들러
  R->>K: epoll_wait
  K-->>R: 준비된 fd 목록
  loop 각 ready 이벤트
    R->>R: 토큰(generation+fd)으로 Channel 해석
    Note over R: stale 토큰이면 skip
    R->>H: on_ready(events)
    Note over H: accept / recv+framing+dispatch / send<br/>매 wakeup마다 EAGAIN까지 drain
  end
```

리액터는 게스트 종류를 구분하지 않아, timerfd(타이머)/signalfd(시그널)/소켓이 모두 같은 `Channel` 디스패치 경로(위 `H`)를 탄다.

세션 레지스트리, 명령 장부, 정책 상태가 모두 한 스레드에서만 만져지므로 락이 없다. 콜백 안에서 블로킹/CPU 바운드 작업을 하면 루프 전체가 멈추므로, 부팅 시 정책 파일 읽기와 Agent의 1회 DNS 조회를 제외한 핫패스는 전부 논블로킹이다.

epoll 토큰에는 raw 포인터 대신 `(generation<<32 | fd)`를 싣고 fd가 닫혀 재사용되면 generation을 올려서, 커널이 close 직전에 큐잉한 [stale](GLOSSARY.md#세션) 이벤트가 재사용된 fd의 다른 Channel로 오배달되는 use-after-free를 막는다.

연결 해체는 deferred-reap으로 미룬다. 콜백(`on_message`)이 전송을 재진입해 `send()`/`disconnect()`를 부를 수 있어서, Controller 서버는 연결을 reap 큐에 넣고 콜백 종료 후 안전 지점에서 해체하며, frame 추출 루프는 매 반복 rx 버퍼를 재조회해 콜백 안에서 해제된 연결을 만나면 깨끗이 끝난다.

리소스 안전은 RAII 규약 두 가지가 떠받친다.

- `io::Fd`는 이동 전용 소유자이고, `io::Channel`은 **여전히 리액터에 등록된 채 close되면 `std::terminate`** 한다(닫기 전에 반드시 등록 해제). 게스트는 리액터보다 먼저 파괴되어야 한다.
- `common::ObjectPool`은 발급한 핸들보다 오래 살아야 한다(아니면 terminate). 그래서 풀은 핸들을 쥔 연결/큐보다 먼저 선언된다.

단일 스레드 리액터의 확장 한계는 sweep tick의 작업 시간이 결정한다. 실측표와 선형 외삽, loopback 주의는 [README 성능 절](../README.md#성능)이 담는다.

## 4. 전송과 프로토콜

wire 포맷과 의미론은 [PROTOCOL.md](PROTOCOL.md)가 권위이고, 이 절은 구현이 지키는 불변식만 담는다.

프레이밍은 양방향 모두 `wire::frame`이 소유하고 양측 transport가 공유한다. 수신 루프(`extract_frames`)는 rx 링버퍼에서 완전한 frame을 풀링된 버퍼로 뽑아내고, 송신 조립(`reserve_header_room`/`seal`)은 헤더 헤드룸을 미리 확보해 길이 헤더를 제자리에서 prepend하므로 복사가 없다.

불변식은 다음과 같다:

- **rx 버퍼 >= 최대 frame:** rx 링 용량은 2의 거듭제곱이면서 최대 frame(헤더 4 + payload 상한 1024 = 1028) 이상이어야 한다(현재 양측 4096). 양측 모두 `static_assert`로 컴파일 차단한다. 링을 1028 밑으로 줄이면 클램프가 아니라 프레이밍 교착이 난다.
- **Mode<->wire 단일 출처:** 네 wire 경계(Controller의 명령 인코딩과 Status 디코딩, Agent의 명령 디코딩과 Status 인코딩)가 전부 `device::encode_mode`/`decode_mode`를 경유한다. 경계에 raw `static_cast<Mode>`를 재도입하면 컴파일은 통과하지만 enum 재정렬 시 조용히 어긋난다.

## 5. 세션 생명주기

**Session**은 Controller가 한 TCP 연결을 한 DeviceId에 바인딩한 관계이다. 수명이 연결과 같아 끊기면 사라지고, 재접속은 완전히 새로운 Session이다. 재접속을 가로지르는 신원은 `register_request.uuid`(= DeviceId)가 운반한다.

세션 상태는 **idle -> handshaking -> confirming -> active**로 전이하며, 등록은 3-way 핸드셰이크이다.

```mermaid
sequenceDiagram
  participant A as Agent
  participant C as Controller
  Note over A,C: TCP 연결 -> Session idle->handshaking
  A->>C: register_request {uuid, group}
  Note over C: enroll(uuid)->DeviceId / 미지 group은 경고만
  opt 같은 DeviceId가 이미 바인딩됨
    C-->>C: 옛 연결 kick-old (new-wins)
  end
  C->>C: bind -> handshaking->confirming
  C-->>A: register_outcome {success}
  A->>C: register_ack
  C->>C: confirm -> confirming->active (이 시점부터 liveness 측정)
  A->>C: status (enter_active 즉시 1회)
  loop active 정상 운영
    A->>C: heartbeat / status / command_ack / command_outcome
    Note over C: 정상 메시지마다 last_seen 갱신
  end
  Note over C: 주기 sweep: 핸드셰이크 단계 시한 초과 -> 종료<br/>active 침묵 시한 초과 -> 종료
```

핵심 규약은 다음과 같다:

- **bind는 요청 시점, liveness는 ack 시점.** Controller는 피어를 식별(`register_request` 디코딩)하는 즉시 Device 슬롯을 선점(bind)하지만, 운영 시계(liveness)는 `register_ack`를 받아 `active`가 된 뒤부터 돈다.
- **단계별 시한.** Controller는 handshaking/confirming의 `last_seen`을 단계 전이에서만 갱신한다(임의 메시지로는 연장하지 않는다). 각 등록 단계가 독립 budget을 받아 느린/침묵 핸드셰이크를 잡는다. `SessionService`의 주기 sweep이 감시한다.
- **active liveness는 모든 정상 트래픽의 합집합.** heartbeat/status/command_ack/command_outcome 어느 것이든 `last_seen`을 갱신한다. heartbeat는 body 없는 keepalive이다. 같은 sweep이 침묵 시한 초과 세션을 축출한다.
- **상태별 수용.** handshaking은 `register_request`만, confirming은 `register_ack`만 받는다. 타입/방향이 어긋나거나 디코딩에 실패하면 프로토콜 위반으로 연결을 끊는다.

상태 결정의 단일 출처는 `SessionRegistry`이다: 연결을 1차 키로, Device를 [역색인](GLOSSARY.md#세션)으로 들고 **"Device당 바인딩된 연결 최대 1"** 불변식을 봉인한다. 같은 uuid의 새 `register_request`가 오면 옛 연결을 동기적으로 끊고 새 연결을 바인딩한다(kick-old, new-wins).

### Status와 liveness: non-finite 정책

active 상태의 `status`는 디코딩 성공 시 **`update_seen(now)`를 먼저** 부르고, 그다음 `StatusService::update_status`로 Shadow를 갱신한다. `update_status`는 `load`/`temp`가 유한한지(`std::isfinite`)만 검사하고, **비유한(NaN/Inf)이면 Shadow를 건드리지 않고 last-good을 보존**한다. non-finite는 liveness 실패가 아니라 나쁜 샘플이라 버리고 직전 Shadow를 유지하며, kick은 오직 **wire 디코딩 실패**에만 적용한다. finiteness 검증은 status ingress가 단일(`StatusService`)인 동안 서비스 경계에 두고, ingress가 둘 이상 생기면 도메인으로 격상한다.

## 6. 정책 엔진

Controller는 각 Device의 Mode를 **Group 단위로 자동** 제어한다. 사람 명령 API는 없다.

- **Group**: 하나의 Policy를 공유하는 Device들의 논리적 묶음(예: zone_a/zone_b/zone_c). Agent가 등록 시 선언한다. 미지 Group 등록은 soft로 처리해, 막지 않고 경고만 한다.
- **Mode**: 공유 어휘 `safe`/`normal`/`performance`. 정책의 출력이자 Device와 Controller가 공유하는 커널 어휘.
- **Policy(GroupRule)**: Group별 규칙: load 히스테리시스(`high_load`/`low_load` + `high_load_mode`/`low_load_mode`)와 선택적 **온도 override**(`high_temp`/`resume_temp`/`high_temp_mode`).

정책은 두 축을 합성하되 **적용 단위가 다르다**: load는 **Group 단위**(집합 부하), 온도는 **Device 단위**(개별 안전)이다.

`PolicyService::evaluate`는 매 sweep tick마다 두 패스로 돈다. (1) Group별로 active Device의 평균 load(`avg = sum/count`)를 집계해 히스테리시스 밴드로 Regime(busy/idle)과 그 Group의 **Base Mode**를 정한다. (2) 각 active Device가 **자기 Shadow 온도**로 thermal(hot/cool)을 따로 판정한다. Device의 **Effective Mode**는 "자기 thermal이 hot이면 `high_temp_mode`, 아니면 Group Base Mode"이고, 정책 엔진은 이 값이 **바뀐 Device에게만** `set_mode`를 발행한다.

아래는 부하 축(Group Regime)의 상태기계이다.

```mermaid
stateDiagram-v2
  [*] --> unknown : 최초 평가 (빈 regime)
  unknown --> busy : avg가 high_load 초과 / SetMode high_load_mode
  unknown --> idle : avg가 low_load 미만 / SetMode low_load_mode
  unknown --> unknown : 밴드 안(low~high) / 무명령
  idle --> busy : avg가 high_load 초과 / SetMode high_load_mode
  busy --> idle : avg가 low_load 미만 / SetMode low_load_mode
  busy --> busy : avg가 low_load 이상 / 무명령
  idle --> idle : avg가 high_load 이하 / 무명령
```

**busy/idle은 Mode 값이 아니라 Regime이다**(밴드 상단 초과 / 하단 미만). 어느 Regime이 어느 Mode를 목표로 할지는 Group마다 operator가 정한다. `config/controller.json`의 `policy.groups` 기본값(데모)은 다음과 같다:

| Group  | high_load | low_load | high_load_mode | low_load_mode |
| ------ | --------- | -------- | -------------- | ------------- |
| zone_a | 70        | 30       | performance    | normal        |
| zone_b | 60        | 45       | performance    | normal        |
| zone_c | 80        | 20       | performance    | normal        |

세 Group은 Mode 매핑(`high_load_mode=performance`/`low_load_mode=normal`/`high_temp_mode=safe`)과 온도 override(`high_temp=65`/`resume_temp=50`)를 공유하고 부하 임계(`high_load`/`low_load`)만 달라, 같은 부하 곡선에도 Group마다 다른 시점에 전환한다.

**온도 override**는 load 축과 무관하게 **Device별**로 동작하는 단방향 과열 보호이다. 어떤 active Device의 Shadow 온도가 `high_temp`를 넘으면 그 Device의 thermal이 hot으로 걸리고, load Regime과 무관하게 그 Device만 `high_temp_mode`로 간다. `resume_temp` 아래로 식으면 풀려 Group Base Mode로 복귀한다(`resume_temp < high_temp` [데드밴드](GLOSSARY.md#group-정책)). Group Regime이 미확정이면 정책 엔진은 `low_load_mode`를 baseline으로 써 thermal latch를 푼다.

```mermaid
stateDiagram-v2
  [*] --> cool : device 등록
  cool --> hot : 자기 temp가 high_temp 초과 / 그 device만 high_temp_mode
  hot --> cool : 자기 temp가 resume_temp 미만 / Group base mode 복귀
  hot --> hot : resume_temp 이상 / 유지
  cool --> cool : high_temp 이하 / 유지
```

이 폐루프는 Agent 쪽 `SimulatedDevice`가 닫는다: Mode별 초당 변화율을 매 보고 주기에 적분해 load/temp를 움직이고(performance는 부하를 빼며 발열, safe는 냉각), Device마다 초기값과 noise(`DDCS_SIM_NOISE`/`DDCS_SIM_JITTER`)가 달라 같은 Group 안에서도 thermal이 서로 다른 시점에 걸린다.

밴드 조건 `low < high`는 ingress와 무관하게 항상 지켜야 하는 불변식이라 `GroupRule::try_make`가 도메인에서 강제한다(역전/동일이면 생성 거부).

집계와 명령의 대상은 roster의 active 집합뿐이라 끊긴 Device의 stale Shadow가 평균을 오염시키지 않는다. 미보고 active Device는 기본 `load=0`으로 들어가고, Group을 선언하지 않은 Device와 빈 Group은 집계에서 제외한다.

정책은 Device별로 "마지막에 보낸 Effective Mode"와 thermal 상태를 기억해 안 바뀌면 명령을 생략하고, Session이 끝나면(정상 종료/kick-old/liveness 축출) `SessionService`의 통지(`DeviceReleaseSink::on_device_left`)로 그 기억을 폐기해 재접속 Device가 다음 평가에서 반드시 현재 Effective Mode를 재명령받게 한다.

명령의 전달 신뢰성(재전송/타임아웃/supersede)은 정책 엔진이 아니라 `CommandService`의 몫이다(다음 절). 정책은 "전환마다 1회"만 책임진다.

## 7. 명령 RPC와 상관

전송은 Device당 하나의 TCP 연결이고 full-duplex다. 같은 소켓으로 Controller가 `command_request`(C->A)를 밀고 Agent가 `command_ack`/`command_outcome`(A->C)를 돌려보낸다. 별도 응답 채널은 없다. 외부 RPC(원격 프로시저 호출) 호출자도 없다: "발행자"는 Controller 자신의 정책 엔진이다.

왕복 흐름은 다음과 같다: 정책 엔진이 결정 -> `CommandService`가 추적/발신 -> Agent가 적용 -> `CommandService`가 정산.

**정상 왕복** (command_id = N):

```mermaid
sequenceDiagram
  participant POL as 정책 엔진
  participant CS as CommandService
  participant AG as Agent
  participant DEV as Device
  POL->>CS: dispatch(device, set_mode)
  CS->>CS: id=N 발급 / 슬롯 등록 [device, N]
  CS->>AG: command_request [N][set_mode]
  AG->>CS: command_ack [N]
  Note over CS: acknowledge -> 마감시한 연장 (in-flight 유지)
  AG->>DEV: decode_mode + apply
  AG->>CS: command_outcome [N][success]
  Note over CS: settle -> RTT 기록 / 슬롯 종료
```

**경합과 실패** (supersede / 재전송 dedup):

```mermaid
sequenceDiagram
  participant POL as 정책 엔진
  participant CS as CommandService
  participant AG as Agent
  Note over POL,AG: supersede -- 같은 (device, type)에 새 명령 M
  POL->>CS: dispatch(device, set_mode)
  CS->>CS: 슬롯 [device, N] 폐기 / id=M 발급
  CS->>AG: command_request [M]
  Note right of CS: 뒤늦은 N 응답은 슬롯 miss -> stale 카운트
  Note over POL,AG: 재전송 -- timeout 시 동일 id M
  CS->>AG: command_request [M] (재전송)
  Note over AG: M == last_command_id -> dedup (재적용 안 함)
  AG->>CS: command_ack [M] + 캐시된 outcome
```

- **상관 키는 `(device, command_id)`**: 연결 단위가 아니다. `command_id`는 Controller 전역 단조 `u64`(1부터, 0은 invalid)라 모든 Device/모든 명령 타입에 걸쳐 유일하고, 재접속으로 리셋되지 않아 새 연결로 도착한 늦은 응답도 같은 Device면 유효하게 수용한다.
- **supersede**: 범위는 `(device, command_type)` 계열. 같은 계열에 새 명령이 나면 옛 미결 슬롯을 폐기하고 새 `command_id`로 교체한다(계열당 미결 1개). TCP 순서 보장으로 Agent는 최신을 마지막에 적용한다.
- **재전송 = 동일 `command_id`**: 타임아웃/실패 시 지수 backoff 후 같은 id로 재전송한다. Agent는 depth-1 dedup(직전 `command_id`만 기억)으로 중복을 흡수해, 재적용 없이 캐시된 ack/outcome를 회신한다(재적용은 dedup miss에서만).
- **ACK-before-apply**: Agent는 `command_request` 디코딩 성공 시 먼저 ack를 보내고, 그다음 적용 후 outcome을 보낸다. ack 시점은 `command_type` 검증 전이라, ack는 "요청이 디코딩됨"만 뜻한다. 미지 `command_type`도 ack 후 failed outcome으로 처리한다. Controller는 ack로 "수신"과 "완료"를 분리해 느린 적용을 명령 유실로 오인하지 않는다.
- **stale 응답**: 닫힌/대체된/미지의 `(device, command_id)`로 오는 응답은 위반이 아니라 supersede/재전송의 정상 부산물이다. 카운트만 하고 무시한다.

현재 명령 어휘(`set_mode`)는 멱등 상태 선언이라 at-least-once 전달과 depth-1 dedup으로 충분하다. **비멱등 명령 타입을 추가한다면** 그 타입은 수신측 dedup + outcome 재송신으로 격상해야 한다([PROTOCOL.md](PROTOCOL.md) 참고).

재전송은 config 없이도 켜져 있다. 조립 폴백(`Controller::Config`)과 배포 설정 `config/controller.json`이 모두 `max_attempts = 3` / `backoff_base_ms = 500`이고, `max_attempts`를 `1`로 낮추면 재전송 경로가 비활성이 되어 첫 실패에서 곧장 포기한다.

## 8. 에이전트 재접속

Agent는 단일 Controller 연결을 유지하고, 연결이 끊기면 jitter가 섞인 지수 backoff로 재접속을 예약한다. 모든 경로가 `Connector::disconnect_and_reconnect()` 한 곳으로 모인다.

```mermaid
stateDiagram-v2
  [*] --> Idle : Agent start
  Idle --> Connecting : try_connect socket+connect
  Connecting --> Backoff : connect 실패 / SO_ERROR / hangup
  Connecting --> Connected : writable 그리고 SO_ERROR==0
  Connected --> Registering : register_request 전송 / register_timeout
  Registering --> Backoff : timeout / 거부 / bad outcome -> close
  Registering --> Live : register_outcome success -> notify_registered backoff 리셋 / ack / enter_active
  Live --> Backoff : hangup/peer_closed / framing 오류 / app close
  Backoff --> Connecting : reconnect_timer 발화 / delay = next_delay
```

- **트리거**: TCP 셋업 실패, 연결/등록 중 error/hangup, `receive`/`transmit`의 peer_closed/error, 프레이밍 위반, app이 부른 `close()`(등록 타임아웃, bad outcome, 예기치 못한 message 등). **liveness 상실은 Controller가 구동한다:** Agent에는 자체 liveness 타이머가 없고, Controller의 liveness sweep이 끊으면 Agent가 hangup으로 관측한다.
- **backoff:** `base`/`cap`은 `BackoffSchedule` 코드 기본 `1s`/`30s`(설정 `transport.reconnect_base_delay_ms`/`reconnect_max_delay_ms`로 튜닝; 배포 `config/agent.json`은 `200ms`/`5000ms`로 낮춘다), jitter `+/-25%`. delay = `base * 2^attempt`를 `cap`으로 클램프(코드 기본값이면 1, 2, 4, 8, 16, 30, 30, ...) 후 jitter 적용. jitter는 결정적 xorshift32다(테스트 가능). 동시 재시도(thundering-herd)를 흩뜨리는 장치이고, cap 클램프 후 적용이라 실제 상한은 `+/-25%`인 ~37.5s이다.
- **리셋 시점: TCP 연결이 아니라 등록 성공.** `notify_registered()`가 `backoff.reset()`을 부르는데, Agent는 이를 성공 `register_outcome` 이후에만 호출해 TCP는 accept하지만 등록을 끝내지 못하는 Controller를 상대로도 backoff가 자란다.
- **재접속 시 전체 3-way 재등록.** 끊기면 연결은 idle로 복귀(fd FIN, rx 클리어, tx drain, app 타이머 취소)하고, 재연결 후 `register_request -> outcome -> ack`를 다시 밟는다. 돌아온 DeviceId는 Controller가 kick-old로 처리한다(5절).

## 9. 관측성

시스템은 JSONL 구조화 로그(양쪽 프로세스)와 Prometheus 메트릭(Controller 전용)으로 관측한다.

### 로깅 (`lib/logger`)

프로세스 전역 싱글턴이 JSONL 형식으로 한 줄에 하나의 JSON 객체를 쓴다. 필드 순서는 고정이다: `ts`(ISO8601 UTC, ms) / `level`(대문자 DEBUG/INFO/WARN/ERROR) / `event`(점으로 구분된 토큰, 예: `session.kick_old`) / 사용자 필드(`logger::kv` 순서대로) / `file`(basename) / `line`. 레벨 임계 기본은 info이다. 비활성 레벨 인자는 매크로가 평가조차 하지 않는다. 싱크는 단일 `Sink*`이다. `clear_sink(expected)`는 현재 싱크가 expected와 같을 때만 분리한다(컴포넌트가 남의 싱크를 떼지 못하게).

### 메트릭 (Controller 전용)

Controller만 Prometheus 텍스트 익스포지션을 `:9000`(기본)에 노출한다. HTTP 서버는 리액터의 두 번째 게스트로 돌고 pull-only이다. 집계 메트릭은 라벨 없는 `uint64`다. Group별 메트릭은 `group`(+`mode`) 라벨을 단다. 요청 라인을 파싱하지 않아 어떤 경로로 와도 전체 scrape를 반환한다. 노출하는 메트릭은 다음과 같다:

- **게이지**: `ddcs_connections`, `ddcs_devices_known`, `ddcs_commands_pending`
- **카운터**: `ddcs_commands_dispatched_total`, `ddcs_commands_completed_total`, `ddcs_commands_timed_out_total`, `ddcs_commands_retried_total`, `ddcs_commands_superseded_total`, `ddcs_commands_stale_total`, `ddcs_commands_gave_up_total`, `ddcs_agents_evicted_total`, `ddcs_handshake_expired_total`
- **히스토그램**: `ddcs_command_rtt_ms`(명령 dispatch->outcome 지연; `_bucket{le}` 누적 + `_sum` + `_count`). 평균 = sum/count, 꼬리 백분위 = `histogram_quantile(0.99, rate(..._bucket[5m]))`.
- **sweep(단일 스레드 포화)**: `ddcs_sweep_duration_us`(직전 tick) / `ddcs_sweep_duration_us_max`(피크) 게이지, `ddcs_sweep_duration_us_sum` + `ddcs_sweep_ticks_total`(평균 = sum/ticks) 카운터. tick 작업 시간이 sweep 주기에 근접하면 한 코어가 포화에 이른다. 실측은 [README 성능 절](../README.md#성능) 참고.
- **Group별(라벨)**: `ddcs_group_load_avg{group}`(평균 load), `ddcs_group_temp_avg{group}`(평균 온도), `ddcs_group_devices{group,mode}`(모드별 active Device 수). 정책에 있는 Group만 노출해 라벨 cardinality를 config로 한정한다.

Agent는 메트릭 엔드포인트가 없다.

## 10. 설정

역할별 단일 JSON 파일 로더(`lib/config`)이다. 키는 점 경로(`session.handshake_timeout_ms`)로 중첩 object를 가리킨다. 값 우선순위는 **환경변수(설정/유효 시) > 파일 > 코드 기본값**이다.

```mermaid
graph TD
  env["환경변수 (예: DDCS_TRANSPORT_PORT)"] -->|우선| R["적용값"]
  file["설정 파일 (config/controller.json 또는 agent.json)"] -->|다음| R
  def["코드 기본값"] -->|마지막| R
```

각 main은 `DDCS_CONFIG_PATH`(기본 `config/controller.json` / `config/agent.json`) 한 파일을 읽는다. 파일 없음은 비치명이다(기본값으로 가동 + stderr 경고). malformed JSON은 치명이다(throw -> `EXIT_FAILURE`). 타입이 불일치하면 설정 로더가 그 키만 기본값으로 두고 경고한다. 시간(ms) 키는 의도적으로 환경변수 override가 없다. 정책은 controller 파일의 `policy` 객체에 인라인되어 있어, 설정 로더가 같은 파일에서 함께 로드한다.

Agent의 Device 신원(`DDCS_DEVICE_ID` / `DDCS_DEVICE_ID_FILE`)은 Config가 아니라 main에서 직접 해석한다: env > 파일 읽기 > 생성+파일 기록(자가발급, persist 성공 시 비-ephemeral).

**Controller는 정책을 `SIGHUP`으로 핫리로드한다**: Controller 프로세스에 SIGHUP을 보내면 `load_policy`가 `controller.json`의 `policy`를 다시 읽어 `set_policy`로 재적용한다(malformed/없음이면 경고 후 옛 정책 유지. validate-before-apply). 재적용 시 정책 엔진은 발신 belief(commanded)만 비워 다음 sweep이 새 정책으로 재명령하게 하고, regime/thermal 히스테리시스 latch는 reload를 넘어 보존해 과열 보호와 데드밴드 상태를 잇는다.

나머지 설정(포트/타임아웃 등)은 부팅 시 1회만 읽는다. 전체 설정 키 레퍼런스(파일/기본값/환경변수)는 [README 설정 절](../README.md#설정)에 있다.
