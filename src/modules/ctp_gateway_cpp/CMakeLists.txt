cmake_minimum_required(VERSION 3.16)
project(ctp_gateway_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: UTF-8 编译选项，避免 C4819 警告
if(MSVC)
    add_compile_options(/utf-8)
endif()

# msgpack-c: 不依赖 Boost
add_definitions(-DMSGPACK_NO_BOOST)

# ── 项目根目录（从 src/modules/ctp_gateway_cpp 回到根）──────────
get_filename_component(TYCHE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# ── 第三方库 ──────────────────────────────────────────────────
set(THIRD_PARTY_DIR "${TYCHE_ROOT}/third_party")

# ── 构建 tyche_module 静态库（防止父项目已添加）────────────────
if(NOT TARGET tyche_module)
    # 引用 tyche/cpp 的 CMakeLists，复用其 libzmq 查找逻辑
    add_subdirectory("${TYCHE_ROOT}/src/tyche/cpp"
                     "${CMAKE_BINARY_DIR}/tyche_core")
endif()

# ── nlohmann/json（FetchContent，GIT 方式）────────────────────
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── CTP SDK 头文件目录 ──────────────────────────────────────
# 默认指向本模块的 include/ctp 占位目录；
# 用户可通过 -DCTP_SDK_DIR=<path> 覆盖指向实际 SDK 头文件目录。
if(NOT DEFINED CTP_SDK_DIR)
    set(CTP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/ctp")
endif()

# ── 可执行目标 ─────────────────────────────────────────────
add_executable(ctp_gateway_cpp
    src/main.cpp
    src/config.cpp
    src/ctp_loader.cpp
    src/md_spi.cpp
    src/td_spi.cpp
    src/ctp_gateway.cpp
)

target_include_directories(ctp_gateway_cpp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"   # 本模块内部头文件
    "${TYCHE_ROOT}/src"                  # #include "tyche/cpp/types.h"
    "${CTP_SDK_DIR}"                     # CTP SDK 头文件
)

target_link_libraries(ctp_gateway_cpp PRIVATE
    tyche_module
    nlohmann_json::nlohmann_json
)

# Windows / Linux 平台附加链接
if(WIN32)
    target_link_libraries(ctp_gateway_cpp PRIVATE ws2_32 iphlpapi)
else()
    target_link_libraries(ctp_gateway_cpp PRIVATE dl pthread)
endif()

# ── 安装规则 ──────────────────────────────────────────────
install(TARGETS ctp_gateway_cpp DESTINATION bin)
