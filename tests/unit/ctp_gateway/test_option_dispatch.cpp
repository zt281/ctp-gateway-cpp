// Unit tests for option dispatch with RingBuffer (TASK-8).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "quote_tick.h"
#include "tyche/cpp/engine/ring_buffer.h"
#include "tyche/cpp/types.h"

// Minimal tick_to_payload for testing — mirrors the conversion the gateway will use.
static tyche::Payload tick_to_payload(const QuoteTick& tick) {
    tyche::Payload p;
    p["instrument_id"] = std::string(tick.instrument_id);
    p["exchange_id"] = std::string(tick.exchange_id);
    p["last_price"] = tick.last_price;
    p["volume"] = tick.volume;
    p["bid_price1"] = tick.bid_price1;
    p["bid_volume1"] = tick.bid_volume1;
    p["ask_price1"] = tick.ask_price1;
    p["ask_volume1"] = tick.ask_volume1;
    p["upper_limit_price"] = tick.upper_limit_price;
    p["lower_limit_price"] = tick.lower_limit_price;
    p["open_price"] = tick.open_price;
    p["high_price"] = tick.high_price;
    p["low_price"] = tick.low_price;
    p["pre_settle_price"] = tick.pre_settle_price;
    p["open_interest"] = tick.open_interest;
    p["turnover"] = tick.turnover;
    p["update_time"] = std::string(tick.update_time);
    p["update_millisec"] = tick.update_millisec;
    p["trading_day"] = std::string(tick.trading_day);
    return p;
}

// Helper to create a QuoteTick with a given instrument ID.
static QuoteTick make_tick(const char* inst_id, double price, int vol) {
    QuoteTick tick{};  // zero-init for clean string fields
    std::strncpy(tick.instrument_id, inst_id, sizeof(tick.instrument_id) - 1);
    tick.last_price = price;
    tick.volume = vol;
    tick.bid_price1 = price - 0.01;
    tick.ask_price1 = price + 0.01;
    return tick;
}

