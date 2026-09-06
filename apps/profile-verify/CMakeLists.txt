add_executable(profile-verify main.cpp)
target_link_libraries(profile-verify PRIVATE ddcs::build_options ddcs::profile)
