#include "ctp_gateway.h"
#include "ctp_loader.h"
#include "ctp_api_adapter.h"
#include <any>
#include <chrono>
#include <cstring>
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
      td_front_mut_(cfg.td_front),
      option_ring_buffer_(65536)  // RingBuffer 容量 65536
{
    // 声明 quote 生产者接口（广播期货行情）
    _register_producer("send_quote", tyche::InterfacePattern::SEND);
    // 声明 send_compute_greeks 生产者接口（期权 job 广播）
    _register_producer("send_compute_greeks", tyche::InterfacePattern::SEND);

    // 注册 gateway_status job handler
    _register_job_handler("gateway_status", [this](const tyche::Payload& req) -> tyche::Payload {
        tyche::Payload resp;
        resp["status"] = ctp_running_.load() ? std::string("running") : std::string("stopped");
        resp["instruments_count"] = static_cast<int>(instruments_.size());
        resp["option_ring_buffer_depth"] = static_cast<int>(option_ring_buffer_.size());
        resp["reconnect_count"] = reconnect_count_.load();
        resp["ticks_received"] = ticks_received_.load();
        resp["ticks_sent"] = ticks_sent_.load();
        resp["option_dropped_count"] = option_dropped_count_.load();
        resp["option_err_count"] = option_err_count_.load();
        resp["tick_stale"] = tick_stale_.load();
        resp["uptime_secs"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count();
        resp["last_tick_age_ms"] = last_tick_age_ms();
        return resp;
    });
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
        LOG_WARN("start() called twice, ignoring");
        return;
    }

    // 1. 调用基类 start：完成 TycheEngine 注册、ZMQ 连接
    tyche::TycheModule::start();

    if (!is_registered()) {
        LOG_ERROR("Failed to register with engine, aborting");
        return;
    }
    LOG_INFO("Registered as %s", module_id().c_str());

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
    option_instrument_set_.clear();

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
                        LOG_WARN("Futures query error (%s.%s): %s",
                                 exchange.c_str(), product.c_str(), err_msg.c_str());
                        continue;
                    }

                    auto ids = extract_instrument_ids(result);
                    LOG_INFO("Futures %s.%s: %zu instruments",
                             exchange.c_str(), product.c_str(), ids.size());
                    instruments_.insert(instruments_.end(), ids.begin(), ids.end());
                } catch (const std::exception& e) {
                    std::string what = e.what();
                    if (what.find("timed out") != std::string::npos) {
                        // 超时也可能意味着 static_data 未就绪
                        any_failed = true;
                        break;
                    }
                    LOG_WARN("Futures query failed (%s.%s): %s",
                             exchange.c_str(), product.c_str(), what.c_str());
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
                        LOG_INFO("Options %s.%s: %zu instruments",
                                 exchange.c_str(), opt_product.c_str(), ids.size());
                        instruments_.insert(instruments_.end(), ids.begin(), ids.end());

                        // 填充期权合约集合（O(1) 查找）
                        for (const auto& id : ids) {
                            option_instrument_set_.insert(id);
                        }
                    } catch (const std::exception& e) {
                        // 期权查询失败属正常（该品种可能无期权）
                        LOG_INFO("Options query (%s.%s): %s (skipped)",
                                 exchange.c_str(), opt_product.c_str(), e.what());
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
            LOG_INFO("static_data not ready, retrying in %ds... (attempt %d/%d)",
                     retry_interval_secs, attempt + 1, max_retries);
            std::this_thread::sleep_for(std::chrono::seconds(retry_interval_secs));
        } else if (any_failed) {
            LOG_ERROR("WARNING: static_data still not available after %d retries, "
                      "proceeding with empty instrument list", max_retries);
        }
    }

    LOG_INFO("Total instruments: %zu (options: %zu)",
             instruments_.size(), option_instrument_set_.size());
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

