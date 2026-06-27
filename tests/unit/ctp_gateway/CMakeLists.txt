# CTP Gateway 单元测试
# 从 src/modules/ctp_gateway_cpp/CMakeLists.txt 迁移
# 支持独立构建（作为顶层 CMake 项目）或作为子目录构建

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(ctp_gateway_unit_tests CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
endif()

if(NOT DEFINED TYCHE_ROOT)
    get_filename_component(TYCHE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    set(TYCHE_ROOT "${TYCHE_ROOT}" CACHE PATH "TycheEngine root directory")
endif()

if(NOT DEFINED THIRD_PARTY_DIR)
    set(THIRD_PARTY_DIR "${TYCHE_ROOT}/third_party" CACHE PATH "Third-party directory")
endif()

set(CTP_GATEWAY_DIR "${TYCHE_ROOT}/src/modules/ctp_gateway_cpp")

# CTP SDK 头文件目录（如果父作用域未定义，使用默认值）
if(NOT DEFINED CTP_SDK_DIR)
    set(CTP_SDK_DIR "${CTP_GATEWAY_DIR}/include/ctp")
endif()

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 测试源文件
file(GLOB CTP_GATEWAY_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(CTP_GATEWAY_TEST_SOURCES)
    # Gateway 源文件（不含 main.cpp）
    set(GATEWAY_SOURCES
        ${CTP_GATEWAY_DIR}/src/ctp_gateway.cpp
        ${CTP_GATEWAY_DIR}/src/ctp_loader.cpp
        ${CTP_GATEWAY_DIR}/src/config.cpp
        ${CTP_GATEWAY_DIR}/src/md_spi.cpp
        ${CTP_GATEWAY_DIR}/src/td_spi.cpp
        "${TYCHE_ROOT}/src/tyche/cpp/engine/shared_memory_queue.cpp"  # SharedMemoryQueue (shm_writer.h)
    )

    add_executable(ctp_gateway_tests
        ${CTP_GATEWAY_TEST_SOURCES}
        ${GATEWAY_SOURCES}
    )

    target_include_directories(ctp_gateway_tests PRIVATE
        "${CTP_GATEWAY_DIR}/src"
        "${TYCHE_ROOT}/src"
        "${TYCHE_ROOT}/third_party"
        "${CTP_SDK_DIR}"
        "${THIRD_PARTY_DIR}/msgpack-c/include"  # msgpack.hpp (shm_writer.h)
    )

    target_link_libraries(ctp_gateway_tests PRIVATE
        GTest::gtest_main
        tyche_module
    )

    if(WIN32)
        target_link_libraries(ctp_gateway_tests PRIVATE ws2_32 iphlpapi)
    else()
        target_link_libraries(ctp_gateway_tests PRIVATE dl pthread)
    endif()

    include(GoogleTest)
    gtest_discover_tests(ctp_gateway_tests)
endif()
