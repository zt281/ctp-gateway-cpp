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
    # 独立构建时按功能×平台分发到 build/{executables,libraries}/{win,linux}
    if(WIN32)
        set(TYCHE_PLATFORM "win")
    else()
        set(TYCHE_PLATFORM "linux")
    endif()
    set(TYCHE_RUNTIME_DIR "${TYCHE_ROOT}/build/executables/${TYCHE_PLATFORM}")
    set(TYCHE_LIB_DIR "${TYCHE_ROOT}/build/libraries/${TYCHE_PLATFORM}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${TYCHE_RUNTIME_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${TYCHE_LIB_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${TYCHE_LIB_DIR})
    foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${TYCHE_RUNTIME_DIR})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${TYCHE_LIB_DIR})
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${TYCHE_LIB_DIR})
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
    add_subdirectory("${TYCHE_ROOT}/src/tyche/cpp/module"
                     "${CMAKE_BINARY_DIR}/tyche_module")
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
    "${THIRD_PARTY_DIR}/msgpack-c/include"  # #include <msgpack.hpp> (shm_writer.h)
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

# ── DLL/SO 共享库目标 ──────────────────────────────────────────
option(CTP_GATEWAY_BUILD_SHARED "Build ctp_gateway_cpp as shared library (DLL/SO)" ON)

if(CTP_GATEWAY_BUILD_SHARED)
    # 共享源文件列表（排除 main.cpp，新增 dll_entry.cpp）
    set(CTP_GATEWAY_SHARED_SOURCES
        src/dll_entry.cpp
        src/config.cpp
        src/ctp_loader.cpp
        src/md_spi.cpp
        src/td_spi.cpp
        src/ctp_gateway.cpp
    )

    add_library(ctp_gateway_cpp_shared SHARED ${CTP_GATEWAY_SHARED_SOURCES})

    target_include_directories(ctp_gateway_cpp_shared PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TYCHE_ROOT}/src"
        "${TYCHE_ROOT}/third_party"
        "${CTP_SDK_DIR}"
        "${THIRD_PARTY_DIR}/msgpack-c/include"
    )

    target_compile_definitions(ctp_gateway_cpp_shared PRIVATE
        CTP_GATEWAY_BUILDING_DLL=1
        MSGPACK_NO_BOOST
    )

    target_link_libraries(ctp_gateway_cpp_shared PRIVATE tyche_module)

    set_target_properties(ctp_gateway_cpp_shared PROPERTIES
        OUTPUT_NAME "ctp_gateway_cpp"
        POSITION_INDEPENDENT_CODE ON
    )

    if(WIN32)
        target_link_libraries(ctp_gateway_cpp_shared PRIVATE ws2_32 iphlpapi)
        set_target_properties(ctp_gateway_cpp_shared PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
        )
    else()
        target_link_libraries(ctp_gateway_cpp_shared PRIVATE dl pthread)
        # Do not set CXX_VISIBILITY_PRESET hidden — extern "C" exports must remain visible
    endif()

    # 输出到 build/libraries/{win,linux}/ (共享库归类为库文件)
    if(NOT DEFINED GATEWAY_OUTPUT_DIR)
        if(WIN32)
            set(GATEWAY_OUTPUT_DIR "${TYCHE_ROOT}/build/libraries/win")
        else()
            set(GATEWAY_OUTPUT_DIR "${TYCHE_ROOT}/build/libraries/linux")
        endif()
    endif()
    file(MAKE_DIRECTORY "${GATEWAY_OUTPUT_DIR}")

    set_target_properties(ctp_gateway_cpp_shared PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GATEWAY_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${GATEWAY_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${GATEWAY_OUTPUT_DIR}"
    )
    foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
        set_target_properties(ctp_gateway_cpp_shared PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${GATEWAY_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${GATEWAY_OUTPUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${GATEWAY_OUTPUT_DIR}"
        )
    endforeach()

    set(CTP_GATEWAY_RUNTIME_MODULE_DIR "${TYCHE_ROOT}/runtime/engine/modules")
    add_custom_command(TARGET ctp_gateway_cpp_shared POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CTP_GATEWAY_RUNTIME_MODULE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ctp_gateway_cpp_shared>"
            "${CTP_GATEWAY_RUNTIME_MODULE_DIR}/$<TARGET_FILE_NAME:ctp_gateway_cpp_shared>"
        COMMENT "Copying ctp_gateway_cpp shared module to runtime/engine/modules"
    )

    install(TARGETS ctp_gateway_cpp_shared DESTINATION gateway)

    message(STATUS "ctp_gateway_cpp: shared library target enabled (output: ${GATEWAY_OUTPUT_DIR})")
endif()
