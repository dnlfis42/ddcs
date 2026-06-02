# syntax=docker/dockerfile:1
# Multi-stage, multi-target build.
# - builder: controller + agent 두 binary 를 한 번에 빌드 (apt/cmake/source 캐시 공유).
# - controller / agent: 각 binary 만 골라서 runtime 이미지 생성.

FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src
RUN cmake --preset release \
    && cmake --build build/release --target ctrl agent --parallel

# 공통 runtime base - 두 target 이 공유.
FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# controller 이미지
FROM runtime AS controller
COPY --from=builder /src/build/release/bin/ctrl /usr/local/bin/ctrl
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/ctrl"]

# agent 이미지
FROM runtime AS agent
COPY --from=builder /src/build/release/bin/agent /usr/local/bin/agent
ENTRYPOINT ["/usr/local/bin/agent"]
