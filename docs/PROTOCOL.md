# DDCS Wire Protocol

DDCS는 controller와 agent 사이 TCP 위에서 동작하는 자체 wire protocol이다.
전송 단위는 **frame** - 고정 크기 헤더 + 가변 길이 payload.

## Frame

### 레이아웃

```
0       1       2       3       4       5
+-------+-------+-------+-------+-------+
|     magic     | type  |    length     | 헤더: 5 byte
+-------+-------+-------+-------+-------+
|           payload (message)           | 메시지: length byte
+---------------------------------------+
```

### 필드

| 필드     | 크기 | 의미                                                                        |
| -------- | ---- | --------------------------------------------------------------------------- |
| `magic`  | 2 B  | DDCS protocol 식별자. 값 = `0xDDC5`.                                        |
| `type`   | 1 B  | message 종류를 결정하는 opaque 바이트. frame 계층은 의미를 모른다.          |
| `length` | 2 B  | Payload 바이트 수. **헤더는 포함하지 않는다.** 범위 `0 <= length <= 65535`. |

### 바이트 순서

헤더의 multi-byte 필드(`magic`, `length`)는 **big-endian** (network byte order).

### 상수

| 이름         | 값           |
| ------------ | ------------ |
| magic        | `0xDDC5`     |
| header size  | 5 bytes      |
| max length   | 128 bytes    |
| length limit | 65,535 bytes |

`length limit`은 `length` 필드의 표현 한계고, `max length`는 protocol이 실제로 허용하는 payload 상한이다. `length > max length`인 frame은 protocol violation으로 connection을 끊는다.

> `type`/`payload`는 frame/infra 계층에서 **opaque**다. 의미를 해석하는 곳은 app 계층뿐이다.
> wire format을 incompatible하게 바꿔야 한다면 `magic`을 새 값으로 재발급해 별개 protocol family로 다룬다.

## Message

Frame payload 위에서 동작하는 논리 단위. Frame header의 `type`이 message 종류를 결정한다.

### Type

| type   | 이름              | 방향 | payload                                                       |
| ------ | ----------------- | ---- | ------------------------------------------------------------- |
| `0x01` | `RegisterRequest` | A->C | `id` : uuid(16B), `group` : string                            |
| `0x02` | `RegisterOutcome` | C->A | `result` : enum(u8), `reason` : string                        |
| `0x03` | `RegisterAck`     | A->C | empty                                                         |
| `0x10` | `Heartbeat`       | A->C | empty                                                         |
| `0x11` | `Status`          | A->C | `mode` : uint8, `load` : f64, `temp` : f64                    |
| `0x20` | `Command`         | C->A | `command_id` : uint64, `type` : uint8, `payload` : raw bytes  |
| `0x21` | `CommandAck`      | A->C | `command_id` : uint64                                         |
| `0x22` | `CommandOutcome`  | A->C | `command_id` : uint64, `result` : enum(u8), `reason` : string |

응답 message 이름은 두 어휘만 쓴다: **Ack**(수신 확인, 본문 최소) / **Outcome**(판정, `result` + `reason`).

type은 고위 nibble로 그룹을 나눈다:

- `0x0x` Register, `0x1x` Telemetry(Heartbeat/Status), `0x2x` Command.
- 같은 그룹 내 확장은 저위 nibble 안에서 추가한다.

`result` enum(u8) 값: `success = 0`, `failed = 1` (RegisterOutcome / CommandOutcome 공용 의미)

### 인코딩 규칙

- 정수는 **little-endian**이다.
- `uuid`는 **raw 16 byte** (길이 prefix 없음).
- `string`은 `uint16` length prefix(little-endian) + UTF-8 raw bytes. null terminator 없음. 길이는 0 이상이며 frame payload 한계 안에서 임의.
- `enum`은 underlying type을 raw로 1 byte 기록한다.
- `f64`는 IEEE 754 binary64 bit pattern을 little-endian으로 기록한다.
- Command body의 `payload(rest)`만 예외: **length prefix 없이 body의 나머지 전부**를 차지한다(중첩 discriminator 참고).
- decode는 *구조적 검증*만 한다. wire 바이트가 schema 길이 요건과 정확히 부합(부족/trailing 바이트 없음)하는지만 본다. enum 값 유효성, 의미 제약은 호출자 책임.

## Command body (중첩 discriminator)

