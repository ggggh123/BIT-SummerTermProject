find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
add_library(ev_pulse_ui STATIC PulseChart.cpp PulseChart.h)
target_include_directories(ev_pulse_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ev_pulse_ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
