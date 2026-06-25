add_executable(ctrl main.cpp)
target_link_libraries(ctrl PRIVATE ddcs::build_options ddcs::config ddcs::ctrl)
