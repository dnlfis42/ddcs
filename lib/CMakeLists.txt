# 기반 타입과 도메인 모델
add_subdirectory(common)
add_subdirectory(device)

# 데이터 형식과 관측
add_subdirectory(json)
add_subdirectory(logger)

# 앱 지원
add_subdirectory(config)

# 프로토콜 형식
add_subdirectory(wire)

# 런타임 I/O
add_subdirectory(io)

# 애플리케이션 계층
add_subdirectory(agent)
add_subdirectory(ctrl)
