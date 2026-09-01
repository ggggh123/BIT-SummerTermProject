find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_contracts tst_contracts.cpp)
target_link_libraries(tst_contracts PRIVATE Qt6::Test ev_contracts)
add_test(NAME contracts COMMAND tst_contracts)

add_executable(tst_framecodec tst_framecodec.cpp)
target_link_libraries(tst_framecodec PRIVATE Qt6::Test ev_protocol)
add_test(NAME framecodec COMMAND tst_framecodec)

add_executable(tst_jsonenvelope tst_jsonenvelope.cpp)
target_link_libraries(tst_jsonenvelope PRIVATE Qt6::Test ev_protocol)
target_compile_definitions(tst_jsonenvelope PRIVATE
    TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/shared/protocol/fixtures"
)
add_test(NAME jsonenvelope COMMAND tst_jsonenvelope)
