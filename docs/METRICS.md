# DDCS 메트릭 레퍼런스

Controller가 노출하는 Prometheus 메트릭 family 24종의 타입, 라벨, 의미를 정리합니다. Agent는 메트릭을 내지 않으며, 양쪽 프로세스의 구조화 로그는 [LOG.md](LOG.md)에 있습니다.

## 노출과 수집

|항목|값|
|---|---|
|노출 주소|`:9000` (기본, `DDCS_PROMETHEUS_PORT`로 변경)|
|형식|Prometheus text exposition format|
|방식|pull 전용. 요청 경로와 무관하게 전체를 반환|
|수집 주기|1초 (`docker/monitoring/prometheus/prometheus.yml`)|
|대시보드|Grafana `:3000`, `docker/monitoring/grafana/dashboards/ddcs-overview.json`|

## Group 축

24개 family 중 아래 세 개만 `group`으로 나뉩니다. 나머지는 Controller 전체의 연결, 명령, tick, 자원을 나타내므로 Group 선택과 관계없이 전체값입니다.

|메트릭|라벨|의미|
|---|---|---|
|`ddcs_group_devices`|`group`, `mode`|상태를 보고한 active Device의 Mode별 수|
|`ddcs_group_load_ratio`|`group`|active Device 평균 load를 `0–100`에서 `0–1`로 정규화한 값|
|`ddcs_group_temperature_celsius`|`group`|active Device 평균 온도(C)|

Group 메트릭은 정책에 정의된 Group이고 Status가 있는 active Device만 대상으로 합니다. 따라서 라벨 cardinality는 `controller.json`의 `policy.groups`로 제한됩니다.

## 전체 목록

### 명령 (10종)

|메트릭|타입|라벨|의미|
|---|---|---|---|
|`ddcs_commands_dispatched_total`|counter|-|첫 송신이 성공해 재시도 상태 기계에 들어간 논리 명령|
|`ddcs_commands_superseded_total`|counter|-|같은 Device·명령 계열의 새 의도로 대체되어 정상 종결된 논리 명령|
|`ddcs_commands_succeeded_total`|counter|-|성공 outcome으로 종결된 논리 명령|
|`ddcs_commands_failed_total`|counter|`reason` = `exhausted`, `offline`, `encode_fail`|terminal failure로 종결된 논리 명령|
|`ddcs_commands_pending`|gauge|-|현재 terminal state가 아닌 논리 명령|
|`ddcs_command_dispatch_failures_total`|counter|`reason` = `offline`, `encode_fail`|첫 송신이 실패해 상태 기계에 들어가지 못한 dispatch 시도|
|`ddcs_command_attempt_failures_total`|counter|`reason` = `agent_failure`, `timeout`|재시도 가능 여부와 무관한 개별 attempt 실패|
|`ddcs_command_resends_total`|counter|-|전송 adapter가 수락한 재송신|
|`ddcs_command_stale_responses_total`|counter|-|닫혔거나 supersede된 명령에 늦게 도착해 무시한 응답|
|`ddcs_command_rtt_seconds`|histogram|-|최초 dispatch부터 성공 outcome까지의 RTT. `_bucket{le}`, `_sum`, `_count`를 함께 노출|

`dispatched`는 첫 송신 실패를 포함하지 않습니다. 같은 프로세스 실행 안에서 다음 항등식이 성립합니다.

```text
dispatched = succeeded + failed + superseded + pending
dispatch attempts = dispatched + dispatch_failures
```

여기서 `failed`와 `dispatch_failures`는 각 `reason` 라벨의 합입니다. timeout과 Agent 거부는 먼저 `attempt_failures`로 세고, 재시도 예산을 소진했을 때만 `failed{reason="exhausted"}`가 추가됩니다. 재송신 중 전송 실패는 각각 `failed{reason="offline"}` 또는 `failed{reason="encode_fail"}`로 종결합니다.

RTT는 내부에서 정수 µs로 관측하지만 Prometheus에는 기본 단위인 초로 정확히 변환해 냅니다. 구조화 로그의 `rtt_ms` 키는 로그 호환성을 위해 ms 정수를 유지합니다.

### Tick (4종)

|메트릭|타입|의미|
|---|---|---|
|`ddcs_tick_duration_seconds`|gauge|가장 최근 Controller tick의 실제 작업 시간|
|`ddcs_tick_duration_seconds_max`|gauge|프로세스 시작 뒤 최대 tick 작업 시간|
|`ddcs_tick_duration_seconds_total`|counter|누적 tick 작업 시간|
|`ddcs_ticks_total`|counter|완료한 Controller tick 수|

tick 하나는 명령 재전송, 세션 liveness 검사, 정책 평가를 순서대로 처리합니다.

### 세션 (4종)

