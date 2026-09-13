cmake_minimum_required(VERSION 3.20)
project(PicoMotionTrackerBridgeMonakaIntegration LANGUAGES C CXX)

set(PICO_OT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_library(pico_ot_bridge_core STATIC
    "${PICO_OT_ROOT}/src/bridge/protocol.cpp"
    "${PICO_OT_ROOT}/src/bridge/clock_sync.cpp"
    "${PICO_OT_ROOT}/src/bridge/monotonic_clock.cpp")
target_compile_features(pico_ot_bridge_core PUBLIC cxx_std_17)
target_include_directories(pico_ot_bridge_core PUBLIC "${PICO_OT_ROOT}/include")
set_target_properties(pico_ot_bridge_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(pico_ot_bridge_transport STATIC
    "${PICO_OT_ROOT}/src/bridge/udp_socket.cpp"
    "${PICO_OT_ROOT}/src/bridge/receiver.cpp")
target_compile_features(pico_ot_bridge_transport PUBLIC cxx_std_17)
target_include_directories(pico_ot_bridge_transport PUBLIC "${PICO_OT_ROOT}/include")
target_link_libraries(pico_ot_bridge_transport PUBLIC pico_ot_bridge_core)
set_target_properties(pico_ot_bridge_transport PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(pico_ot_bridge_capi SHARED
    "${PICO_OT_ROOT}/src/bridge/receiver_c_api.cpp")
target_compile_features(pico_ot_bridge_capi PRIVATE cxx_std_17)
target_include_directories(pico_ot_bridge_capi PUBLIC "${PICO_OT_ROOT}/include")
target_link_libraries(pico_ot_bridge_capi PRIVATE pico_ot_bridge_transport)
target_compile_definitions(pico_ot_bridge_capi PRIVATE PICO_OT_BRIDGE_BUILD_DLL)
set_target_properties(pico_ot_bridge_capi PROPERTIES
    OUTPUT_NAME "pico_ot_bridge_capi"
    PREFIX "")

add_library(pico_ot_bridge_publisher STATIC
    "${PICO_OT_ROOT}/src/bridge/publisher.cpp")
target_compile_features(pico_ot_bridge_publisher PUBLIC cxx_std_17)
target_include_directories(pico_ot_bridge_publisher PUBLIC "${PICO_OT_ROOT}/include")
target_link_libraries(pico_ot_bridge_publisher PUBLIC pico_ot_bridge_transport)

add_executable(pico_ot_bridge_monaka_smoke_publisher
    "${PICO_OT_ROOT}/tools/monaka_smoke_publisher.cpp")
target_compile_features(pico_ot_bridge_monaka_smoke_publisher PRIVATE cxx_std_17)
target_include_directories(pico_ot_bridge_monaka_smoke_publisher PRIVATE "${PICO_OT_ROOT}/include")
target_link_libraries(pico_ot_bridge_monaka_smoke_publisher PRIVATE pico_ot_bridge_publisher)

if(MSVC)
    foreach(target IN ITEMS
            pico_ot_bridge_core
            pico_ot_bridge_transport
            pico_ot_bridge_capi
            pico_ot_bridge_publisher
            pico_ot_bridge_monaka_smoke_publisher)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endforeach()
    target_link_libraries(pico_ot_bridge_transport PRIVATE ws2_32)
else()
    foreach(target IN ITEMS
            pico_ot_bridge_core
            pico_ot_bridge_transport
            pico_ot_bridge_capi
            pico_ot_bridge_publisher
            pico_ot_bridge_monaka_smoke_publisher)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

enable_testing()
add_executable(pico_ot_bridge_receiver_c_api_tests
    "${PICO_OT_ROOT}/tests/receiver_c_api_tests.cpp")
target_compile_features(pico_ot_bridge_receiver_c_api_tests PRIVATE cxx_std_17)
target_include_directories(pico_ot_bridge_receiver_c_api_tests PRIVATE "${PICO_OT_ROOT}/include")
target_link_libraries(pico_ot_bridge_receiver_c_api_tests PRIVATE
    pico_ot_bridge_capi
    pico_ot_bridge_publisher)

if(MSVC)
    target_compile_options(pico_ot_bridge_receiver_c_api_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(pico_ot_bridge_receiver_c_api_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME pico_ot_bridge_receiver_c_api_tests
    COMMAND pico_ot_bridge_receiver_c_api_tests)
