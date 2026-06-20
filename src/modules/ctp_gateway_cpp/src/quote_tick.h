#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>

// QuoteTick — POD struct for zero-allocation hot-path quote data.
// Mirrors CThostFtdcDepthMarketDataField with fixed-size char arrays.
//
// Cache-line aligned layout (64 bytes per cache line):
//   - Hot fields (first cache line): instrument_id, last_price, bid_price1, ask_price1, volumes
//   - Cold fields (second cache line): open_interest, turnover, limits, open/high/low/pre_settle
//   - Metadata (third cache line): update_time, exchange_id, trading_day, volume, millisec, ts, seq, flags
//
// Total: 192 bytes (3 cache lines)
//
// NOTE: #pragma pack(push, 1) ensures exact layout; alignas(64) on the struct
// ensures 64-byte alignment when allocated standalone. Do NOT use alignas inside
// pack block (MSVC treats it as overriding pack).

#pragma pack(push, 1)
struct QuoteTick {
    // ── Hot fields — first cache line (64 bytes) ──────────────────────
    char     instrument_id[32];     // 32 bytes
    double   last_price;            // 8 bytes
    double   bid_price1;            // 8 bytes
    double   ask_price1;            // 8 bytes
    int32_t  bid_volume1;            // 4 bytes
    int32_t  ask_volume1;            // 4 bytes
    // Total: 64 bytes

    // ── Cold fields — second cache line (64 bytes) ────────────────────
    double   open_interest;         // 8 bytes
    double   turnover;             // 8 bytes
    double   upper_limit_price;    // 8 bytes
    double   lower_limit_price;    // 8 bytes
    double   open_price;            // 8 bytes
    double   high_price;            // 8 bytes
    double   low_price;             // 8 bytes
    double   pre_settle_price;     // 8 bytes
    // Total: 64 bytes

    // ── Metadata — third cache line (64 bytes) ────────────────────────
    char     update_time[9];       // 9 bytes  (HH:MM:SS + null)
    char     exchange_id[9];       // 9 bytes  (e.g. SHFE + null)
    char     trading_day[9];       // 9 bytes  (YYYYMMDD + null)
    int32_t  volume;                // 4 bytes
    int32_t  update_millisec;      // 4 bytes
    uint64_t receive_ts_ns;        // 8 bytes
    uint32_t sequence;             // 4 bytes
    uint8_t  flags;                 // 1 byte
    uint8_t  _pad[16];             // 16 bytes padding to 64 bytes
    // Total: 64 bytes

    // Return a string_view of instrument_id without constructing a std::string
    std::string_view instrument_id_sv() const {
        size_t len = 0;
        while (len < sizeof(instrument_id) && instrument_id[len] != '\0') {
            ++len;
        }
        return std::string_view(instrument_id, len);
    }
};
#pragma pack(pop)

static_assert(sizeof(QuoteTick) == 192, "QuoteTick must be 192 bytes (3 cache lines)");
static_assert(alignof(QuoteTick) == 1, "QuoteTick pack(1) alignment should be 1");
