find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network Test)

add_executable(tst_core_workflow
    tst_core_workflow.cpp
    ../../simulator/src/ui/SimulatorWindow.cpp
    ../../simulator/src/ui/SimulatorWindow.h
)

target_compile_definitions(tst_core_workflow PRIVATE
    EV_ADMIN_SERVER_PATH="$<TARGET_FILE:ev_admin_server>"
    EV_TEST_GOLDEN_DB="${CMAKE_SOURCE_DIR}/runtime/golden/core.db"
)
target_include_directories(tst_core_workflow PRIVATE ../../simulator/src)
target_link_libraries(tst_core_workflow PRIVATE
    ev_user_client_core
    simulator_core
    ev_protocol
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
)
add_dependencies(tst_core_workflow ev_admin_server)

add_test(NAME core_workflow COMMAND tst_core_workflow)
set_tests_properties(core_workflow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
)
