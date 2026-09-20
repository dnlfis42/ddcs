# syntax=docker/dockerfile:1
# builder에서 ctrl, agent, agent-fleet을 함께 빌드한다.
# 실행 이미지는 공통 runtime에 필요한 실행 파일을 복사해 만든다.

FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src
ARG DDCS_BUILD_JOBS=4
RUN cmake --preset release -DDDCS_ENABLE_SCRIPT_TEST=OFF \
    && cmake --build build/release --target ctrl agent agent-fleet --parallel ${DDCS_BUILD_JOBS}

# 세 실행 이미지에서 공통으로 사용하는 라이브러리를 설치한다.
FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Controller 실행 이미지
FROM runtime AS controller
COPY --from=builder /src/build/release/bin/ctrl /usr/local/bin/ctrl
# Controller 설정과 정책이 담긴 config/controller.json을 함께 배포한다.
COPY --from=builder /src/config /config
EXPOSE 8080 9000
ENTRYPOINT ["/usr/local/bin/ctrl"]

# 단일 Agent 실행 이미지
FROM runtime AS agent
COPY --from=builder /src/build/release/bin/agent /usr/local/bin/agent
ENTRYPOINT ["/usr/local/bin/agent"]

# 다중 Agent 실행 이미지
FROM runtime AS agent-fleet
COPY --from=builder /src/build/release/bin/agent-fleet /usr/local/bin/agent-fleet
COPY --from=builder /src/config /config
ENTRYPOINT ["/usr/local/bin/agent-fleet"]
