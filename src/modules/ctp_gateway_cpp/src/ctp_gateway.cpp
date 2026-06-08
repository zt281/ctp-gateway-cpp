#include "ctp_gateway.h"
#include "ctp_loader.h"
#include <any>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

// ── 构造函数 ───────────────────────────────────────────────────
CtpGateway::CtpGateway(const GatewayConfig& cfg)
    : tyche::TycheModule(
          tyche::Endpoint{cfg.engine_host, cfg.engine_port},
          cfg.family_name,
          tyche::Endpoint{cfg.engine_host, cfg.engine_port + 5}),
      cfg_(cfg),
      md_front_mut_(cfg.md_front),
      td_front_mut_(cfg.td_front)
{
    // 声明 quote 生产者接口（广播期货行情）
    _register_producer("send_quote", tyche::InterfacePattern::SEND);
    // 声明 compute_greeks 生产者接口（期权 job 分发）
    _register_producer("request_compute_greeks", tyche::InterfacePattern::REQUEST);
}

// ── 析构 ───────────────────────────────────────────────────────
CtpGateway::~CtpGateway() {
    // 若尚未停止，走完整 stop() 流程（join + 基类清理）
    if (!stopping_.load()) {
        stop();
    }
}

// ── start() ───────────────────────────────────────────────────
void CtpGateway::start() {
    if (started_.exchange(true)) {
        std::cerr << "[CtpGateway] start() called twice, ignoring\n";
        return;
    }

    // 1. 调用基类 start：完成 TycheEngine 注册、ZMQ 连接
    tyche::TycheModule::start();

    if (!is_registered()) {
        std::cerr << "[CtpGateway] Failed to register with engine, aborting\n";
        return;
    }
    std::cout << "[CtpGateway] Registered as " << module_id() << "\n";

    // 2. 查询合约列表
    resolve_instruments();

    // 3. 初始化 CTP（设置 ctp_running_ = true）
    init_ctp();

    // 4. 启动期权行情分发后台线程（必须在 init_ctp 之后，否则 ctp_running_=false 线程立即退出）
    option_dispatch_thread_ = std::thread([this]() { option_dispatch_loop(); });
}

// ── _start_workers() ──────────────────────────────────────────
void CtpGateway::_start_workers() {
    tyche::TycheModule::_start_workers();
}

// ── resolve_instruments() ─────────────────────────────────────
void CtpGateway::resolve_instruments() {
    instruments_.clear();

    // 等待 static_data 模块就绪（最多重试 max_retries 次）
    const int max_retries = 12;  // 12 * 5s = 60s 最大等待时间
    const int retry_interval_secs = 5;

    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        instruments_.clear();
        bool any_failed = false;

        for (auto& [exchange, products] : cfg_.underlyings) {
            for (auto& product : products) {
                // ── 查询期货合约（product_class="1"）────────────────
                try {
                    tyche::Payload req;
                    req["exchange_id"]     = exchange;
                    req["product_id"]      = product;
                    req["product_class"]   = std::string("1");
                    req["inst_life_phase"] = std::string("1");

                    auto result = request_event(
                        "query_instruments", req,
                        static_cast<float>(cfg_.static_data_timeout_secs));

                    // 检查是否返回了 "No handler" 错误
                    auto err_it = result.find("error");
                    if (err_it != result.end()) {
                        std::string err_msg;
                        try { err_msg = std::any_cast<std::string>(err_it->second); }
                        catch (...) { err_msg = "unknown error"; }
                        if (err_msg.find("No handler") != std::string::npos) {
                            any_failed = true;
                            break;
                        }
                        // 其他错误直接跳过该品种
                        std::cerr << "[CtpGateway] Futures query error ("
                                  << exchange << "." << product << "): " << err_msg << "\n";
                        continue;
                    }

                    auto ids = extract_instrument_ids(result);
                    std::cout << "[CtpGateway] Futures " << exchange << "." << product
                              << ": " << ids.size() << " instruments\n";
                    instruments_.insert(instruments_.end(), ids.begin(), ids.end());
                } catch (const std::exception& e) {
                    std::string what = e.what();
                    if (what.find("timed out") != std::string::npos) {
                        // 超时也可能意味着 static_data 未就绪
                        any_failed = true;
                        break;
                    }
                    std::cerr << "[CtpGateway] Futures query failed ("
                              << exchange << "." << product << "): " << what << "\n";
                }

                if (any_failed) break;

                // ── 查询期权合约（product_class="2"）────────────────
                // 按交易所规则推导期权 product_id
                std::vector<std::string> option_products;
                if (exchange == "SHFE" || exchange == "DCE" ||
                    exchange == "INE"  || exchange == "GFEX") {
                    option_products = {product + "_o"};
                } else if (exchange == "CZCE") {
                    option_products = {product + "C", product + "P"};
                } else if (exchange == "CFFEX") {
                    option_products = {product};  // CFFEX 期权与期货共用 product_id
                } else {
                    option_products = {product};
                }

                for (auto& opt_product : option_products) {
                    try {
                        tyche::Payload req;
                        req["exchange_id"]     = exchange;
                        req["product_id"]      = opt_product;
                        req["product_class"]   = std::string("2");
                        req["inst_life_phase"] = std::string("1");

                        auto result = request_event(
                            "query_instruments", req,
                            static_cast<float>(cfg_.static_data_timeout_secs));

                        auto ids = extract_instrument_ids(result);
                        std::cout << "[CtpGateway] Options " << exchange << "." << opt_product
                                  << ": " << ids.size() << " instruments\n";
                        instruments_.insert(instruments_.end(), ids.begin(), ids.end());
                        // 记录期权合约 ID 用于行情路由区分
                        option_instruments_.insert(
                            option_instruments_.end(), ids.begin(), ids.end());
                    } catch (const std::exception& e) {
                        // 期权查询失败属正常（该品种可能无期权）
                        std::cout << "[CtpGateway] Options query (" << exchange << "."
                                  << opt_product << "): " << e.what() << " (skipped)\n";
                    }
                }
            }
            if (any_failed) break;
        }

        if (!any_failed && !instruments_.empty()) {
            // 查询成功
            break;
        }

        if (any_failed && attempt < max_retries) {
            std::cout << "[CtpGateway] static_data not ready, retrying in "
                      << retry_interval_secs << "s... (attempt "
                      << (attempt + 1) << "/" << max_retries << ")\n";
            std::this_thread::sleep_for(std::chrono::seconds(retry_interval_secs));
        } else if (any_failed) {
            std::cerr << "[CtpGateway] WARNING: static_data still not available after "
                      << max_retries << " retries, proceeding with empty instrument list\n";
        }
    }

    // 去重并排序，启动后只读，热路径无需加锁
    std::sort(option_instruments_.begin(), option_instruments_.end());
    option_instruments_.erase(
        std::unique(option_instruments_.begin(), option_instruments_.end()),
        option_instruments_.end());

    std::cout << "[CtpGateway] Total instruments: " << instruments_.size()
              << " (options: " << option_instruments_.size() << ")\n";
}

