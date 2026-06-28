# DDCS 용어집 (Glossary)

분산 장치 제어 시스템. 하나의 Controller가 다수의 Agent를 조율하고, 각 Agent는 자기 Device의 상태를 주기 보고하며 Controller가 내린 명령을 적용한다. 진실의 원천인 Device는 Agent 쪽에 있고, Controller는 그 투영인 DeviceShadow를 보관한다.

## Language

### 행위자

**Controller**:
다수 Device의 집합적 Mode를 Group 단위 Policy로 조율하는 단일 서버. Agent를 통해 각 Device를 관측(Status)하고 구동(Command)하며, Agent 연결 관리는 그 수단이다.
_Avoid_: server, master, broker

**Agent**:
하나의 Device를 호스팅해 Controller에 등록하고 상태를 보고하며 명령을 적용하는 클라이언트.
_Avoid_: client, node, worker

### Device 모델

**Device**:
Agent가 제어하는 실제 장치. 상태와 신원의 진실의 원천. agent는 이 신원을 controller에 등록할 뿐, 자기만의 신원을 따로 갖지 않는다(1 agent : 1 device).
_Avoid_: DeviceShadow, DeviceInfo, DeviceState

**DeviceId**:
Device의 영속 신원. DeviceShadow가 이것으로 키잉되고 재접속을 가로질러 지속된다. agent가 등록 시 제시하지만 device의 것이다(agent 프로세스의 것이 아님).
_Avoid_: agent uuid, login id, connection id

**DeviceShadow**:
Controller가 DeviceId로 보관하는 Device의 투영. 최근 보고된 Status의 캐시이며 재접속을 가로질러 지속된다.
_Avoid_: Shadow(단독), Twin, DeviceInfo, Device

### 텔레메트리

**Status**:
Device의 상태 스냅샷(mode, load, temp). Agent가 보고하고 DeviceShadow가 최신값을 보관한다.
_Avoid_: State, DeviceState, telemetry, reading

### 세션

**Session**:
등록된 Agent와 Controller 사이의 살아있는 관계 하나. 한 연결에 묶여 한 Device에 바인딩되고, 끊기면 사라진다(재접속은 새 Session).
_Avoid_: Agent(=클라이언트), connection, Twin

### 정책

**Group**:
하나의 Policy를 공유하는 Device들의 논리적 묶음(예: `zone_a`). Agent가 등록 시 선언한다.
_Avoid_: cluster, tag

**Policy**:
한 Group의 Mode를 정하는 규칙. 두 축을 합성하되 적용 단위가 다르다. **load는 Group 단위** -- 집계 load를 히스테리시스 밴드(low/high)와 비교해 regime(busy/idle)을 정하고 각 regime을 그 Group의 base Mode로 매핑한다(busy=상단 초과 / idle=하단 미만은 regime이지 Mode 값이 아니다). **온도 override는 device 단위** -- 개별 device가 자기 온도로 `high_temp`를 넘으면 그 device만 `high_temp_mode`로 트립하고 `resume_temp` 아래에서 해제된다(데드밴드). 밴드/임계를 넘는 전환에서만 발효된다(플래핑 방지).
_Avoid_: rule, config

**Mode**:
Device의 운영 상태 어휘. safe / normal / performance. Device와 Controller가 공유하는 커널 어휘이자 Policy의 목표.
_Avoid_: OperatingMode, State
