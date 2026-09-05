find_package(Qt6 6.2 REQUIRED COMPONENTS Test Sql)

add_executable(tst_admin_auth
    tst_admin_auth.cpp
    ../../apps/admin-server/src/services/AuthService.cpp
    ../../apps/admin-server/src/services/AuthService.h
    ../../apps/admin-server/src/core/Result.h
)

target_include_directories(tst_admin_auth PRIVATE ../../apps/admin-server/src)
target_link_libraries(tst_admin_auth PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME admin_auth COMMAND tst_admin_auth)

add_executable(tst_request_log
    tst_request_log.cpp
    ../../apps/admin-server/src/services/RequestLogService.cpp
    ../../apps/admin-server/src/services/RequestLogService.h
    ../../apps/admin-server/src/core/Result.h
)

target_include_directories(tst_request_log PRIVATE ../../apps/admin-server/src)
target_link_libraries(tst_request_log PRIVATE Qt6::Test Qt6::Sql ev_protocol)
add_test(NAME request_log COMMAND tst_request_log)

add_executable(tst_user_flow
    tst_user_flow.cpp
    ../../apps/admin-server/src/services/AuthService.cpp
    ../../apps/admin-server/src/services/AuthService.h
    ../../apps/admin-server/src/services/UserService.cpp
    ../../apps/admin-server/src/services/UserService.h
    ../../apps/admin-server/src/core/Result.h
)

target_include_directories(tst_user_flow PRIVATE ../../apps/admin-server/src)
target_link_libraries(tst_user_flow PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME user_flow COMMAND tst_user_flow)

add_executable(tst_admin_service
    tst_admin_service.cpp
    ../../apps/admin-server/src/services/AdminService.cpp
    ../../apps/admin-server/src/services/AdminService.h
    ../../apps/admin-server/src/core/Result.h
)

target_include_directories(tst_admin_service PRIVATE ../../apps/admin-server/src)
target_link_libraries(tst_admin_service PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME admin_service COMMAND tst_admin_service)

add_executable(tst_dashboard_service
    tst_dashboard_service.cpp
    ../../apps/admin-server/src/services/DashboardService.cpp
    ../../apps/admin-server/src/services/DashboardService.h
)

target_include_directories(tst_dashboard_service PRIVATE ../../apps/admin-server/src)
target_link_libraries(tst_dashboard_service PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME dashboard_service COMMAND tst_dashboard_service)

add_executable(tst_telemetry_service
    tst_telemetry_service.cpp
    ../../apps/admin-server/src/services/TelemetryService.cpp
    ../../apps/admin-server/src/services/TelemetryService.h
    ../../apps/admin-server/src/core/Result.h
)

target_include_directories(tst_telemetry_service PRIVATE ../../apps/admin-server/src)
target_link_libraries(tst_telemetry_service PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME telemetry_service COMMAND tst_telemetry_service)
