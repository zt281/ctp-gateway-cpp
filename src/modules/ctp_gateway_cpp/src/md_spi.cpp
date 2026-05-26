#include "md_spi.h"
#include <cstring>
#include <iostream>

// ── 构造函数 ───────────────────────────────────────────────────
MdSpiImpl::MdSpiImpl(const GatewayConfig& cfg,
                     CThostFtdcMdApi*     md_api,
                     QuoteCallback         on_quote)
    : cfg_(cfg), md_api_(md_api), on_quote_cb_(std::move(on_quote)) {}

// ── 合约列表设置 ───────────────────────────────────────────────
void MdSpiImpl::set_instruments(std::vector<std::string> instruments) {
    {
        std::lock_guard<std::mutex> lock(inst_mtx_);
        instruments_ = std::move(instruments);
    }
    // 若已登录则立即订阅（先释放锁，避免 do_subscribe 内重复加锁）
    if (logged_in_.load()) {
        do_subscribe();
    }
}

// ── 连接建立 ───────────────────────────────────────────────────
void MdSpiImpl::OnFrontConnected() {
    std::cout << "[MdSpi] Front connected, logging in...\n";
    do_login();
}

void MdSpiImpl::do_login() {
    CThostFtdcReqUserLoginField req{};
    strncpy(req.BrokerID, cfg_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.UserID,   cfg_.user_id.c_str(),   sizeof(req.UserID)   - 1);
    strncpy(req.Password, cfg_.password.c_str(),  sizeof(req.Password) - 1);
    int ret = md_api_->ReqUserLogin(&req, ++req_id_);
    if (ret != 0) {
        std::cerr << "[MdSpi] ReqUserLogin failed, ret=" << ret << "\n";
    }
}

// ── 连接断开 ───────────────────────────────────────────────────
void MdSpiImpl::OnFrontDisconnected(int nReason) {
    logged_in_ = false;
    std::cerr << "[MdSpi] Front disconnected, reason=" << nReason
              << ", MdApi will auto-reconnect...\n";
}

// ── 心跳警告 ───────────────────────────────────────────────────
void MdSpiImpl::OnHeartBeatWarning(int nTimeLapse) {
    std::cerr << "[MdSpi] HeartBeat warning, timeLapse=" << nTimeLapse << "s\n";
}

// ── 登录响应 ───────────────────────────────────────────────────
void MdSpiImpl::OnRspUserLogin(CThostFtdcRspUserLoginField* pLogin,
                                CThostFtdcRspInfoField*       pInfo,
                                int /*nRequestID*/, bool /*bIsLast*/) {
    if (pInfo && pInfo->ErrorID != 0) {
        std::cerr << "[MdSpi] Login failed, ErrorID=" << pInfo->ErrorID
                  << " Msg=" << (pInfo->ErrorMsg ? pInfo->ErrorMsg : "") << "\n";
        return;
    }
    logged_in_ = true;
    const char* day = (pLogin && pLogin->TradingDay[0]) ? pLogin->TradingDay : "N/A";
    std::cout << "[MdSpi] Login OK, TradingDay=" << day << "\n";
    do_subscribe();  // do_subscribe 内部自行加锁
}

// ── 登出响应 ───────────────────────────────────────────────────
void MdSpiImpl::OnRspUserLogout(CThostFtdcUserLogoutField* /*pUserLogout*/,
                                 CThostFtdcRspInfoField*     pInfo,
                                 int /*nRequestID*/, bool /*bIsLast*/) {
    logged_in_ = false;
    if (pInfo && pInfo->ErrorID != 0) {
        std::cerr << "[MdSpi] Logout error, ErrorID=" << pInfo->ErrorID << "\n";
    } else {
        std::cout << "[MdSpi] Logged out\n";
    }
}

// ── 订阅实现 ───────────────────────────────────────────────────
void MdSpiImpl::do_subscribe() {
    // 统一在此处加锁；调用方（set_instruments / OnRspUserLogin）均不持有该锁。
    std::lock_guard<std::mutex> lock(inst_mtx_);
    if (instruments_.empty()) {
        std::cout << "[MdSpi] No instruments to subscribe\n";
        return;
    }
    // CTP SubscribeMarketData 需要 char** 数组
    std::vector<char*> ids;
    ids.reserve(instruments_.size());
    for (auto& s : instruments_) {
        ids.push_back(const_cast<char*>(s.c_str()));
    }
    int ret = md_api_->SubscribeMarketData(ids.data(),
                                           static_cast<int>(ids.size()));
    std::cout << "[MdSpi] SubscribeMarketData("
              << ids.size() << " instruments) ret=" << ret << "\n";
}

// ── 订阅响应 ───────────────────────────────────────────────────
void MdSpiImpl::OnRspSubMarketData(
    CThostFtdcSpecificInstrumentField* pInst,
    CThostFtdcRspInfoField*             pInfo,
    int /*nRequestID*/, bool /*bIsLast*/) {
    if (pInfo && pInfo->ErrorID != 0) {
        std::cerr << "[MdSpi] Subscribe failed for "
                  << (pInst ? pInst->InstrumentID : "?")
                  << " ErrorID=" << pInfo->ErrorID
                  << " Msg=" << (pInfo->ErrorMsg ? pInfo->ErrorMsg : "") << "\n";
    } else {
        std::cout << "[MdSpi] Subscribe OK: "
                  << (pInst ? pInst->InstrumentID : "?") << "\n";
    }
}

// ── 行情推送 ───────────────────────────────────────────────────
void MdSpiImpl::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* p) {
    if (!p) return;
    tyche::Payload payload = depth_to_payload(p);
    on_quote_cb_(payload);
}

// ── DepthMarketData → Payload 转换 ────────────────────────────
tyche::Payload MdSpiImpl::depth_to_payload(
    const CThostFtdcDepthMarketDataField* d) {
    tyche::Payload p;
    p["instrument_id"]     = std::string(d->InstrumentID);
    p["exchange_id"]       = std::string(d->ExchangeID);
    p["last_price"]        = d->LastPrice;
    p["volume"]            = static_cast<int>(d->Volume);
    p["bid_price1"]        = d->BidPrice1;
    p["bid_volume1"]       = static_cast<int>(d->BidVolume1);
    p["ask_price1"]        = d->AskPrice1;
    p["ask_volume1"]       = static_cast<int>(d->AskVolume1);
    p["upper_limit_price"] = d->UpperLimitPrice;
    p["lower_limit_price"] = d->LowerLimitPrice;
    p["open_price"]        = d->OpenPrice;
    p["high_price"]        = d->HighestPrice;
    p["low_price"]         = d->LowestPrice;
    p["pre_settle_price"]  = d->PreSettlementPrice;
    p["open_interest"]     = d->OpenInterest;
    p["turnover"]          = d->Turnover;
    p["update_time"]       = std::string(d->UpdateTime);
    p["update_millisec"]   = static_cast<int>(d->UpdateMillisec);
    p["trading_day"]       = std::string(d->TradingDay);
    return p;
}
