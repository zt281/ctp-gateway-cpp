#pragma once
#include "config.h"
#include "ThostFtdcTraderApi.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

// TdSpiImpl — CTP 交易 SPI 实现。
//
// 主要职责：完成认证（可选）→ 登录流程，并通过 wait_for_login()
// 提供阻塞等待接口，供上层在 MdApi 启动前确认柜台连接正常。
class TdSpiImpl : public CThostFtdcTraderSpi {
public:
    TdSpiImpl(const GatewayConfig& cfg, CThostFtdcTraderApi* td_api);

    // 阻塞等待登录完成，超过 timeout_secs 秒后返回 false。
    bool wait_for_login(int timeout_secs);

    bool is_logged_in() const noexcept { return logged_in_.load(); }

    // ── CThostFtdcTraderSpi 回调 ─────────────────────────────────
    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;

    void OnRspAuthenticate(
        CThostFtdcRspAuthenticateField* pRspAuthenticateField,
        CThostFtdcRspInfoField*          pRspInfo,
        int nRequestID, bool bIsLast) override;

    void OnRspUserLogin(
        CThostFtdcRspUserLoginField* pRspUserLogin,
        CThostFtdcRspInfoField*       pRspInfo,
        int nRequestID, bool bIsLast) override;

    void OnRspUserLogout(
        CThostFtdcUserLogoutField* pUserLogout,
        CThostFtdcRspInfoField*     pRspInfo,
        int nRequestID, bool bIsLast) override;

private:
    // 根据 appid 是否为空，选择认证或直接登录
    void do_authenticate_or_login();
    void do_login();

    const GatewayConfig&       cfg_;
    CThostFtdcTraderApi*    td_api_;
    std::atomic<bool>        logged_in_{false};
    std::mutex               mtx_;
    std::condition_variable  cv_;
    std::atomic<int>         req_id_{0};
};
