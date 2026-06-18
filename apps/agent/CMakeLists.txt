add_executable(agent main.cpp)
target_link_libraries(
    agent
    PRIVATE ddcs::build_options ddcs::agent ddcs::agent::domain
)
