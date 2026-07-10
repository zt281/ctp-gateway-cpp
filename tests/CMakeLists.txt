# CTP Gateway 单元测试

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
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ctp_gateway.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ctp_loader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/md_spi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/td_spi.cpp
    )

    add_executable(ctp_gateway_tests
        ${CTP_GATEWAY_TEST_SOURCES}
        ${GATEWAY_SOURCES}
    )

    target_compile_definitions(ctp_gateway_tests PRIVATE MSGPACK_NO_BOOST)

    target_include_directories(ctp_gateway_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
        "${CTP_SDK_DIR}"
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
    gtest_discover_tests(ctp_gateway_tests DISCOVERY_MODE POST_BUILD)
endif()
