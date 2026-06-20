#include "td_spi.h"
#include "gateway_log.h"
#include <chrono>
#include <cstring>
#include <string.h>  // for strnlen (MSVC compat)

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>  // for SecureZeroMemory
#endif

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
    LOG_INFO("Front connected");
    do_authenticate_or_login();
}

void TdSpiImpl::do_authenticate_or_login() {
    if (!td_api_) {
        LOG_ERROR("td_api_ is null, cannot authenticate or login");
        return;
    }
    if (!cfg_.appid.empty()) {
        // 有 appid，先发认证请求
        CThostFtdcReqAuthenticateField req{};
        safe_copy(req.BrokerID, sizeof(req.BrokerID), cfg_.broker_id.c_str());
        safe_copy(req.UserID,   sizeof(req.UserID),   cfg_.user_id.c_str());
        safe_copy(req.AppID,    sizeof(req.AppID),    cfg_.appid.c_str());
        safe_copy(req.AuthCode, sizeof(req.AuthCode), cfg_.authcode.c_str());
        int ret = td_api_->ReqAuthenticate(&req, ++req_id_);
        zero_password_field(req.AuthCode, sizeof(req.AuthCode));
        if (ret != 0) {
            LOG_ERROR("ReqAuthenticate failed, ret=%d", ret);
        } else {
            LOG_INFO("ReqAuthenticate sent");
        }
    } else {
        // 无 appid，直接登录
        do_login();
    }
}

void TdSpiImpl::do_login() {
    if (!td_api_) {
        LOG_ERROR("td_api_ is null, cannot login");
        return;
    }
    CThostFtdcReqUserLoginField req{};
    safe_copy(req.BrokerID, sizeof(req.BrokerID), cfg_.broker_id.c_str());
    safe_copy(req.UserID,   sizeof(req.UserID),   cfg_.user_id.c_str());
    safe_copy(req.Password, sizeof(req.Password), cfg_.password.c_str());
    int ret = td_api_->ReqUserLogin(&req, ++req_id_);
    zero_password_field(req.Password, sizeof(req.Password));
    if (ret != 0) {
        LOG_ERROR("ReqUserLogin failed, ret=%d", ret);
    } else {
        LOG_INFO("ReqUserLogin sent");
    }
}

// ── 连接断开 ───────────────────────────────────────────────────
void TdSpiImpl::OnFrontDisconnected(int nReason) {
    logged_in_ = false;
    LOG_ERROR("Front disconnected, reason=%d, TdApi will auto-reconnect...", nReason);
}

// ── 认证响应 ───────────────────────────────────────────────────
void TdSpiImpl::OnRspAuthenticate(
    CThostFtdcRspAuthenticateField* /*pRspAuth*/,
    CThostFtdcRspInfoField*          pInfo,
    int /*nRequestID*/, bool /*bIsLast*/) {
    if (pInfo && pInfo->ErrorID != 0) {
        LOG_ERROR("Authenticate failed, ErrorID=%d Msg=%s",
                  pInfo->ErrorID,
                  pInfo->ErrorMsg ? pInfo->ErrorMsg : "");
        return;
    }
    LOG_INFO("Authenticate OK, sending login...");
    do_login();
}

// ── 登录响应 ───────────────────────────────────────────────────
void TdSpiImpl::OnRspUserLogin(
    CThostFtdcRspUserLoginField* pLogin,
    CThostFtdcRspInfoField*       pInfo,
    int /*nRequestID*/, bool /*bIsLast*/) {
    if (pInfo && pInfo->ErrorID != 0) {
        LOG_ERROR("Login failed, ErrorID=%d Msg=%s",
                  pInfo->ErrorID,
                  pInfo->ErrorMsg ? pInfo->ErrorMsg : "");
        return;
    }
    std::string day = (pLogin && pLogin->TradingDay[0])
        ? std::string(pLogin->TradingDay, strnlen(pLogin->TradingDay, sizeof(pLogin->TradingDay)))
        : "N/A";
    LOG_INFO("Login OK, TradingDay=%s FrontID=%d SessionID=%d",
             day.c_str(),
             pLogin ? pLogin->FrontID : 0,
             pLogin ? pLogin->SessionID : 0);
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
        LOG_ERROR("Logout error, ErrorID=%d", pInfo->ErrorID);
    } else {
        LOG_INFO("Logged out");
    }
}