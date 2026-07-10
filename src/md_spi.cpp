#include "md_spi.h"
#include "quote_tick.h"
#include <cstring>
#include <chrono>
#include <iostream>
#include <string.h>  // for strnlen (MSVC compat)

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>  // for SecureZeroMemory
#endif

using namespace std::chrono;

// 安全复制字符串到固定大小缓冲区，确保以 '\0' 结尾
static void safe_copy(char* dest, size_t dest_size, const char* src) {
    if (dest_size == 0) return;
    std::strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// 显式清零密码字段，防止编译器优化掉 memset
static void zero_password_field(char* field, size_t len) {
#if defined(_WIN32)
    SecureZeroMemory(field, len);
#elif defined(__STDC_LIB_EXT1__)
    memset_s(field, len, 0, len);
#else
    volatile char* p = field;
    while (len--) *p++ = '\0';
#endif
}

// 安全地将 CTP 定长字符数组转换为 std::string（防止缺少 '\0' 导致越界）
static std::string safe_string(const char* src, size_t max_len) {
    return std::string(src, strnlen(src, max_len));
}

// ── 构造函数 ───────────────────────────────────────────────────
MdSpiImpl::MdSpiImpl(const GatewayConfig& cfg,
                     CThostFtdcMdApi*   md_api,
                     QuoteCallback      on_quote)
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
    safe_copy(req.BrokerID, sizeof(req.BrokerID), cfg_.broker_id.c_str());
    safe_copy(req.UserID,   sizeof(req.UserID),   cfg_.user_id.c_str());
    safe_copy(req.Password, sizeof(req.Password), cfg_.password.c_str());
    int ret = md_api_->ReqUserLogin(&req, ++req_id_);
    zero_password_field(req.Password, sizeof(req.Password));
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
    std::string day = (pLogin && pLogin->TradingDay[0])
        ? safe_string(pLogin->TradingDay, sizeof(pLogin->TradingDay))
        : "N/A";
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
    // 将合约 ID 复制到持久缓冲区，避免 instruments_ 内部 realloc 导致指针失效
    instrument_buffers_.clear();
    instrument_buffers_.reserve(instruments_.size());
    std::vector<char*> ids;
    ids.reserve(instruments_.size());
    for (auto& s : instruments_) {
        instrument_buffers_.emplace_back(s.begin(), s.end());
        instrument_buffers_.back().push_back('\0');
        ids.push_back(instrument_buffers_.back().data());
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

    // Create QuoteTick for efficient hot-path processing
    QuoteTick tick = depth_to_tick(p);

    // Update last tick timestamp
    last_tick_time_.store(tick.receive_ts_ns > 0
        ? steady_clock::time_point(steady_clock::duration(tick.receive_ts_ns))
        : steady_clock::now());

    // Call depth_to_payload for backward compatibility
    tyche::Payload payload = depth_to_payload(p);
    on_quote_cb_(payload);
}

// ── DepthMarketData → Payload 转换 ────────────────────────────
tyche::Payload MdSpiImpl::depth_to_payload(
    const CThostFtdcDepthMarketDataField* d) {
    tyche::Payload p;
    p["instrument_id"]     = safe_string(d->InstrumentID, sizeof(d->InstrumentID));
    p["exchange_id"]       = safe_string(d->ExchangeID,   sizeof(d->ExchangeID));
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
    p["update_time"]       = safe_string(d->UpdateTime, sizeof(d->UpdateTime));
    p["update_millisec"]   = static_cast<int>(d->UpdateMillisec);
    p["trading_day"]       = safe_string(d->TradingDay, sizeof(d->TradingDay));
    return p;
}

// ── DepthMarketData → QuoteTick 转换 ──────────────────────────
QuoteTick MdSpiImpl::depth_to_tick(
    const CThostFtdcDepthMarketDataField* d) {
    QuoteTick tick{};
    safe_copy(tick.instrument_id, sizeof(tick.instrument_id), d->InstrumentID);
    safe_copy(tick.exchange_id,   sizeof(tick.exchange_id),   d->ExchangeID);
    safe_copy(tick.update_time,   sizeof(tick.update_time),   d->UpdateTime);
    safe_copy(tick.trading_day,   sizeof(tick.trading_day),   d->TradingDay);
    tick.last_price         = d->LastPrice;
    tick.volume             = static_cast<int>(d->Volume);
    tick.bid_price1         = d->BidPrice1;
    tick.bid_volume1        = static_cast<int>(d->BidVolume1);
    tick.ask_price1         = d->AskPrice1;
    tick.ask_volume1        = static_cast<int>(d->AskVolume1);
    tick.upper_limit_price  = d->UpperLimitPrice;
    tick.lower_limit_price  = d->LowerLimitPrice;
    tick.open_price         = d->OpenPrice;
    tick.high_price         = d->HighestPrice;
    tick.low_price          = d->LowestPrice;
    tick.pre_settle_price   = d->PreSettlementPrice;
    tick.open_interest      = d->OpenInterest;
    tick.turnover           = d->Turnover;
    tick.update_millisec    = static_cast<int>(d->UpdateMillisec);
    tick.receive_ts_ns      = steady_clock::now().time_since_epoch().count();
    return tick;
}