// ── tick_to_payload() ─────────────────────────────────────────
// 将 QuoteTick 转换为 Payload 用于 greeks engine 消费
tyche::Payload CtpGateway::tick_to_payload(const QuoteTick& tick) {
    tyche::Payload payload;
    payload["instrument_id"] = tick.instrument_id_sv();
    payload["exchange_id"]   = std::string(tick.exchange_id);
    payload["last_price"]    = tick.last_price;
    payload["volume"]       = tick.volume;
    payload["bid_price1"]   = tick.bid_price1;
    payload["bid_volume1"]   = tick.bid_volume1;
    payload["ask_price1"]   = tick.ask_price1;
    payload["ask_volume1"]  = tick.ask_volume1;
    payload["upper_limit"]  = tick.upper_limit_price;
    payload["lower_limit"]  = tick.lower_limit_price;
    payload["open_price"]   = tick.open_price;
    payload["high_price"]   = tick.high_price;
    payload["low_price"]    = tick.low_price;
    payload["pre_settle"]   = tick.pre_settle_price;
    payload["open_interest"]= tick.open_interest;
    payload["turnover"]     = tick.turnover;
    payload["update_time"]  = std::string(tick.update_time);
    payload["update_millisec"] = tick.update_millisec;
    payload["trading_day"]  = std::string(tick.trading_day);
    return payload;
}

// ── init_ctp() ────────────────────────────────────────────────
void CtpGateway::init_ctp() {
    // ── TdApi（可选）──────────────────────────────────────────
    if (!cfg_.td_front.empty()) {
        try {
            auto td_result = CtpLoader::create_td_api(
                cfg_.dll_dir, cfg_.td_dll, "");
            td_dll_ = std::move(td_result.first);
            td_api_ = std::move(td_result.second);
            td_spi_ = std::make_unique<TdSpiImpl>(cfg_, td_api_.get());
            td_api_->RegisterSpi(td_spi_.get());
            td_api_->RegisterFront(td_front_mut_.data());
            td_api_->SubscribePublicTopic(THOST_TERT_QUICK);
            td_api_->SubscribePrivateTopic(THOST_TERT_QUICK);
            td_api_->Init();
            LOG_INFO("TdApi initialized, connecting to %s", cfg_.td_front.c_str());

            bool ok = td_spi_->wait_for_login(cfg_.static_data_timeout_secs);
            if (!ok) {
                LOG_WARN("TdApi login timeout (%ds)",
                         cfg_.static_data_timeout_secs);
            }
        } catch (const std::exception& e) {
            LOG_WARN("TdApi init failed: %s (continuing with MdApi only)",
                     e.what());
            // 清理部分初始化的 TdApi 资源，防止 dangling SPI 引用
            td_spi_.reset();
            td_api_.reset();
            td_dll_.reset();
        }
    }

    // ── MdApi（必须）─────────────────────────────────────────
    try {
        auto md_result = CtpLoader::create_md_api(
            cfg_.dll_dir, cfg_.md_dll, "");
        md_dll_ = std::move(md_result.first);
        md_api_ = std::move(md_result.second);
        md_spi_ = std::make_unique<MdSpiImpl>(
            cfg_, md_api_.get(),
            std::function<void(const tyche::Payload&)>(
                [this](const tyche::Payload& p) { on_quote_received(p); }));

        md_spi_->set_instruments(instruments_);
        md_api_->RegisterSpi(md_spi_.get());
        md_api_->RegisterFront(md_front_mut_.data());
        md_api_->Init();
        ctp_running_ = true;

        LOG_INFO("MdApi initialized, connecting to %s", cfg_.md_front.c_str());
    } catch (const std::exception& e) {
        // 清理部分初始化的 MdApi 资源，同时清理之前已成功初始化的 TdApi
        cleanup_ctp();
        throw;
    }
}

