// dll_entry.cpp — CTP Gateway DLL/SO entry point.
//
// Implements the Tyche module interface (module_interface.h) for loading
// ctp_gateway_cpp as a shared library via SharedMemoryBridge.
//
// Lifecycle:
//   tyche_module_init() → create CtpGateway, open SHM queue
//   tyche_module_run()  → start gateway, block until stop
//   tyche_module_stop() → signal shutdown

#include "tyche/cpp/engine/module_interface.h"
#include "tyche/cpp/engine/shared_memory_queue.h"
#include "ctp_gateway.h"
#include "config.h"
#include "gateway_log.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// ── 全局状态 ──────────────────────────────────────────────────
static std::unique_ptr<CtpGateway> g_gateway;
static std::unique_ptr<tyche::SharedMemoryQueue> g_queue;
static std::atomic<bool> g_running{false};

// ── 配置文件路径解析 ─────────────────────────────────────────
// 优先级：
//   1. 环境变量 CTP_GATEWAY_CONFIG
//   2. 当前工作目录下的 ctp_gateway.json（DLL 模式下 cwd 是引擎工作目录）
static std::string resolve_config_path() {
    // 1. 环境变量
    const char* env_path = std::getenv("CTP_GATEWAY_CONFIG");
    if (env_path && env_path[0] != '\0') {
        return std::string(env_path);
    }

    // 2. 约定路径
    return "ctp_gateway.json";
}

// ── 接口声明 JSON ────────────────────────────────────────────
static const char* INTERFACES_JSON =
    R"([{"name":"send_quote","pattern":"send","event_type":"quote"},)"
    R"({"name":"send_compute_greeks","pattern":"send","event_type":"compute_greeks"}])";

extern "C" {

// ── tyche_module_init ─────────────────────────────────────────
int tyche_module_init(const char* shm_queue_name) {
    try {
        std::cerr << "[CtpGateway-DLL] init with queue: " << shm_queue_name << std::endl;

        // 1. 加载配置
        std::string config_path = resolve_config_path();
        GatewayConfig cfg = GatewayConfig::from_file(config_path);
        cfg.use_shared_memory = true;
        cfg.shm_queue_name = shm_queue_name ? shm_queue_name : "";

        std::cerr << "[CtpGateway-DLL] config loaded: " << config_path << "\n"
                  << "  md_front  = " << cfg.md_front << "\n"
                  << "  td_front  = " << (cfg.td_front.empty() ? "(disabled)" : cfg.td_front) << "\n"
                  << "  family    = " << cfg.family_name << "\n"
                  << "  shm_queue = " << cfg.shm_queue_name << "\n"
                  << "  instruments (pre-resolved) = " << cfg.pre_resolved_instruments.size() << "\n"
                  << "  shm_tuning: slots=" << cfg.shm_tuning.shm_slot_count
                  << " max_msg=" << cfg.shm_tuning.shm_max_msg_size
                  << " ring_cap=" << cfg.shm_tuning.ring_buffer_capacity << "\n"
                  << std::endl;

        // 2. 打开共享内存队列（owner=false，引擎是 owner）
        tyche::SharedMemoryQueue::Config qcfg;
        qcfg.name = cfg.shm_queue_name;
        qcfg.slot_count = cfg.shm_tuning.shm_slot_count;
        qcfg.max_msg_size = cfg.shm_tuning.shm_max_msg_size;
        g_queue = std::make_unique<tyche::SharedMemoryQueue>(qcfg, /*owner=*/false);

        if (!g_queue->is_valid()) {
            std::cerr << "[CtpGateway-DLL] ERROR: failed to open SHM queue: " << cfg.shm_queue_name << std::endl;
            g_queue.reset();
            return 1;
        }

        // 3. 创建 CtpGateway（SHM 模式下不会实际连接 ZMQ）
        g_gateway = std::make_unique<CtpGateway>(cfg);

        // 4. 设置 SHM 队列，启用 SHM 通信模式
        g_gateway->set_shm_queue(g_queue.get());

        std::cerr << "[CtpGateway-DLL] init complete" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[CtpGateway-DLL] init failed: " << e.what() << std::endl;
        g_gateway.reset();
        g_queue.reset();
        return 2;
    } catch (...) {
        std::cerr << "[CtpGateway-DLL] init failed: unknown exception" << std::endl;
        g_gateway.reset();
        g_queue.reset();
        return 3;
    }
}

// ── tyche_module_run ──────────────────────────────────────────
int tyche_module_run(void) {
    if (!g_gateway) {
        std::cerr << "[CtpGateway-DLL] ERROR: run called without successful init" << std::endl;
        return 2;
    }

    std::cerr << "[CtpGateway-DLL] run started" << std::endl;

    try {
        g_running.store(true, std::memory_order_seq_cst);

        // 启动网关（非阻塞）
        g_gateway->start();

        // 阻塞等待 stop 信号
        while (g_running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // 停止网关
        g_gateway->stop();

    } catch (const std::exception& e) {
        std::cerr << "[CtpGateway-DLL] run error: " << e.what() << std::endl;
        g_running.store(false, std::memory_order_seq_cst);
        try { g_gateway->stop(); } catch (...) {}
        return 1;
    } catch (...) {
        std::cerr << "[CtpGateway-DLL] run error: unknown exception" << std::endl;
        g_running.store(false, std::memory_order_seq_cst);
        try { g_gateway->stop(); } catch (...) {}
        return 1;
    }

    std::cerr << "[CtpGateway-DLL] run exiting" << std::endl;
    return 0;
}

// ── tyche_module_stop ─────────────────────────────────────────
void tyche_module_stop(void) {
    std::cerr << "[CtpGateway-DLL] stop called" << std::endl;
    g_running.store(false, std::memory_order_seq_cst);
}

// ── tyche_module_get_interfaces ───────────────────────────────
const char* tyche_module_get_interfaces(void) {
    return INTERFACES_JSON;
}

// ── tyche_module_version ──────────────────────────────────────
const char* tyche_module_version(void) {
    return TYCHE_MODULE_ABI_VERSION;  // "1.0" from module_interface.h
}

} // extern "C"
