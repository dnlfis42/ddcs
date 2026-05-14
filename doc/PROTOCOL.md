# DDCS Wire Protocol

DDCS는 controller와 agent 사이 TCP 위에서 동작하는 자체 wire protocol이다.
전송 단위는 **frame** — 고정 크기 헤더 + 가변 길이 payload.

## Frame

### 레이아웃

```
0       1       2       3       4       5       6
+-------+-------+-------+-------+-------+-------+
|     magic     |version|opcode |    length     | 헤더 6 byte
+-------+-------+-------+-------+-------+-------+
|             payload (length bytes)            |
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
