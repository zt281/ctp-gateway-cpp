#pragma once
#include "ictp_api.h"
#include <vector>
#include <string>
#include <cstring>

// MockMdApi — test double for ICtpMdApi.
// Records all method calls and allows synthetic response injection.
class MockMdApi : public ICtpMdApi {
public:
    struct CallRecord {
        std::string method;
        std::string front_address;
        std::vector<std::string> instruments;
        int request_id = 0;
    };

    // -- ICtpMdApi overrides --
    void RegisterFront(char* pszFrontAddress) override {
        last_call_.method = "RegisterFront";
        last_call_.front_address = pszFrontAddress ? pszFrontAddress : "";
        calls_.push_back(last_call_);
    }

    void RegisterSpi(CThostFtdcMdSpi* pSpi) override {
        last_call_.method = "RegisterSpi";
        spi_ = pSpi;
        calls_.push_back(last_call_);
    }

    void Init() override {
        last_call_.method = "Init";
        calls_.push_back(last_call_);
        if (auto_connect_on_init_) {
            simulate_front_connected();
        }
    }

    int Join() override {
        last_call_.method = "Join";
        calls_.push_back(last_call_);
        return 0;
    }

    void Release() override {
        last_call_.method = "Release";
        calls_.push_back(last_call_);
    }

    int SubscribeMarketData(char* ppInstrumentID[], int nCount) override {
        last_call_.method = "SubscribeMarketData";
        last_call_.instruments.clear();
        for (int i = 0; i < nCount; ++i) {
            if (ppInstrumentID[i]) {
                last_call_.instruments.emplace_back(ppInstrumentID[i]);
            }
        }
        calls_.push_back(last_call_);
        return subscribe_return_value_;
    }

    int ReqUserLogout(CThostFtdcUserLogoutField* /*pUserLogout*/, int nRequestID) override {
        last_call_.method = "ReqUserLogout";
        last_call_.request_id = nRequestID;
        calls_.push_back(last_call_);
        return 0;
    }

    int ReqUserLogin(CThostFtdcReqUserLoginField* pReqUserLoginField,
                     int nRequestID) override {
        last_call_.method = "ReqUserLogin";
        last_call_.request_id = nRequestID;
        calls_.push_back(last_call_);
        if (auto_login_success_ && spi_ && pReqUserLoginField) {
            simulate_login_response(pReqUserLoginField, nRequestID);
        }
        return req_user_login_return_value_;
    }

    // -- Test control --
    void set_auto_connect_on_init(bool v) { auto_connect_on_init_ = v; }
    void set_auto_login_success(bool v) { auto_login_success_ = v; }
    void set_subscribe_return_value(int v) { subscribe_return_value_ = v; }
    void set_req_user_login_return_value(int v) { req_user_login_return_value_ = v; }

    const std::vector<CallRecord>& calls() const { return calls_; }
    const CallRecord& last_call() const { return last_call_; }
    CThostFtdcMdSpi* spi() const { return spi_; }
    void clear_calls() { calls_.clear(); }

    // -- Synthetic event injection --
    void simulate_front_connected() {
        if (spi_) {
            spi_->OnFrontConnected();
        }
    }

    void simulate_login_response(CThostFtdcReqUserLoginField* req, int request_id) {
        if (!spi_) return;
        CThostFtdcRspUserLoginField login{};
        if (req) {
            std::strncpy(login.TradingDay, req->TradingDay, sizeof(login.TradingDay) - 1);
            std::strncpy(login.BrokerID, req->BrokerID, sizeof(login.BrokerID) - 1);
            std::strncpy(login.UserID, req->UserID, sizeof(login.UserID) - 1);
        }
        CThostFtdcRspInfoField info{};
        info.ErrorID = 0;
        spi_->OnRspUserLogin(&login, &info, request_id, true);
    }

    void simulate_depth_market_data(const CThostFtdcDepthMarketDataField& data) {
        if (spi_) {
            spi_->OnRtnDepthMarketData(const_cast<CThostFtdcDepthMarketDataField*>(&data));
        }
    }

private:
    std::vector<CallRecord> calls_;
    CallRecord last_call_;
    CThostFtdcMdSpi* spi_ = nullptr;
    bool auto_connect_on_init_ = false;
    bool auto_login_success_ = false;
    int subscribe_return_value_ = 0;
    int req_user_login_return_value_ = 0;
};

