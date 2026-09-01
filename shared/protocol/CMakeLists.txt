find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(ev_contracts INTERFACE)
target_include_directories(ev_contracts INTERFACE ${CMAKE_SOURCE_DIR}/shared)

add_library(ev_protocol
    FrameCodec.cpp
    FrameCodec.h
    JsonEnvelope.cpp
    JsonEnvelope.h
    Envelope.h
)
target_include_directories(ev_protocol PUBLIC ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(ev_protocol PUBLIC Qt6::Core Qt6::Network ev_contracts)
