# DDCS 설정 레퍼런스

접속 대상과 보고 주기, 제어 기준은 실행 환경에 따라 달라집니다.
DDCS는 Controller와 Agent의 설정을 JSON 파일로 읽고, 일부 값은 환경변수로 덮어쓸 수 있도록 했습니다.
이 문서는 설정값의 선택 순서와 항목, 실행 중 정책을 변경하는 방법을 정리합니다.

## 목차

- [해석 규칙](#해석-규칙)
- [신원과 Group](#신원과-group)
- [키 목록](#키-목록)
- [정책](#정책)
- [프로파일 설정](#프로파일-설정)
- [모의 장치 설정](#모의-장치-설정)

## 해석 규칙

런타임 설정은 역할별 단일 JSON 파일(`config/controller.json`, `config/agent.json`)이며, `lib/config`의 로더가 읽습니다.
키는 점 경로(`session.handshake_timeout_ms`)로 중첩 object를 가리키고, 값 우선순위는 **환경변수(설정되어 있고 값이 유효한 경우) > 파일 > 코드 기본값**입니다.

그림 1은 일반 실행 설정에 적용되는 값의 우선순위를 보여줍니다.

```mermaid
flowchart LR
  defaults["코드 기본값"] --> file["파일 값<br/>유효한 값이 있으면 덮어쓰기"]
  file --> env["환경변수 값<br/>대응하는 유효한 값이 있으면 덮어쓰기"]
  env --> result["최종 값 적용"]
```

*그림 1. 일반 실행 설정의 적용 우선순위*

- 각 `main`은 `DDCS_CONFIG_PATH`(기본 `config/controller.json`, `config/agent.json`) 한 파일을 읽습니다.
- 파일이 없으면 경고를 남기고 기본값과 환경변수를 사용합니다. 읽기 권한 오류나 JSON 문법 오류는 기동 실패로 처리합니다. 일반 실행 설정의 타입이 맞지 않으면 해당 키의 기본값을 사용하고 경고합니다. 정책과 프로파일 설정은 별도의 검증 규칙을 적용합니다.
- 시간(ms) 키는 환경변수로 덮어쓸 수 없습니다.
- 정책은 controller 파일의 `policy` 객체에 인라인으로 포함됩니다.

wire(`:8080`)와 메트릭(`:9000`) 리스너는 항상 모든 인터페이스(`0.0.0.0`)에서 수신 대기하며, 바인드 주소는 설정으로 제한할 수 없습니다([보안 가정](architecture.md#신뢰-경계)).

## 신원과 Group

Group은 agent의 `device.group` 키(환경변수 `DDCS_DEVICE_GROUP`, 기본 `zone_a`)로 지정합니다.
DeviceId는 유효한 `DDCS_DEVICE_ID` 환경변수 값, `DDCS_DEVICE_ID_FILE`이 가리키는 파일의 유효한 UUID, 새 UUID 발급 순으로 결정합니다.
둘 다 지정하지 않으면 어디에도 기록하지 않으므로 기동할 때마다 새 Device가 됩니다. 재시작한 뒤에도 같은 Device로 남으려면 둘 중 하나를 지정해야 합니다.

파일로 신원을 남길 경우 프로젝트의 표준 위치는 `var/state/agent/<agent-name>.uuid`입니다. 이 경로는
git에서 제외되는 runtime state이며, Agent마다 **서로 다른 파일 하나씩**을 지정해야 합니다.

여러 Agent를 실행할 때는 각자 고유한 UUID나 저장 파일을 사용해야 합니다.
같은 신원을 공유하면 Controller가 기존 연결을 새 연결로 교체하므로 서로의 연결을 끊게 됩니다.
재시작 전후의 신원을 유지할 필요가 없는 시험에서는 두 환경변수를 생략할 수 있습니다.
파일을 지정했지만 유효한 UUID를 읽지 못하면 새 UUID를 발급해 저장을 시도하며, 저장 실패는 로그로 남깁니다.

AgentFleet은 장치별 UUID를 직접 발급하므로 두 신원 환경변수를 사용하지 않습니다.
Group을 지정하는 방법도 다르며, [부하 생성기 설정](performance.md#부하-생성기-agentfleet)에 정리했습니다.

## 키 목록

**`config/controller.json`**

표 1과 표 2는 저장소의 설정 파일 값과 키를 생략했을 때의 코드 기본값을 비교합니다. 시간은 ms, 버퍼 크기는 B 단위입니다.

|키|파일 값|코드 기본값|설명|
|---|---:|---:|---|
|`log.level`|`info`|`info`|debug / info / warn / error|
|`controller.sweep_interval_ms`|`1000` ms|`1000` ms|이전 예약 시각 기준 sweep 주기. 초과한 주기는 건너뜀 (재전송/축출/정책 평가)|
|`prometheus.port`|`9000`|`9000`|메트릭 포트|
|`transport.port`|`8080`|`8080`|wire listen 포트|
|`transport.accept_backlog`|`128`|`128`|listen backlog|
|`transport.rx_buffer_size`|`4096` B|`4096` B|연결별 rx ring 용량. frame 최대 크기 이상인 2의 거듭제곱으로 올림 보정|
|`session.handshake_timeout_ms`|`3000` ms|`3000` ms|핸드셰이크 단계별 제한 시간|
|`session.liveness_timeout_ms`|`1500` ms|`3000` ms|active Session liveness 제한 시간|
|`command.timeout_ms`|`5000` ms|`5000` ms|명령 응답 대기 제한 시간|
|`command.max_attempts`|`3`|`3`|명령 전송 시도 횟수 (1이면 재전송 없음)|
|`command.backoff_base_ms`|`500` ms|`500` ms|명령 재전송 백오프 시작값|

*표 1. Controller 실행 설정*

**`config/agent.json`**

|키|파일 값|코드 기본값|설명|
|---|---:|---:|---|
|`log.level`|`info`|`info`|debug / info / warn / error|
|`transport.host`|`127.0.0.1`|`127.0.0.1`|연결할 Controller 호스트/IP|
|`transport.port`|`8080`|`8080`|Controller 연결 포트|
|`transport.rx_buffer_size`|`4096` B|`4096` B|연결 rx ring 용량. frame 최대 크기 이상인 2의 거듭제곱으로 올림 보정|
|`transport.reconnect_base_delay_ms`|`200` ms|`1000` ms|재접속 백오프 시작값 (지수 증가)|
|`transport.reconnect_max_delay_ms`|`5000` ms|`30000` ms|재접속 백오프 상한|
|`session.registration_timeout_ms`|`5000` ms|`2000` ms|register_outcome 대기 제한 시간|
|`session.heartbeat_interval_ms`|`500` ms|`1000` ms|heartbeat 주기|
|`session.status_report_interval_ms`|`1000` ms|`5000` ms|Status 보고 주기|
|`device.group`|미지정|`zone_a`|Device가 속한 Group (기본 파일에는 없음, compose가 환경변수로 지정)|

*표 2. Agent 실행 설정*

표 3의 환경변수로 해당 키를 덮어쓸 수 있습니다. 표에 없는 일반 실행 설정 키는 환경변수로 변경하지 않습니다.

|환경변수|설정 키|적용 대상|
|---|---|---|
|`DDCS_LOG_LEVEL`|`log.level`|Controller·Agent|
|`DDCS_PROMETHEUS_PORT`|`prometheus.port`|Controller|
|`DDCS_TRANSPORT_PORT`|`transport.port`|Controller·Agent|
|`DDCS_TRANSPORT_HOST`|`transport.host`|Agent|
|`DDCS_DEVICE_GROUP`|`device.group`|Agent|

*표 3. 일반 실행 설정의 환경변수 대응*

- 기본 설정 파일은 짧은 시연에서 동작을 빨리 관찰하려고 대부분 코드 기본값보다 짧은 주기를 씁니다.
  보고 주기와 제한 시간은 실제 지연과 부하를 관찰하며 함께 조정해야 합니다.

## 정책

구역별 제어 기준은 Controller 설정 파일의 `policy.groups`에 지정합니다.
각 Group에는 부하 규칙을 설정하고, 온도 보호가 필요하면 온도 규칙도 함께 추가합니다.

|키|필수 여부|입력과 조건|
|---|---|---|
|`busy_load` / `idle_load`|필수|숫자. `idle_load < busy_load`|
|`busy_mode` / `idle_mode`|필수|각 부하 상태에서 적용할 `safe`, `normal`, `performance` 중 하나|
|`hot_temp` / `cool_temp`|온도 보호 사용 시 필수|숫자. `cool_temp < hot_temp`|
|`hot_mode`|온도 보호 사용 시 필수|과열 시 적용할 `safe`, `normal`, `performance` 중 하나|

*표 4. 구역별 정책 설정*

온도 규칙은 세 항목을 모두 지정하거나 모두 생략합니다. 다음은 한 구역을 설정하는 예입니다.
기존 파일의 다른 실행 설정을 유지한 채 `policy` 객체에 반영합니다.

```json
{
  "policy": {
    "groups": {
      "zone_a": {
        "busy_load": 70,
        "idle_load": 30,
        "busy_mode": "performance",
        "idle_mode": "normal",
        "hot_temp": 65,
        "cool_temp": 50,
        "hot_mode": "safe"
      }
    }
  }
}
```

파일을 수정한 뒤 Controller에 SIGHUP을 보내면 `policy` 객체를 다시 읽습니다.
로컬 실행에서는 다음 명령의 `<controller-pid>`를 실행 중인 Controller의 PID로 바꿉니다.

```sh
kill -HUP <controller-pid>
```

Docker 실행 명령은 [README의 빠른 실행](../README.md#빠른-실행)에 정리했습니다.
파일을 읽지 못하거나 정책이 유효하지 않으면 기존 정책을 유지합니다. `policy` 객체가 없는 경우에도 기존 정책을 유지하며,
모든 구역 규칙을 제거하려면 `"policy": {"groups": {}}`처럼 빈 객체를 명시합니다.
기동 시 설정 파일이 없거나, 정상적으로 읽은 JSON에 유효한 정책이 없으면 구역별 제어 규칙이 없는 상태로 시작합니다.
파일 읽기 오류나 JSON 문법 오류는 앞서 설명한 기동 실패 규칙을 따릅니다.

포트나 보고 주기 등 `policy` 이외의 설정은 프로세스를 재시작해야 적용됩니다.
정책 변경 시 판단 상태와 명령 발행이 어떻게 달라지는지는 [정책 엔진](policy.md#정책-변경)에 정리했습니다.

## 프로파일 설정

Controller의 내부 처리 시간을 직접 수집할 때 사용하는 설정입니다.
일반적인 수집은 [프로파일링 실행 스크립트](profiling.md)가 실행별 값을 지정합니다.

|JSON 키|환경변수|입력과 조건|
|---|---|---|
|`profile.enabled`|`DDCS_PROFILE_ENABLED`|기본 `false`. JSON은 불리언, 환경변수는 `true` / `false` / `1` / `0`|
|`profile.capacity`|`DDCS_PROFILE_CAPACITY`|활성화 시 필수. 기록 버퍼에 담을 표본 수인 양의 정수|
|`profile.output_path`|`DDCS_PROFILE_OUTPUT_PATH`|활성화 시 필수. 기록을 저장할 파일 경로|
|`profile.run_id`|`DDCS_PROFILE_RUN_ID`|활성화 시 필수. 비어 있지 않은 실행 식별자|

*표 5. 프로파일 수집 설정*

환경변수가 파일 값보다 우선합니다. `profile` 객체를 생략하면 비활성화되지만, 객체를 작성한 경우에는 `enabled`를 명시해야 합니다(환경변수로 지정한 경우 제외).
일반 실행 설정과 달리 프로파일 설정이 잘못되면 기동에 실패합니다.
출력 디렉터리는 미리 존재해야 하며, 이미 존재하는 출력 파일 경로는 기동 시 거부합니다.
기록을 저장할 수 있도록 디렉터리에 쓰기 권한이 필요합니다.

## 모의 장치 설정

Agent와 AgentFleet은 모의 장치의 상태 변화에 다음 환경변수를 사용합니다.
JSON 파일의 키가 아니라 프로세스 환경변수로 지정하며, Docker에서는 컨테이너의 `environment`로 전달합니다.

|환경변수|의미|
|---|---|
|`DDCS_SIM_NOISE`|부하 변화의 잡음 크기. 기본 `1.0`. 온도 잡음은 이 값의 절반|
|`DDCS_SIM_JITTER`|장치별 부하 변화율의 편차. 기본 `0.10`, 0 이하는 편차를 끄고 `0.99`를 넘으면 상한 적용|

*표 6. 모의 장치의 상태 변화 설정*

모의 장치의 역할과 검증 범위는 [검증 시나리오](scenario.md#모의-장치)에 정리했습니다.
