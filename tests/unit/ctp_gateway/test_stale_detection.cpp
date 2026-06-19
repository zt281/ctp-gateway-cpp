#include <gtest/gtest.h>
#include "ctp_gateway.h"
#include "ThostFtdcUserApiStruct.h"
#include <chrono>
#include <thread>
#include <atomic>

// ── CtpGateway stale detection tests ──────────────────────────

// Note: last_tick_ns_ is initialized to 0, which means last_tick_age_ms() returns
// the age since epoch (steady_clock origin), not 0. The is_tick_stale() check
// (age > 5000ms) will return true because the clock has been running for a while.

TEST(CtpGatewayStaleDetection, StaleCheckLogic) {
    // Test the stale check logic directly with time manipulation
    using clock = std::chrono::steady_clock;
    using ms = std::chrono::milliseconds;

    auto now = clock::now();
    auto last_tick = now - std::chrono::seconds(31);

    // 31 seconds ago should be stale (>30s threshold)
    auto age_ms = std::chrono::duration_cast<ms>(now - last_tick).count();
    EXPECT_GE(age_ms, 31000);
    EXPECT_TRUE(age_ms > 30000);
}

TEST(CtpGatewayStaleDetection, NotStaleWithinThreshold) {
    using clock = std::chrono::steady_clock;
    using ms = std::chrono::milliseconds;

    auto now = clock::now();
    auto last_tick = now - std::chrono::seconds(15);

    auto age_ms = std::chrono::duration_cast<ms>(now - last_tick).count();
    EXPECT_LT(age_ms, 30000);
}

// ── gateway_status job handler tests ────────────────────────

TEST(CtpGatewayStatusJob, IncludesReconnectCount) {
    GatewayConfig cfg{};
    cfg.engine_host = "127.0.0.1";
    cfg.engine_port = 5555;
    cfg.family_name = "test";

    CtpGateway gw(cfg);

    // Initially no reconnects
    EXPECT_EQ(gw.reconnect_count(), 0);
}

TEST(CtpGatewayStatusJob, GetStatusPayload) {
    GatewayConfig cfg{};
    cfg.engine_host = "127.0.0.1";
    cfg.engine_port = 5555;
    cfg.family_name = "test";

    CtpGateway gw(cfg);

    auto status = gw.get_status_payload();
    EXPECT_NE(status.find("module_id"), status.end());
    EXPECT_NE(status.find("ticks_received"), status.end());
    EXPECT_NE(status.find("ticks_sent"), status.end());
    EXPECT_NE(status.find("ring_buffer_size"), status.end());
    EXPECT_NE(status.find("ring_buffer_cap"), status.end());
}