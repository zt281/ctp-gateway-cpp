#pragma once
#include "config.h"
#include "md_spi.h"
#include "td_spi.h"
#include "tyche/cpp/module.h"
#include "tyche/cpp/engine/ring_buffer.h"
#include "quote_tick.h"
#include "quote_validator.h"
#include "dll_handle.h"
#include "ctp_api_raii.h"
#include "gateway_log.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>

// CtpGateway — C++ TycheModule，封装 CTP 行情/交易 API。
//
// 启动序列：
//   1. TycheModule::start() — 向 TycheEngine 注册，建立 ZMQ 连接
//   2. resolve_instruments() — 通过 static_data Job 查询合约列表
//   3. init_ctp() — 加载 DLL，连接前置机，登录，订阅行情
//
// 混合路由策略：
//   - 期货行情: send_event("quote", ...) 广播给所有 greeks_engine 实例
//   - 期权行情: send_event("send_compute_greeks", ...) 广播到 greeks_engine 实例
class CtpGateway : public tyche::TycheModule {
public:
    explicit CtpGateway(const GatewayConfig& cfg);
    ~CtpGateway() override;

    void start() override;
    void stop()  override;

    // Return a snapshot of gateway metrics as a Payload.
    // Used by the "gateway_status" job handler and for testing.
    tyche::Payload get_status_payload() const;

    // Convert QuoteTick to Payload for greeks engine consumption
    static tyche::Payload tick_to_payload(const QuoteTick& tick);

    // Get age of last tick in milliseconds
    int64_t last_tick_age_ms() const {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto last = last_tick_ns_.load(std::memory_order_acquire);
        return static_cast<int64_t>((now - last) / 1'000'000);
    }

    // Check if last tick is stale (no update for >5 seconds)
    bool is_tick_stale() const {
        return last_tick_age_ms() > 5000;
    }

    // Get reconnection count
    int reconnect_count() const {
        return reconnect_count_.load(std::memory_order_relaxed);
    }

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
    std::vector<std::string>   instruments_;           // 全部合约（用于 CTP 订阅）
    std::unordered_set<std::string> option_instrument_set_; // 期权合约集合（O(1) 查找）

    // 期权行情 RingBuffer（CTP 回调线程→分发线程，MPSC 安全）
    tyche::RingBuffer<QuoteTick> option_ring_buffer_;
    std::thread                 option_dispatch_thread_;

    // CTP API 对象 + DLL 句柄（RAII 管理）
    MdApiPtr               md_api_;
    TdApiPtr               td_api_;
    DllHandle              md_dll_;
    DllHandle              td_dll_;
    std::unique_ptr<MdSpiImpl> md_spi_;
    std::unique_ptr<TdSpiImpl> td_spi_;
    std::atomic<bool>      ctp_running_{false};
    std::atomic<bool>      cleanup_done_{false};
    std::atomic<bool>      started_{false};      // 防止重复启动
    std::atomic<bool>      stopping_{false};     // 防止重复停止
    std::atomic<uint64_t>  ticks_received_{0};
    std::atomic<uint64_t>  ticks_sent_{0};
    std::atomic<int>       option_err_count_{0};
    std::atomic<int>       option_dropped_count_{0};
    std::atomic<bool>      tick_stale_{false};
    std::atomic<uint64_t>  last_tick_ns_{0};     // 最后行情时间戳（纳秒）
    std::atomic<int>       reconnect_count_{0}; // 重连次数
    std::chrono::steady_clock::time_point start_time_;

    // QuoteValidator 用于数据质量检查
    QuoteValidator         quote_validator_;

    // RegisterFront 需要可变 char*（CTP 内部会复制，但签名要求非 const）
    std::string            md_front_mut_;
    std::string            td_front_mut_;
};