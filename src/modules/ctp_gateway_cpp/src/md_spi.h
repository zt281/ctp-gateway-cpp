#pragma once
#include "config.h"
#include "ThostFtdcMdApi.h"
#include "tyche/cpp/types.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// MdSpiImpl — CTP 行情 SPI 实现。
//
// 处理连接→登录→订阅→行情推送的完整流程。
// 登录成功后自动订阅通过 set_instruments() 设置的合约列表。
// 每条 DepthMarketData 回调都会将数据转换为标准 Payload 并触发 on_quote 回调。
class MdSpiImpl : public CThostFtdcMdSpi {
public:
    // 行情回调函数类型
    using QuoteCallback = std::function<void(const tyche::Payload&)>;

    MdSpiImpl(const GatewayConfig& cfg,
              CThostFtdcMdApi*   md_api,
              QuoteCallback       on_quote);

    // 设置待订阅的合约 ID 列表。
    // 可在登录前调用（登录成功后自动订阅），也可在登录后调用（立即订阅）。
    void set_instruments(std::vector<std::string> instruments);

    // ── CThostFtdcMdSpi 回调 ─────────────────────────────────────
    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void OnHeartBeatWarning(int nTimeLapse) override;

    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                        CThostFtdcRspInfoField*       pRspInfo,
                        int nRequestID, bool bIsLast) override;

    void OnRspUserLogout(CThostFtdcUserLogoutField* pUserLogout,
                         CThostFtdcRspInfoField*     pRspInfo,
                         int nRequestID, bool bIsLast) override;

    void OnRspSubMarketData(
        CThostFtdcSpecificInstrumentField* pSpecificInstrument,
        CThostFtdcRspInfoField*             pRspInfo,
        int nRequestID, bool bIsLast) override;

    void OnRtnDepthMarketData(
        CThostFtdcDepthMarketDataField* pDepthMarketData) override;

private:
    void do_login();
    void do_subscribe();

    // 将 CTP DepthMarketData 转换为标准 quote Payload
    static tyche::Payload depth_to_payload(
        const CThostFtdcDepthMarketDataField* d);

    const GatewayConfig&     cfg_;
    CThostFtdcMdApi*         md_api_;
    QuoteCallback             on_quote_cb_;
    std::vector<std::string>  instruments_;
    std::mutex                inst_mtx_;
    std::atomic<bool>         logged_in_{false};
    std::atomic<int>          req_id_{0};
};
