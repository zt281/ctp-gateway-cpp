#include "config.h"
#include <fstream>
#include <stdexcept>
#include "nlohmann_json.hpp"

GatewayConfig GatewayConfig::from_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open config: " + path);
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error in " + path + ": " + e.what());
    }

    GatewayConfig cfg;

    // engine 节点（可选，有默认值）
    if (j.contains("engine")) {
        const auto& eng = j["engine"];
        cfg.engine_host = eng.value("host", "127.0.0.1");
        cfg.engine_port = eng.value("port", 5555);
        if (cfg.engine_port <= 0 || cfg.engine_port > 65535) {
            throw std::runtime_error("Config invalid engine.port: "
                                     + std::to_string(cfg.engine_port));
        }
    }

    // gateway 节点（必须）
    if (!j.contains("gateway")) {
        throw std::runtime_error("Config missing 'gateway' section: " + path);
    }
    const auto& gw = j.at("gateway");

    cfg.family_name = gw.value("family_name", "ctp_gateway");
    cfg.md_front    = gw.value("md_front",    "");
    cfg.td_front    = gw.value("td_front",    "");
    cfg.broker_id   = gw.value("broker_id",   "");
    cfg.user_id     = gw.value("user_id",     "");
    cfg.password    = gw.value("password",    "").c_str();
    cfg.appid       = gw.value("appid",       "");
    cfg.authcode    = gw.value("authcode",    "");
    cfg.dll_dir     = gw.value("dll_dir",     "");
    cfg.md_dll      = gw.value("md_dll",      "");
    cfg.td_dll      = gw.value("td_dll",      "");

    cfg.reconnect_interval_secs  = gw.value("reconnect_interval_secs",  5);
    cfg.static_data_timeout_secs = gw.value("static_data_timeout_secs", 15);

    if (cfg.reconnect_interval_secs < 0) {
        throw std::runtime_error("Config invalid reconnect_interval_secs: must be >= 0");
    }
    if (cfg.static_data_timeout_secs <= 0) {
        throw std::runtime_error("Config invalid static_data_timeout_secs: must be > 0");
    }

    // 必填字段校验
    if (cfg.md_front.empty()) {
        throw std::runtime_error("Config missing required field: gateway.md_front");
    }
    if (cfg.broker_id.empty()) {
        throw std::runtime_error("Config missing required field: gateway.broker_id");
    }
    if (cfg.user_id.empty()) {
        throw std::runtime_error("Config missing required field: gateway.user_id");
    }
    if (cfg.password.empty()) {
        throw std::runtime_error("Config missing required field: gateway.password");
    }
    if (cfg.dll_dir.empty()) {
        throw std::runtime_error("Config missing required field: gateway.dll_dir");
    }

    // underlyings: {"SHFE": ["ag", "au"], "DCE": ["i"]}
    if (gw.contains("underlyings")) {
        for (auto& [exchange, products] : gw["underlyings"].items()) {
            cfg.underlyings[exchange] = products.get<std::vector<std::string>>();
        }
    }
    if (cfg.underlyings.empty()) {
        throw std::runtime_error("Config missing required field: gateway.underlyings");
    }

    return cfg;
}
