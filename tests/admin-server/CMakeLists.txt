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
