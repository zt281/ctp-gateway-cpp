#pragma once

// shm_writer.h -- helper to write events into a SharedMemoryQueue.
//
// Message format on the SHM queue (see module_interface.h):
//   [uint16_t topic_len (little-endian)] [char topic[topic_len]] [uint8_t msgpack_payload[...]]
//
// Header-only. Not thread-safe; caller must synchronize if needed.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <msgpack.hpp>

#include "tyche/cpp/engine/shared_memory_queue.h"
#include "tyche/cpp/message.h"
#include "tyche/cpp/types.h"
#include "quote_tick.h"

// Serialize a tyche::Payload into msgpack bytes (map format).
// Reuses tyche::pack_any() for per-value serialization, which handles
// string, double, int, int64_t, uint64_t, float, bool, nullptr,
// nested Payload, vector<string>, and vector<any>.
inline std::vector<uint8_t> serialize_payload_msgpack(const tyche::Payload& payload) {
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);

    pk.pack_map(static_cast<uint32_t>(payload.size()));
    for (const auto& [key, value] : payload) {
        pk.pack(key);
        tyche::pack_any(pk, value);
    }

    return std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(sbuf.data()),
        reinterpret_cast<const uint8_t*>(sbuf.data()) + sbuf.size());
}

// Write a topic + sender + Payload event into a SharedMemoryQueue.
//
// Builds a tyche::Message (msg_type=EVENT) and serializes it, so the wire
// format matches what ZMQ-mode send_event() produces.  Downstream consumers
// (greeks_engine, etc.) expect Message-format bytes.
//
// Wire format:
//   [uint16_t topic_len LE] [topic bytes] [msgpack-serialized Message]
//
// Returns true on success, false if the queue is full or the message
// exceeds the queue's max_msg_size.
inline bool write_shm_event(tyche::SharedMemoryQueue* queue,
                            const std::string& topic,
                            const std::string& sender,
                            const tyche::Payload& payload) {
    if (!queue) return false;

    // Build tyche::Message (same as TycheModule::send_event in ZMQ mode)
    tyche::Message msg;
    msg.msg_type = tyche::MessageType::EVENT;
    msg.sender   = sender;
    msg.event    = topic;
    msg.payload  = payload;

    // Serialize the full Message to msgpack
    auto msgpack_bytes = tyche::serialize(msg);

    // topic_len must fit in uint16_t
    if (topic.size() > UINT16_MAX) return false;
    auto topic_len = static_cast<uint16_t>(topic.size());

    // Build the wire buffer: 2 + topic_len + msgpack_size
    const size_t header_size = sizeof(uint16_t);
    const size_t total_size  = header_size + topic_len + msgpack_bytes.size();

    std::vector<uint8_t> buffer(total_size);

    // topic_len in little-endian (write raw bytes, already LE on x86/ARM)
    buffer[0] = static_cast<uint8_t>(topic_len & 0xFF);
    buffer[1] = static_cast<uint8_t>((topic_len >> 8) & 0xFF);

    // topic bytes
    if (topic_len > 0) {
        std::memcpy(buffer.data() + header_size, topic.data(), topic_len);
    }

    // msgpack-serialized Message
    if (!msgpack_bytes.empty()) {
        std::memcpy(buffer.data() + header_size + topic_len,
                    msgpack_bytes.data(), msgpack_bytes.size());
    }

    return queue->write(buffer.data(), buffer.size());
}

// Zero-allocation SHM event write using thread-local serialization.
//
// Uses tyche::serialize_tls() to avoid heap allocation. The returned
// BufferView is valid only until the next serialize_tls call on this thread.
// Caller MUST ensure single-threaded access (shm_write_lock_ in CtpGateway).
//
// Wire format identical to write_shm_event():
//   [uint16_t topic_len LE] [topic bytes] [msgpack-serialized Message]
inline bool write_shm_event_tls(tyche::SharedMemoryQueue* queue,
                                 const std::string& topic,
                                 const std::string& sender,
                                 const tyche::Payload& payload) {
    if (!queue) return false;
    if (topic.size() > UINT16_MAX) return false;

    // Build tyche::Message (same as write_shm_event)
    tyche::Message msg;
    msg.msg_type = tyche::MessageType::EVENT;
    msg.sender   = sender;
    msg.event    = topic;
    msg.payload  = payload;

    // Serialize into TLS buffer — zero heap allocation
    auto view = tyche::serialize_tls(msg);
    if (view.size == 0) return false;

    auto topic_len = static_cast<uint16_t>(topic.size());
    const size_t header_size = sizeof(uint16_t);
    const size_t total_size  = header_size + topic_len + view.size;

    // Assemble wire buffer on stack (safe for messages < 4KB)
    // For larger messages, fall back to vector
    constexpr size_t STACK_BUF_SIZE = 4096;
    if (total_size <= STACK_BUF_SIZE) {
        uint8_t buf[STACK_BUF_SIZE];
        buf[0] = static_cast<uint8_t>(topic_len & 0xFF);
        buf[1] = static_cast<uint8_t>((topic_len >> 8) & 0xFF);
        if (topic_len > 0) {
            std::memcpy(buf + header_size, topic.data(), topic_len);
        }
        std::memcpy(buf + header_size + topic_len, view.data, view.size);
        return queue->write(buf, total_size);
    } else {
        // Fallback for oversized messages (should be rare with 4KB max_msg_size)
        std::vector<uint8_t> buffer(total_size);
        buffer[0] = static_cast<uint8_t>(topic_len & 0xFF);
        buffer[1] = static_cast<uint8_t>((topic_len >> 8) & 0xFF);
        if (topic_len > 0) {
            std::memcpy(buffer.data() + header_size, topic.data(), topic_len);
        }
        std::memcpy(buffer.data() + header_size + topic_len, view.data, view.size);
        return queue->write(buffer.data(), buffer.size());
    }
}

