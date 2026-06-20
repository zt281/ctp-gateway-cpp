#pragma once

#include "ThostFtdcMdApi.h"
#include "ThostFtdcTraderApi.h"

#include <memory>

/**
 * @brief RAII deleter for CThostFtdcMdApi
 *
 * Ensures proper cleanup: unregisters SPI, joins any internal thread,
 * and releases the API instance.
 */
struct MdApiDeleter {
    void operator()(CThostFtdcMdApi* api) const noexcept {
        if (api) {
            api->RegisterSpi(nullptr);
            api->Join();
            api->Release();
        }
    }
};

/**
 * @brief RAII deleter for CThostFtdcTraderApi
 *
 * Ensures proper cleanup: unregisters SPI, joins any internal thread,
 * and releases the API instance.
 */
struct TdApiDeleter {
    void operator()(CThostFtdcTraderApi* api) const noexcept {
        if (api) {
            api->RegisterSpi(nullptr);
            api->Join();
            api->Release();
        }
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