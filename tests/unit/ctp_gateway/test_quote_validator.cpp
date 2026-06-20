#include <gtest/gtest.h>
#include "quote_validator.h"
#include <cstring>

// Helper to build a QuoteTick with minimal fields
static QuoteTick make_tick(const char* id, double last_price, double upper, double lower,
                           int volume, const char* time, int millisec, const char* day) {
    QuoteTick t{};
    std::strncpy(t.instrument_id, id, sizeof(t.instrument_id) - 1);
    t.last_price = last_price;
    t.upper_limit_price = upper;
    t.lower_limit_price = lower;
    t.volume = volume;
    std::strncpy(t.update_time, time, sizeof(t.update_time) - 1);
    t.update_millisec = millisec;
    std::strncpy(t.trading_day, day, sizeof(t.trading_day) - 1);
    return t;
}

// ── Normal tick passes ──────────────────────────────────────
TEST(QuoteValidatorTest, NormalTickPasses) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260613");
    QuoteTick curr = make_tick("rb2501", 3505.0, 3800.0, 3200.0, 1005, "10:30:01", 600, "20260613");

    EXPECT_TRUE(validator.validate(curr, &prev));
}

// ── Price jump beyond 10% and outside limits rejected ─────
TEST(QuoteValidatorTest, PriceJumpBeyondLimitsRejected) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260613");
    // Price jump > 10% (from 3500 to 5000 = 42.8%) and above upper limit
    QuoteTick curr = make_tick("rb2501", 5000.0, 3800.0, 3200.0, 1005, "10:30:01", 600, "20260613");

    EXPECT_FALSE(validator.validate(curr, &prev));
}

// ── Price jump within limits allowed ────────────────────────
TEST(QuoteValidatorTest, PriceJumpWithinLimitsAllowed) {
    QuoteValidator validator;
    // 3500 -> 3900 = 11.4% > 10%, within limits (upper=4000, lower=3000)
    QuoteTick prev = make_tick("rb2501", 3500.0, 4000.0, 3000.0, 1000, "10:30:00", 500, "20260613");
    QuoteTick curr = make_tick("rb2501", 3900.0, 4000.0, 3000.0, 1005, "10:30:01", 600, "20260613");

    EXPECT_TRUE(validator.validate(curr, &prev));
}

// ── Timestamp regression rejected ─────────────────────────
TEST(QuoteValidatorTest, TimestampRegressionRejected) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:01", 500, "20260613");
    QuoteTick curr = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1005, "10:30:00", 600, "20260613");

    EXPECT_FALSE(validator.validate(curr, &prev));
}

// ── Volume decrease on same day rejected ──────────────────
TEST(QuoteValidatorTest, VolumeDecreaseSameDayRejected) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260613");
    QuoteTick curr = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 900, "10:30:01", 600, "20260613");

    EXPECT_FALSE(validator.validate(curr, &prev));
}

// ── Volume decrease on cross-day allowed ──────────────────
TEST(QuoteValidatorTest, VolumeDecreaseCrossDayAllowed) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260612");
    QuoteTick curr = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 900, "10:30:01", 600, "20260613");

    EXPECT_TRUE(validator.validate(curr, &prev));
}

// ── No previous tick (first tick) always passes ───────────
TEST(QuoteValidatorTest, FirstTickAlwaysPasses) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 0.0, 0.0, 0.0, 0, "", 0, "");
    QuoteTick curr = make_tick("rb2501", 5000.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260613");

    EXPECT_TRUE(validator.validate(curr, &prev));
}

// ── Price at exact limit allowed ─────────────────────────
TEST(QuoteValidatorTest, PriceAtExactLimitAllowed) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260613");
    QuoteTick curr = make_tick("rb2501", 3800.0, 3800.0, 3200.0, 1005, "10:30:01", 600, "20260613");

    EXPECT_TRUE(validator.validate(curr, &prev));
}

// ── Price below lower limit rejected ─────────────────────
TEST(QuoteValidatorTest, PriceBelowLowerLimitRejected) {
    QuoteValidator validator;
    QuoteTick prev = make_tick("rb2501", 3500.0, 3800.0, 3200.0, 1000, "10:30:00", 500, "20260613");
    QuoteTick curr = make_tick("rb2501", 3100.0, 3800.0, 3200.0, 1005, "10:30:01", 600, "20260613");

    EXPECT_FALSE(validator.validate(curr, &prev));
}
