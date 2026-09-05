find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network Sql)

add_executable(ev_admin_server
    src/main.cpp
    src/app/AppContext.cpp
    src/app/AppContext.h
    src/core/Result.h
    src/core/Types.h
    src/dashboard/SnapshotWriter.cpp
    src/dashboard/SnapshotWriter.h
    src/db/DatabaseManager.cpp
    src/db/DatabaseManager.h
    src/network/ApiServer.cpp
    src/network/ApiServer.h
    src/services/AuthService.cpp
    src/services/AuthService.h
    src/services/DashboardService.cpp
    src/services/DashboardService.h
    src/services/ForecastService.cpp
    src/services/ForecastService.h
    src/services/RequestLogService.cpp
    src/services/RequestLogService.h
    src/ui/LoginDialog.cpp
    src/ui/LoginDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

target_include_directories(ev_admin_server PRIVATE src)
target_compile_definitions(ev_admin_server PRIVATE EV_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(ev_admin_server PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    ev_protocol
)
