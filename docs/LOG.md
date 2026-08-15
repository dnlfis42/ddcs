# DDCS 로그 레퍼런스

Controller와 Agent가 남기는 JSON Lines 로그의 형식과 이벤트 68종을 정리했습니다.
구조화 로그는 Controller와 Agent 양쪽 프로세스가 남깁니다.

## 목차

- [줄 형식](#줄-형식)
- [레벨](#레벨)
- [이벤트 카탈로그](#이벤트-카탈로그)
- [자주 쓰는 필터](#자주-쓰는-필터)
- [설계 제약](#설계-제약)

## 줄 형식

한 줄에 JSON 객체 하나이고 필드 순서는 고정입니다.

|필드|내용|
|---|---|
|`ts`|ISO8601 UTC, 밀리초|
|`level`|`DEBUG` / `INFO` / `WARN` / `ERROR`|
|`event`|점으로 구분된 토큰|
|사용자 필드|`logger::kv`에 전달한 순서 그대로|
|`file` / `line`|파일 basename과 행 번호|

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
|`WARN`|한쪽이 잘못했지만 시스템은 계속 가는 것. 거부, 제한 시간 초과, 값 검증 실패|
|`ERROR`|계약이 깨진 것. 인코딩 실패, 리스너 실패, 정의되지 않은 상태 전이|

## 이벤트 카탈로그

### Session과 등록 (`session.*`, 12종)

Controller와 Agent 양쪽의 Session 상태 전이

|event|레벨|필드|
|---|---|---|
|`session.command.apply`|INFO|`command_id`, `ok`, `reason`|
|`session.command.dedup`|DEBUG|`command_id`|
|`session.connection.active`|INFO|`conn`, `device`|
|`session.connection.connect`|INFO|`conn`|
|`session.connection.disconnect`|INFO|`conn`, `reason`|
|`session.connection.duplicate`|WARN|`conn`|
|`session.connection.heartbeat`|DEBUG|-|
|`session.connection.register.accept`|INFO|`conn`, `device`|
|`session.connection.register.reject`|WARN|`conn`, `reason`|
|`session.connection.register.request`|DEBUG|-|
|`session.connection.register.success`|INFO|`device`|
|`session.connection.unknown`|WARN|`conn`|

### 명령 RPC (`command.*`, 10종)

명령 발행부터 결과까지. 재전송과 대체가 여기 남습니다

|event|레벨|필드|
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

### 정책 (`policy.*`, 5종)

정책 적재와 Regime, thermal 판정

|event|레벨|필드|
|---|---|---|
|`policy.load`|INFO|`path`, `groups`, `trigger`|
|`policy.load.absent`|WARN|`path`, `trigger`|
|`policy.load.fail`|WARN|`path`, `reason`, `trigger`|
|`policy.regime.update`|INFO|`group`, `regime`, `load`|
|`policy.thermal.update`|INFO|`device`, `thermal`, `temp`|

### Device (`device.*`, 7종)

신원 해석과 Status 보고

|event|레벨|필드|
|---|---|---|
|`device.group.unknown`|WARN|`device`, `group`|
|`device.id`|INFO|`device`, `source`|
|`device.id.not_persisted`|WARN|`device`|
|`device.id.unknown`|WARN|`device`|
|`device.status`|DEBUG|`mode`, `load`, `temp`|
|`device.status.non_finite`|WARN|`device`, `load`, `temp`|
|`device.status.update`|DEBUG|`device`, `mode`, `load`, `temp`|

### 전송 (`transport.*`, 26종)

연결, 프레이밍, 리액터 등록 실패

|event|레벨|필드|
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
|`transport.connection.setup.fail`|ERROR|-|
|`transport.disconnect`|INFO|`reason`|
|`transport.frame.decode.corrupt`|ERROR|-|
|`transport.frame.decode.fail`|WARN|`reason`|
|`transport.frame.encode.fail`|ERROR|`size`|
|`transport.host.resolve.fail`|ERROR|`host`, `eai`|
|`transport.host.resolve.recover`|INFO|`host`, `attempts`|
|`transport.listen`|INFO|`port`|
|`transport.listen.fail`|ERROR|`events`|
|`transport.reactor.add.fail`|WARN|`errno`|
|`transport.reactor.modify.fail`|WARN|`errno`|
|`transport.receive.fail`|WARN|`errno`|
|`transport.reconnect.schedule`|DEBUG|`delay_ms`|
|`transport.rx_buffer.adjust`|WARN|`requested`, `effective`|
|`transport.send.fail`|WARN|`errno`|
|`transport.transition.invalid`|ERROR|`from`, `to`|

### message 코덱 (`message.*`, 3종)

디코딩과 인코딩 실패

|event|레벨|필드|
|---|---|---|
|`message.decode.fail`|WARN|`type`|
|`message.encode.fail`|ERROR|`type`|
|`message.unexpected`|WARN|`type`, `state`|

### 설정 (`config.*`, 3종)

설정 파일 경로와 값 검증

|event|레벨|필드|
|---|---|---|
|`config.path`|INFO|`key`, `path`|
|`config.path.absent`|WARN|`path`|
|`config.value.invalid`|WARN|`source`, `key`, `expected`, `actual`|

### 메트릭 종단 (`prometheus.*`, 2종)

메트릭 HTTP 리스너

|event|레벨|필드|
|---|---|---|
|`prometheus.listen`|INFO|`port`|
|`prometheus.listen.fail`|ERROR|`events`|

## 자주 쓰는 필터

`jq`가 있으면 이렇게 봅니다.

```sh
# 특정 Device의 흐름만
docker logs ddcs-controller 2>&1 | jq -c 'select(.device | startswith("1111"))'

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

> [!NOTE]
> 검증 시나리오와 성능 스크립트가 이벤트 이름을 문자열로 직접 셉니다
> (`scripts/scenario-lib.sh`의 `logcount`, `dispatch_count`, `register_count`, `hot_distinct`).
> 이름을 바꾸면 그 단언들이 조용히 0을 세게 되므로, 개명할 때는 `scripts/`를 함께 고쳐야 합니다.

## 설계 제약

프로세스 전역 싱글턴이 기록합니다.
싱크 설치와 레벨 설정은 진입점(`main`, 테스트)이 조립 전에 한 번 수행하며, `clear_sink(expected)`는 현재 싱크가 `expected`와 같을 때만 분리해 다른 컴포넌트의 싱크를 건드리지 못하게 합니다.

비활성 레벨의 인자는 매크로가 평가하지 않습니다.
로그를 끄면 인자를 만드는 비용도 함께 사라지므로, 잦은 경로에 `DEBUG`를 두어도 운영 기본값(`info`)에서는 값을 만들지 않습니다.
