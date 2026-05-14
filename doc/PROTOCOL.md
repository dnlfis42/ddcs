# DDCS Wire Protocol

DDCS는 controller와 agent 사이 TCP 위에서 동작하는 자체 wire protocol이다.
전송 단위는 **frame** — 고정 크기 헤더 + 가변 길이 payload.

## Frame

### 레이아웃

```
0       1       2       3       4       5       6
+-------+-------+-------+-------+-------+-------+
|     magic     |version|opcode |    length     | 헤더: 6 byte
+-------+-------+-------+-------+-------+-------+
|               payload (message)               | 메시지: length byte
+-----------------------------------------------+
```

### 필드

| 필드      | 크기 | 의미                                                                      |
| --------- | ---- | ------------------------------------------------------------------------- |
| `magic`   | 2 B  | DDCS protocol 식별자. 값 = `0xDDC5`.                                      |
| `version` | 1 B  | Payload 해석 규약 버전. 현재 = `0x01`.                                    |
| `opcode`  | 1 B  | `(version, opcode)` 쌍으로 message 종류를 결정한다.                       |
| `length`  | 2 B  | Payload 바이트 수. **헤더는 포함하지 않는다.** 범위 `0 ≤ length ≤ 65535`. |

### 바이트 순서

모든 multi-byte 필드는 **big-endian** (network byte order).

### 버전 정책

- `version`은 body의 vocabulary와 encoding 규약을 분기한다.
- **헤더 layout 자체는 모든 version에서 동일하며 절대 바뀌지 않는다.**
- wire format을 incompatible하게 바꿔야 한다면 `magic`을 새 값으로 재발급해 별개 protocol family로 다룬다.

### 상수

| 이름        | 값           |
| ----------- | ------------ |
| magic       | `0xDDC5`     |
| header size | 6 bytes      |
| max payload | 65,535 bytes |

## Message

Frame payload 위에서 동작하는 논리 단위. Frame header의 `opcode`가 message 종류를 결정한다.

### Opcode

| opcode | 이름              | 방향 | payload                                          |
| ------ | ----------------- | ---- | ------------------------------------------------ |
| `0x10` | `RegisterRequest` | A→C  | `agent_tag` : string                             |
| `0x11` | `RegisterSuccess` | C→A  | (empty)                                          |
| `0x12` | `RegisterFail`    | C→A  | `reason` : string                                |
| `0x20` | `Status`          | A→C  | `timestamp_ns` : uint64, `state` : string (JSON) |
| `0x30` | `Command`         | C→A  | `command_id` : uint64, `body` : string (JSON)    |
| `0x31` | `CommandAck`      | A→C  | `command_id` : uint64                            |
| `0x32` | `CommandSuccess`  | A→C  | `command_id` : uint64                            |
| `0x33` | `CommandFail`     | A→C  | `command_id` : uint64, `reason` : string         |

opcode는 그룹별로 분리한다:

- 고위 nibble = 그룹 (`0x1x` Register, `0x2x` Status, `0x3x` Command)
- 저위 nibble = variant. 그룹당 `0x?0`–`0x?F`를 예약하므로 같은 그룹 내 확장은 nibble 안에서 추가.

### 인코딩 규칙

- Message payload의 multi-byte 정수는 **little-endian**이다.
- `string` 타입은 `uint16` length prefix(little-endian) + UTF-8 raw bytes로 표현한다.
  null terminator는 없다. 길이는 0 이상이며 frame payload 한계 안에서 임의.
- JSON 타입(`state`, `body`)은 wire 표현상 `string`과 동일하다.
  JSON 텍스트 자체의 유효성은 application 책임이며 protocol은 검증하지 않는다.
- Message decode는 *구조적 검증*만 수행한다. 즉 wire 바이트가 schema 길이 요건과 부합하는지만 본다.
  enum 값 유효성, JSON 파싱, 의미적 제약은 호출자가 책임진다.

### 의미론

- `Status`는 주기 송신이며 동시에 heartbeat 역할을 한다.
  payload `state`가 비어 있는 Status도 valid한 heartbeat 신호다.
- `RegisterFail`과 `CommandFail`은 _상대를 식별한 뒤 거부할 때만_ 송신한다.
  식별 자체가 불가능하면 응답 없이 connection을 종료한다.
- TCP 연결이 곧 agent 식별이며, agent ID는 wire에 박지 않는다.
  controller는 내부적으로만 agent를 관리한다.

### Unknown opcode

Frame layer는 opcode를 검증하지 않고 raw `uint8`을 그대로 보존한다.
Message dispatch 단계에서 `Opcode` 카탈로그에 매칭되지 않는 값은 거부한다.
이 정책은 향후 새 opcode 추가 시 protocol family 분기 없이 progressive하게 확장 가능하게 한다.
