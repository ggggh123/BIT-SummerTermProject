find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME environment_tools
    COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_SOURCE_DIR}/tests/scripts/test_environment_tools.py -q)
set_tests_properties(environment_tools PROPERTIES TIMEOUT 60 LABELS "core;environment")

add_test(NAME demo_runtime_unit
    COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_SOURCE_DIR}/tests/scripts/test_demo_runtime.py -q)
set_tests_properties(demo_runtime_unit PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 90
    LABELS "core;runtime")

add_test(NAME demo_runtime_live
    COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_SOURCE_DIR}/tests/scripts/test_demo_runtime_live.py -q -s)
set_tests_properties(demo_runtime_live PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "EV_DEMO_BUILD_DIR=${CMAKE_BINARY_DIR};QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
    RESOURCE_LOCK "core_demo_runtime"
    LABELS "core;runtime;live-offscreen-not-manual-gui")
