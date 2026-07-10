cmake_minimum_required(VERSION 3.16)
project(ctp_gateway_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# msgpack-c: 不依赖 Boost
add_definitions(-DMSGPACK_NO_BOOST)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ── 构建产物输出目录 ─────────────────────────────────────────────
if(WIN32)
    set(TYCHE_PLATFORM "win")
else()
    set(TYCHE_PLATFORM "linux")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# ── TycheCore-CPP 依赖（submodule 或 find_package）──────────────
option(CTP_USE_SUBMODULE "Use TycheCore-CPP as git submodule" ON)

if(CTP_USE_SUBMODULE)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/TycheCore-CPP/CMakeLists.txt")
        message(FATAL_ERROR
            "TycheCore-CPP submodule not found.\n"
            "Please run:\n"
            "  git submodule add https://github.com/zt281/TycheCore-CPP.git third_party/TycheCore-CPP\n"
            "  git submodule update --init --recursive")
    endif()
    add_subdirectory(third_party/TycheCore-CPP)
else()
    find_package(TycheCore REQUIRED)
endif()

# ── tyche_module 目标检查 ─────────────────────────────────────────
if(NOT TARGET tyche_module)
    message(FATAL_ERROR
        "tyche_module target not found. "
        "Ensure TycheCore-CPP is properly configured.")
endif()

# ── CTP SDK 头文件目录检测 ────────────────────────────────────────
# 默认指向本项目的 include/ctp 占位目录；
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
        "  Please copy CTP headers to include/ctp/\n"
        "  Or specify via: -DCTP_SDK_DIR=<path_to_ctp_include>")
else()
    message(STATUS "CTP SDK headers found: ${CTP_SDK_DIR}")
endif()

# ── 可执行目标 ─────────────────────────────────────────────────
add_executable(ctp_gateway_cpp
    src/main.cpp
    src/config.cpp
    src/ctp_loader.cpp
    src/md_spi.cpp
    src/td_spi.cpp
    src/ctp_gateway.cpp
)

target_include_directories(ctp_gateway_cpp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"   # 内部头文件
    "${CTP_SDK_DIR}"                    # CTP SDK 头文件
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

# ── 安装规则 ──────────────────────────────────────────────────
install(TARGETS ctp_gateway_cpp DESTINATION bin)

# ── DLL/SO 共享库目标 ────────────────────────────────────────────
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
        "${CTP_SDK_DIR}"
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

    # DLL/SO 输出目录
    set(GATEWAY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/gateway")
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

    install(TARGETS ctp_gateway_cpp_shared DESTINATION gateway)

    message(STATUS "ctp_gateway_cpp: shared library target enabled (output: ${GATEWAY_OUTPUT_DIR})")
endif()

# ── 测试 ──────────────────────────────────────────────────────
option(CTP_GATEWAY_BUILD_TESTS "Build unit tests" ON)

if(CTP_GATEWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
