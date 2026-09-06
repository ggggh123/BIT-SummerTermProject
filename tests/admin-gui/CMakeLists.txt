find_package(Qt6 6.2 REQUIRED COMPONENTS Test Widgets Network Sql)

add_executable(tst_admin_runtime
    tst_admin_runtime.cpp
)

target_compile_definitions(tst_admin_runtime PRIVATE
    EV_ADMIN_SERVER_PATH="$<TARGET_FILE:ev_admin_server>"
    EV_TEST_GOLDEN_DB="${CMAKE_SOURCE_DIR}/runtime/golden/core.db"
)
target_link_libraries(tst_admin_runtime PRIVATE Qt6::Test Qt6::Network Qt6::Sql ev_protocol)
add_dependencies(tst_admin_runtime ev_admin_server)
add_test(NAME admin_runtime COMMAND tst_admin_runtime)
set_tests_properties(admin_runtime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_admin_window_refresh
    tst_admin_window_refresh.cpp
    ../../apps/admin-server/src/ui/LoginDialog.cpp
    ../../apps/admin-server/src/ui/LoginDialog.h
    ../../apps/admin-server/src/db/DatabaseWorker.cpp
    ../../apps/admin-server/src/db/DatabaseWorker.h
    ../../apps/admin-server/src/network/ConnectionWorker.cpp
    ../../apps/admin-server/src/network/ConnectionWorker.h
    ../../apps/admin-server/src/services/RequestDispatcher.cpp
    ../../apps/admin-server/src/services/RequestDispatcher.h
    ../../apps/admin-server/src/app/AppContext.cpp
    ../../apps/admin-server/src/app/AppContext.h
    ../../apps/admin-server/src/dashboard/SnapshotWriter.cpp
    ../../apps/admin-server/src/dashboard/SnapshotWriter.h
    ../../apps/admin-server/src/db/DatabaseManager.cpp
    ../../apps/admin-server/src/db/DatabaseManager.h
    ../../apps/admin-server/src/network/ApiServer.cpp
    ../../apps/admin-server/src/network/ApiServer.h
    ../../apps/admin-server/src/services/AdminService.cpp
    ../../apps/admin-server/src/services/AdminService.h
    ../../apps/admin-server/src/services/AuthService.cpp
    ../../apps/admin-server/src/services/AuthService.h
    ../../apps/admin-server/src/services/DashboardService.cpp
    ../../apps/admin-server/src/services/DashboardService.h
    ../../apps/admin-server/src/services/ForecastService.cpp
    ../../apps/admin-server/src/services/ForecastService.h
    ../../apps/admin-server/src/services/RequestLogService.cpp
    ../../apps/admin-server/src/services/RequestLogService.h
    ../../apps/admin-server/src/services/TelemetryService.cpp
    ../../apps/admin-server/src/services/TelemetryService.h
    ../../apps/admin-server/src/services/UserService.cpp
    ../../apps/admin-server/src/services/UserService.h
    ../../apps/admin-server/src/ui/MainWindow.cpp
    ../../apps/admin-server/src/ui/MainWindow.h
)
target_include_directories(tst_admin_window_refresh PRIVATE ../../apps/admin-server/src)
target_compile_definitions(tst_admin_window_refresh PRIVATE
    EV_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    EV_TEST_GOLDEN_DB="${CMAKE_SOURCE_DIR}/runtime/golden/core.db"
)
target_link_libraries(tst_admin_window_refresh PRIVATE
    Qt6::Test Qt6::Widgets Qt6::Network Qt6::Sql ev_protocol)
add_test(NAME admin_window_refresh COMMAND tst_admin_window_refresh)
set_tests_properties(admin_window_refresh PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