// ── extract_instrument_ids() ──────────────────────────────────
// 从 static_data 响应中提取合约 ID 列表。
// 若响应格式不合法则抛出异常，调用方负责处理。
std::vector<std::string> CtpGateway::extract_instrument_ids(
    const tyche::Payload& response)
{
    // response structure: {"result": {"instruments": [...]}}
    const auto& result_any = response.at("result");
    const auto& result     = std::any_cast<const tyche::Payload&>(result_any);
    const auto& insts_any  = result.at("instruments");
    const auto& insts      = std::any_cast<const std::vector<std::any>&>(insts_any);

    std::vector<std::string> ids;
    ids.reserve(insts.size());
    for (const auto& item_any : insts) {
        const auto& inst   = std::any_cast<const tyche::Payload&>(item_any);
        const auto& id_any = inst.at("InstrumentID");
        ids.push_back(std::any_cast<const std::string&>(id_any));
    }
    return ids;
}

// ── init_ctp() ────────────────────────────────────────────────
void CtpGateway::init_ctp() {
    // ── TdApi（可选）──────────────────────────────────────────
    if (!cfg_.td_front.empty()) {
        try {
            td_api_ = CtpLoader::create_td_api(cfg_.dll_dir, cfg_.td_dll, "");
            td_spi_ = std::make_unique<TdSpiImpl>(cfg_, td_api_);
            td_api_->RegisterSpi(td_spi_.get());
            td_api_->RegisterFront(td_front_mut_.data());
            td_api_->SubscribePublicTopic(THOST_TERT_QUICK);
            td_api_->SubscribePrivateTopic(THOST_TERT_QUICK);
            td_api_->Init();
            std::cout << "[CtpGateway] TdApi initialized, connecting to "
                      << cfg_.td_front << "\n";

            bool ok = td_spi_->wait_for_login(cfg_.static_data_timeout_secs);
            if (!ok) {
                std::cerr << "[CtpGateway] TdApi login timeout ("
                          << cfg_.static_data_timeout_secs << "s)\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[CtpGateway] TdApi init failed: " << e.what()
                      << " (continuing with MdApi only)\n";
            // 清理部分初始化的 TdApi 资源，防止 dangling SPI 引用
            if (td_api_) {
                td_api_->RegisterSpi(nullptr);
                td_api_->Release();
                td_api_ = nullptr;
            }
            td_spi_.reset();
        }
    }

    // ── MdApi（必须）─────────────────────────────────────────
    try {
        md_api_ = CtpLoader::create_md_api(cfg_.dll_dir, cfg_.md_dll, "");
        md_spi_ = std::make_unique<MdSpiImpl>(
            cfg_, md_api_,
            [this](const tyche::Payload& p) { on_quote_received(p); });

        md_spi_->set_instruments(instruments_);
        md_api_->RegisterSpi(md_spi_.get());
        md_api_->RegisterFront(md_front_mut_.data());
        md_api_->Init();
        ctp_running_ = true;

        std::cout << "[CtpGateway] MdApi initialized, connecting to "
                  << cfg_.md_front << "\n";
    } catch (const std::exception& e) {
        // 清理部分初始化的 MdApi 资源，同时清理之前已成功初始化的 TdApi
        cleanup_ctp();
        throw;
    }
}

// ── on_quote_received() ───────────────────────────────────────
void CtpGateway::on_quote_received(const tyche::Payload& payload) {
    // 混合路由：期货广播，期权轮询 job
    std::string instrument_id;
    try {
        auto it = payload.find("instrument_id");
        if (it != payload.end()) {
            instrument_id = std::any_cast<std::string>(it->second);
        }
    } catch (...) {}

    bool is_option = false;
    if (!instrument_id.empty()) {
        is_option = std::binary_search(
            option_instruments_.begin(), option_instruments_.end(), instrument_id);
    }

    if (is_option) {
        // 期权: 推入分发队列，由后台线程发送 compute_greeks job
        bool dropped = false;
        {
            std::lock_guard<std::mutex> lock(option_queue_mtx_);
            if (option_queue_.size() < 10000) {
                option_queue_.push(payload);
            } else {
                dropped = true;
            }
        }
        if (dropped) {
            int dc = option_dropped_count_.fetch_add(1, std::memory_order_relaxed);
            if (dc % 1000 == 0) {
                std::cerr << "[CtpGateway] WARNING: option queue full, dropped "
                          << (dc + 1) << " ticks so far\n";
            }
        } else {
            option_queue_cv_.notify_one();
        }
    } else {
        // 期货: 广播给所有 greeks_engine 实例
        send_event("quote", payload);
    }
}

// ── option_dispatch_loop() ───────────────────────────────────
void CtpGateway::option_dispatch_loop() {
    std::cout << "[CtpGateway] Option dispatch thread started\n";

    while (ctp_running_.load() || !option_queue_.empty()) {
        tyche::Payload tick;
        {
            std::unique_lock<std::mutex> lock(option_queue_mtx_);
            option_queue_cv_.wait_for(lock, std::chrono::milliseconds(200),
                [this]() { return !option_queue_.empty() || !ctp_running_.load(); });
            if (option_queue_.empty()) {
                if (!ctp_running_.load()) break;
                continue;
            }
            tick = std::move(option_queue_.front());
            option_queue_.pop();
        }

        // 发送 compute_greeks job（阻塞等响应，但在独立线程不影响 CTP 回调）
        try {
            request_event("compute_greeks", tick, 10.0f);
        } catch (const std::exception& e) {
            // 超时或无 handler，记录但不崩溃
            int ec = option_err_count_.fetch_add(1, std::memory_order_relaxed);
            if (ec < 5) {
                std::cerr << "[CtpGateway] compute_greeks dispatch failed: "
                          << e.what() << "\n";
            } else if (ec == 5) {
                std::cerr << "[CtpGateway] compute_greeks errors suppressed\n";
            }
        }
    }

    int dropped = option_dropped_count_.load(std::memory_order_relaxed);
    if (dropped > 0) {
        std::cerr << "[CtpGateway] Option dispatch thread stopped (" << dropped
                  << " ticks dropped due to queue full)\n";
    } else {
        std::cout << "[CtpGateway] Option dispatch thread stopped\n";
    }
}

// ── stop() ────────────────────────────────────────────────────
void CtpGateway::stop() {
    if (stopping_.exchange(true)) return;

    // 1. 先停止分发线程循环
    ctp_running_ = false;
    option_queue_cv_.notify_all();

    // 2. 等待期权分发线程结束
    if (option_dispatch_thread_.joinable()) {
        option_dispatch_thread_.join();
    }

    // 3. 再释放 CTP 资源（此时 dispatch 线程已退出，不会调用 request_event）
    cleanup_ctp();

    // 4. 最后关闭 ZMQ 等基类资源
    tyche::TycheModule::stop();
}

// ── cleanup_ctp() ─────────────────────────────────────────────
// 从析构函数调用，必须保证不抛出异常（noexcept）
void CtpGateway::cleanup_ctp() noexcept {
    if (cleanup_done_.exchange(true)) return;  // 已清理过，直接返回
    ctp_running_ = false;

    try {
        std::cout << "[CtpGateway] Releasing CTP resources...\n";

        if (md_api_) {
            md_api_->RegisterSpi(nullptr);
            md_api_->Join();
            md_api_->Release();
            md_api_ = nullptr;
        }
        if (td_api_) {
            td_api_->RegisterSpi(nullptr);
            td_api_->Join();
            td_api_->Release();
            td_api_ = nullptr;
        }
        md_spi_.reset();
        td_spi_.reset();

        std::cout << "[CtpGateway] CTP resources released\n";
    } catch (...) {
        // 析构路径禁止异常传播
        std::cerr << "[CtpGateway] FATAL: exception in cleanup_ctp()\n";
    }
}
