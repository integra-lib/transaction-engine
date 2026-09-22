#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace integra
{

inline constexpr std::size_t TRANSACTION_MAX_PAYLOAD_LEN = 64U;

// Wire header: txnId(4) + type(1) + timestamp(4) + payloadLen(2).
inline constexpr std::size_t TRANSACTION_HEADER_LEN   = 11U;
inline constexpr std::size_t TRANSACTION_CHECKSUM_LEN = 2U;
inline constexpr std::size_t TRANSACTION_MAX_FRAME_LEN =
    TRANSACTION_HEADER_LEN + TRANSACTION_MAX_PAYLOAD_LEN + TRANSACTION_CHECKSUM_LEN;

struct TransactionFrame
{
    std::uint32_t txnId{};
    std::uint8_t type{};
    std::uint32_t timestamp{};
    std::uint16_t payloadLen{};
    std::array<std::uint8_t, TRANSACTION_MAX_PAYLOAD_LEN> payload{};
};

// Reserved `type` value for a transaction-layer ACK frame, sent by the
// receiver in reply to any processed transaction. Not a business command code.
inline constexpr std::uint8_t TRANSACTION_ACK_TYPE = 0xFFU;

// Encodes frame into out. Returns the number of bytes written, or 0 if
// frame.payloadLen exceeds TRANSACTION_MAX_PAYLOAD_LEN or out is too small.
[[nodiscard]] std::size_t EncodeTransactionFrame(const TransactionFrame& frame, std::span<std::uint8_t> out);

// Decodes in into outFrame. Returns false on checksum mismatch, truncation,
// or an inconsistent length field.
[[nodiscard]] bool DecodeTransactionFrame(std::span<const std::uint8_t> in, TransactionFrame& outFrame);

} // namespace integra
