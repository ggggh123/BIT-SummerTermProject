if(NOT TARGET ev_protocol)
    message(FATAL_ERROR "apps/user-client requires the shared ev_protocol target")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets Test)

add_library(ev_user_client_core STATIC
    src/app/UserAppConfig.cpp
    src/app/WebEngineRuntime.cpp
    src/domain/Formatters.cpp
    src/net/TcpJsonClient.cpp
    src/ui/MainWindow.cpp
)
target_include_directories(ev_user_client_core PUBLIC src)
target_link_libraries(ev_user_client_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::WebEngineWidgets
        ev_protocol
)

add_executable(ev_user_client src/main.cpp)
target_link_libraries(ev_user_client PRIVATE ev_user_client_core)

add_executable(tst_user_formatters tests/tst_formatters.cpp)
target_link_libraries(tst_user_formatters PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_formatters COMMAND tst_user_formatters)
set_tests_properties(user_formatters PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_user_webengine_runtime tests/tst_webengine_runtime.cpp)
target_link_libraries(tst_user_webengine_runtime PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_webengine_runtime COMMAND tst_user_webengine_runtime)

add_executable(tst_user_tcpjsonclient tests/tst_tcpjsonclient.cpp)
target_link_libraries(tst_user_tcpjsonclient PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_tcpjsonclient COMMAND tst_user_tcpjsonclient)
