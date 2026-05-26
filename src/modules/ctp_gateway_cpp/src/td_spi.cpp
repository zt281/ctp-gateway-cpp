#include "td_spi.h"
#include <chrono>
#include <cstring>
#include <iostream>

// ── 构造函数 ───────────────────────────────────────────────────
TdSpiImpl::TdSpiImpl(const GatewayConfig& cfg, CThostFtdcTraderApi* td_api)
    : cfg_(cfg), td_api_(td_api) {}

// ── 阻塞等待登录 ───────────────────────────────────────────────
bool TdSpiImpl::wait_for_login(int timeout_secs) {
    std::unique_lock<std::mutex> lock(mtx_);
    return cv_.wait_for(lock,
                        std::chrono::seconds(timeout_secs),
                        [this] { return logged_in_.load(); });
}

// ── 连接建立 ───────────────────────────────────────────────────
void TdSpiImpl::OnFrontConnected() {
    std::cout << "[TdSpi] Front connected\n";
    do_authenticate_or_login();
}

void TdSpiImpl::do_authenticate_or_login() {
    if (!cfg_.appid.empty()) {
        // 有 appid，先发认证请求
        CThostFtdcReqAuthenticateField req{};
        strncpy(req.BrokerID,  cfg_.broker_id.c_str(),  sizeof(req.BrokerID)  - 1);
        strncpy(req.UserID,    cfg_.user_id.c_str(),    sizeof(req.UserID)    - 1);
        strncpy(req.AppID,     cfg_.appid.c_str(),      sizeof(req.AppID)     - 1);
        strncpy(req.AuthCode,  cfg_.authcode.c_str(),   sizeof(req.AuthCode)  - 1);
        int ret = td_api_->ReqAuthenticate(&req, ++req_id_);
        if (ret != 0) {
            std::cerr << "[TdSpi] ReqAuthenticate failed, ret=" << ret << "\n";
        } else {
            std::cout << "[TdSpi] ReqAuthenticate sent\n";
        }
    } else {
        // 无 appid，直接登录
        do_login();
    }
}

void TdSpiImpl::do_login() {
    CThostFtdcReqUserLoginField req{};
    strncpy(req.BrokerID, cfg_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.UserID,   cfg_.user_id.c_str(),   sizeof(req.UserID)   - 1);
    strncpy(req.Password, cfg_.password.c_str(),  sizeof(req.Password) - 1);
    int ret = td_api_->ReqUserLogin(&req, ++req_id_);
    if (ret != 0) {
        std::cerr << "[TdSpi] ReqUserLogin failed, ret=" << ret << "\n";
    } else {
        std::cout << "[TdSpi] ReqUserLogin sent\n";
    }
}

// ── 连接断开 ───────────────────────────────────────────────────
void TdSpiImpl::OnFrontDisconnected(int nReason) {
    logged_in_ = false;
    std::cerr << "[TdSpi] Front disconnected, reason=" << nReason
              << ", TdApi will auto-reconnect...\n";
}

// ── 认证响应 ───────────────────────────────────────────────────
void TdSpiImpl::OnRspAuthenticate(
    CThostFtdcRspAuthenticateField* /*pRspAuth*/,
    CThostFtdcRspInfoField*          pInfo,
    int /*nRequestID*/, bool /*bIsLast*/) {
    if (pInfo && pInfo->ErrorID != 0) {
        std::cerr << "[TdSpi] Authenticate failed, ErrorID=" << pInfo->ErrorID
                  << " Msg=" << (pInfo->ErrorMsg ? pInfo->ErrorMsg : "") << "\n";
        return;
    }
    std::cout << "[TdSpi] Authenticate OK, sending login...\n";
    do_login();
}

// ── 登录响应 ───────────────────────────────────────────────────
void TdSpiImpl::OnRspUserLogin(
    CThostFtdcRspUserLoginField* pLogin,
    CThostFtdcRspInfoField*       pInfo,
    int /*nRequestID*/, bool /*bIsLast*/) {
    if (pInfo && pInfo->ErrorID != 0) {
        std::cerr << "[TdSpi] Login failed, ErrorID=" << pInfo->ErrorID
                  << " Msg=" << (pInfo->ErrorMsg ? pInfo->ErrorMsg : "") << "\n";
        return;
    }
    const char* day = (pLogin && pLogin->TradingDay[0]) ? pLogin->TradingDay : "N/A";
    std::cout << "[TdSpi] Login OK, TradingDay=" << day
              << " FrontID=" << (pLogin ? pLogin->FrontID : 0)
              << " SessionID=" << (pLogin ? pLogin->SessionID : 0) << "\n";
    {
        std::lock_guard<std::mutex> lock(mtx_);
        logged_in_ = true;
    }
    cv_.notify_all();
}

// ── 登出响应 ───────────────────────────────────────────────────
void TdSpiImpl::OnRspUserLogout(
    CThostFtdcUserLogoutField* /*pUserLogout*/,
    CThostFtdcRspInfoField*     pInfo,
    int /*nRequestID*/, bool /*bIsLast*/) {
    logged_in_ = false;
    if (pInfo && pInfo->ErrorID != 0) {
        std::cerr << "[TdSpi] Logout error, ErrorID=" << pInfo->ErrorID << "\n";
    } else {
        std::cout << "[TdSpi] Logged out\n";
    }
}
