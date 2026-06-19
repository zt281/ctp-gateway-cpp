#pragma once
#include "quote_tick.h"

#include <cstdint>

// Maximum allowed price jump ratio outside limit bounds
constexpr double MAX_PRICE_JUMP_RATIO = 0.10;

// QuoteValidator — validates incoming quote ticks for data quality anomalies.
// All checks are stateless; prev tick state is passed in from the caller.
class QuoteValidator {
public:
    // Validate a quote tick against its predecessor.
    // @param current  The incoming tick to validate
    // @param prev     The previous tick for the same instrument (may be null)
    // @return true if tick passes all checks, false if anomaly detected
    static bool validate(const QuoteTick& current, const QuoteTick* prev) {
        // First tick (no predecessor) — pass through
        if (prev == nullptr || prev->last_price == 0.0) {
            return true;
        }

        // Cross-day detection: volume decrease across trading day boundary is allowed
        if (current.trading_day[0] != '\0' && prev->trading_day[0] != '\0') {
            if (std::strncmp(current.trading_day, prev->trading_day,
                            sizeof(current.trading_day)) != 0) {
                // Different trading day — cross-day volume decrease is allowed
                return true;
            }
        }

        // Price jump >10% outside limit-up/limit-down bounds -> invalid
        if (!is_price_within_limits(current, *prev)) {
            return false;
        }

        // Timestamp regression -> invalid
        if (is_timestamp_regression(current, *prev)) {
            return false;
        }

        // Same-day volume decrease -> invalid
        if (current.volume < prev->volume) {
            return false;
        }

        return true;
    }

private:
    // Check if price jump exceeds MAX_PRICE_JUMP_RATIO outside limit bounds.
    // Price at exact limit is allowed (passes).
    static bool is_price_within_limits(const QuoteTick& current,
                                       const QuoteTick& prev) {
        if (prev.last_price <= 0.0) {
            return true;  // No prior price to compare
        }

        const double prev_limit_low = prev.lower_limit_price;
        const double prev_limit_high = prev.upper_limit_price;

        // If previous tick had no limits defined, allow through
        if (prev_limit_low <= 0.0 || prev_limit_high <= 0.0) {
            return true;
        }

        // Check if previous price was at or outside the limits
        const bool prev_was_at_limit = (prev.last_price <= prev_limit_low ||
                                        prev.last_price >= prev_limit_high);

        if (prev_was_at_limit) {
            // Previous price was at limit — allow current to stay within new limits
            return true;
        }

        // Normal case: check price jump ratio
        const double price_diff = current.last_price - prev.last_price;
        const double price_change_ratio =
            (price_diff >= 0.0) ? price_diff / prev.last_price
                                : -price_diff / prev.last_price;

        if (price_change_ratio > MAX_PRICE_JUMP_RATIO) {
            // Price jumped more than 10% — check if still within limits
            const double current_limit_low = current.lower_limit_price;
            const double current_limit_high = current.upper_limit_price;

            // Allow if price is within current tick's limit bounds
            return (current.last_price >= current_limit_low &&
                    current.last_price <= current_limit_high);
        }

        return true;
    }

    // Check for timestamp regression using combined update_time + update_millisec.
    static bool is_timestamp_regression(const QuoteTick& current,
                                        const QuoteTick& prev) {
        // Compare trading day first — regression only matters within same day
        if (current.trading_day[0] != '\0' && prev.trading_day[0] != '\0') {
            if (std::strncmp(current.trading_day, prev.trading_day,
                            sizeof(current.trading_day)) != 0) {
                return false;  // Cross-day, not a regression
            }
        }

        // Parse update_time: HH:MM:SS format (8 chars + null)
        int cur_h = 0, cur_m = 0, cur_s = 0;
        int prev_h = 0, prev_m = 0, prev_s = 0;

        // Current tick time
        if (std::sscanf(current.update_time, "%d:%d:%d",
                       &cur_h, &cur_m, &cur_s) != 3) {
            return false;  // Invalid format, skip check
        }

        // Previous tick time
        if (std::sscanf(prev.update_time, "%d:%d:%d",
                       &prev_h, &prev_m, &prev_s) != 3) {
            return false;  // Invalid format, skip check
        }

        // Convert to milliseconds of day for comparison
        const uint64_t cur_ms = (static_cast<uint64_t>(cur_h) * 3600000 +
                                 static_cast<uint64_t>(cur_m) * 60000 +
                                 static_cast<uint64_t>(cur_s) * 1000 +
                                 static_cast<uint64_t>(current.update_millisec));

        const uint64_t prev_ms = (static_cast<uint64_t>(prev_h) * 3600000 +
                                  static_cast<uint64_t>(prev_m) * 60000 +
                                  static_cast<uint64_t>(prev_s) * 1000 +
                                  static_cast<uint64_t>(prev.update_millisec));

        return cur_ms < prev_ms;
    }
};