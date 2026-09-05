if(NOT TARGET ev_protocol)
    message(FATAL_ERROR "apps/user-client requires the shared ev_protocol target")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets Test)

add_library(ev_user_client_core STATIC
    src/app/UserAppConfig.cpp
    src/app/WebEngineRuntime.cpp
    src/domain/ContractTimestamp.cpp
    src/domain/Formatters.cpp
    src/net/TcpJsonClient.cpp
    src/net/TencentMapClient.cpp
    src/services/UserApi.cpp
    src/ui/ChargePage.cpp
    src/ui/HistoryPage.cpp
    src/ui/LoginPage.cpp
    src/ui/MainWindow.cpp
    src/ui/NavigationPage.cpp
    src/ui/NearbyPage.cpp
    src/ui/ProfilePage.cpp
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

add_executable(ev_user_client src/main.cpp resources/resources.qrc)
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

add_executable(tst_user_api tests/tst_userapi.cpp resources/resources.qrc)
target_link_libraries(tst_user_api PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_api COMMAND tst_user_api)
set_tests_properties(user_api PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_user_tencentmap tests/tst_tencentmap.cpp resources/resources.qrc)
target_link_libraries(tst_user_tencentmap PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_tencentmap COMMAND tst_user_tencentmap)
set_tests_properties(user_tencentmap PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_DISABLE_SANDBOX=1")

add_executable(tst_user_chargepage tests/tst_chargepage.cpp resources/resources.qrc)
target_link_libraries(tst_user_chargepage PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_chargepage COMMAND tst_user_chargepage)
set_tests_properties(user_chargepage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_user_recommendation tests/tst_recommendation.cpp resources/resources.qrc)
target_link_libraries(tst_user_recommendation PRIVATE ev_user_client_core Qt6::Test)
add_test(NAME user_recommendation COMMAND tst_user_recommendation)
set_tests_properties(user_recommendation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Explicit opt-in: real Tencent calls must never run in the default build/CTest gate.
add_executable(user_map_online_smoke EXCLUDE_FROM_ALL
    tests/tst_online_map_smoke.cpp resources/resources.qrc)
target_link_libraries(user_map_online_smoke PRIVATE ev_user_client_core Qt6::Test)
