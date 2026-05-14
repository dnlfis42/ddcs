# Distributed Device Control System

분산 장치 제어 시스템.

## 개요

- 중앙 **controller**(서버)가 다수의 **agent**(클라이언트)를 조율한다.
- 각 agent는 디바이스 위에서 실행되며 제어 동작을 수행한다.
- controller와 agent의 통신은 TCP 기반 자체 와이어 프로토콜을 사용한다.

## 요구사항

- Ubuntu 24.04 (x86_64)
- GCC 13
- CMake 3.25+
- C++20

## 빌드

```bash
cmake --workflow --preset [debug | asan | release]
```

## 사용법

## 프로젝트 구조

```
doc/    # 디자인 문서
app/    # 실행 파일 (agent, controller)
lib/    # 라이브러리
test/   # 통합 테스트
script/ # 개발 보조 스크립트
docker/ # 개발/CI 컨테이너 이미지
```

## 상태

## 라이선스

MIT — [LICENSE](LICENSE)
