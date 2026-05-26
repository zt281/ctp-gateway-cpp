#include "ctp_gateway.h"
#include "ctp_loader.h"
#include <any>
#include <iostream>
#include <stdexcept>

// ── 构造函数 ───────────────────────────────────────────────────
CtpGateway::CtpGateway(const GatewayConfig& cfg)
    : tyche::TycheModule(
          tyche::Endpoint{cfg.engine_host, cfg.engine_port},
          cfg.family_name,
          tyche::Endpoint{cfg.engine_host, cfg.engine_port + 5}),
      cfg_(cfg)
{
    // 声明 quote 生产者接口（广播行情）
    _register_producer("send_quote", tyche::InterfacePattern::SEND);
}

// ── 析构 ───────────────────────────────────────────────────────
CtpGateway::~CtpGateway() {
    cleanup_ctp();
}

// ── start() ───────────────────────────────────────────────────
void CtpGateway::start() {
    // 1. 调用基类 start：完成 TycheEngine 注册、ZMQ 连接
    tyche::TycheModule::start();

    if (!is_registered()) {
        std::cerr << "[CtpGateway] Failed to register with engine, aborting\n";
        return;
    }
    std::cout << "[CtpGateway] Registered as " << module_id() << "\n";

    // 2. 查询合约列表
    resolve_instruments();

    // 3. 初始化 CTP
    init_ctp();
}

// ── _start_workers() ──────────────────────────────────────────
void CtpGateway::_start_workers() {
    tyche::TycheModule::_start_workers();
}

// ── resolve_instruments() ─────────────────────────────────────
void CtpGateway::resolve_instruments() {
    instruments_.clear();

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

                auto ids = extract_instrument_ids(result);
                std::cout << "[CtpGateway] Futures " << exchange << "." << product
                          << ": " << ids.size() << " instruments\n";
                instruments_.insert(instruments_.end(), ids.begin(), ids.end());
            } catch (const std::exception& e) {
                std::cerr << "[CtpGateway] Futures query failed ("
                          << exchange << "." << product << "): " << e.what() << "\n";
            }

            // ── 查询期权合约（product_class="2"）────────────────
            try {
                tyche::Payload req;
                req["exchange_id"]     = exchange;
                req["product_id"]      = product;
                req["product_class"]   = std::string("2");
                req["inst_life_phase"] = std::string("1");

                auto result = request_event(
                    "query_instruments", req,
                    static_cast<float>(cfg_.static_data_timeout_secs));

                auto ids = extract_instrument_ids(result);
                std::cout << "[CtpGateway] Options " << exchange << "." << product
                          << ": " << ids.size() << " instruments\n";
                instruments_.insert(instruments_.end(), ids.begin(), ids.end());
            } catch (const std::exception& e) {
                // 期权查询失败属正常（该品种可能无期权）
                std::cout << "[CtpGateway] Options query (" << exchange << "."
                          << product << "): " << e.what() << " (skipped)\n";
            }
        }
    }

    std::cout << "[CtpGateway] Total instruments: " << instruments_.size() << "\n";
}

// ── extract_instrument_ids() ──────────────────────────────────
std::vector<std::string> CtpGateway::extract_instrument_ids(
    const tyche::Payload& response)
{
    std::vector<std::string> ids;
    try {
        // response["result"] → Payload
        const auto& result_any = response.at("result");
        const auto& result     = std::any_cast<const tyche::Payload&>(result_any);

        // result["instruments"] → vector<any>
        const auto& insts_any = result.at("instruments");
        const auto& insts     = std::any_cast<const std::vector<std::any>&>(insts_any);

        ids.reserve(insts.size());
        for (const auto& item_any : insts) {
            const auto& inst   = std::any_cast<const tyche::Payload&>(item_any);
            const auto& id_any = inst.at("InstrumentID");
            ids.push_back(std::any_cast<const std::string&>(id_any));
        }
    } catch (const std::bad_any_cast& e) {
        std::cerr << "[CtpGateway] extract_instrument_ids bad_any_cast: "
                  << e.what() << "\n";
    } catch (const std::out_of_range& e) {
        std::cerr << "[CtpGateway] extract_instrument_ids key missing: "
                  << e.what() << "\n";
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
            td_api_->RegisterFront(const_cast<char*>(cfg_.td_front.c_str()));
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
            // td_api_ 可能为 null，cleanup_ctp 会保护性判断
        }
    }

    // ── MdApi（必须）─────────────────────────────────────────
    md_api_ = CtpLoader::create_md_api(cfg_.dll_dir, cfg_.md_dll, "");
    md_spi_ = std::make_unique<MdSpiImpl>(
        cfg_, md_api_,
        [this](const tyche::Payload& p) { on_quote_received(p); });

    md_spi_->set_instruments(instruments_);
    md_api_->RegisterSpi(md_spi_.get());
    md_api_->RegisterFront(const_cast<char*>(cfg_.md_front.c_str()));
    md_api_->Init();
    ctp_running_ = true;

    std::cout << "[CtpGateway] MdApi initialized, connecting to "
              << cfg_.md_front << "\n";
}

// ── on_quote_received() ───────────────────────────────────────
void CtpGateway::on_quote_received(const tyche::Payload& payload) {
    send_event("quote", payload);
}

// ── stop() ────────────────────────────────────────────────────
void CtpGateway::stop() {
    cleanup_ctp();
    tyche::TycheModule::stop();
}

// ── cleanup_ctp() ─────────────────────────────────────────────
void CtpGateway::cleanup_ctp() {
    if (!ctp_running_.exchange(false)) return;

    std::cout << "[CtpGateway] Releasing CTP resources...\n";

    if (md_api_) {
        md_api_->RegisterSpi(nullptr);
        md_api_->Release();
        md_api_ = nullptr;
    }
    if (td_api_) {
        td_api_->RegisterSpi(nullptr);
        td_api_->Release();
        td_api_ = nullptr;
    }
    md_spi_.reset();
    td_spi_.reset();

    std::cout << "[CtpGateway] CTP resources released\n";
}
