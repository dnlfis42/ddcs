# 기반 라이브러리
add_subdirectory(common)
add_subdirectory(json)
add_subdirectory(logger)

# 앱 지원
add_subdirectory(config)

# 런타임 라이브러리
add_subdirectory(io)
add_subdirectory(net)

# 계약 라이브러리
add_subdirectory(device)
add_subdirectory(wire)

# 애플리케이션 라이브러리
add_subdirectory(ctrl)
add_subdirectory(agent)