`Command`(`0x20`)는 2단계로 종류를 가린다.

1. frame `type` = `0x20` (Command) - message가 명령임을 결정
2. `Command.type` (body 내 uint8) = **CommandType** - 뒤따르는 `payload(rest)`의 해석을 결정

`payload(rest)`는 length prefix 없이 body의 나머지 전부이며, `CommandType`에 따라 디코드한다.

### CommandType

| 값     | 이름      | payload(rest)     |
| ------ | --------- | ----------------- |
| `0x01` | `SetMode` | `mode` : enum(u8) |

`Status.mode`와 `SetMode.mode` enum(u8) 값:

| 값  | 이름          |
| --- | ------------- |
| `0` | `safe`        |
| `1` | `normal`      |
| `2` | `performance` |

## 의미론

- **등록은 3-way handshake다**: `RegisterRequest`(A->C) -> `RegisterOutcome`(C->A) -> `RegisterAck`(A->C).
  controller는 `RegisterAck` 수신 시점부터 liveness를 측정한다. 등록 왕복(outcome 전달 + ack 회신) 지연이
  liveness 시한을 잠식하지 않게 하기 위함이며, 등록 구간(요청 대기 / ack 대기)은 단계별로 별도 시한이 걸린다.
- agent는 success `RegisterOutcome`을 받으면 즉시 `RegisterAck`을 보내고, 곧바로 첫 `Heartbeat`를 시작한다.
- 등록 완료 전에 유효한 A->C message는 단계당 하나다: 등록 전엔 `RegisterRequest`, 판정 송신 후엔 `RegisterAck`.
  그 외 message는 프로토콜 위반으로 connection을 종료한다.
- **Liveness**는 active 세션에서 수신한 모든 정상 message로 갱신한다. `Heartbeat`는 payload 없는 keepalive다.
  controller는 liveness 타임아웃 시 연결을 강제 종료한다.
- `RegisterOutcome`은 *상대를 식별한 뒤*에만 송신한다. 식별 자체가 불가능하면(RegisterRequest decode 실패) 응답 없이 connection을 종료한다.
  `result = failed`면 판정 송신 후 connection을 종료한다(`RegisterAck`을 기대하지 않는다). 실패 사유 전달은 best-effort다(송신 직후 종료라 tx가 flush되지 않을 수 있다).
- **kick-old(new-wins)**: 같은 `RegisterRequest.id`로 새 연결이 등록하면 controller가 옛 연결을 강제 종료하고 새 연결을 바인딩한다.
- TCP 연결이 곧 transport 식별 단위다. controller 내부 식별자는 wire에 별도로 싣지 않으며, `RegisterRequest.id`가 재접속을 가로질러 등록 주체를 식별한다.
- **명령 상관은 `(device, command_id)`다** (연결 단위가 아님). controller는 미결 명령에 타임아웃을 두고, 응답(`CommandAck`/`CommandOutcome`)을 그 device의 현재 등록 연결에서 받아 상관한다. 재접속 후 새 연결로 온 늦은 `CommandOutcome`도 수용한다(같은 device면 유효, 중복 실행 방지에 유리).
- **재전송은 동일 `command_id`로 한다.** 무응답(timeout) 또는 실패 `CommandOutcome` 시 controller가 지수 backoff 후 **같은 id**로 재전송하고, agent는 **중복 수신 시 멱등하게 재실행**한다. 현재 명령 어휘(`SetMode`)가 멱등 상태 선언이라 성립한다(비멱등 명령 type을 추가하려면 그 type은 수신측 dedup + outcome 재송신으로 격상해야 한다). 닫힌/대체된 `command_id`로 오는 늦은 응답은 무시한다.
- **supersede(최신 의도 우선)**: 같은 device의 같은 명령 계열(`Command.type`)에 새 명령이 발급되면 controller는 옛 미결을 폐기하고 새 명령(새 `command_id`)으로 교체한다. TCP 순서 보장 덕에 agent는 항상 최신을 마지막으로 적용한다.

## Unknown / 비정상 type

Frame 계층은 `type`을 검증하지 않고 raw `uint8`을 그대로 보존한다.
app dispatch 단계에서 카탈로그에 없거나 방향이 어긋난(예: C->A 전용을 controller가 수신) `type`은 프로토콜 위반으로 보고 연결을 종료한다.