// Direct QuoteTick → SHM wire format for "send_compute_greeks" event.
// Skips intermediate tyche::Payload construction, using direct msgpack packing.
// Wire format: [uint16_t topic_len LE] [topic bytes] [msgpack-serialized Message]
//
// The msgpack Message payload fields match tick_to_payload() output exactly.
inline bool write_shm_quote_tick(tyche::SharedMemoryQueue* queue,
                                  const std::string& sender,
                                  const QuoteTick& tick) {
    if (!queue) return false;

    // Topic is fixed: "send_compute_greeks"
    static const std::string TOPIC = "send_compute_greeks";
    auto topic_len = static_cast<uint16_t>(TOPIC.size());

    // Build the tyche::Message with Payload constructed from QuoteTick
    // We still need to build a Payload for Message serialization,
    // but we use serialize_tls to avoid the serialize() heap alloc
    tyche::Payload payload;
    payload["instrument_id"] = std::string(tick.instrument_id_sv());
    payload["exchange_id"]   = std::string(tick.exchange_id);
    payload["last_price"]    = tick.last_price;
    payload["volume"]        = tick.volume;
    payload["bid_price1"]    = tick.bid_price1;
    payload["bid_volume1"]   = tick.bid_volume1;
    payload["ask_price1"]    = tick.ask_price1;
    payload["ask_volume1"]   = tick.ask_volume1;
    payload["upper_limit"]   = tick.upper_limit_price;
    payload["lower_limit"]   = tick.lower_limit_price;
    payload["open_price"]    = tick.open_price;
    payload["high_price"]    = tick.high_price;
    payload["low_price"]     = tick.low_price;
    payload["pre_settle"]    = tick.pre_settle_price;
    payload["open_interest"] = tick.open_interest;
    payload["turnover"]      = tick.turnover;
    payload["update_time"]   = std::string(tick.update_time);
    payload["update_millisec"] = tick.update_millisec;
    payload["trading_day"]   = std::string(tick.trading_day);

    tyche::Message msg;
    msg.msg_type = tyche::MessageType::EVENT;
    msg.sender   = sender;
    msg.event    = TOPIC;
    msg.payload  = std::move(payload);

    // Use TLS serialization — zero heap alloc
    auto view = tyche::serialize_tls(msg);
    if (view.size == 0) return false;

    // Assemble wire buffer on stack
    const size_t header_size = sizeof(uint16_t);
    const size_t total_size  = header_size + topic_len + view.size;

    constexpr size_t STACK_BUF_SIZE = 4096;
    if (total_size <= STACK_BUF_SIZE) {
        uint8_t buf[STACK_BUF_SIZE];
        buf[0] = static_cast<uint8_t>(topic_len & 0xFF);
        buf[1] = static_cast<uint8_t>((topic_len >> 8) & 0xFF);
        std::memcpy(buf + header_size, TOPIC.data(), topic_len);
        std::memcpy(buf + header_size + topic_len, view.data, view.size);
        return queue->write(buf, total_size);
    } else {
        std::vector<uint8_t> buffer(total_size);
        buffer[0] = static_cast<uint8_t>(topic_len & 0xFF);
        buffer[1] = static_cast<uint8_t>((topic_len >> 8) & 0xFF);
        std::memcpy(buffer.data() + header_size, TOPIC.data(), topic_len);
        std::memcpy(buffer.data() + header_size + topic_len, view.data, view.size);
        return queue->write(buffer.data(), buffer.size());
    }
}
