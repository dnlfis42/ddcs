add_subdirectory(e2e)

if(DDCS_ENABLE_SCRIPT_TEST)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME ddcs_perf_assess_test
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf_assess_test.py"
    )
    set_tests_properties(ddcs_perf_assess_test PROPERTIES TIMEOUT 30)

    add_test(
        NAME ddcs_perf_layout_test
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf_layout_test.py"
    )
    set_tests_properties(ddcs_perf_layout_test PROPERTIES TIMEOUT 30)

    add_test(
        NAME ddcs_perf_ramp_test
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf_ramp_test.sh"
    )
    set_tests_properties(ddcs_perf_ramp_test PROPERTIES TIMEOUT 30)

    add_test(
        NAME ddcs_profile_workflow_test
        COMMAND
            bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/profile_workflow_test.sh"
    )
    set_tests_properties(ddcs_profile_workflow_test PROPERTIES TIMEOUT 30)

    add_test(
        NAME ddcs_scenario_evidence_test
        COMMAND
            bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/scenario_evidence_test.sh"
    )
    set_tests_properties(ddcs_scenario_evidence_test PROPERTIES TIMEOUT 30)

    add_test(
        NAME ddcs_scenario_lifecycle_test
        COMMAND
            bash
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/scenario_lifecycle_test.sh"
    )
    set_tests_properties(ddcs_scenario_lifecycle_test PROPERTIES TIMEOUT 30)

    add_test(
        NAME ddcs_script_safety_test
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/script_safety_test.sh"
    )
    set_tests_properties(ddcs_script_safety_test PROPERTIES TIMEOUT 30)
endif()
