find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network)

add_library(simulator_core STATIC
    src/core/TelemetryEngine.cpp
    src/core/TelemetryEngine.h
    src/net/SimulatorClient.cpp
    src/net/SimulatorClient.h
    src/app/SimulatorConfig.cpp
    src/app/SimulatorConfig.h
    src/app/RuntimeStatusWriter.cpp
    src/app/RuntimeStatusWriter.h
)
target_include_directories(simulator_core PUBLIC src)
target_link_libraries(simulator_core PUBLIC Qt6::Core Qt6::Network ev_protocol)

add_executable(ev_charger_simulator
    src/main.cpp
    src/ui/SimulatorWindow.cpp
    src/ui/SimulatorWindow.h
)
target_link_libraries(ev_charger_simulator PRIVATE simulator_core ev_pulse_ui Qt6::Widgets)

# --- tests ---
if(BUILD_TESTING)
find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
add_executable(qsave_probe EXCLUDE_FROM_ALL tests/tools/qsave_probe.cpp)
target_link_libraries(qsave_probe PRIVATE Qt6::Core)
add_executable(tst_simulator_engine tests/tst_telemetryengine.cpp)
target_link_libraries(tst_simulator_engine PRIVATE simulator_core Qt6::Test)
add_test(NAME simulator_engine COMMAND tst_simulator_engine)

add_executable(tst_simulator_client tests/tst_simulatorclient.cpp)
target_link_libraries(tst_simulator_client PRIVATE simulator_core Qt6::Test)
add_test(NAME simulator_client COMMAND tst_simulator_client)

add_executable(tst_simulator_runtime_status tests/tst_runtimestatus.cpp)
target_link_libraries(tst_simulator_runtime_status PRIVATE simulator_core Qt6::Test)
add_dependencies(tst_simulator_runtime_status ev_charger_simulator)
add_test(NAME simulator_runtime_status COMMAND tst_simulator_runtime_status)

add_executable(tst_simulator_window
    tests/tst_simulatorwindow.cpp
    src/ui/SimulatorWindow.cpp
)
target_link_libraries(tst_simulator_window PRIVATE simulator_core ev_pulse_ui Qt6::Widgets Qt6::Test)
add_test(NAME simulator_window COMMAND tst_simulator_window)
set_tests_properties(simulator_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
