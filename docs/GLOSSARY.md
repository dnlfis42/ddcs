# DDCS 용어집

**분산 디바이스 제어 시스템 (DDCS)**: 하나의 Controller가 다수의 Agent를 조율하는 시스템

- 각 Agent는 자기 Device의 Status를 주기적으로 보고하며 Controller가 내린 Command를 적용한다.
- 진실의 원천인 Device는 Agent 쪽에 있고, Controller는 Device의 투영인 DeviceShadow를 보관한다.

## 행위자

**Controller**: 다수 Device의 집합적 Mode를 Group 단위 Policy로 조율하는 단일 서버

- Agent를 통해 각 Device를 관측(Status)하고 구동(Command)한다. Agent 연결 관리는 이 관측과 구동을 지탱하는 수단이다.

**Agent**: 하나의 Device를 호스팅해 Controller에 등록하고 Status를 보고하며 Command를 적용하는 클라이언트

- 자기만의 신원 없이 자신이 맡은 Device의 신원을 대리 등록하므로, 시스템 어디서나 1 Agent = 1 Device가 성립한다.

## Device 모델

**Device**: Agent가 제어하는 실제 장치

- 상태와 신원의 진실의 원천이며, Controller가 보는 것은 어디까지나 그 투영(DeviceShadow)이다.

**DeviceId**: Device의 영속 신원

- Agent가 등록 시 제시하지만, 신원 자체는 Agent 프로세스가 아니라 Device의 것이다.
- DeviceShadow의 키이며 재접속을 가로질러 지속된다.

**Mode**: Device의 운영 상태 어휘 (safe/normal/performance)

- Device와 Controller가 공유하는 커널 어휘이자 Policy가 계산하는 목표값이다.

**Status**: Device의 상태 스냅샷 (`mode`, `load`, `temp`)

- Agent가 주기적으로 보고하고 DeviceShadow가 최신값을 보관한다.
- non-finite(NaN/Inf) 값이 보고되면 Controller는 그 값을 버리고 직전 정상값(last-good)을 보존하되, 보고 자체는 liveness 신호로 인정한다.

**DeviceShadow**: Controller가 DeviceId로 보관하는 Device의 투영

- 최근 Status의 캐시이며, 재접속을 가로질러 이어지도록 Session보다 오래 산다.

## 세션

**Session**: 등록된 Agent와 Controller 사이의 살아있는 관계 하나

- 하나의 TCP 연결(Connection)에 묶여 있어 연결이 끊기면 함께 사라지고, 같은 DeviceId로 재접속해도 새 Session이 만들어진다.
- handshaking 중에는 Device 없이 존재하다가 confirming 단계부터 Device에 바인딩되고 역색인에 등재되며, liveness 감시는 active부터 시작한다.

**Handshake (3-way)**: Agent 등록 프로토콜

- Agent와 Controller가 `register_request` → `register_outcome` → `register_ack` 순서로 주고받는다.
- 그동안 Session 상태는 idle → handshaking → confirming → active로 전이한다.

**Liveness**: active Session이 살아있다는 신호

- heartbeat를 비롯한 모든 정상 message가 이 신호를 갱신하며, 시한 내 아무 신호가 없으면 Controller는 그 Session이 죽은 것으로 보아 축출(Eviction)한다.

**Eviction**: liveness 시한을 넘긴 active Session을 Controller가 강제 종료하는 것

- `SessionService`의 주기 sweep이 시한을 넘긴 Session을 감지해 축출한다.
- 메트릭: `ddcs_agents_evicted_total`

**Kick-old (new-wins)**: 같은 DeviceId로 새 Session이 등록되면 옛 Session을 동기적으로 끊는 규칙

- 같은 DeviceId의 새 등록은 대개 Device가 재접속했다는 신호이다. 따라서 Controller는 옛 연결을 더는 유효하지 않은 것으로 판단해 새 연결로 즉시 교체한다.
- "Device당 바인딩된 연결 최대 1" 불변식을 지킨다.
- 로그 이벤트: `session.kick_old`

**역색인**: DeviceId에서 그 Device가 바인딩된 Session으로 가는 역방향 색인

- `SessionRegistry`가 유지하며, Kick-old가 옛 Session을 찾을 때와 Command 발신이 대상 연결을 찾을 때 이 색인을 거친다.
- "Device당 바인딩된 연결 최대 1" 불변식의 키이다.

**Roster (active set)**: 명령 가능한 active Device 집합

- Policy의 load 집계와 Command 발신 대상을 정하는 단일 진실로, 끊긴 Device의 stale Shadow가 집계에 섞이지 않게 배제한다.

**stale**: 더 이상 현재 상태나 의도를 반영하지 않아 집계와 처리에서 배제되는 데이터

- 끊긴 Device의 Shadow가 해당하며, Roster가 load 집계에서 배제한다.
- supersede된 Command의 늦은 응답도 해당하는데, Controller는 이를 무시하고 카운터로만 관측한다.
- 메트릭: `ddcs_commands_stale_total`

## 런타임

**sweep tick**: Controller의 주기 도메인 작업 한 바퀴

- 주기 sweep 타이머(기본 1s, `controller.sweep_interval_ms`)가 tick마다 명령 재전송, 핸드셰이크/liveness 만료 축출, 정책 평가를 순서대로 구동한다.
- Controller는 단일 스레드라 tick 작업 시간이 sweep 주기에 근접하면 그 코어가 포화에 이른다.
- 메트릭: `ddcs_sweep_duration_us`, `ddcs_sweep_ticks_total`

**지수 backoff (Exponential backoff)**: 실패가 반복될수록 재시도 간격을 두 배씩 늘리는 재시도 전략

