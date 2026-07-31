add_executable(agent main.cpp)
target_link_libraries(
    agent
    PRIVATE ddcs::build_options ddcs::config ddcs::wire::frame ddcs::agent::domain
            ddcs::agent
)