// ── on_quote_received() ────────────────────────────────────────
void CtpGateway::on_quote_received(const tyche::Payload& payload) {
    // 更新最后行情时间戳
    auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    last_tick_ns_.store(static_cast<uint64_t>(now_ns), std::memory_order_release);

    // 获取 instrument_id
    std::string instrument_id;
    try {
        auto it = payload.find("instrument_id");
        if (it != payload.end()) {
            instrument_id = std::any_cast<std::string>(it->second);
        }
    } catch (...) {}

    // 使用 unordered_set 进行 O(1) 期权检测
    bool is_option = !instrument_id.empty() &&
                     option_instrument_set_.count(instrument_id) > 0;

    if (is_option) {
        // 期权: 构造 QuoteTick 并推入 RingBuffer
        QuoteTick tick{};
        std::memset(&tick, 0, sizeof(tick));

        // 从 payload 填充 tick
        try {
            if (auto it = payload.find("instrument_id"); it != payload.end()) {
                auto id = std::any_cast<std::string>(it->second);
                std::strncpy(tick.instrument_id, id.c_str(),
                             sizeof(tick.instrument_id) - 1);
            }
            if (auto it = payload.find("exchange_id"); it != payload.end()) {
                auto id = std::any_cast<std::string>(it->second);
                std::strncpy(tick.exchange_id, id.c_str(),
                             sizeof(tick.exchange_id) - 1);
            }
            if (auto it = payload.find("last_price"); it != payload.end()) {
                tick.last_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("volume"); it != payload.end()) {
                tick.volume = std::any_cast<int>(it->second);
            }
            if (auto it = payload.find("bid_price1"); it != payload.end()) {
                tick.bid_price1 = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("bid_volume1"); it != payload.end()) {
                tick.bid_volume1 = std::any_cast<int>(it->second);
            }
            if (auto it = payload.find("ask_price1"); it != payload.end()) {
                tick.ask_price1 = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("ask_volume1"); it != payload.end()) {
                tick.ask_volume1 = std::any_cast<int>(it->second);
            }
            if (auto it = payload.find("upper_limit"); it != payload.end()) {
                tick.upper_limit_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("lower_limit"); it != payload.end()) {
                tick.lower_limit_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("open_price"); it != payload.end()) {
                tick.open_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("high_price"); it != payload.end()) {
                tick.high_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("low_price"); it != payload.end()) {
                tick.low_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("pre_settle"); it != payload.end()) {
                tick.pre_settle_price = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("open_interest"); it != payload.end()) {
                tick.open_interest = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("turnover"); it != payload.end()) {
                tick.turnover = std::any_cast<double>(it->second);
            }
            if (auto it = payload.find("update_time"); it != payload.end()) {
                auto t = std::any_cast<std::string>(it->second);
                std::strncpy(tick.update_time, t.c_str(),
                             sizeof(tick.update_time) - 1);
            }
            if (auto it = payload.find("update_millisec"); it != payload.end()) {
                tick.update_millisec = std::any_cast<int>(it->second);
            }
            if (auto it = payload.find("trading_day"); it != payload.end()) {
                auto t = std::any_cast<std::string>(it->second);
                std::strncpy(tick.trading_day, t.c_str(),
                             sizeof(tick.trading_day) - 1);
            }
        } catch (...) {
            // 部分字段提取失败不影响处理
        }

        tick.receive_ts_ns = static_cast<uint64_t>(now_ns);

        // 尝试推入 RingBuffer，失败则覆盖旧数据
        if (!option_ring_buffer_.try_push(std::move(tick))) {
            option_ring_buffer_.push_overwrite(std::move(tick));
            int dc = option_dropped_count_.fetch_add(1, std::memory_order_relaxed);
            if (dc % 1000 == 0) {
                LOG_WARN("Option ring buffer full, dropped %d ticks so far", dc + 1);
            }
        }
    } else {
        // 期货: 广播给所有 greeks_engine 实例
        send_event("quote", payload);
        ticks_sent_.fetch_add(1, std::memory_order_relaxed);
    }

    // 递增接收计数
    ticks_received_.fetch_add(1, std::memory_order_relaxed);
}

