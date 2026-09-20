add_executable(agent-fleet main.cpp)
target_link_libraries(agent-fleet PRIVATE ddcs::build_options ddcs::agent)
