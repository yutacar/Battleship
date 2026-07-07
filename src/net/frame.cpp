/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "net/frame.h"

#include <algorithm>
#include <string>  // std::string::npos (kept explicit: libstdc++ does not provide it transitively)

namespace battleship::net {

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

std::vector<uint8_t> encode_frame(const Frame& frame) {
    const size_t payload_len = std::min(frame.payload.size(), kMaxPayloadLen);

    std::vector<uint8_t> out;
    out.reserve(kHeaderLen + payload_len + kCrcLen);
    out.push_back(kMagicByte0);
    out.push_back(kMagicByte1);
    out.push_back(frame.version);
    out.push_back(static_cast<uint8_t>(frame.type));
    out.push_back(frame.seq);
    out.push_back(static_cast<uint8_t>(payload_len));
    out.insert(out.end(), frame.payload.begin(), frame.payload.begin() + static_cast<long>(payload_len));

    const uint16_t crc = crc16(out.data(), out.size());
    out.push_back(static_cast<uint8_t>(crc >> 8));
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
    return out;
}

void FrameDecoder::feed(const uint8_t* data, size_t len) {
    buffer_.insert(buffer_.end(), data, data + len);
}

std::optional<Frame> FrameDecoder::try_extract() {
    while (true) {
        // Search for MAGIC. Discard any leading bytes that don't match --
        // this is the resynchronization behavior: a corrupted/misaligned
        // stream self-heals as soon as a genuine MAGIC pair reappears.
        size_t magic_pos = std::string::npos;
        for (size_t i = 0; i + 1 < buffer_.size(); ++i) {
            if (buffer_[i] == kMagicByte0 && buffer_[i + 1] == kMagicByte1) {
                magic_pos = i;
                break;
            }
        }
        if (magic_pos == std::string::npos) {
            // No magic found. Keep at most the last byte (it might be the
            // first half of a MAGIC pair split across feed() calls).
            if (buffer_.size() > 1) {
                buffer_.erase(buffer_.begin(), buffer_.end() - 1);
            }
            return std::nullopt;
        }
        if (magic_pos > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(magic_pos));
        }

        // Need at least the fixed header to read the length field.
        if (buffer_.size() < kHeaderLen) {
            return std::nullopt;
        }

        const uint8_t len = buffer_[5];
        const size_t frame_len = kHeaderLen + len + kCrcLen;
        if (buffer_.size() < frame_len) {
            return std::nullopt;  // wait for more bytes
        }

        const uint16_t expected_crc = crc16(buffer_.data(), kHeaderLen + len);
        const uint16_t actual_crc = (static_cast<uint16_t>(buffer_[kHeaderLen + len]) << 8) |
                                     static_cast<uint16_t>(buffer_[kHeaderLen + len + 1]);
        if (expected_crc != actual_crc) {
            // Corrupt frame (or we locked onto a MAGIC-like byte pair that
            // wasn't really a frame start). Drop just the magic bytes and
            // resume scanning -- this lets us recover mid-stream instead of
            // discarding everything buffered so far.
            ++crc_failures_;
            buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
            continue;
        }

        Frame frame;
        frame.version = buffer_[2];
        frame.type = static_cast<MessageType>(buffer_[3]);
        frame.seq = buffer_[4];
        frame.payload.assign(buffer_.begin() + kHeaderLen, buffer_.begin() + static_cast<long>(kHeaderLen + len));

        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(frame_len));
        return frame;
    }
}

}  // namespace battleship::net
