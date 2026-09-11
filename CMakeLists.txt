cmake_minimum_required(VERSION 3.20)
project(PicoMotionTrackerBackend VERSION 0.1.0 LANGUAGES C CXX)

option(PICO_OT_BUILD_TESTS "Build host-side backend/bridge tests" ON)
option(PICO_OT_BUILD_SHARED "Build shared backend library" ON)
option(PICO_OT_BUILD_STEAMVR_DRIVER "Build the Windows SteamVR server driver" OFF)

set(PICO_OT_SOURCES
    src/backend.cpp
    src/c_api.cpp
    src/internal/decoder.cpp
    src/internal/clock_mapper.cpp)

if(ANDROID AND CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    enable_language(ASM)
    list(APPEND PICO_OT_SOURCES
        src/runtime/current_firmware_adapter.cpp
        src/runtime/aarch64_call_shim.S)
else()
    list(APPEND PICO_OT_SOURCES src/runtime/unsupported_runtime_adapter.cpp)
endif()

if(PICO_OT_BUILD_SHARED)
    add_library(pico_ot_backend SHARED ${PICO_OT_SOURCES})
else()
    add_library(pico_ot_backend STATIC ${PICO_OT_SOURCES})
endif()

add_library(PicoOT::Backend ALIAS pico_ot_backend)
target_compile_features(pico_ot_backend PUBLIC cxx_std_17)
target_include_directories(pico_ot_backend
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(pico_ot_backend PRIVATE /W4 /permissive-)
else()
    target_compile_options(pico_ot_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ANDROID)
    target_link_libraries(pico_ot_backend PRIVATE dl log)
elseif(UNIX)
    target_link_libraries(pico_ot_backend PRIVATE dl)
endif()

# Platform-neutral HMD<->PC bridge core. It deliberately has no dependency on
# the private PICO runtime or OpenVR so Android and Windows share exactly the
# same packet and clock-domain semantics.
add_library(pico_ot_bridge_core STATIC
    src/bridge/protocol.cpp
    src/bridge/clock_sync.cpp
    src/bridge/monotonic_clock.cpp)
add_library(PicoOT::BridgeCore ALIAS pico_ot_bridge_core)
target_compile_features(pico_ot_bridge_core PUBLIC cxx_std_17)
target_include_directories(pico_ot_bridge_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(pico_ot_bridge_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(pico_ot_bridge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Network transport and PC receiver are independent of the PICO private ABI.
add_library(pico_ot_bridge_transport STATIC
    src/bridge/udp_socket.cpp
    src/bridge/receiver.cpp)
add_library(PicoOT::BridgeTransport ALIAS pico_ot_bridge_transport)
target_compile_features(pico_ot_bridge_transport PUBLIC cxx_std_17)
target_include_directories(pico_ot_bridge_transport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pico_ot_bridge_transport PUBLIC PicoOT::BridgeCore)

if(MSVC)
    target_compile_options(pico_ot_bridge_transport PRIVATE /W4 /permissive-)
    target_link_libraries(pico_ot_bridge_transport PRIVATE ws2_32)
else()
    target_compile_options(pico_ot_bridge_transport PRIVATE -Wall -Wextra -Wpedantic)
endif()

# HMD publisher is the only bridge target that consumes normalized Backend
# types. This keeps the Windows SteamVR driver independent of pico_ot_backend.
add_library(pico_ot_bridge_publisher STATIC
    src/bridge/publisher.cpp)
add_library(PicoOT::BridgePublisher ALIAS pico_ot_bridge_publisher)
target_compile_features(pico_ot_bridge_publisher PUBLIC cxx_std_17)
target_include_directories(pico_ot_bridge_publisher
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pico_ot_bridge_publisher
    PUBLIC PicoOT::BridgeTransport PicoOT::Backend)

if(MSVC)
    target_compile_options(pico_ot_bridge_publisher PRIVATE /W4 /permissive-)
else()
    target_compile_options(pico_ot_bridge_publisher PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PICO_OT_BUILD_STEAMVR_DRIVER)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "PICO_OT_BUILD_STEAMVR_DRIVER currently requires 64-bit Windows")
    endif()

    include(FetchContent)
    FetchContent_Declare(
        openvr_sdk
        GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
        GIT_TAG v2.15.6
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(openvr_sdk)
    if(NOT openvr_sdk_POPULATED)
        FetchContent_Populate(openvr_sdk)
    endif()

    add_library(openvr_api_prebuilt STATIC IMPORTED GLOBAL)
    set_target_properties(openvr_api_prebuilt PROPERTIES
        IMPORTED_LOCATION "${openvr_sdk_SOURCE_DIR}/lib/win64/openvr_api.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${openvr_sdk_SOURCE_DIR}/headers")

    add_library(pico_ot_steamvr_driver SHARED
        outputs/steamvr/driver/hmd_driver_factory.cpp
        outputs/steamvr/driver/device_provider.cpp
        outputs/steamvr/driver/tracker_device.cpp)
    target_compile_features(pico_ot_steamvr_driver PRIVATE cxx_std_17)
    target_include_directories(pico_ot_steamvr_driver PRIVATE
        outputs/steamvr/driver)
    target_link_libraries(pico_ot_steamvr_driver PRIVATE
        PicoOT::BridgeTransport
        openvr_api_prebuilt)
    target_compile_options(pico_ot_steamvr_driver PRIVATE /W4 /permissive-)
    set_target_properties(pico_ot_steamvr_driver PROPERTIES
        OUTPUT_NAME "driver_pico_ot_bridge"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/steamvr/pico_ot_bridge/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/steamvr/pico_ot_bridge/bin/win64")

    add_custom_command(TARGET pico_ot_steamvr_driver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/steamvr/pico_ot_bridge"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/outputs/steamvr/package/driver.vrdrivermanifest"
            "${CMAKE_BINARY_DIR}/steamvr/pico_ot_bridge/driver.vrdrivermanifest")
endif()

if(PICO_OT_BUILD_TESTS AND NOT ANDROID)
    enable_testing()

    add_executable(pico_ot_backend_tests
        tests/decoder_tests.cpp
        src/internal/decoder.cpp
        src/internal/clock_mapper.cpp)
    target_compile_features(pico_ot_backend_tests PRIVATE cxx_std_17)
    target_include_directories(pico_ot_backend_tests PRIVATE include src)
    if(MSVC)
        target_compile_options(pico_ot_backend_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(pico_ot_backend_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME pico_ot_backend_tests COMMAND pico_ot_backend_tests)

    add_executable(pico_ot_bridge_protocol_tests tests/protocol_tests.cpp)
    target_compile_features(pico_ot_bridge_protocol_tests PRIVATE cxx_std_17)
    target_link_libraries(pico_ot_bridge_protocol_tests PRIVATE PicoOT::BridgeCore)
    add_test(NAME pico_ot_bridge_protocol_tests COMMAND pico_ot_bridge_protocol_tests)

    add_executable(pico_ot_bridge_clock_sync_tests tests/clock_sync_tests.cpp)
    target_compile_features(pico_ot_bridge_clock_sync_tests PRIVATE cxx_std_17)
    target_link_libraries(pico_ot_bridge_clock_sync_tests PRIVATE PicoOT::BridgeCore)
    add_test(NAME pico_ot_bridge_clock_sync_tests COMMAND pico_ot_bridge_clock_sync_tests)

    add_executable(pico_ot_bridge_udp_socket_tests tests/udp_socket_tests.cpp)
    target_compile_features(pico_ot_bridge_udp_socket_tests PRIVATE cxx_std_17)
    target_link_libraries(pico_ot_bridge_udp_socket_tests PRIVATE PicoOT::BridgeTransport)
    add_test(NAME pico_ot_bridge_udp_socket_tests COMMAND pico_ot_bridge_udp_socket_tests)

    add_executable(pico_ot_bridge_publisher_tests tests/publisher_tests.cpp)
    target_compile_features(pico_ot_bridge_publisher_tests PRIVATE cxx_std_17)
    target_link_libraries(pico_ot_bridge_publisher_tests PRIVATE PicoOT::BridgePublisher)
    add_test(NAME pico_ot_bridge_publisher_tests COMMAND pico_ot_bridge_publisher_tests)

    add_executable(pico_ot_bridge_receiver_tests tests/receiver_tests.cpp)
    target_compile_features(pico_ot_bridge_receiver_tests PRIVATE cxx_std_17)
    target_link_libraries(pico_ot_bridge_receiver_tests PRIVATE
        PicoOT::BridgePublisher PicoOT::BridgeTransport)
    add_test(NAME pico_ot_bridge_receiver_tests COMMAND pico_ot_bridge_receiver_tests)

    foreach(test_target IN ITEMS
            pico_ot_bridge_protocol_tests
            pico_ot_bridge_clock_sync_tests
            pico_ot_bridge_udp_socket_tests
            pico_ot_bridge_publisher_tests
            pico_ot_bridge_receiver_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()
