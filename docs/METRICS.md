# DDCS 메트릭 레퍼런스

Controller가 노출하는 메트릭 24종의 타입과 라벨, 뜻입니다.
Agent는 메트릭을 내지 않습니다. 양쪽 프로세스가 남기는 구조화 로그는 [LOG.md](LOG.md)에 있습니다.

## 목차

- [노출과 수집](#노출과-수집)
- [Group 축이 있는 것과 없는 것](#group-축이-있는-것과-없는-것)
- [전체 목록](#전체-목록)
- [자주 쓰는 질의](#자주-쓰는-질의)
- [포화 판정](#포화-판정)
- [읽을 때 주의할 것](#읽을-때-주의할-것)

## 노출과 수집

Controller만 메트릭을 냅니다. Agent는 내지 않습니다.

|항목|값|
|---|---|
|노출 주소|`:9000`(기본, `DDCS_PROMETHEUS_PORT`로 변경)|
|형식|Prometheus 텍스트|
|방식|pull 전용. 요청 경로를 파싱하지 않으므로 어떤 경로로 요청해도 전체를 반환합니다|
|수집 주기|1초 (`docker/monitoring/prometheus/prometheus.yml`의 `scrape_interval`)|
|대시보드|Grafana `:3000`, `docker/monitoring/grafana/dashboards/ddcs-overview.json`|

## Group 축이 있는 것과 없는 것

**24종 중 셋만 Group으로 쪼개집니다.** 나머지 21종은 Controller 하나를 재는 값이라 Group별로 나눌 수 없습니다.

|Group 축|메트릭|
|---|---|
|있음|`ddcs_group_devices`, `ddcs_group_load_avg`, `ddcs_group_temp_avg`|
|없음|나머지 전부. 연결 수, 명령 카운터, sweep, 유입, 자원|

대시보드의 `Group` 선택창은 위 셋만 거릅니다.
상단 `Fleet 개요`의 타일은 Group을 골라도 전체값을 그대로 보여줍니다. 거를 축이 없기 때문이며 값 자체는 정확합니다.

Group별 메트릭은 정책에 정의된 Group에 대해서만 노출합니다. 라벨 조합 수가 `controller.json`의 `policy.groups`로 제한됩니다.

## 전체 목록

### Group별 (3종)

|메트릭|타입|라벨|뜻|
|---|---|---|---|
|`ddcs_group_devices`|gauge|`group`, `mode`|Group의 Mode별 active Device 수|
|`ddcs_group_load_avg`|gauge|`group`|Group의 active Device 평균 부하|
|`ddcs_group_temp_avg`|gauge|`group`|Group의 active Device 평균 온도(C)|

Status를 아직 보고하지 않은 Device는 셋 다에서 제외됩니다.

### Session과 연결 (4종)

|메트릭|타입|뜻|
|---|---|---|
|`ddcs_connections`|gauge|현재 연결 수. handshaking, confirming, active 전부 포함|
|`ddcs_devices_known`|gauge|uuid 기준으로 등록된 Device 수. **누적이라 줄지 않습니다**|
|`ddcs_agents_evicted_total`|counter|liveness 제한 시간 초과로 강제 종료한 수|
|`ddcs_handshake_expired_total`|counter|제한 시간 안에 등록을 마치지 못해 끊은 연결 수|

### 명령 RPC (9종)

|메트릭|타입|뜻|
|---|---|---|
|`ddcs_commands_dispatched_total`|counter|정책이 발행해 Session으로 내보낸 명령|
|`ddcs_commands_completed_total`|counter|성공 결과를 받은 명령|
|`ddcs_commands_retried_total`|counter|제한 시간 초과나 거부 뒤 다시 보낸 횟수|
|`ddcs_commands_timed_out_total`|counter|결과 없이 버려진 시도|
|`ddcs_commands_gave_up_total`|counter|재시도를 소진했거나 송신에 실패해 포기한 명령|
|`ddcs_commands_superseded_total`|counter|같은 Device와 종류의 새 명령으로 대체된 것|
|`ddcs_commands_stale_total`|counter|이미 닫히거나 대체된 명령에 늦게 온 응답. 무시합니다|
|`ddcs_commands_pending`|gauge|결과를 기다리는 미결 명령|
|`ddcs_command_rtt_ms`|histogram|발행부터 결과까지의 왕복 시간. `_bucket{le}`, `_sum`, `_count`|

### 포화와 자원 (8종)

|메트릭|타입|라벨|뜻|
|---|---|---|---|
|`ddcs_sweep_duration_us`|gauge|-|직전 sweep tick 하나가 실제로 일한 시간|
|`ddcs_sweep_duration_us_max`|gauge|-|시작 후 최대. **정의상 내려가지 않습니다**|
|`ddcs_sweep_duration_us_sum`|counter|-|누적 작업 시간|
|`ddcs_sweep_ticks_total`|counter|-|실행된 sweep tick 횟수|
|`ddcs_messages_received_total`|counter|-|Agent에서 Session 계층으로 들어온 message 전체|
|`ddcs_tx_queued_messages`|gauge|-|전 연결의 송신 큐 대기 합. **상한 없는 큐의 유일한 계기**|
|`ddcs_pool_capacity`|gauge|`pool`|오브젝트 풀 용량. 청크 단위로 늘고 줄지 않습니다|
|`ddcs_pool_acquired`|gauge|`pool`|현재 사용 중인 슬롯|

sweep tick 한 번은 명령 재전송, liveness 검사, 정책 평가를 함께 처리합니다.

## 자주 쓰는 질의

|보려는 것|질의|
|---|---|
|명령 왕복 평균(ms)|`rate(ddcs_command_rtt_ms_sum[5m]) / rate(ddcs_commands_completed_total[5m])`|
|명령 왕복 p99|`histogram_quantile(0.99, rate(ddcs_command_rtt_ms_bucket[5m]))`|
|sweep 평균(us)|`ddcs_sweep_duration_us_sum / ddcs_sweep_ticks_total`|
|sweep이 주기에서 차지하는 몫|위 값을 sweep 주기(기본 1,000,000us)로 나눕니다|
|유입(초당 message)|`rate(ddcs_messages_received_total[1m])`|
|Group별 Device 수|`sum by (group) (ddcs_group_devices)`|
|특정 Group만|`ddcs_group_devices{group="zone_a"}`|
|명령 성공 비율|`rate(ddcs_commands_completed_total[5m]) / rate(ddcs_commands_dispatched_total[5m])`|

유입 기대치는 대략 `3 × Agent 수`에 명령 응답을 더한 값입니다. heartbeat가 초당 2회, Status 보고가 초당 1회이기 때문입니다(배포 설정 기준).
기대치에 못 미치면 병목이 Controller가 아니라 부하 생성 쪽이므로, 같은 구간의 다른 지표도 다시 읽어야 합니다.

## 포화 판정

두 값을 함께 봅니다.

`rate(ddcs_messages_received_total)`이 유입이고 기대치는 대략 `3 × Agent 수`에 명령 응답을 더한 값입니다.
sweep 평균이 주기에서 차지하는 몫이 처리 여유입니다.

유입이 기대에 못 미치면 병목은 Controller가 아니라 부하 생성 쪽입니다. 그 구간의 다른 지표도 다시 읽어야 합니다.
sweep 평균이 주기에 근접하면 코어 하나로 감당할 수 있는 한계이고, 미결 명령이 쌓이거나 축출이 튀는 것도 같은 신호로 봅니다.

Group별 메트릭은 정책에 정의된 Group에 대해서만 냅니다. 라벨 조합 수가 설정 파일로 제한됩니다.

## 읽을 때 주의할 것

**누적 게이지 둘을 순간값으로 읽으면 안 됩니다.**
`ddcs_devices_known`은 uuid 기준으로 쌓기만 하므로 Device가 떠나도 줄지 않습니다. 현재 붙어 있는 수는 `ddcs_connections`입니다.
`ddcs_sweep_duration_us_max`도 시작 후 최대라 내려가지 않습니다. 추세를 보려면 `ddcs_sweep_duration_us`나 `_sum` 기반 평균을 봅니다.

**단위는 `_ms`와 `_us` 정수입니다.**
Prometheus 관례(기본 단위 초, `_seconds` 접미사)와 다릅니다.
값이 항상 정수라 텍스트 직렬화가 단순해지고, 시나리오와 성능 스크립트의 셸 파서가 소수점 없이 읽을 수 있기 때문입니다.
외부 시스템과 연동한다면 초 단위 히스토그램으로 바꾸는 것이 맞습니다. 그 전까지는 이 문서와 각 `HELP` 문자열이 단위의 단일 출처입니다.

**왕복 시간 히스토그램은 해상도가 정수 밀리초입니다.**
1ms 미만이 전부 첫 버킷에 들어갑니다. 버킷 경계와 어긋나는 분위수(예: p50 0.6ms)는 버킷 내부 보간값이므로 근거로 쓰지 않습니다.

**`ddcs_tx_queued_messages`가 자라면 소비가 멈춘 연결이 있다는 뜻입니다.**
송신 큐에 상한이 없어 Controller 메모리를 계속 차지합니다. 한계와 개선 방향은 [ARCHITECTURE 한계점](ARCHITECTURE.md#102-제약과-개선-방향)에 있습니다.
