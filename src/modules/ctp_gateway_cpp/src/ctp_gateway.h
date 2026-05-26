#pragma once
#include "config.h"
#include "md_spi.h"
#include "td_spi.h"
#include "tyche/cpp/module.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// CtpGateway — C++ TycheModule，封装 CTP 行情/交易 API。
//
// 启动序列：
//   1. TycheModule::start() — 向 TycheEngine 注册，建立 ZMQ 连接
//   2. resolve_instruments() — 通过 static_data Job 查询合约列表
//   3. init_ctp() — 加载 DLL，连接前置机，登录，订阅行情
//
// 每条行情通过 send_event("quote", payload) 广播到 TycheEngine。
class CtpGateway : public tyche::TycheModule {
public:
    explicit CtpGateway(const GatewayConfig& cfg);
    ~CtpGateway() override;

    void start() override;
    void stop()  override;

protected:
    void _start_workers() override;

private:
    // 步骤 2：向 static_data 查询期货 + 期权合约列表
    void resolve_instruments();

    // 步骤 3：创建 CTP API，连接前置机，登录，订阅行情
    void init_ctp();

    // 清理 CTP API 对象
    void cleanup_ctp();

    // MdSpi 行情回调：转发到 TycheEngine
    void on_quote_received(const tyche::Payload& payload);

    // 从 request_event 响应中安全提取 InstrumentID 列表
    static std::vector<std::string> extract_instrument_ids(
        const tyche::Payload& response);

    GatewayConfig              cfg_;
    std::vector<std::string>   instruments_;   // static_data 查询结果汇总

    // CTP API 对象（运行时通过 ctp_loader 创建）
    CThostFtdcMdApi*           md_api_  = nullptr;
    CThostFtdcTraderApi*       td_api_  = nullptr;
    std::unique_ptr<MdSpiImpl> md_spi_;
    std::unique_ptr<TdSpiImpl> td_spi_;
    std::atomic<bool>          ctp_running_{false};
};
