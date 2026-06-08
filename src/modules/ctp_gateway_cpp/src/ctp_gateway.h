#pragma once
#include "config.h"
#include "md_spi.h"
#include "td_spi.h"
#include "tyche/cpp/module.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>

// CtpGateway — C++ TycheModule，封装 CTP 行情/交易 API。
//
// 启动序列：
//   1. TycheModule::start() — 向 TycheEngine 注册，建立 ZMQ 连接
//   2. resolve_instruments() — 通过 static_data Job 查询合约列表
//   3. init_ctp() — 加载 DLL，连接前置机，登录，订阅行情
//
// 混合路由策略：
//   - 期货行情: send_event("quote", ...) 广播给所有 greeks_engine 实例
//   - 期权行情: request_event("compute_greeks", ...) 轮询分发到单个实例
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

    // 清理 CTP API 对象（noexcept：从析构函数调用）
    void cleanup_ctp() noexcept;

    // MdSpi 行情回调：混合路由分发
    void on_quote_received(const tyche::Payload& payload);

    // 期权 job 分发后台线程
    void option_dispatch_loop();

    // 从 request_event 响应中安全提取 InstrumentID 列表
    static std::vector<std::string> extract_instrument_ids(
        const tyche::Payload& response);

    GatewayConfig              cfg_;
    std::vector<std::string>   instruments_;        // 全部合约（用于 CTP 订阅）
    std::vector<std::string>   option_instruments_; // 排序后的期权合约列表（启动后只读）

    // 期权行情分发队列（CTP 回调线程→分发线程）
    std::queue<tyche::Payload>      option_queue_;
    std::mutex                      option_queue_mtx_;
    std::condition_variable         option_queue_cv_;
    std::thread                     option_dispatch_thread_;

    // CTP API 对象（运行时通过 ctp_loader 创建）
    CThostFtdcMdApi*           md_api_  = nullptr;
    CThostFtdcTraderApi*       td_api_  = nullptr;
    std::unique_ptr<MdSpiImpl> md_spi_;
    std::unique_ptr<TdSpiImpl> td_spi_;
    std::atomic<bool>          ctp_running_{false};
    std::atomic<bool>          cleanup_done_{false};
    std::atomic<bool>          started_{false};      // 防止重复启动
    std::atomic<bool>          stopping_{false};     // 防止重复停止
    std::atomic<int>           option_err_count_{0};
    std::atomic<int>           option_dropped_count_{0};

    // RegisterFront 需要可变 char*（CTP 内部会复制，但签名要求非 const）
    std::string                md_front_mut_;
    std::string                td_front_mut_;
};
