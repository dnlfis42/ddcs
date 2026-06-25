add_executable(agent main.cpp)
target_link_libraries(
    agent
    PRIVATE ddcs::build_options ddcs::config ddcs::agent::domain ddcs::agent
)
