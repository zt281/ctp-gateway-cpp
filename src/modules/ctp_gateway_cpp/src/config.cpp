#include "config.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

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
    cfg.password    = gw.value("password",    "");
    cfg.appid       = gw.value("appid",       "");
    cfg.authcode    = gw.value("authcode",    "");
    cfg.dll_dir     = gw.value("dll_dir",     "");
    cfg.md_dll      = gw.value("md_dll",      "");
    cfg.td_dll      = gw.value("td_dll",      "");

    cfg.reconnect_interval_secs  = gw.value("reconnect_interval_secs",  5);
    cfg.static_data_timeout_secs = gw.value("static_data_timeout_secs", 15);

    // underlyings: {"SHFE": ["ag", "au"], "DCE": ["i"]}
    if (gw.contains("underlyings")) {
        for (auto& [exchange, products] : gw["underlyings"].items()) {
            cfg.underlyings[exchange] = products.get<std::vector<std::string>>();
        }
    }

    return cfg;
}
