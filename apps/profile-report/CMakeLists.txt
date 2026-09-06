add_executable(profile-report main.cpp)
target_link_libraries(profile-report PRIVATE ddcs::build_options ddcs::profile)
