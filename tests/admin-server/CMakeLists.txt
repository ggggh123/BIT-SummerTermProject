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
