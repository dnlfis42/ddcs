# 프로파일링

Controller의 tick이 길어졌다는 사실만으로는 어느 처리가 시간을 차지하는지 알 수 없습니다.
DDCS는 전체 tick과 명령 재시도 검사, 세션 검사, 정책 평가의 시간을 따로 기록해 지연이 발생한 단계를 확인합니다.

계측 자체도 실행 시간을 늘릴 수 있으므로 프로파일은 기본으로 끄고, 수집한 기록을 메트릭과 대조한 뒤 켰을 때와 껐을 때의 비용을 비교합니다. 부하 생성 방식과 전체 처리 성능의 판정은 [성능 측정](performance.md)에 정리했습니다.

## 목차

- [기록 방식](#기록-방식)
- [프로파일 수집](#프로파일-수집)
- [분석 도구](#분석-도구)
- [계측 비용](#계측-비용)
- [결과 저장](#결과-저장)

## 기록 방식

tick profiler는 `command sweep`, `session sweep`, `policy evaluate`와 전체 tick의 시간을 고정 크기 메모리 버퍼에 기록합니다.
실행 중 파일 출력이 계측에 개입하지 않도록 정상 종료 시 JSON으로 덤프합니다. 수집 스크립트는 SIGTERM으로 종료하며, SIGKILL·OOM·crash에서는 원문을 보장하지 않습니다.
비활성화 상태에서는 버퍼와 덤프를 만들지 않습니다.

Controller의 저수준 설정은 `DDCS_PROFILE_ENABLED`, `DDCS_PROFILE_CAPACITY`, `DDCS_PROFILE_OUTPUT_PATH`, `DDCS_PROFILE_RUN_ID`입니다.
일반 수집에서는 아래 스크립트가 실행별 임시 경로와 run ID를 주입하므로 직접 지정할 필요가 없습니다. 수집 버퍼 용량은 `DDCS_PROFILE_CAPACITY`로 바꿀 수 있으며 스크립트 기본값은 16,384개입니다. 전체 항목은 [프로파일 설정](config.md#프로파일-설정)에 정리했습니다.

## 프로파일 수집

저장소 루트에서 Release 분석 도구를 빌드한 뒤 수집합니다. 로컬 빌드 환경 외에 Linux, Docker Compose v2, Bash, Git, curl, jq, Python 3가 필요합니다.

```sh
cmake --preset release
cmake --build --preset release --target profile-report profile-verify
scripts/profiling/capture-controller-profile.sh balance 4000 300
```

인자는 배치 방식, 총 Agent 수, 측정 시간(초)입니다. 위 예시는 현재 설정의 1초 tick을 5분간 관측해 분포를 수집합니다.
스크립트는 새 Controller와 Fleet을 실행하고, 연결 안정화와 예열, 시작·종료 메트릭 수집, Controller 정상 종료, 원문과 메트릭의 교차 검증을 순서대로 수행합니다.

부하는 [AgentFleet](performance.md#부하-생성기-agentfleet)으로 생성합니다. 기본값은 Fleet당 1,000대이며, `single`은 모두 zone_a에, `balance`는 네 구역에 균등 배치합니다.
기본 설정에서 `single`은 총수가 1,000의 배수, `balance`는 4,000의 배수여야 합니다. 총수 입력 범위는 1~65,504이며, 작은 실행에서는 `DDCS_PERF_AGENTS_PER_FLEET`도 함께 낮춥니다.
실행마다 독립 Compose 프로젝트를 사용하지만 다른 스택과 호스트 포트는 공유할 수 없습니다.

준비·측정 시작·종료 시 연결 수와 구역별 Status를 보유한 장치 수가 목표와 맞는지 검사합니다.
준비 제한은 `DDCS_PROFILE_READY_TIMEOUT`(기본 90초), 예열은 `DDCS_PROFILE_WARMUP_SECONDS`(기본 30초)로 지정합니다.

### 수집 결과 확인

수집 요약에는 지정한 측정 시간, 검증 여부, 버퍼의 `capacity`·`captured`·`dropped`와 완전성, 선택한 tick 수와 예외 단계 수를 기록합니다.
전체 tick·세 단계·tick 시작 간격에 대해 count, mean, p95, max를 ns 단위로 계산합니다.

`verified: true`는 `profile-verify`가 원문의 누락·예외·µs 절삭 규약을 종료 직전 메트릭과 대조해 통과했다는 뜻입니다.
버퍼에서 기록을 버렸거나(`dropped > 0`) 검증에 실패한 결과는 대표 성능 수치로 사용하지 않습니다.

기본적으로 `scripts/measurement/verify-environment.sh fleet`을 실행합니다. 이 검사는 호스트 설정을 바꾸지 않습니다.
진단 목적으로 `DDCS_PROFILE_SKIP_PREFLIGHT=1`을 지정해 생략할 수 있지만, 검사를 생략하거나 WARN이 있으면 `DDCS_PROFILE_RECORD_CAPTURE=true`여도 대표 결과를 저장하지 않고 기존 결과를 유지합니다.
진단 요약은 표준 출력과 선택적으로 `DDCS_PROFILE_RESULT_JSON`에 남습니다. `preflight_skipped`, `preflight_warning_count`, `representative_eligible`로 저장 자격을 구분합니다.

## 분석 도구

수집 스크립트는 원문을 분포로 변환하는 `profile-report`와 메트릭에 대조하는 `profile-verify`를 자동으로 호출합니다.
개발 중 원문을 직접 다룰 때에는 다음처럼 실행합니다. 두 도구는 Controller를 실행하지 않습니다.

```sh
build/release/bin/profile-report /tmp/tick-profile.json --from-ns 0
build/release/bin/profile-verify /tmp/tick-profile.json /tmp/metrics-end.prom
```

`profile-report`는 원문 JSON에서 CSV 분포를 만들며, `profile-verify`는 종료 직전 Prometheus snapshot에 포함된 tick 구간을 원문과 교차 검증합니다.
원문의 timestamp는 Controller의 monotonic recording origin에서 지난 정수 ns입니다.
`recording_origin_utc`의 시간 범위를 사용하면 `profile-report --from-unix-ns/--to-unix-ns`가 측정 창 안에 확실히 들어가는 원문 구간으로 보수적으로 변환합니다.
p95는 nearest-rank 방식이며, 평균은 정수 ns로 내림합니다.

## 계측 비용

프로파일을 켠 실행과 끈 실행을 한 번씩만 비교하면 CPU 온도나 다른 프로세스의 부하 변화가 계측 비용처럼 보일 수 있습니다.
비교 스크립트는 두 조건을 번갈아 세 번씩 실행하고, 각 지표의 중앙값을 비교합니다.

```sh
cmake --preset release
cmake --build --preset release --target profile-report profile-verify
scripts/profiling/measure-profiler-overhead.sh single 1000
```

실행 순서는 `off-01 → on-01 → off-02 → on-02 → off-03 → on-03`이며 각 측정 구간은 120초입니다.
Controller와 Fleet 이미지는 한 번 빌드해 여섯 실행에 재사용합니다. 각 실행의 build identity, 부하 구성, 프로파일 활성화 여부와 검증 상태가 맞는지도 확인합니다.

비교 항목은 tick 평균 작업 시간(µs), Controller 평균 CPU 사용률, 명령 평균 RTT(ms)입니다.
각 항목에 `off`, `on`, 차이인 `delta`, 비율인 `ratio`를 기록합니다. CPU를 읽을 수 없거나 완료 명령이 없어 RTT를 계산할 수 없으면 해당 값은 `null`이며, 기준값이 0이면 비율도 `null`입니다.

공통 사전 검사는 부모 스크립트가 수행합니다. 진단용 `DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT=1`로 생략하거나 WARN이 있으면 요약만 출력하고 대표 결과는 저장하지 않습니다.
하위 수집은 검사를 반복하지 않아 각 요약에 `preflight_skipped=true`가 남지만, 최종 저장 여부는 부모가 수행한 공통 검사로 결정합니다.

## 결과 저장

프로파일 결과도 [공통 build identity](performance.md#저장되는-근거)에 귀속됩니다. 성능 측정과 같은 Fleet 이미지를 사용하며, 설정이나 소스가 달라지면 결과를 별도로 구분합니다.

대표 요약은 `var/result/build-<sha256>/build.json`에 기록합니다. 표 1은 이 파일에서 결과를 찾는 위치입니다.

|항목|내용|
|---|---|
|`profile.capture.<condition>`|수집 시간, 검증 여부, 분포 요약|
|`profile.overhead.<condition>`|측정 시간, 반복 수, off/on 중앙값과 차이|

*표 1. build.json의 결과 구분*

조건 이름은 `배치방식-총Agent수`이며 Agent 수는 최소 네 자리로 표시합니다. 예를 들어 4대는 `single-0004`, 1,000대는 `single-1000`, 10,000대는 `single-10000`입니다.
입력 가능한 수와 실제 공급 가능한 부하는 다릅니다. 메모리·CPU·Fleet 출발지 포트 범위의 제약도 함께 확인해야 합니다.

프로파일 수집은 원문 JSON, 시작·종료 메트릭과 검증 출력을 임시 디렉터리에서만 사용하고 스택 정리가 성공하면 제거합니다.
정리가 실패하면 마운트된 임시 디렉터리를 보존한 채 실패 종료하고 대표 결과를 기록하지 않습니다.
계측 비용 비교도 여섯 실행의 원시 표본과 run ID를 보존하지 않습니다. 따라서 `build.json`에는 요약을 저장하며, 물리 run ID나 임시 경로를 근거로 남기지 않습니다.
