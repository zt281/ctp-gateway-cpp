#include <gtest/gtest.h>
#include "quote_tick.h"
#include <cstring>
#include <vector>
#include <unordered_set>
#include <string_view>

// ── QuoteTick struct tests ───────────────────────────────────

TEST(QuoteTick, BasicFieldAccess) {
    QuoteTick tick{};
    std::strncpy(tick.instrument_id, "rb2501", sizeof(tick.instrument_id));
    std::strncpy(tick.exchange_id, "SHFE", sizeof(tick.exchange_id));
    tick.last_price = 3500.5;
    tick.volume = 100;
    tick.bid_price1 = 3500.0;
    tick.ask_price1 = 3501.0;

    EXPECT_EQ(std::string(tick.instrument_id), "rb2501");
    EXPECT_EQ(std::string(tick.exchange_id), "SHFE");
    EXPECT_DOUBLE_EQ(tick.last_price, 3500.5);
    EXPECT_EQ(tick.volume, 100);
}

TEST(QuoteTick, InstrumentIdStringView) {
    QuoteTick tick{};
    std::strncpy(tick.instrument_id, "m2501-C-3000", sizeof(tick.instrument_id));

    std::string_view sv = tick.instrument_id_sv();
    EXPECT_EQ(sv, "m2501-C-3000");
    EXPECT_EQ(sv.size(), 12u);
}

TEST(QuoteTick, EmptyInstrumentIdStringView) {
    QuoteTick tick{};
    std::memset(tick.instrument_id, 0, sizeof(tick.instrument_id));

    std::string_view sv = tick.instrument_id_sv();
    EXPECT_EQ(sv.size(), 0u);
    EXPECT_TRUE(sv.empty());
}

TEST(QuoteTick, SizeIsStable) {
    // sizeof(QuoteTick) must be stable and known for ring buffer sizing
    // Actual size is 192 bytes (3 cache lines, pack(1))
    EXPECT_EQ(sizeof(QuoteTick), 192u);
}

// ── Hash-set option detection tests ────────────────────────────

class OptionDetector {
public:
    std::unordered_set<std::string> option_instrument_set_;

    void populate(const std::vector<std::string>& options) {
        option_instrument_set_.clear();
        for (const auto& id : options) {
            option_instrument_set_.insert(id);
        }
    }

    bool is_option(const std::string& instrument_id) const {
        return option_instrument_set_.count(instrument_id) > 0;
    }

    bool is_option_sv(const QuoteTick& tick) const {
        std::string_view sv = tick.instrument_id_sv();
        return option_instrument_set_.count(std::string(sv)) > 0;
    }
};

TEST(OptionDetection, FutureIsNotOption) {
    OptionDetector detector;
    detector.populate({"m2501-C-3000", "m2501-P-3000"});

    EXPECT_FALSE(detector.is_option("rb2501"));
    EXPECT_FALSE(detector.is_option("m2501"));
}

TEST(OptionDetection, OptionIsDetected) {
    OptionDetector detector;
    detector.populate({"m2501-C-3000", "m2501-P-3000", "SR501C5000"});

    EXPECT_TRUE(detector.is_option("m2501-C-3000"));
    EXPECT_TRUE(detector.is_option("m2501-P-3000"));
    EXPECT_TRUE(detector.is_option("SR501C5000"));
}

TEST(OptionDetection, UnknownInstrumentIsFuture) {
    OptionDetector detector;
    detector.populate({"m2501-C-3000"});

    EXPECT_FALSE(detector.is_option("unknown999"));
    EXPECT_FALSE(detector.is_option(""));
}

TEST(OptionDetection, StringViewLookup) {
    OptionDetector detector;
    detector.populate({"m2501-C-3000", "rb2501_o-C-3500"});

    QuoteTick opt_tick{};
    std::strncpy(opt_tick.instrument_id, "m2501-C-3000", sizeof(opt_tick.instrument_id));
    EXPECT_TRUE(detector.is_option_sv(opt_tick));

    QuoteTick fut_tick{};
    std::strncpy(fut_tick.instrument_id, "rb2501", sizeof(fut_tick.instrument_id));
    EXPECT_FALSE(detector.is_option_sv(fut_tick));
}

