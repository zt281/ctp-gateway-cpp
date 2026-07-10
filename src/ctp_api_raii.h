#pragma once

#include "ThostFtdcMdApi.h"
#include "ThostFtdcTraderApi.h"

#include <memory>
#include <thread>

/**
 * @brief RAII deleter for CThostFtdcMdApi
 *
 * Ensures proper cleanup: unregisters SPI, releases the API instance,
 * and joins any internal thread without blocking the shutdown path
 * indefinitely when the network is unreachable.
 */
struct MdApiDeleter {
    void operator()(CThostFtdcMdApi* api) const noexcept {
        if (!api) return;

        api->RegisterSpi(nullptr);
        api->Release();

        // Join may block forever if the connection is down, so we run
        // it in a detached thread to avoid hanging the shutdown path.
        std::thread([api]() {
            api->Join();
        }).detach();
    }
};

/**
 * @brief RAII deleter for CThostFtdcTraderApi
 *
 * Ensures proper cleanup: unregisters SPI, releases the API instance,
 * and joins any internal thread without blocking the shutdown path
 * indefinitely when the network is unreachable.
 */
struct TdApiDeleter {
    void operator()(CThostFtdcTraderApi* api) const noexcept {
        if (!api) return;

        api->RegisterSpi(nullptr);
        api->Release();

        // Join may block forever if the connection is down, so we run
        // it in a detached thread to avoid hanging the shutdown path.
        std::thread([api]() {
            api->Join();
        }).detach();
    }
};

/**
 * @brief Smart pointer type for CThostFtdcMdApi with automatic cleanup
 */
using MdApiPtr = std::unique_ptr<CThostFtdcMdApi, MdApiDeleter>;

/**
 * @brief Smart pointer type for CThostFtdcTraderApi with automatic cleanup
 */
using TdApiPtr = std::unique_ptr<CThostFtdcTraderApi, TdApiDeleter>;