// MockTdApi — test double for ICtpTdApi.
class MockTdApi : public ICtpTdApi {
public:
    struct CallRecord {
        std::string method;
        std::string front_address;
        int request_id = 0;
    };

    void RegisterFront(char* pszFrontAddress) override {
        last_call_.method = "RegisterFront";
        last_call_.front_address = pszFrontAddress ? pszFrontAddress : "";
        calls_.push_back(last_call_);
    }

    void RegisterSpi(CThostFtdcTraderSpi* pSpi) override {
        last_call_.method = "RegisterSpi";
        spi_ = pSpi;
        calls_.push_back(last_call_);
    }

    void Init() override {
        last_call_.method = "Init";
        calls_.push_back(last_call_);
        if (auto_connect_on_init_) {
            simulate_front_connected();
        }
    }

    int Join() override {
        last_call_.method = "Join";
        calls_.push_back(last_call_);
        return 0;
    }

    void Release() override {
        last_call_.method = "Release";
        calls_.push_back(last_call_);
    }

    void SubscribePublicTopic(int nResumeType) override {
        last_call_.method = "SubscribePublicTopic";
        calls_.push_back(last_call_);
    }

    void SubscribePrivateTopic(int nResumeType) override {
        last_call_.method = "SubscribePrivateTopic";
        calls_.push_back(last_call_);
    }

    int ReqUserLogout(CThostFtdcUserLogoutField* pUserLogout, int nRequestID) override {
        last_call_.method = "ReqUserLogout";
        last_call_.request_id = nRequestID;
        calls_.push_back(last_call_);
        return 0;
    }

    int ReqAuthenticate(CThostFtdcReqAuthenticateField* pReqAuthenticateField,
                        int nRequestID) override {
        last_call_.method = "ReqAuthenticate";
        last_call_.request_id = nRequestID;
        calls_.push_back(last_call_);
        if (auto_auth_success_ && spi_ && pReqAuthenticateField) {
            simulate_auth_response(nRequestID);
        }
        return req_authenticate_return_value_;
    }

    int ReqUserLogin(CThostFtdcReqUserLoginField* pReqUserLoginField,
                     int nRequestID) override {
        last_call_.method = "ReqUserLogin";
        last_call_.request_id = nRequestID;
        calls_.push_back(last_call_);
        if (auto_login_success_ && spi_ && pReqUserLoginField) {
            simulate_login_response(pReqUserLoginField, nRequestID);
        }
        return req_user_login_return_value_;
    }

    // -- Test control --
    void set_auto_connect_on_init(bool v) { auto_connect_on_init_ = v; }
    void set_auto_auth_success(bool v) { auto_auth_success_ = v; }
    void set_auto_login_success(bool v) { auto_login_success_ = v; }
    void set_req_authenticate_return_value(int v) { req_authenticate_return_value_ = v; }
    void set_req_user_login_return_value(int v) { req_user_login_return_value_ = v; }

    const std::vector<CallRecord>& calls() const { return calls_; }
    const CallRecord& last_call() const { return last_call_; }
    CThostFtdcTraderSpi* spi() const { return spi_; }
    void clear_calls() { calls_.clear(); }

    // -- Synthetic event injection --
    void simulate_front_connected() {
        if (spi_) {
            spi_->OnFrontConnected();
        }
    }

    void simulate_auth_response(int request_id) {
        if (!spi_) return;
        CThostFtdcRspInfoField info{};
        info.ErrorID = 0;
        spi_->OnRspAuthenticate(nullptr, &info, request_id, true);
    }

    void simulate_login_response(CThostFtdcReqUserLoginField* req, int request_id) {
        if (!spi_) return;
        CThostFtdcRspUserLoginField login{};
        if (req) {
            std::strncpy(login.TradingDay, req->TradingDay, sizeof(login.TradingDay) - 1);
            std::strncpy(login.BrokerID, req->BrokerID, sizeof(login.BrokerID) - 1);
            std::strncpy(login.UserID, req->UserID, sizeof(login.UserID) - 1);
        }
        CThostFtdcRspInfoField info{};
        info.ErrorID = 0;
        spi_->OnRspUserLogin(&login, &info, request_id, true);
    }

private:
    std::vector<CallRecord> calls_;
    CallRecord last_call_;
    CThostFtdcTraderSpi* spi_ = nullptr;
    bool auto_connect_on_init_ = false;
    bool auto_auth_success_ = false;
    bool auto_login_success_ = false;
    int req_authenticate_return_value_ = 0;
    int req_user_login_return_value_ = 0;
};
