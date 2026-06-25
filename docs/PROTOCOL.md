# DDCS Wire Protocol

DDCS는 controller와 agent 사이 TCP 위에서 동작하는 자체 wire protocol이다.
전송 단위는 **frame** - 고정 크기 헤더 + 가변 길이 payload. payload 하나가 곧 하나의 **message**다.

와이어는 두 계층으로 갈린다: `wire::frame`(프레이밍)과 `wire::message`(메시지). frame은 payload의 의미를 모른다 - message type조차 frame이 아니라 payload 안에 있다.

## Frame (`wire::frame`)

### 레이아웃

```
0       1       2       3       4
+-------+-------+-------+-------+
|     magic     |    length     | 헤더: 4 byte
+-------+-------+-------+-------+
|      payload (message)        | message: length byte
+-------------------------------+
```

### 필드

| 필드     | 크기 | 의미                                                     |
| -------- | ---- | -------------------------------------------------------- |
| `magic`  | 2 B  | DDCS protocol 식별자. 값 = `0xDDC5`.                     |
| `length` | 2 B  | payload(=message) 바이트 수. **헤더는 포함하지 않는다.** |

frame 헤더엔 message type이 없다. type은 payload(message)의 선두 바이트다(아래 Message 참고).

### 바이트 순서

헤더의 multi-byte 필드(`magic`, `length`)는 **big-endian** (network byte order).

### 상수

| 이름               | 값           |
| ------------------ | ------------ |
| magic              | `0xDDC5`     |
| header size        | 4 bytes      |
| max payload length | 1024 bytes   |
| length 필드 한계   | 65,535 bytes |

`length 필드 한계`는 `length`(u16)의 표현 한계고, `max payload length`(1024)는 protocol이 실제 허용하는 payload 상한이다. `length > max payload length`인 frame은 protocol violation으로 connection을 끊는다. 이 검사는 codec이 아니라 **caller**가 한다(codec은 구조 검증, caller는 정책).

> frame 계층은 payload를 **opaque**로 다룬다. magic 검증과 length 추출만 하고 안의 type/body는 모른다. 의미 해석은 message/app 계층뿐이다.
> wire format을 incompatible하게 바꿔야 하면 `magic`을 새 값으로 재발급해 별개 protocol family로 다룬다.

## Message (`wire::message`)

frame payload 위에서 동작하는 논리 단위. payload 선두 1바이트가 **message type**이고 나머지가 body다: `[type][body]`.

### Type

| type   | 이름               | 방향 | body                                                      |
| ------ | ------------------ | ---- | --------------------------------------------------------- |
| `0x01` | `register_request` | A->C | `uuid` : uuid(16B), `group` : string                      |
| `0x02` | `register_outcome` | C->A | `code` : enum(u8)                                         |
| `0x03` | `register_ack`     | A->C | empty                                                     |
| `0x10` | `heartbeat`        | A->C | empty                                                     |
| `0x11` | `status`           | A->C | `mode` : u8, `load` : f64, `temp` : f64                   |
| `0x20` | `command_request`  | C->A | `command_id` : u64, `command_type` : u8, `payload` : rest |
| `0x21` | `command_ack`      | A->C | `command_id` : u64                                        |
| `0x22` | `command_outcome`  | A->C | `command_id` : u64, `code` : enum(u8)                     |

응답 message는 두 어휘만 쓴다: **ack**(수신 확인, body 최소) / **outcome**(판정, `code`만). 판정 사유(reason)는 로컬 로그 전용이고 **wire에는 싣지 않는다**.

type은 고위 nibble로 그룹을 나눈다:

- `0x0x` Register, `0x1x` Telemetry(heartbeat/status), `0x2x` Command.
- 같은 그룹 내 확장은 저위 nibble 안에서 추가한다.

`code` enum(u8) 값: `success = 0`, `failed = 1` (register_outcome / command_outcome 공용 의미)

### 인코딩 규칙

- message type은 body 선두 1바이트(u8)다.
- **frame 헤더는 big-endian, message body의 정수는 little-endian**이다.
- `uuid`는 **raw 16 byte** (길이 prefix 없음).
- `string`은 `uint16` length prefix(little-endian) + UTF-8 raw bytes. null terminator 없음. 길이는 0 이상이며 frame payload 한계 안에서 임의.
- `f64`(`load`, `temp`)는 IEEE 754 binary64 bit pattern을 little-endian으로 기록한다.
- `command_request`의 `payload(rest)`만 예외: **length prefix 없이 body의 나머지 전부**를 차지한다(아래 2단 discriminator 참고).
- decode는 *구조적 검증*만 한다. wire 바이트가 schema 길이 요건과 정확히 부합(부족/trailing 바이트 없음)하는지만 본다. enum 값 유효성, 의미 제약은 호출자 책임.

