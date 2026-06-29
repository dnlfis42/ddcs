# DDCS 용어집

**분산 장치 제어 시스템**: 하나의 Controller가 다수의 Agent를 조율하는 시스템

- 각 Agent는 자기 Device의 Status를 주기적으로 보고하며 Controller가 내린 Command를 적용한다.
- 진실의 원천인 Device는 Agent 쪽에 있고, Controller는 그 투영인 DeviceShadow를 보관한다.

## 행위자

**Controller**: 다수 Device의 집합적 Mode를 Group 단위 Policy로 조율하는 단일 서버

- Agent를 통해 각 Device를 관측(Status)하고 구동(Command)하며, Agent 연결 관리는 그 수단이다.

**Agent**: 하나의 Device를 호스팅해 Controller에 등록하고 Status를 보고하며 Command를 적용하는 클라이언트

- 자기만의 신원 없이 Device 신원을 대리 등록한다. (1 Agent = 1 Device)

## Device 모델

**Device**: Agent가 제어하는 실제 장치

- 상태와 신원의 진실의 원천이다.

**DeviceId**: Device의 영속 신원

- Agent가 등록 시 제시하지만 Agent 프로세스가 아니라 Device의 것이다.
- DeviceShadow의 키이며 재접속을 가로질러 지속된다.

**DeviceShadow**: Controller가 DeviceId로 보관하는 Device의 투영

- 최근 Status의 캐시이며 Session보다 오래 산다.

## 세션

**Session**: 등록된 Agent와 Controller 사이의 살아있는 관계 하나

- 하나의 Connection에 묶이고 끊기면 사라진다. 같은 DeviceId 재접속도 새 Session으로 묶인다.
- Device 바인딩은 confirming 단계부터이다. handshaking 중에는 device 없이 존재하고, liveness/역색인 대상은 active부터다.

**Liveness**: active Session이 살아있다는 신호

- Agent의 주기 heartbeat가 갱신하고, Controller가 시한 내 침묵을 관측하면 죽은 것으로 보아 축출(Eviction)한다.

**Handshake (3-way)**: Agent 등록 프로토콜

- register_request -> register_outcome -> register_ack로 Session이 idle -> handshaking -> confirming -> active로 진행한다.

**Kick-old**: 같은 DeviceId로 새 Session이 등록되면 옛 Session을 동기적으로 끊는 규칙

- "Device당 live Session 1개" 불변식을 지킨다.

**Eviction**: liveness 시한을 넘긴 active Session을 Controller가 강제 종료하는 것 (메트릭 `ddcs_agents_evicted_total`)

**Roster (active set)**: 명령 가능한 active Device 집합

- Policy의 load 집계와 Command 발신 대상의 단일 진실이며, 끊긴 Device의 stale Shadow를 배제한다.

## 텔레메트리와 명령

**Status**: Device의 상태 스냅샷(mode, load, temp)

- Agent가 보고하고 DeviceShadow가 최신값을 보관한다.
- non-finite(NaN/Inf) 보고는 버리고 직전 last-good을 보존한다.

**Command**: Controller가 Policy 결과로 Device에 목표 Mode를 지시하는 메시지

- `(DeviceId, CommandId)` 상관 + supersede + 재전송/dedup으로 신뢰 전달한다.

**Supersede**: 같은 Device의 같은 명령 계열에 새 의도가 도착하면 옛 미결 Command를 교체하는 것 (옛 명령의 늦은 응답은 stale로 무시된다.)

**Mode**: Device의 운영 상태 어휘 (safe/normal/performance)

- Device와 Controller가 공유하는 커널 어휘이자 Policy의 목표값이다.

## Group 정책

**Group**: 하나의 Policy를 공유하는 Device들의 논리적 묶음 (`zone_a`/`zone_b`/`zone_c`)

- Agent가 등록 시 선언한다.

**Policy**: 한 Group의 목표 Mode를 정하는 규칙

- load 축과 온도 축을 합성하되 적용 단위가 다르다
  - load: Group 단위 집계
  - temp: 개별 device 단위

**Regime**: Group 집계 load의 부하 상태

- avg load가 `high_load`를 넘으면 busy, `low_load` 아래면 idle이다.
- busy/idle은 밴드 위치이지 Mode가 아니다
- 어느 regime이 어느 base Mode가 될지는 Group마다 operator가 정한다.

**부하 밴드 (Load band)**: Regime 전환에 쓰는 히스테리시스 밴드

- `low_load`와 `high_load` 사이 데드밴드에서는 regime을 유지해 플래핑을 막는다.

**온도 override (Temperature override)**: load와 독립인 device 단위 안전 트립

- Device 온도가 `high_temp`를 넘으면 그 device만 `high_temp_mode`로 가고 `resume_temp` 아래로 식으면 해제된다.
- load보다 thermal을 우선시한다.

**Thermal (hot/cool)**: 온도 override 뒤의 per-device 상태

- `high_temp` 초과로 hot 트립, `resume_temp` 미만으로 cool 해제하는 단방향 히스테리시스 latch이다.

**Base Mode**: Group의 Regime이 매핑하는 기본 Mode (busy면 high_load_mode, idle면 low_load_mode)

- 온도 override가 없는 Device가 따른다.

**Effective mode**: 한 Device에 실제로 명령되는 최종 목표 Mode

- thermal이 hot이면 `high_temp_mode`, 아니면 Base Mode다.
- 스팸 방지를 위해서 직전 발신값과 달라진 Device에만 SetMode를 발행한다.