|메트릭|타입|라벨|의미|
|---|---|---|---|
|`ddcs_connections`|gauge|-|현재 Session 수. handshaking, confirming, active를 모두 포함|
|`ddcs_devices`|gauge|-|이 Controller 프로세스 수명 동안 관리한 Device 수. 연결 종료로 줄지 않음|
|`ddcs_messages_received_total`|counter|-|Agent에서 Session 계층으로 도착한 모든 message|
|`ddcs_connections_closed_total`|counter|`reason`|registry에서 실제로 제거된 Session의 종료 수|

`ddcs_connections_closed_total`의 `reason` 어휘는 고정되어 있습니다: `peer_closed`, `io_error`, `frame_error`, `shutdown`, `kicked`, `handshake_expired`, `liveness_expired`, `register_rejected`, `bad_message`, `unexpected_message`, `internal_error`.

따라서 `ddcs_connections`는 지금 연결된 장치 측의 Session 수이고, `ddcs_devices`는 Controller가 관리해 온 Device 수입니다. 둘은 의도적으로 다른 질문에 답합니다.

### 전송 자원 (3종)

|메트릭|타입|라벨|의미|
|---|---|---|---|
|`ddcs_send_queue_messages`|gauge|-|모든 연결의 송신 큐 대기 message 합|
|`ddcs_pool_slots`|gauge|`pool` = `connection`, `message`|ObjectPool의 현재 slot capacity|
|`ddcs_pool_slots_acquired`|gauge|`pool` = `connection`, `message`|현재 획득 중인 ObjectPool slot|

풀은 청크 단위로 늘고 줄지 않습니다. 송신 큐는 현재 상한이 없으므로 지속 증가를 느린 소비자 또는 메모리 압박 신호로 봅니다.

## 자주 쓰는 질의

|보려는 것|PromQL|
|---|---|
|명령 평균 RTT (ms)|`1000 * rate(ddcs_command_rtt_seconds_sum[5m]) / rate(ddcs_command_rtt_seconds_count[5m])`|
|명령 RTT p99 (ms)|`1000 * histogram_quantile(0.99, sum(rate(ddcs_command_rtt_seconds_bucket[5m])) by (le))`|
|tick 평균 (µs)|`1e6 * rate(ddcs_tick_duration_seconds_total[5m]) / rate(ddcs_ticks_total[5m])`|
|tick이 1초 주기에서 차지하는 비율|`rate(ddcs_tick_duration_seconds_total[5m]) / rate(ddcs_ticks_total[5m])`|
|유입 (message/s)|`rate(ddcs_messages_received_total[1m])`|
|논리 명령 성공 비율|`rate(ddcs_commands_succeeded_total[5m]) / rate(ddcs_commands_dispatched_total[5m])`|
|terminal failure, 이유별|`sum by (reason) (rate(ddcs_commands_failed_total[5m]))`|
|attempt failure, 이유별|`sum by (reason) (rate(ddcs_command_attempt_failures_total[5m]))`|
|liveness 종료율|`rate(ddcs_connections_closed_total{reason="liveness_expired"}[5m])`|
|Group별 Device 수|`sum by (group) (ddcs_group_devices)`|
|특정 Group의 load ratio|`ddcs_group_load_ratio{group="zone_a"}`|

명령 성공 비율은 새로 dispatch된 명령이 terminal state에 도달하기 전에는 pending 때문에 짧은 창에서 1보다 작게 보일 수 있습니다. 완료 분모를 보고 싶다면 `succeeded + sum(failed) + superseded`의 변화량을 별도로 사용합니다.

## 해석할 때 주의할 점

- 시간 메트릭은 모두 Prometheus 기본 단위인 초입니다. 화면에서 ms 또는 µs로 보려면 질의에서 환산하거나 Grafana unit을 설정합니다.
- `ddcs_tick_duration_seconds_max`와 `ddcs_devices`는 프로세스 재시작 전까지 감소하지 않습니다. 추세에는 최근 tick 값이나 `_total`의 rate를 사용합니다.
- `ddcs_command_rtt_seconds`는 성공 outcome만 관측합니다. failure나 supersede는 RTT 분모에 들어가지 않습니다.
- `ddcs_command_stale_responses_total`은 supersede·재접속 뒤의 정상적인 늦은 응답도 포함합니다. 그 자체만으로 프로토콜 오류를 뜻하지는 않습니다.
- `ddcs_connections_closed_total`은 실제 registry 제거가 일어난 경우에만 증가합니다. 이미 종료된 connection의 중복 콜백은 다시 세지 않습니다.

## 포화 판정

유입과 tick 여유를 함께 봅니다. 배포 기본값에서 기대 유입은 대략 `3 × Agent 수`에 명령 응답을 더한 값입니다(heartbeat 초당 2회, Status 초당 1회).

유입이 기대보다 낮으면 Controller가 아니라 Agent 또는 측정 하네스가 병목일 수 있습니다. 반대로 평균 tick 시간이 1초 주기에 가까워지고, `ddcs_commands_pending`, `ddcs_send_queue_messages`, `liveness_expired` 종료가 함께 증가하면 단일 Controller tick이 입력을 따라가지 못하는 신호입니다.