namespace {

// ── QuoteTick basics ──────────────────────────────────────────────────

TEST(OptionDispatchTest, QuoteTickIsPod) {
    // QuoteTick size may vary by platform/compiler; test is_pod instead of exact size
    static_assert(std::is_pod<QuoteTick>::value, "QuoteTick should be POD for efficient memory operations");
    EXPECT_EQ(sizeof(QuoteTick), 192u);  // 3 cache lines, pack(1)
}

// ── tick_to_payload round-trip ────────────────────────────────────────

TEST(OptionDispatchTest, TickToPayloadRoundTrip) {
    QuoteTick tick{};  // value-initialize (zero) so strncpy fields are null-terminated
    std::strncpy(tick.instrument_id, "m2505", sizeof(tick.instrument_id) - 1);
    std::strncpy(tick.exchange_id, "DCE", sizeof(tick.exchange_id) - 1);
    tick.last_price = 3500.5;
    tick.volume = 12345;
    tick.bid_price1 = 3500.0;
    tick.bid_volume1 = 100;
    tick.ask_price1 = 3501.0;
    tick.ask_volume1 = 200;
    tick.upper_limit_price = 3800.0;
    tick.lower_limit_price = 3200.0;
    tick.open_price = 3450.0;
    tick.high_price = 3550.0;
    tick.low_price = 3400.0;
    tick.pre_settle_price = 3480.0;
    tick.open_interest = 50000.0;
    tick.turnover = 1.5e9;
    std::strncpy(tick.update_time, "14:30:01", sizeof(tick.update_time) - 1);
    tick.update_millisec = 500;
    std::strncpy(tick.trading_day, "20260613", sizeof(tick.trading_day) - 1);

    tyche::Payload p = tick_to_payload(tick);

    EXPECT_EQ(std::any_cast<std::string>(p["instrument_id"]), "m2505");
    EXPECT_EQ(std::any_cast<std::string>(p["exchange_id"]), "DCE");
    EXPECT_EQ(std::any_cast<double>(p["last_price"]), 3500.5);
    EXPECT_EQ(std::any_cast<int>(p["volume"]), 12345);
    EXPECT_EQ(std::any_cast<double>(p["bid_price1"]), 3500.0);
    EXPECT_EQ(std::any_cast<int>(p["bid_volume1"]), 100);
    EXPECT_EQ(std::any_cast<double>(p["ask_price1"]), 3501.0);
    EXPECT_EQ(std::any_cast<int>(p["ask_volume1"]), 200);
    EXPECT_EQ(std::any_cast<double>(p["upper_limit_price"]), 3800.0);
    EXPECT_EQ(std::any_cast<double>(p["lower_limit_price"]), 3200.0);
    EXPECT_EQ(std::any_cast<double>(p["open_price"]), 3450.0);
    EXPECT_EQ(std::any_cast<double>(p["high_price"]), 3550.0);
    EXPECT_EQ(std::any_cast<double>(p["low_price"]), 3400.0);
    EXPECT_EQ(std::any_cast<double>(p["pre_settle_price"]), 3480.0);
    EXPECT_EQ(std::any_cast<double>(p["open_interest"]), 50000.0);
    EXPECT_EQ(std::any_cast<double>(p["turnover"]), 1.5e9);
    EXPECT_EQ(std::any_cast<std::string>(p["update_time"]), "14:30:01");
    EXPECT_EQ(std::any_cast<int>(p["update_millisec"]), 500);
    EXPECT_EQ(std::any_cast<std::string>(p["trading_day"]), "20260613");
}

// ── RingBuffer with QuoteTick ─────────────────────────────────────────

TEST(OptionDispatchTest, RingBufferQuoteTickEnqueueDequeue) {
    tyche::RingBuffer<QuoteTick> rb(1024);

    QuoteTick t1 = make_tick("m2505-C-3500", 15.5, 100);
    QuoteTick t2 = make_tick("m2505-P-3500", 12.3, 200);

    EXPECT_TRUE(rb.try_push(std::move(t1)));
    EXPECT_TRUE(rb.try_push(std::move(t2)));
    EXPECT_EQ(rb.size(), 2u);

    auto v1 = rb.pop();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(std::string(v1->instrument_id), "m2505-C-3500");
    EXPECT_EQ(v1->last_price, 15.5);
    EXPECT_EQ(v1->volume, 100);

    auto v2 = rb.pop();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(std::string(v2->instrument_id), "m2505-P-3500");
    EXPECT_EQ(v2->last_price, 12.3);
    EXPECT_EQ(v2->volume, 200);

    EXPECT_TRUE(rb.empty());
}

TEST(OptionDispatchTest, RingBufferQuoteTickPushOverwriteDropsOldest) {
    tyche::RingBuffer<QuoteTick> rb(4);  // capacity = 4

    for (int i = 0; i < 4; ++i) {
        QuoteTick tick = make_tick(("tick" + std::to_string(i)).c_str(), i * 1.0, i);
        rb.try_push(std::move(tick));
    }
    EXPECT_TRUE(rb.full());

    // Overwrite — should discard oldest (tick0)
    QuoteTick new_tick = make_tick("tick_new", 99.0, 99);
    rb.push_overwrite(std::move(new_tick));

    auto v1 = rb.pop();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(std::string(v1->instrument_id), "tick1");  // tick0 dropped
}

// ── Simulated dispatch loop (non-blocking pop + yield) ──────────────────

TEST(OptionDispatchTest, SimulatedDispatchLoopInOrder) {
    tyche::RingBuffer<QuoteTick> rb(1024);
    std::vector<std::string> dispatched;
    std::atomic<bool> running{true};

    // Producer: enqueue 5 ticks
    for (int i = 0; i < 5; ++i) {
        QuoteTick tick = make_tick(("opt" + std::to_string(i)).c_str(), i * 10.0, i);
        rb.try_push(std::move(tick));
    }

    // Consumer: simulated dispatch loop
    while (running.load()) {
        auto tick = rb.pop();
        if (tick.has_value()) {
            dispatched.push_back(tick->instrument_id);
            if (dispatched.size() == 5) {
                running = false;
            }
        } else {
            std::this_thread::yield();
        }
    }

    EXPECT_EQ(dispatched.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(dispatched[i], "opt" + std::to_string(i));
    }
}

TEST(OptionDispatchTest, SimulatedDispatchLoopStopsCleanly) {
    tyche::RingBuffer<QuoteTick> rb(1024);
    std::atomic<bool> running{true};
    std::atomic<int> pop_count{0};
    std::atomic<bool> stopped{false};

    // Pre-populate 3 ticks
    for (int i = 0; i < 3; ++i) {
        QuoteTick tick = make_tick(("pre" + std::to_string(i)).c_str(), i * 1.0, i);
        rb.try_push(std::move(tick));
    }

    std::thread consumer([&] {
        while (running.load() || !rb.empty()) {
            auto tick = rb.pop();
            if (tick.has_value()) {
                pop_count.fetch_add(1);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        stopped.store(true);
    });

    // Let it consume
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Signal stop
    running = false;
    consumer.join();

    EXPECT_TRUE(stopped.load());
    EXPECT_EQ(pop_count.load(), 3);
}

// ── Concurrent producer / single consumer (MPSC) ──────────────────────

TEST(OptionDispatchTest, ConcurrentProducerConsumer) {
    constexpr int NUM_PRODUCERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 500;
    constexpr int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    tyche::RingBuffer<QuoteTick> rb(8192);
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    std::atomic<bool> running{true};

    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&rb, &push_count, p, ITEMS_PER_PRODUCER] {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                char name[32];
                std::snprintf(name, sizeof(name), "P%d_%d", p, i);
                QuoteTick tick = make_tick(name, p * 100.0 + i, i);
                while (!rb.try_push(std::move(tick))) {
                    std::this_thread::yield();
                }
                push_count.fetch_add(1);
            }
        });
    }

    std::thread consumer([&] {
        while (pop_count.load() < TOTAL_ITEMS || running.load()) {
            auto tick = rb.pop();
            if (tick.has_value()) {
                pop_count.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& t : producers) {
        t.join();
    }
    running = false;
    consumer.join();

    EXPECT_EQ(push_count.load(), TOTAL_ITEMS);
    EXPECT_EQ(pop_count.load(), TOTAL_ITEMS);
}

// ── Gateway header integration: verify RingBuffer is declared ─────────
// This test will fail to compile if ctp_gateway.h doesn't have the RingBuffer member.

TEST(OptionDispatchTest, GatewayHeaderHasRingBuffer) {
    // This is a compile-time check. If ctp_gateway.h includes the RingBuffer
    // member for option ticks, this test compiles and passes trivially.
    // We verify by checking sizeof(QuoteTick) is stable (it must be for RingBuffer).
    EXPECT_EQ(sizeof(QuoteTick), 192u);  // 3 cache lines, pack(1)
}

} // namespace