- Agent 재접속(코드 기본 1s 시작, 30s 상한)과 Controller의 명령 재전송이 함께 쓰며, 등록에 성공하면 재접속 backoff는 리셋된다.
- 상한으로 클램프한 뒤 ±25% jitter를 더해, 동시에 끊긴 여러 Agent가 같은 박자로 재시도하지 않게 한다.

## 명령

**Command**: Controller가 Policy 결과로 Device에 목표 Mode를 지시하는 message

- Controller는 응답(`command_ack`/`command_outcome`)을 `(DeviceId, CommandId)` 키로 원 명령에 대응시킨다.
- 유실은 재전송이 메운다. 재전송으로 생기는 중복 적용은 Agent의 중복 제거(dedup)가 거른다. 낡은 의도는 Supersede가 새 명령으로 교체한다.

**CommandId**: Controller가 명령마다 발급하는 상관 토큰

- `(DeviceId, CommandId)`가 명령 상관 키이며, wire에서는 raw u64(`command_id`)로 오간다.
- 재전송은 같은 CommandId를 유지하고, Supersede는 새 CommandId로 교체한다.
- `command_id = 0`은 invalid로 예약된다.

**Supersede**: 같은 Device의 같은 명령 계열에 새 의도가 도착하면 옛 미결 Command를 교체하는 것

- 같은 계열에는 최신 의도 하나만 미결로 남으며, 옛 Command의 늦은 응답은 Controller가 stale로 무시한다.

## Group 정책

**Group**: 하나의 Policy를 공유하는 Device들의 논리적 묶음 (`zone_a`/`zone_b`/`zone_c`)

- Agent가 등록 시 `device.group` 키로 선언한다. 정책에 없는 Group 등록은 Controller가 soft로 처리해, 등록은 허용하되 정책 명령 대상에서만 제외한다.

**Policy**: 한 Group의 목표 Mode를 정하는 규칙

- load 축과 온도 축을 합성하되 적용 단위가 달라, load는 Group 단위로 집계하고 temp는 개별 Device 단위로 판정한다.

**Regime**: Group 집계 load의 부하 상태

- avg load가 `high_load`를 넘으면 busy, `low_load` 아래면 idle이다.
- busy/idle은 밴드 위치이지 Mode가 아니며, 어느 Regime이 어느 Base Mode로 이어질지는 Group마다 operator가 정한다.

**히스테리시스 (Hysteresis)**: 진입 임계와 해제 임계를 다르게 두어 상태가 경계에서 떨리지 않게 하는 기법

- 단일 임계만 쓰면 측정치가 임계 근처에서 떨릴 때마다 상태와 Mode 명령이 함께 오가는 **플래핑**(flapping)이 나타나는데, 두 임계 사이 구간인 데드밴드가 이 진동을 흡수한다.
- 데드밴드 안에서는 측정치만으로 상태를 정할 수 없어 직전 상태를 기억해 둬야 하는데, 이 기억을 **latch**(걸쇠)라고 한다.
- load 축(부하 밴드)과 온도 축(thermal latch)이 모두 이 기법을 쓴다.

**부하 밴드 (Load band)**: Regime 전환에 쓰는 load 축 히스테리시스 밴드

- `low_load`와 `high_load` 두 임계로 이루어지며, 그 사이에서는 Regime이 유지된다.

**데드밴드 (Deadband)**: 히스테리시스의 두 임계 사이, 상태 전환이 일어나지 않는 구간

- load 축에서는 `low_load`~`high_load` 사이가, 온도 축에서는 `resume_temp`~`high_temp` 사이가 데드밴드이다. 전자는 Regime을, 후자는 thermal latch의 hot/cool을 유지한다.

**온도 override (Temperature override)**: load와 무관하게 Device 단위로 동작하는 과열 보호

- Device 온도가 `high_temp`를 넘으면 그 Device만 load와 무관하게 `high_temp_mode`로 가고, `resume_temp` 아래로 식으면 해제된다.
- Effective Mode 합성에서 thermal은 load보다 우선한다.

**Thermal (hot/cool)**: 온도 override 뒤의 Device별 상태

- `high_temp`를 넘으면 hot으로 걸리고, `resume_temp` 아래로 식어야 cool로 풀린다. 두 임계 사이에서 직전 상태를 그대로 기억하는 히스테리시스 latch이다.
- 잠금은 과열(hot) 한 방향으로만 걸리고(단방향), cool은 잠긴 상태가 아니라 override가 풀린 보통 상태이다.

**Base Mode**: Group의 Regime이 매핑하는 기본 Mode

- busy Regime이면 `high_load_mode`, idle이면 `low_load_mode`가 되며, 온도 override가 걸리지 않은 Device가 이 Mode를 따른다.

**Effective Mode**: Controller가 한 Device에 실제로 명령하는 최종 목표 Mode

- thermal이 hot이면 `high_temp_mode`, 아니면 Base Mode다.
- 직전 발신값(belief)과 달라진 Device에만 `set_mode`를 발행해 중복 명령을 억제한다.

**belief**: Controller가 Device별로 기억하는 직전 제어 상태 (commanded Mode와 thermal latch)

- Effective Mode가 commanded belief와 다를 때만 `set_mode`를 발행하므로, belief는 중복 명령을 거르는 기준값이다.
- 세션이 끝나면 Controller가 그 Device의 belief를 폐기해, 재접속(리부트)한 Device가 stale 명령 belief에 갇히지 않고 현재 정책으로 재명령받는다.
- SIGHUP 정책 교체는 commanded belief만 비우며, thermal은 히스테리시스 latch라 유지한다.