// ── option_dispatch_loop() ───────────────────────────────────
void CtpGateway::option_dispatch_loop() {
    LOG_INFO("Option dispatch thread started");

    // Stale detection: check every 30 seconds if last tick is >30 seconds old
    const auto stale_check_interval = std::chrono::seconds(30);
    const int64_t stale_threshold_ms = 30000;
    auto last_stale_check = std::chrono::steady_clock::now();

    // Adaptive spin/yield/sleep state
    int idle_spins = 0;
    constexpr int SPIN_THRESHOLD = 1000;
    constexpr int YIELD_THRESHOLD = 10000;
    constexpr int BATCH_SIZE = 64;

    while (ctp_running_.load(std::memory_order_relaxed)) {
        // Stale detection check
        auto now = std::chrono::steady_clock::now();
        if (now - last_stale_check >= stale_check_interval) {
            if (last_tick_age_ms() > stale_threshold_ms) {
                tick_stale_.store(true, std::memory_order_release);
            }
            last_stale_check = now;
        }

        // 批量弹出期权行情
        QuoteTick batch[BATCH_SIZE];
        size_t n = 0;
        while (n < BATCH_SIZE) {
            auto tick_opt = option_ring_buffer_.pop();
            if (!tick_opt.has_value()) break;
            batch[n++] = std::move(*tick_opt);
        }

        if (n > 0) {
            idle_spins = 0;
            for (size_t i = 0; i < n; ++i) {
                // 发送 send_compute_greeks 事件（广播到 greeks_engine）
                try {
                    send_event("send_compute_greeks", tick_to_payload(batch[i]));
                    ticks_sent_.fetch_add(1, std::memory_order_relaxed);
                } catch (const std::exception& e) {
                    int ec = option_err_count_.fetch_add(1, std::memory_order_relaxed);
                    if (ec < 5) {
                        LOG_WARN("send_compute_greeks dispatch failed: %s", e.what());
                    } else if (ec == 5) {
                        LOG_WARN("send_compute_greeks errors suppressed");
                    }
                }
            }
        } else {
            // Adaptive wait: spin -> yield -> sleep
            if (++idle_spins < SPIN_THRESHOLD) {
#if defined(_MSC_VER)
                _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ __volatile__("yield" :::);
#endif
            } else if (idle_spins < YIELD_THRESHOLD) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }

    // CTP 停止后 drain 剩余数据
    while (!option_ring_buffer_.empty()) {
        auto tick_opt = option_ring_buffer_.pop();
        if (!tick_opt.has_value()) {
            break;
        }
        try {
            send_event("send_compute_greeks", tick_to_payload(tick_opt.value()));
            ticks_sent_.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            // 忽略 drain 阶段的错误
        }
    }

    int dropped = option_dropped_count_.load(std::memory_order_relaxed);
    if (dropped > 0) {
        LOG_WARN("Option dispatch thread stopped (%d ticks dropped)", dropped);
    } else {
        LOG_INFO("Option dispatch thread stopped");
    }
}

// ── get_status_payload() ───────────────────────────────────────
tyche::Payload CtpGateway::get_status_payload() const {
    tyche::Payload status;
    status["module_id"]        = module_id();
    status["ticks_received"]   = static_cast<int64_t>(
        ticks_received_.load(std::memory_order_relaxed));
    status["ticks_sent"]       = static_cast<int64_t>(
        ticks_sent_.load(std::memory_order_relaxed));
    status["option_dropped"]   = option_dropped_count_.load(
        std::memory_order_relaxed);
    status["option_errors"]    = option_err_count_.load(
        std::memory_order_relaxed);
    status["tick_stale"]       = is_tick_stale();
    status["last_tick_age_ms"] = last_tick_age_ms();
    status["reconnect_count"]  = reconnect_count();
    status["ctp_running"]      = ctp_running_.load(std::memory_order_relaxed);
    status["ring_buffer_size"] = option_ring_buffer_.size();
    status["ring_buffer_cap"]   = option_ring_buffer_.capacity();
    return status;
}

// ── stop() ────────────────────────────────────────────────────
void CtpGateway::stop() {
    if (stopping_.exchange(true)) return;

    // 1. 先停止分发线程循环
    ctp_running_ = false;

    // 2. 等待期权分发线程结束
    if (option_dispatch_thread_.joinable()) {
        option_dispatch_thread_.join();
    }

    // 3. 再释放 CTP 资源（此时 dispatch 线程已退出，不会调用 send_event）
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
        LOG_INFO("Releasing CTP resources...");

        // MdApi 通过 unique_ptr + MdApiDeleter 自动释放
        md_spi_.reset();
        md_api_.reset();
        md_dll_.reset();

        // TdApi 通过 unique_ptr + TdApiDeleter 自动释放
        td_spi_.reset();
        td_api_.reset();
        td_dll_.reset();

        LOG_INFO("CTP resources released");
    } catch (...) {
        // 析构路径禁止异常传播
        LOG_ERROR("FATAL: exception in cleanup_ctp()");
    }
}