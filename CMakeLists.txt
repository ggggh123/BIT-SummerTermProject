cmake_minimum_required(VERSION 3.22)

project(EvChargingPlatform VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

enable_testing()

add_subdirectory(shared/protocol)
add_subdirectory(apps/admin-server)
add_subdirectory(apps/user-client)
add_subdirectory(simulator)
add_subdirectory(tests/admin-server)
add_subdirectory(tests/admin-gui)
add_subdirectory(tests/shared)
add_subdirectory(tests/integration)
