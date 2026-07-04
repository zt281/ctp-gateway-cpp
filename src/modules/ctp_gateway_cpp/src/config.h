#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// 安全字符串：析构时显式清零内存，防止密码残留在堆上
class secure_string {
public:
    secure_string() = default;
    secure_string(const char* s) : data_(s ? s : "") {}
    secure_string(const std::string& s) : data_(s) {}
    secure_string(std::string&& s) : data_(std::move(s)) {}
    ~secure_string() { clear(); }

    secure_string(const secure_string&) = default;
    secure_string& operator=(const secure_string&) = default;
    secure_string(secure_string&& other) noexcept : data_(std::move(other.data_)) {}
    secure_string& operator=(secure_string&& other) noexcept {
        if (this != &other) {
            clear();
            data_ = std::move(other.data_);
        }
        return *this;
    }

    const char* c_str() const { return data_.c_str(); }
    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }

    secure_string& operator=(const char* s) {
        clear();
        data_ = s ? s : "";
        return *this;
    }
    secure_string& operator=(const std::string& s) {
        clear();
        data_ = s;
        return *this;
    }

    void clear() {
        if (!data_.empty()) {
            std::fill(data_.begin(), data_.end(), '\0');
        }
        data_.clear();
    }

private:
    std::string data_;
};

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
    secure_string password;
    std::string appid;
    std::string authcode;

    // DLL/SO 动态加载
    std::string dll_dir;  // DLL 所在目录路径（必须为绝对路径）
    std::string md_dll;   // MD DLL 文件名（空=自动推断）
    std::string td_dll;   // TD DLL 文件名（空=自动推断）

    // 订阅配置：每实例一个交易所，格式 {"SHFE": ["ag", "au"]}
    std::map<std::string, std::vector<std::string>> underlyings;

    // 重连与超时
    int reconnect_interval_secs  = 5;
    int static_data_timeout_secs = 15;

    // ── SHM 调优参数 ──
    struct ShmTuning {
        uint32_t ring_buffer_capacity = 65536;   // RingBuffer 容量
        uint32_t shm_slot_count       = 2048;    // SHM 队列槽位数
        uint32_t shm_max_msg_size     = 4096;    // SHM 单槽最大消息字节数
        uint32_t dispatch_batch_size  = 64;      // option_dispatch_loop 批量弹出数
        uint32_t spin_threshold       = 1000;    // 自适应自旋阈值
        uint32_t yield_threshold      = 10000;   // 自适应让出阈值
        uint32_t sleep_us             = 100;     // 自适应休眠微秒数
    };
    ShmTuning shm_tuning;

    // ── 共享内存配置（DLL 模式）──
    std::string shm_queue_name;                           // SHM 队列名（DLL 模式由引擎传入）
    size_t shm_buffer_size = 1048576;                     // SHM 缓冲区大小（默认 1MB）
    bool use_shared_memory = false;                       // 启用 SHM 通信
    bool enable_zmq_side_channel = false;                 // SHM 模式下启用 ZMQ job/control 通道
    int zmq_connect_retries = 30;                         // ZMQ side channel 注册重试次数
    int zmq_connect_retry_interval_ms = 500;              // ZMQ side channel 重试间隔
    std::vector<std::string> pre_resolved_instruments;    // 预解析合约列表（DLL 模式）
    std::vector<std::string> pre_resolved_option_instruments; // 预解析期权合约列表

    // 从 JSON 文件加载配置
    static GatewayConfig from_file(const std::string& path);
};
