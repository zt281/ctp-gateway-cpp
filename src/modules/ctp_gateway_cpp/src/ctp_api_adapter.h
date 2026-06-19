#pragma once

// CtpMdApiAdapter / CtpTdApiAdapter — thin wrappers around real CTP API pointers.
//
// These adapt CThostFtdcMdApi / CThostFtdcTraderApi to the ICtpMdApi / ICtpTdApi
// interfaces. All methods are inline and simply forward to the underlying API.

#include "ictp_api.h"
#include "ThostFtdcMdApi.h"
#include "ThostFtdcTraderApi.h"

// ── Market Data API Adapter ────────────────────────────────────
class CtpMdApiAdapter : public ICtpMdApi {
public:
    explicit CtpMdApiAdapter(CThostFtdcMdApi* api) : api_(api) {}

    void RegisterFront(char* pszFrontAddress) override {
        api_->RegisterFront(pszFrontAddress);
    }

    void RegisterSpi(CThostFtdcMdSpi* pSpi) override {
        api_->RegisterSpi(pSpi);
    }

    void Init() override {
        api_->Init();
    }

    int Join() override {
        return api_->Join();
    }

    void Release() override {
        api_->Release();
    }

    int SubscribeMarketData(char* ppInstrumentID[], int nCount) override {
        return api_->SubscribeMarketData(ppInstrumentID, nCount);
    }

    int ReqUserLogin(CThostFtdcReqUserLoginField* pReqUserLoginField,
                     int nRequestID) override {
        return api_->ReqUserLogin(pReqUserLoginField, nRequestID);
    }

    int ReqUserLogout(CThostFtdcUserLogoutField* pUserLogout,
                      int nRequestID) override {
        return api_->ReqUserLogout(pUserLogout, nRequestID);
    }

private:
    CThostFtdcMdApi* api_;
};

// ── Trader API Adapter ─────────────────────────────────────────
class CtpTdApiAdapter : public ICtpTdApi {
public:
    explicit CtpTdApiAdapter(CThostFtdcTraderApi* api) : api_(api) {}

    void RegisterFront(char* pszFrontAddress) override {
        api_->RegisterFront(pszFrontAddress);
    }

    void RegisterSpi(CThostFtdcTraderSpi* pSpi) override {
        api_->RegisterSpi(pSpi);
    }

    void Init() override {
        api_->Init();
    }

    int Join() override {
        return api_->Join();
    }

    void Release() override {
        api_->Release();
    }

    void SubscribePublicTopic(int nResumeType) override {
        api_->SubscribePublicTopic(static_cast<THOST_TE_RESUME_TYPE>(nResumeType));
    }

    void SubscribePrivateTopic(int nResumeType) override {
        api_->SubscribePrivateTopic(static_cast<THOST_TE_RESUME_TYPE>(nResumeType));
    }

    int ReqAuthenticate(CThostFtdcReqAuthenticateField* pReqAuthenticateField,
                        int nRequestID) override {
        return api_->ReqAuthenticate(pReqAuthenticateField, nRequestID);
    }

    int ReqUserLogin(CThostFtdcReqUserLoginField* pReqUserLoginField,
                     int nRequestID) override {
        return api_->ReqUserLogin(pReqUserLoginField, nRequestID);
    }

    int ReqUserLogout(CThostFtdcUserLogoutField* pUserLogout,
                      int nRequestID) override {
        return api_->ReqUserLogout(pUserLogout, nRequestID);
    }

private:
    CThostFtdcTraderApi* api_;
};
