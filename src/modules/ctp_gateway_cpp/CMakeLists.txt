cmake_minimum_required(VERSION 3.16)
project(ctp_gateway_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# msgpack-c: 不依赖 Boost
add_definitions(-DMSGPACK_NO_BOOST)

# ── 项目根目录（从 src/modules/ctp_gateway_cpp 回到根）──────────
if(NOT DEFINED TYCHE_ROOT)
    get_filename_component(TYCHE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    set(TYCHE_ROOT "${TYCHE_ROOT}" CACHE PATH "TycheEngine root directory")
endif()

# ── 独立构建模式检测 ──
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(STATUS "src/modules/ctp_gateway_cpp: standalone build mode")
    cmake_minimum_required(VERSION 3.16)
    project(ctp_gateway_cpp CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
    # 独立构建时设置输出目录
    if(WIN32)
        set(TYCHE_RUNTIME_DIR "${TYCHE_ROOT}/runtime/win")
    else()
        set(TYCHE_RUNTIME_DIR "${TYCHE_ROOT}/runtime/linux")
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${TYCHE_RUNTIME_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${TYCHE_RUNTIME_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${TYCHE_RUNTIME_DIR})
    foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${TYCHE_RUNTIME_DIR})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${TYCHE_RUNTIME_DIR})
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${TYCHE_RUNTIME_DIR})
    endforeach()
else()
    message(STATUS "src/modules/ctp_gateway_cpp: subdirectory build mode (parent: ${CMAKE_SOURCE_DIR})")
endif()

# ── 第三方库 ──────────────────────────────────────────────────
if(NOT DEFINED THIRD_PARTY_DIR)
    set(THIRD_PARTY_DIR "${TYCHE_ROOT}/third_party" CACHE PATH "Third-party directory")
endif()

# ── 构建 tyche_module 静态库（防止父项目已添加）────────────────
if(NOT TARGET tyche_module)
    # 引用 tyche/cpp 的 CMakeLists，复用其 libzmq 查找逻辑
    add_subdirectory("${TYCHE_ROOT}/src/tyche/cpp"
                     "${CMAKE_BINARY_DIR}/tyche_core")
endif()

# ── nlohmann/json（header-only，本地单文件）───────────────────
# 避免网络依赖，使用 third_party 目录下的 json.hpp 单文件
set(NLOHMANN_JSON_HEADER "${THIRD_PARTY_DIR}/nlohmann_json.hpp")
if(NOT EXISTS ${NLOHMANN_JSON_HEADER})
    message(FATAL_ERROR
        "nlohmann/json header not found: ${NLOHMANN_JSON_HEADER}\n"
        "Please download json.hpp from:\n"
        "  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp\n"
        "  and place it at: ${THIRD_PARTY_DIR}/nlohmann_json.hpp")
endif()

# ── CTP SDK 头文件目录检测 ────────────────────────────────────
# 默认指向本模块的 include/ctp 占位目录；
# 用户可通过 -DCTP_SDK_DIR=<path> 覆盖指向实际 SDK 头文件目录。
if(NOT DEFINED CTP_SDK_DIR)
    set(CTP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/ctp")
endif()

# 检查 CTP SDK 头文件是否存在（至少检查关键头文件）
set(CTP_HEADER_MD "${CTP_SDK_DIR}/ThostFtdcMdApi.h")
set(CTP_HEADER_TD "${CTP_SDK_DIR}/ThostFtdcTraderApi.h")

if(NOT EXISTS "${CTP_HEADER_MD}" OR NOT EXISTS "${CTP_HEADER_TD}")
    message(WARNING
        "CTP SDK headers not found in: ${CTP_SDK_DIR}\n"
        "  Missing: ThostFtdcMdApi.h / ThostFtdcTraderApi.h\n"
        "  Please copy CTP headers to src/modules/ctp_gateway_cpp/include/ctp/\n"
        "  Or specify via: -DCTP_SDK_DIR=<path_to_ctp_include>")
else()
    message(STATUS "CTP SDK headers found: ${CTP_SDK_DIR}")
endif()

# 将 CTP_SDK_DIR 提升到父作用域，供测试子目录使用
set(CTP_SDK_DIR "${CTP_SDK_DIR}" PARENT_SCOPE)

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
    "${TYCHE_ROOT}/third_party"          # #include "nlohmann_json.hpp"
    "${CTP_SDK_DIR}"                     # CTP SDK 头文件
)

target_link_libraries(ctp_gateway_cpp PRIVATE
    tyche_module
)

# Windows / Linux 平台附加链接
if(WIN32)
    target_link_libraries(ctp_gateway_cpp PRIVATE ws2_32 iphlpapi)
else()
    target_link_libraries(ctp_gateway_cpp PRIVATE dl pthread)
endif()

# ── 安装规则 ──────────────────────────────────────────────
install(TARGETS ctp_gateway_cpp DESTINATION bin)