TEST(OptionDetection, EmptySetAllAreFutures) {
    OptionDetector detector;
    detector.populate({});

    EXPECT_FALSE(detector.is_option("anything"));
    EXPECT_FALSE(detector.is_option("m2501-C-3000"));
}

TEST(OptionDetection, LargeSetPerformance) {
    OptionDetector detector;
    std::vector<std::string> options;
    for (int i = 0; i < 10000; ++i) {
        options.push_back("opt" + std::to_string(i) + "-C-" + std::to_string(i * 100));
    }
    detector.populate(options);

    // All lookups should be O(1)
    EXPECT_TRUE(detector.is_option("opt5000-C-500000"));
    EXPECT_TRUE(detector.is_option("opt0-C-0"));
    EXPECT_TRUE(detector.is_option("opt9999-C-999900"));
    EXPECT_FALSE(detector.is_option("not_present"));
}

// ── Routing logic tests ────────────────────────────────────────

class QuoteRouter {
public:
    std::unordered_set<std::string> option_instrument_set_;
    std::vector<std::string> sent_quotes_;      // futures
    std::vector<std::string> queued_options_;   // options

    void resolve(const std::vector<std::string>& options) {
        option_instrument_set_.clear();
        for (const auto& id : options) {
            option_instrument_set_.insert(id);
        }
    }

    void on_quote(const QuoteTick& tick) {
        std::string_view sv = tick.instrument_id_sv();
        if (option_instrument_set_.count(std::string(sv)) > 0) {
            queued_options_.emplace_back(sv);
        } else {
            sent_quotes_.emplace_back(sv);
        }
    }
};

TEST(QuoteRouting, FuturesPassThrough) {
    QuoteRouter router;
    router.resolve({"m2501-C-3000", "m2501-P-3000"});

    QuoteTick tick{};
    std::strncpy(tick.instrument_id, "rb2501", sizeof(tick.instrument_id));
    router.on_quote(tick);

    EXPECT_EQ(router.sent_quotes_.size(), 1u);
    EXPECT_EQ(router.sent_quotes_[0], "rb2501");
    EXPECT_TRUE(router.queued_options_.empty());
}

TEST(QuoteRouting, OptionsAreQueued) {
    QuoteRouter router;
    router.resolve({"m2501-C-3000"});

    QuoteTick tick{};
    std::strncpy(tick.instrument_id, "m2501-C-3000", sizeof(tick.instrument_id));
    router.on_quote(tick);

    EXPECT_TRUE(router.sent_quotes_.empty());
    EXPECT_EQ(router.queued_options_.size(), 1u);
    EXPECT_EQ(router.queued_options_[0], "m2501-C-3000");
}

TEST(QuoteRouting, UnknownInstrumentTreatedAsFuture) {
    QuoteRouter router;
    router.resolve({"m2501-C-3000"});

    QuoteTick tick{};
    std::strncpy(tick.instrument_id, "unknown999", sizeof(tick.instrument_id));
    router.on_quote(tick);

    EXPECT_EQ(router.sent_quotes_.size(), 1u);
    EXPECT_EQ(router.sent_quotes_[0], "unknown999");
    EXPECT_TRUE(router.queued_options_.empty());
}

TEST(QuoteRouting, MixedFlow) {
    QuoteRouter router;
    router.resolve({"m2501-C-3000", "m2501-P-3000", "SR501C5000"});

    // Send 3 futures and 2 options
    QuoteTick t1{}; std::strncpy(t1.instrument_id, "rb2501", sizeof(t1.instrument_id));
    QuoteTick t2{}; std::strncpy(t2.instrument_id, "m2501-C-3000", sizeof(t2.instrument_id));
    QuoteTick t3{}; std::strncpy(t3.instrument_id, "cu2501", sizeof(t3.instrument_id));
    QuoteTick t4{}; std::strncpy(t4.instrument_id, "m2501-P-3000", sizeof(t4.instrument_id));
    QuoteTick t5{}; std::strncpy(t5.instrument_id, "al2501", sizeof(t5.instrument_id));

    router.on_quote(t1);
    router.on_quote(t2);
    router.on_quote(t3);
    router.on_quote(t4);
    router.on_quote(t5);

    EXPECT_EQ(router.sent_quotes_.size(), 3u);
    EXPECT_EQ(router.queued_options_.size(), 2u);
}
