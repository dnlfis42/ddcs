# DDCS - Distributed Device Control System

분산 장치 제어 시스템. 중앙 **controller** 가 다수의 **agent** 를 관리하고, 각 agent 는 자기 장치의 상태를 주기 보고합니다.

> **Status**: Register / Heartbeat / Status 텔레메트리 + 양방향 명령(`CMD_*`, 재시도/백오프) + 히스테리시스 정책 엔진 + Prometheus 메트릭이 E2E 로 동작합니다. config 핫리로드는 향후 과제입니다.

## 주요 기능

- 단일 스레드 **EPOLL ET** 리액터 (controller / agent 양쪽), gen-token 핸들러 테이블
- 명시적 Connection FSM (controller 7-state, agent 4-state)
- agent **자동 재연결** (exponential backoff + jitter, cap 30s)
- 길이-prefix 프레이밍 + 타입-안전 메시지 카탈로그
- **양방향 명령 RPC** (command_id 상관, 타임아웃 + 재시도/백오프 부분실패 보상)
- **히스테리시스 정책 엔진** (그룹 부하 집계 -> 모드 자동 전환, 플래핑 방지)
- **Prometheus** 메트릭 스크레이프 엔드포인트 (HTTP)
- 재귀 **JSON DOM** (텔레메트리/정책), device shared kernel (Mode)
- ObjectPool 기반 zero-copy 버퍼, RAII fd
- **구조화 JSON 로그** (NDJSON, kv 필드, source_location)
- ASan/UBSan preset, GoogleTest 단위/통합/E2E

## 빠른 시작

요구: Ubuntu 24.04+ (x86_64), GCC 13+, CMake 3.25+, C++20.

```sh
# configure + build + test 한 번에
cmake --workflow --preset debug

# 또는 단계별
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

로컬 실행:

```sh
./build/debug/bin/ctrl &             # 8080 에서 listen
./build/debug/bin/agent              # 127.0.0.1:8080 에 connect
```

## Docker Compose

controller 1 + agent 3 을 한 번에 띄웁니다.

```sh
docker compose -f docker/docker-compose.yml up --build -d
docker compose -f docker/docker-compose.yml logs -f
docker compose -f docker/docker-compose.yml down
```

agent 들은 `DDCS_AGENT_UUID` 로 고정 식별되어 controller 의 kick-old 정책 검증에 사용 가능합니다.

controller 는 `9090` 에 Prometheus 메트릭을 노출하고 `config/policy.json` (그룹 `edge`) 정책을 부팅 시 로드합니다.

## 환경 변수 (agent)

| 변수                   | 기본값      | 비고                                                                    |
| ---------------------- | ----------- | ----------------------------------------------------------------------- |
| `DDCS_CONTROLLER_HOST` | `127.0.0.1` | controller 호스트명 또는 IP                                             |
| `DDCS_CONTROLLER_PORT` | `8080`      | controller TCP 포트                                                     |
| `DDCS_AGENT_UUID`      | random      | UUID 표준(`8-4-4-4-12`) 또는 32자 hex. 미지정 시 부팅마다 새 random     |
| `DDCS_AGENT_GROUP`     | `edge`      | 정책 타깃 그룹명 (policy.json 의 group 키와 매칭)                        |

## 빌드 프로파일

| Preset    | 용도      | 비고                           |
| --------- | --------- | ------------------------------ |
| `debug`   | 개발      | 디버그 심볼, 경고 그대로       |
| `asan`    | 위생 검사 | ASan + UBSan, `RelWithDebInfo` |
| `release` | 배포      | `-O2`, `-Werror`               |

```sh
cmake --workflow --preset asan
cmake --workflow --preset release
```

## 테스트

```sh
ctest --test-dir build/debug --output-on-failure
```

전체 `ctest` 39개 통과 - 공통 라이브러리 단위, 프로토콜 코덱, controller/agent FSM, 정책 엔진, controller<->agent 왕복 E2E.

## 디렉토리 구조

```
apps/   # 실행 파일 (agent, ctrl)
lib/
  common/   # clock, endian, fd, buffer, object_pool, ring_buffer, strong_id, uuid
  device/   # 공유 도메인 어휘 (Mode)
  json/     # 재귀 JSON DOM (dump/parse)
  logger/   # NDJSON 구조화 로거
  proto/    # wire protocol (frame / msg / cmd)
  io/       # epoll reactor, timer, signal
  agent/    # agent 측 (domain / port / app / infra + facade)
  ctrl/     # controller 측 (domain / port / app / infra + facade)
docker/     # Dockerfile + compose
docs/       # 설계 문서 (PROTOCOL.md)
test/e2e/   # controller <-> agent E2E
```

## 라이선스

[MIT License](LICENSE)
