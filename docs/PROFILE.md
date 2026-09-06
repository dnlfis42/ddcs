# Controller tick 프로파일

Controller의 tick profiler는 command sweep, session sweep, policy evaluate와 전체 tick 시간을
고정 크기 메모리 버퍼에 기록한다. 정상 종료(SIGTERM) 뒤에만 JSON으로 덤프하므로 SIGKILL, OOM,
crash에서는 raw dump를 보장하지 않는다. 기본값은 비활성화이며, 비활성화 상태에서는 버퍼와 dump를
만들지 않는다.

`DDCS_PROFILE_ENABLED`, `DDCS_PROFILE_CAPACITY`, `DDCS_PROFILE_OUTPUT_PATH`,
`DDCS_PROFILE_RUN_ID`는 Controller의 저수준 설정이다. 일반 측정에서는 직접 설정하지 않고 아래
runner가 실행별 임시 경로를 주입한다.

## 결과 계약

모든 측정과 시나리오는 하나의 build identity에 귀속된다. identity에는 release configuration,
source revision·dirty 상태, Controller/Agent image ID, `config/` 전체 SHA-256이 들어간다.

```text
var/
└── result/
    └── build-<sha256>/
        ├── build.json
        └── performance/
            ├── <UTC>-perf-balance-<PID>/
            └── <UTC>-perf-suite-<PID>/
```

`build.json`은 build identity와 다음 세 종류의 대표 결과를 함께 가진다.

```json
{
  "profile": {
    "overhead": {
      "single-1000": {
        "duration_seconds": 120,
        "repetitions": 3,
        "statistic": "median"
      }
    },
    "capture": {
      "balance-1000": {
        "duration_seconds": 300,
        "verified": true,
        "summary": {}
      }
    }
  },
  "scenario": {
    "thermal": "pass",
    "agent-reconnect": "not_run"
  }
}
```

`single-1000`처럼 workload 조건은 `배치방식-총Agent수(4자리)`로 표현한다.

- `balance`: 총 Agent를 zone 네 개에 균등 분배한다. 총수는 4의 배수여야 한다.
- `single`: 전체 Agent를 zone_a 하나에 둔다.

`performance/`만 raw evidence를 보존한다. profile raw JSON, 양끝 metrics snapshot, profile-verify
출력은 `/tmp/ddcs-profile-*`에서만 사용하고, 검증과 대표값 계산이 끝나면 제거한다. 따라서
`build.json`에는 물리 run ID나 임시 경로가 남지 않는다.

## 대표 profile 수집

release의 분석 도구를 먼저 build한다.

```sh
cmake --build --preset release --target profile-report profile-verify
scripts/profile-capture.sh balance 1000 300
```

인자는 순서대로 workload 방식, 총 Agent 수, 측정 시간(초)다. 5분(300초)은 tick이 1 Hz인 현재
설정에서 분포와 p95를 볼 수 있는 기본 길이다. runner는 연결 안정화, 시작·종료 metrics snapshot,
Controller의 정상 종료, raw/metrics prefix 교차 검증을 순서대로 수행한다.

성공하면 `profile.capture.<condition>`에 다음을 기록한다.

- `duration_seconds`, `verified`
- raw buffer의 `capacity`, `captured`, `dropped`, complete 여부
- 선택된 tick 수와 예외 단계 수
- tick·세 단계·tick start interval의 count, mean, p95, max (ns)

`verified: true`는 `profile-verify`가 raw의 누락·예외·µs 절삭 규약을 종료 직전 metrics와 대조해
통과했다는 뜻이다. `dropped > 0` 또는 검증 실패 결과는 포트폴리오 수치로 사용하지 않는다.

기본적으로 `perf-preflight.sh`를 통과해야 한다. 진단용으로만 생략하려면
`DDCS_PROFILE_SKIP_PREFLIGHT=1`을 지정할 수 있지만, 그 결과는 비교 수치로 쓰지 않는다.

## Profiler overhead

Profiler를 켠 상태와 끈 상태의 비용은 시간에 따라 달라지는 CPU 온도·background load를 피하려고
교차 수집한다.

```sh
cmake --build --preset release --target profile-report profile-verify
scripts/profile-overhead.sh single 1000
```

한 조건에서 `off-01 → on-01 → off-02 → on-02 → off-03 → on-03`으로 실행한다. 각 run은 2분이고,
최종 결과는 세 값의 중앙값 하나다. `profile.overhead.<condition>`에는 다음 비교값을 기록한다.

- tick 평균 작업 시간(µs)
- Controller 평균 CPU 사용률(가능한 경우)
- 명령 완료가 있던 창의 평균 RTT(ms), 없으면 `null`

각 지표는 `off`, `on`, `delta`, `ratio`를 가진다. raw sample과 six run의 ID는 보존하지 않는다.
전제 검사는 기본으로 수행하며, 진단 목적으로만
`DDCS_PROFILE_OVERHEAD_SKIP_PREFLIGHT=1`을 사용할 수 있다.

## profile-report와 profile-verify

`profile-report`는 raw JSON을 CSV 분포로 바꾸는 순수 분석기이고, `profile-verify`는 raw profile과
종료 직전 Prometheus snapshot의 tick prefix를 교차 검증하는 순수 검사기다. runner가 두 프로그램을
자동 호출한다. 개발 중 raw 파일을 직접 다룰 때만 다음 형식을 쓴다.

```sh
build/release/bin/profile-report /tmp/tick-profile.json --from-ns 0
build/release/bin/profile-verify /tmp/tick-profile.json /tmp/metrics-end.prom
```

raw timestamp는 Controller monotonic recording origin에서 지난 정수 ns다. `recording_origin_utc`의
bracket을 사용하면 `profile-report --from-unix-ns/--to-unix-ns`가 측정 창 안에 확실히 들어가는
raw interval로 보수적으로 변환한다. p95는 nearest-rank, 평균은 정수 ns 내림이다.
