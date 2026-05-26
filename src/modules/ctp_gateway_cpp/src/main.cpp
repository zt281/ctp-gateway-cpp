#include "ctp_gateway.h"
#include "config.h"
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

// ── 全局关闭标志与网关指针 ────────────────────────────────────
static std::atomic<bool> g_shutdown{false};
static CtpGateway*       g_gateway = nullptr;

// ── 信号/控制台事件处理 ───────────────────────────────────────
#ifdef _WIN32
BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        std::cout << "\n[CtpGateway] Shutdown signal received\n";
        g_shutdown.store(true);
        if (g_gateway) g_gateway->stop();
        return TRUE;
    }
    return FALSE;
}
#else
static void signal_handler(int /*sig*/) {
    g_shutdown.store(true);
    if (g_gateway) g_gateway->stop();
}
#endif

// ── 用法提示 ─────────────────────────────────────────────────
static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " --config <path> [OPTIONS]\n"
        << "\n"
        << "Options:\n"
        << "  --config <path>    JSON config file (required)\n"
        << "  --help, -h         Show this help\n"
        << "\n"
        << "Example:\n"
        << "  " << prog << " --config config/gateway_cpp_futures.json\n";
}

// ── main ─────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h")     == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "[CtpGateway] Error: --config requires a path\n";
                return 1;
            }
            config_path = argv[++i];
        }
    }

    if (config_path.empty()) {
        std::cerr << "[CtpGateway] Error: --config is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // ── 注册信号处理器 ────────────────────────────────────────
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    // ── 加载配置并启动 ────────────────────────────────────────
    try {
        GatewayConfig cfg = GatewayConfig::from_file(config_path);

        std::cout << "[CtpGateway] Starting\n"
                  << "  engine  = " << cfg.engine_host << ":" << cfg.engine_port << "\n"
                  << "  md_front= " << cfg.md_front << "\n"
                  << "  td_front= " << (cfg.td_front.empty() ? "(disabled)" : cfg.td_front) << "\n"
                  << "  family  = " << cfg.family_name << "\n";

        CtpGateway gateway(cfg);
        g_gateway = &gateway;

        // run() 阻塞直到 stop() 被调用
        gateway.run();

        g_gateway = nullptr;
        std::cout << "[CtpGateway] Stopped cleanly\n";

    } catch (const std::exception& ex) {
        std::cerr << "[CtpGateway] Fatal: " << ex.what() << "\n";
        return 2;
    } catch (...) {
        std::cerr << "[CtpGateway] Fatal: unknown exception\n";
        return 3;
    }

    return 0;
}
