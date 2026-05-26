#pragma once
#include <map>
#include <string>
#include <vector>

struct GatewayConfig {
    // TycheEngine 连接
    std::string engine_host = "127.0.0.1";
    int         engine_port = 5555;
    std::string family_name = "ctp_gateway";

    // CTP 连接参数
    std::string md_front;
    std::string td_front;
    std::string broker_id;
    std::string user_id;
    std::string password;
    std::string appid;
    std::string authcode;

    // DLL/SO 动态加载
    std::string dll_dir;  // DLL 所在目录路径
    std::string md_dll;   // MD DLL 文件名（空=自动推断）
    std::string td_dll;   // TD DLL 文件名（空=自动推断）

    // 订阅配置：每实例一个交易所，格式 {"SHFE": ["ag", "au"]}
    std::map<std::string, std::vector<std::string>> underlyings;

    // 重连与超时
    int reconnect_interval_secs  = 5;
    int static_data_timeout_secs = 15;

    // 从 JSON 文件加载配置
    static GatewayConfig from_file(const std::string& path);
};
