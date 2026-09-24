#include <cstddef>
#include <cstdint>
#include <hwlib/algorithms/crc.hpp>
#include <hwlib/communication/transaction_engine/frame.hpp>
#include <hwlib/utilities/bit_ops.hpp>
#include <span>

namespace hwlib::communication
{

namespace
{

constexpr std::size_t HEADER_LEN   = TRANSACTION_HEADER_LEN;
constexpr std::size_t CHECKSUM_LEN = TRANSACTION_CHECKSUM_LEN;

// Wire header field offsets: txnId(4) + type(1) + timestamp(4) + payloadLen(2).
constexpr std::size_t TXN_ID_OFFSET      = 0U;
constexpr std::size_t TYPE_OFFSET        = 4U;
constexpr std::size_t TIMESTAMP_OFFSET   = 5U;
constexpr std::size_t PAYLOAD_LEN_OFFSET = 9U;

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// Internal helpers: callers guarantee offset + field width fits in the span before calling.
void WriteU16Le(std::span<std::uint8_t> out, std::size_t offset, std::uint16_t value)
{
    out[offset]     = hwlib::utilities::GetByteByIndex<0>(value);
    out[offset + 1] = hwlib::utilities::GetByteByIndex<1>(value);
}

void WriteU32Le(std::span<std::uint8_t> out, std::size_t offset, std::uint32_t value)
{
    out[offset]     = hwlib::utilities::GetByteByIndex<0>(value);
    out[offset + 1] = hwlib::utilities::GetByteByIndex<1>(value);
    out[offset + 2] = hwlib::utilities::GetByteByIndex<2>(value);
    out[offset + 3] = hwlib::utilities::GetByteByIndex<3>(value);
}

std::uint16_t ReadU16Le(std::span<const std::uint8_t> in, std::size_t offset)
{
    return hwlib::utilities::AssembleBytes<std::uint16_t>(in[offset], in[offset + 1]);
}

std::uint32_t ReadU32Le(std::span<const std::uint8_t> in, std::size_t offset)
{
    return hwlib::utilities::AssembleBytes<std::uint32_t>(in[offset], in[offset + 1], in[offset + 2], in[offset + 3]);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

} // namespace

std::size_t EncodeTransactionFrame(const TransactionFrame& frame, std::span<std::uint8_t> out)
{
    if (frame.payloadLen > TRANSACTION_MAX_PAYLOAD_LEN)
    {
        return 0U;
    }

    const std::size_t total = HEADER_LEN + frame.payloadLen + CHECKSUM_LEN;
    if (out.size() < total)
    {
        return 0U;
    }

    WriteU32Le(out, TXN_ID_OFFSET, frame.txnId);
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - out.size() >=
    // total, frame.payloadLen <= TRANSACTION_MAX_PAYLOAD_LEN, checked above
    out[TYPE_OFFSET] = frame.type;
    WriteU32Le(out, TIMESTAMP_OFFSET, frame.timestamp);
    WriteU16Le(out, PAYLOAD_LEN_OFFSET, frame.payloadLen);
    for (std::uint16_t i = 0U; i < frame.payloadLen; ++i)
    {
        out[HEADER_LEN + i] = frame.payload[i];
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

    const std::uint16_t crc = hwlib::algorithms::Crc16Ccitt(out.subspan(0U, HEADER_LEN + frame.payloadLen));
    WriteU16Le(out, HEADER_LEN + frame.payloadLen, crc);

    return total;
}

bool DecodeTransactionFrame(std::span<const std::uint8_t> in, TransactionFrame& outFrame)
{
    if (in.size() < HEADER_LEN + CHECKSUM_LEN)
    {
        return false;
    }

    const std::uint16_t payloadLen = ReadU16Le(in, PAYLOAD_LEN_OFFSET);
    if (payloadLen > TRANSACTION_MAX_PAYLOAD_LEN)
    {
        return false;
    }

    const std::size_t total = HEADER_LEN + payloadLen + CHECKSUM_LEN;
    if (in.size() != total)
    {
        return false;
    }

    const std::uint16_t expectedCrc = hwlib::algorithms::Crc16Ccitt(in.subspan(0U, HEADER_LEN + payloadLen));
    const std::uint16_t actualCrc   = ReadU16Le(in, HEADER_LEN + payloadLen);
    if (expectedCrc != actualCrc)
    {
        return false;
    }

    outFrame.txnId = ReadU32Le(in, TXN_ID_OFFSET);
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - in.size() ==
    // total, payloadLen <= TRANSACTION_MAX_PAYLOAD_LEN, checked above
    outFrame.type       = in[TYPE_OFFSET];
    outFrame.timestamp  = ReadU32Le(in, TIMESTAMP_OFFSET);
    outFrame.payloadLen = payloadLen;
    for (std::uint16_t i = 0U; i < payloadLen; ++i)
    {
        outFrame.payload[i] = in[HEADER_LEN + i];
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

    return true;
}

} // namespace hwlib::communication
