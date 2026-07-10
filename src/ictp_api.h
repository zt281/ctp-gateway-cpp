#pragma once
#include "ThostFtdcMdApi.h"
#include "ThostFtdcTraderApi.h"

// ICtpMdApi — CTP MarketData API abstraction interface.
// Decouples SPI logic from the concrete CTP SDK, enabling offline unit tests.
class ICtpMdApi {
public:
    virtual ~ICtpMdApi() = default;

    virtual void RegisterFront(char* pszFrontAddress) = 0;
    virtual void RegisterSpi(CThostFtdcMdSpi* pSpi) = 0;
    virtual void Init() = 0;
    virtual int Join() = 0;
    virtual void Release() = 0;
    virtual int SubscribeMarketData(char* ppInstrumentID[], int nCount) = 0;
    virtual int ReqUserLogin(CThostFtdcReqUserLoginField* pReqUserLoginField, int nRequestID) = 0;
    virtual int ReqUserLogout(CThostFtdcUserLogoutField* pUserLogout, int nRequestID) = 0;
};

// ICtpTdApi — CTP Trader API abstraction interface.
class ICtpTdApi {
public:
    virtual ~ICtpTdApi() = default;

    virtual void RegisterFront(char* pszFrontAddress) = 0;
    virtual void RegisterSpi(CThostFtdcTraderSpi* pSpi) = 0;
    virtual void Init() = 0;
    virtual int Join() = 0;
    virtual void Release() = 0;
    virtual void SubscribePublicTopic(int nResumeType) = 0;
    virtual void SubscribePrivateTopic(int nResumeType) = 0;
    virtual int ReqAuthenticate(CThostFtdcReqAuthenticateField* pReqAuthenticateField, int nRequestID) = 0;
    virtual int ReqUserLogin(CThostFtdcReqUserLoginField* pReqUserLoginField, int nRequestID) = 0;
    virtual int ReqUserLogout(CThostFtdcUserLogoutField* pUserLogout, int nRequestID) = 0;
};
