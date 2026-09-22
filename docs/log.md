# 로그

연결이 끊기거나 명령이 지연되면 집계 수치만으로는 원인을 찾기 어렵습니다.
Controller와 Agent는 등록, 정책 판단, 명령 처리, 전송 오류를 JSON Lines로 기록합니다.
장치 ID와 명령 ID로 사건을 연결하면 어느 단계에서 처리가 멈췄는지 추적할 수 있습니다.

## 목차

- [줄 형식](#줄-형식)
- [레벨](#레벨)
- [자주 쓰는 필터](#자주-쓰는-필터)
- [설계 제약](#설계-제약)
- [이벤트 카탈로그](#이벤트-카탈로그)

## 줄 형식

한 줄에 JSON 객체 하나이고 필드 순서는 고정입니다.

|필드|내용|
|---|---|
|`ts`|ISO8601 UTC, 밀리초|
|`level`|`DEBUG` / `INFO` / `WARN` / `ERROR`|
|`event`|점으로 구분된 토큰|
|사용자 필드|`logger::kv`에 전달한 순서 그대로|
|`file` / `line`|파일 basename과 행 번호|

*표 1. 줄 형식*

```json
{"ts":"2026-08-15T06:39:34.401Z","level":"INFO","event":"command.complete","device":"4444...","command_id":34,"rtt_ms":0,"file":"command_service.cpp","line":122}
```

## 레벨

임계 기본값은 `info`이고 `DDCS_LOG_LEVEL`로 바꿉니다.
비활성 레벨의 인자는 매크로가 평가하지 않으므로, 로그를 끄면 인자를 만드는 비용도 사라집니다.

|레벨|쓰는 자리|
|---|---|
|`DEBUG`|heartbeat, 중복 제거, 상태 갱신처럼 정상 운영에서 매우 잦은 것|
|`INFO`|등록, 명령 발행과 완료, 정책 전환처럼 흐름을 재구성할 수 있는 것|
|`WARN`|처리를 계속할 수 있는 거부, 제한 시간 초과, 값 검증 실패|
|`ERROR`|인코딩 실패, 리스너 실패, 정의되지 않은 상태 전이|

*표 2. 레벨*

## 자주 쓰는 필터

`jq`가 있으면 이렇게 봅니다.

```sh
# 특정 Device의 흐름만
docker logs ddcs-controller 2>&1 | jq -c 'select((.device // "") | startswith("1111"))'

# WARN 이상만
docker logs ddcs-controller 2>&1 | jq -c 'select(.level != "INFO" and .level != "DEBUG")'

# 명령 왕복 시간이 긴 것
docker logs ddcs-controller 2>&1 | jq -c 'select(.event == "command.complete" and .rtt_ms > 20)'

# 이벤트별 건수
docker logs ddcs-controller 2>&1 | jq -r .event | sort | uniq -c | sort -rn
```

`jq` 없이 셀 때는 문자열 일치로 충분합니다.

```sh
docker logs ddcs-controller 2>&1 | grep -c '"event":"policy.regime.update"'
```

검증 시나리오와 성능 스크립트는 이벤트 이름으로 발생 횟수를 셉니다.
이름을 변경할 때는 `scripts/lib/scenario.sh`의 `logcount`, `dispatch_count`, `register_count`, `hot_distinct` 등 소비 코드도 함께 수정해야 합니다.

## 설계 제약

프로세스 전역 싱글턴이 기록합니다.
싱크 설치와 레벨 설정은 진입점(`main`, 테스트)이 조립 전에 한 번 수행하며, `clear_sink(expected)`는 현재 싱크가 `expected`와 같을 때만 분리해 다른 컴포넌트의 싱크를 건드리지 못하게 합니다.

## 이벤트 카탈로그

68개 이벤트를 상태를 관리하는 계층별로 묶었습니다. 표의 필드는 공통 필드를 제외한 이벤트별 값입니다.

### Session과 등록 (`session.*`, 12종)

Controller와 Agent의 Session 상태 전이와 등록 결과를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`session.command.apply`|INFO|`command_id`, `ok`, `reason`|
|`session.command.dedup`|DEBUG|`command_id`|
|`session.connection.active`|INFO|`conn`, `device`|
|`session.connection.connect`|INFO|`conn`|
|`session.connection.disconnect`|INFO|`conn`, `reason`|
|`session.connection.duplicate`|WARN|`conn`|
|`session.connection.heartbeat`|DEBUG|없음|
|`session.connection.register.accept`|INFO|`conn`, `device`|
|`session.connection.register.reject`|WARN|`conn`, `reason`|
|`session.connection.register.request`|DEBUG|없음|
|`session.connection.register.success`|INFO|`device`|
|`session.connection.unknown`|WARN|`conn`|

*표 3. Session과 등록 (`session.*`, 12종)*

### 명령 RPC (`command.*`, 10종)

명령 발행부터 완료까지의 흐름과 재전송·대체를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`command.ack`|INFO|`device`, `command_id`, `attempts`|
|`command.complete`|INFO|`device`, `command_id`, `rtt_ms`|
|`command.dispatch`|INFO|`device`, `command_id`|
|`command.dispatch.fail`|WARN|`device`, `command_id`, `reason`|
|`command.fail`|WARN|`device`, `command_id`, `attempts`, `reason`|
|`command.reject`|WARN|`device`, `command_id`, `code`|
|`command.retry`|INFO|`device`, `command_id`, `attempts`|
|`command.stale_response`|DEBUG|`device`, `command_id`|
|`command.supersede`|INFO|`device`, `command_id`|
|`command.timeout`|WARN|`device`, `command_id`, `attempts`|

*표 4. 명령 RPC (`command.*`, 10종)*

### 정책 (`policy.*`, 5종)

정책 적재와 부하·온도 상태 판단을 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`policy.load`|INFO|`path`, `groups`, `trigger`|
|`policy.load.absent`|WARN|`path`, `trigger`|
|`policy.load.fail`|WARN|`path`, `reason`, `trigger`|
|`policy.regime.update`|INFO|`group`, `regime`, `load`|
|`policy.thermal.update`|INFO|`device`, `thermal`, `temp`|

*표 5. 정책 (`policy.*`, 5종)*

### Device (`device.*`, 7종)

장치 식별과 상태 보고를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`device.group.unknown`|WARN|`device`, `group`|
|`device.id`|INFO|`device`, `source`|
|`device.id.not_persisted`|WARN|`device`|
|`device.id.unknown`|WARN|`device`|
|`device.status`|DEBUG|`mode`, `load`, `temp`|
|`device.status.non_finite`|WARN|`device`, `load`, `temp`|
|`device.status.update`|DEBUG|`device`, `mode`, `load`, `temp`|

*표 6. Device (`device.*`, 7종)*

### 전송 (`transport.*`, 26종)

연결과 프레이밍, 리액터 등록 과정의 결과를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`transport.accept.fail`|WARN|`errno`|
|`transport.accept.fd_exhausted`|WARN|`errno`|
|`transport.accept.fd_recover`|INFO|`rejected`|
|`transport.accept.spare_fd.fail`|ERROR|`errno`|
|`transport.connect`|DEBUG|`host`, `port`|
|`transport.connect.fail`|WARN|`errno`|
|`transport.connect.success`|INFO|`host`, `port`|
|`transport.connection.duplicate`|WARN|`conn`|
|`transport.connection.notify.fail`|ERROR|`conn`, `event`|
|`transport.connection.register.fail`|WARN|`conn`, `errno`|
|`transport.connection.setup.fail`|ERROR|없음|
|`transport.disconnect`|INFO|`reason`|
|`transport.frame.decode.corrupt`|ERROR|Controller 장치 연결: `conn`</br>Agent: 없음|
|`transport.frame.decode.fail`|WARN|Controller 장치 연결: `conn`, `reason`</br>Agent: `reason`|
|`transport.frame.encode.fail`|ERROR|Controller 장치 연결: `conn`, `size`</br>Agent: `size`|
|`transport.host.resolve.fail`|ERROR|`host`, `eai`|
|`transport.host.resolve.recover`|INFO|`host`, `attempts`|
|`transport.listen`|INFO|`port`|
|`transport.listen.fail`|ERROR|`events`|
|`transport.reactor.add.fail`|WARN|`errno`|
|`transport.reactor.modify.fail`|WARN|Controller 장치 연결: `conn`, `errno`</br>Agent: `errno`|
|`transport.receive.fail`|WARN|Controller 장치 연결: `conn`, `errno`</br>Agent: `errno`|
|`transport.reconnect.schedule`|DEBUG|`delay_ms`|
|`transport.rx_buffer.adjust`|WARN|`requested`, `effective`|
|`transport.send.fail`|WARN|Controller 장치 연결: `conn`, `errno`</br>Agent: `errno`|
|`transport.transition.invalid`|ERROR|`from`, `to`|

*표 7. 전송 (`transport.*`, 26종)*

### message 코덱 (`message.*`, 3종)

메시지 인코딩·디코딩 실패와 예상하지 않은 메시지를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`message.decode.fail`|WARN|`type`|
|`message.encode.fail`|ERROR|`type`|
|`message.unexpected`|WARN|`type`, `state`|

*표 8. message 코덱 (`message.*`, 3종)*

### 설정 (`config.*`, 3종)

설정 파일 탐색과 값 검증 결과를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`config.path`|INFO|`key`, `path`|
|`config.path.absent`|WARN|`path`|
|`config.value.invalid`|WARN|`source`, `key`, `expected`, `actual`|

*표 9. 설정 (`config.*`, 3종)*

### 메트릭 종단 (`prometheus.*`, 2종)

메트릭 HTTP 리스너의 기동 결과를 기록합니다.

|이벤트 (`event`)|레벨|추가 필드|
|---|---|---|
|`prometheus.listen`|INFO|`port`|
|`prometheus.listen.fail`|ERROR|`events`|

*표 10. 메트릭 종단 (`prometheus.*`, 2종)*
