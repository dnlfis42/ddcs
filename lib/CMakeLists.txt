# 기반 라이브러리
add_subdirectory(common)
add_subdirectory(json)

# 앱 지원
add_subdirectory(logger)
add_subdirectory(config)

# 런타임 라이브러리
add_subdirectory(io)
add_subdirectory(net)

# 도메인 커널
add_subdirectory(device)

# 프로토콜
add_subdirectory(wire)

# 애플리케이션 라이브러리
add_subdirectory(ctrl)
add_subdirectory(agent)
