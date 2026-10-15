cmake_minimum_required(VERSION 3.20)
project(PicoOTBackendTestApp LANGUAGES C CXX)

set(PICO_OT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PICO_OT_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(PICO_OT_BUILD_STEAMVR_DRIVER OFF CACHE BOOL "" FORCE)

# app/src/main/cpp -> repository root
get_filename_component(PICO_OT_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_subdirectory("${PICO_OT_REPO_ROOT}" "${CMAKE_BINARY_DIR}/pico_ot_backend")

add_library(pico_ot_backend_test SHARED backend_test_jni.cpp)
target_compile_features(pico_ot_backend_test PRIVATE cxx_std_17)
target_include_directories(pico_ot_backend_test PRIVATE "${PICO_OT_REPO_ROOT}/src")
target_link_libraries(pico_ot_backend_test PRIVATE
    PicoOT::Backend
    PicoOT::BridgePublisher
    log)
