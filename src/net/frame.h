/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Wire frame codec for networked play (PLAN.md §2.2). Transport-agnostic: no
 * sockets, no LVGL, no threads -- just byte encode/decode so it can run over
 * BLE and LoRa transports alike without change (PLAN.md 2.2 / 7). Frame layout (all multi-byte integers big-endian):
 *
 *   [MAGIC(2B)] [ver(1B)] [type(1B)] [seq(1B)] [len(1B)] [payload(len B)] [CRC16(2B)]
 *
 * CRC16 covers everything from MAGIC through the end of payload (i.e. the
 * whole frame except the CRC field itself), so header corruption is caught
 * exactly like payload corruption. Payload is capped at kMaxPayloadLen bytes
 * (len is a single byte) -- comfortably above what any MessageType below
 * needs (a handful of bytes each), keeping frames tiny for a future
 * low-bandwidth LoRa transport.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace battleship::net {

/// Fixed 2-byte magic marking the start of a frame, chosen to be unlikely to
/// occur naturally and to give the stream resynchronizer (see
/// FrameDecoder::feed) a distinctive byte pair to hunt for after corruption.
constexpr uint8_t kMagicByte0 = 0xB5;
constexpr uint8_t kMagicByte1 = 0x17;

/// Wire protocol version. Bump when the frame layout or MessageType payload
/// shapes change incompatibly.
constexpr uint8_t kProtocolVersion = 1;

/// Message types exchanged between peers (PLAN.md §2.2).
enum class MessageType : uint8_t {
    Hello = 1,   ///< protocol version + random tie-breaker (who goes first)
    Ready = 2,   ///< sender has finished fleet placement
    Shot = 3,    ///< SHOT(x, y) fired at the receiver's board
    Result = 4,  ///< RESULT for a previously-sent Shot (outcome + ship type)
    GameOver = 5,///< match has ended; payload carries the winner
    Ack = 6,     ///< acknowledges receipt of a specific seq (reliability layer)
    Ping = 7,    ///< liveness probe, sent periodically, expects an Ack
};

/// Fixed header sizes/limits.
constexpr size_t kHeaderLen = 6;   // MAGIC(2) + ver(1) + type(1) + seq(1) + len(1)
constexpr size_t kCrcLen = 2;
constexpr size_t kMaxPayloadLen = 255;
constexpr size_t kMaxFrameLen = kHeaderLen + kMaxPayloadLen + kCrcLen;

/// A single decoded frame. `payload` excludes the header/CRC.
struct Frame {
    uint8_t version = kProtocolVersion;
    MessageType type = MessageType::Ping;
    uint8_t seq = 0;
    std::vector<uint8_t> payload;
};

/// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), computed over `data`.
uint16_t crc16(const uint8_t* data, size_t len);

/// Encodes `frame` into a fresh byte buffer ready to hand to an ITransport.
/// Truncates (never throws) if payload exceeds kMaxPayloadLen -- callers are
/// expected to keep MessageType payloads a few bytes long (PLAN.md §2.2), so
/// this should never trigger in practice; kept as a defensive clamp.
std::vector<uint8_t> encode_frame(const Frame& frame);

/// Incremental, resynchronizing frame decoder for a byte stream. Feed it
/// bytes as they arrive (from any ITransport, in any chunking) and it
/// extracts complete, CRC-valid frames as they become available. On CRC
/// failure or a malformed length, it discards one byte and re-scans for the
/// next MAGIC occurrence rather than giving up on the whole stream -- this is
/// what makes it resilient to a transport that can split/duplicate/corrupt
/// bytes (see LoopbackTransport's fault injection and, eventually, a lossy
/// LoRa transport).
class FrameDecoder {
public:
    /// Appends `data` (len bytes) to the internal buffer.
    void feed(const uint8_t* data, size_t len);
    void feed(const std::vector<uint8_t>& data) { feed(data.data(), data.size()); }

    /// Attempts to extract the next complete, valid frame from the buffered
    /// bytes. Returns std::nullopt if no complete valid frame is available
    /// yet (more bytes needed) -- callers should stop polling until more data
    /// arrives. May consume/discard leading garbage bytes internally while
    /// searching for a valid frame; call repeatedly to drain multiple queued
    /// frames.
    std::optional<Frame> try_extract();

    /// Number of bytes currently buffered (diagnostic/testing use).
    [[nodiscard]] size_t buffered_bytes() const { return buffer_.size(); }

    /// Number of frames rejected due to CRC mismatch since construction
    /// (diagnostic/testing use, e.g. asserting corruption was actually
    /// detected in tests).
    [[nodiscard]] int crc_failures() const { return crc_failures_; }

private:
    std::vector<uint8_t> buffer_;
    int crc_failures_ = 0;
};

}  // namespace battleship::net
