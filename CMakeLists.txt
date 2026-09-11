cmake_minimum_required(VERSION 3.20)
project(PicoMotionTrackerBackend VERSION 0.1.0 LANGUAGES C CXX)

option(PICO_OT_BUILD_TESTS "Build host-side decoder/clock tests" ON)
option(PICO_OT_BUILD_SHARED "Build shared backend library" ON)

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
endif()