## Command body (2단 discriminator)

`command_request`(`0x20`)는 2단계로 종류를 가린다.

1. message type = `0x20`(command_request) - message가 명령임을 결정
2. `command_type`(body 내 u8) = **CommandType** - 뒤따르는 `payload(rest)`의 해석을 결정

`payload(rest)`는 length prefix 없이 body의 나머지 전부이며, `CommandType`에 따라 디코드한다.

### CommandType

| 값     | 이름       | payload(rest)     |
| ------ | ---------- | ----------------- |
| `0x00` | `invalid`  | -                 |
| `0x01` | `set_mode` | `mode` : enum(u8) |

`status.mode`와 `set_mode.mode` enum(u8) 값:

| 값  | 이름          |
| --- | ------------- |
| `0` | `safe`        |
| `1` | `normal`      |
| `2` | `performance` |

mode 어휘<->wire byte 매핑은 `device` 커널이 소유한다(`device::encode_mode`/`decode_mode`). wire codec은 raw u8만 싣고, 어휘 유효성 검증은 수신측(`device::decode_mode`)이 한다.

## 의미론

- **등록은 3-way handshake다**: `register_request`(A->C) -> `register_outcome`(C->A) -> `register_ack`(A->C)
  controller는 `register_ack` 수신 시점부터 liveness를 측정한다.
  등록 왕복(outcome 전달 + ack 회신) 지연이 liveness 시한을 잠식하지 않게 하기 위함이며, 등록 구간(요청 대기 / ack 대기)은 단계별로 별도 시한이 걸린다.
- agent는 success `register_outcome`을 받으면 즉시 `register_ack`을 보내고, 곧바로 첫 `heartbeat`를 시작한다.
- 등록 완료 전에 유효한 A->C message는 단계당 하나다: 등록 전엔 `register_request`, 판정 송신 후엔 `register_ack`
  그 외 message는 프로토콜 위반으로 connection을 종료한다.
- **Liveness**는 active 세션에서 수신한 모든 정상 message로 갱신한다. `heartbeat`는 body 없는 keepalive다.
  controller는 liveness 타임아웃 시 연결을 강제 종료한다.
- `register_outcome`은 *상대를 식별한 뒤*에만 송신한다. 식별 자체가 불가능하면(`register_request` decode 실패) 응답 없이 connection을 종료한다.
  `code = failed`면 판정 송신 후 connection을 종료한다(`register_ack`을 기대하지 않는다).
  판정 사유는 wire에 없고 로컬 로그로만 남으며, 실패 판정 전달은 best-effort다(송신 직후 종료라 tx가 flush되지 않을 수 있다).
- **kick-old(new-wins)**: 같은 `register_request.uuid`로 새 연결이 등록하면 controller가 옛 연결을 강제 종료하고 새 연결을 바인딩한다.
- TCP 연결이 곧 transport 식별 단위다. controller 내부 식별자는 wire에 별도로 싣지 않으며, `register_request.uuid`가 재접속을 가로질러 등록 주체를 식별한다.
- **명령 상관은 `(device, command_id)`다** (연결 단위가 아님). controller는 미결 명령에 타임아웃을 두고, 응답(`command_ack`/`command_outcome`)을 그 device의 현재 등록 연결에서 받아 상관한다. 재접속 후 새 연결로 온 늦은 `command_outcome`도 수용한다(같은 device면 유효, 중복 실행 방지에 유리).
- **재전송은 동일 `command_id`로 한다.** 무응답(timeout) 또는 실패 `command_outcome` 시 controller가 지수 backoff 후 **같은 id**로 재전송하고, agent는 **중복 수신 시 멱등하게 재실행**한다.
  현재 명령 어휘(`set_mode`)가 멱등 상태 선언이라 성립한다(비멱등 명령 type을 추가하려면 그 type은 수신측 dedup + outcome 재송신으로 격상해야 한다). 닫힌/대체된 `command_id`로 오는 늦은 응답은 무시한다.
- **supersede(최신 의도 우선)**: 같은 device의 같은 명령 계열(`command_type`)에 새 명령이 발급되면 controller는 옛 미결을 폐기하고 새 명령(새 `command_id`)으로 교체한다.
  TCP 순서 보장 덕에 agent는 항상 최신을 마지막으로 적용한다.

## Unknown / 비정상 type

frame 계층은 payload를 열지 않으니 type을 전혀 보지 않는다(magic/length만 검증). type 판별은 message/app 계층의 몫이다.

- `message_type()`은 payload 선두 바이트를 그대로 `MessageType`으로 읽는다(검증/소비 없음). 빈 payload면 `invalid`.
- app dispatch 단계에서 카탈로그에 없거나 방향이 어긋난(예: C->A 전용을 controller가 수신) type은 프로토콜 위반으로 보고 연결을 종료한다.
