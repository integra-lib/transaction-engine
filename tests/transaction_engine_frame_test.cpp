#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <hwlib/communication/transaction_engine.hpp>
#include <span>

namespace
{

hwlib::communication::TransactionFrame MakeFrame(std::uint16_t payloadLen)
{
    hwlib::communication::TransactionFrame frame{};
    frame.txnId      = 0x01020304U;
    frame.type       = 0x42U;
    frame.timestamp  = 0x0A0B0C0DU;
    frame.payloadLen = payloadLen;
    for (std::uint16_t i = 0U; i < payloadLen; ++i)
    {
        frame.payload[i] = static_cast<std::uint8_t>(i);
    }
    return frame;
}

TEST(TransactionFrameTest, EncodesAndDecodesBackToTheSameFrame)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(8U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};

    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_EQ(len, hwlib::communication::TRANSACTION_HEADER_LEN + 8U + hwlib::communication::TRANSACTION_CHECKSUM_LEN);

    hwlib::communication::TransactionFrame decoded{};
    ASSERT_TRUE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
    EXPECT_EQ(decoded.txnId, frame.txnId);
    EXPECT_EQ(decoded.type, frame.type);
    EXPECT_EQ(decoded.timestamp, frame.timestamp);
    EXPECT_EQ(decoded.payloadLen, frame.payloadLen);
    EXPECT_EQ(decoded.payload, frame.payload);
}

TEST(TransactionFrameTest, RoundTripsAnEmptyPayload)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(0U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};

    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_EQ(len, hwlib::communication::TRANSACTION_HEADER_LEN + hwlib::communication::TRANSACTION_CHECKSUM_LEN);

    hwlib::communication::TransactionFrame decoded{};
    ASSERT_TRUE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
    EXPECT_EQ(decoded.payloadLen, 0U);
}

TEST(TransactionFrameTest, RoundTripsAMaximumPayload)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(hwlib::communication::TRANSACTION_MAX_PAYLOAD_LEN);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};

    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_EQ(len, hwlib::communication::TRANSACTION_MAX_FRAME_LEN);

    hwlib::communication::TransactionFrame decoded{};
    ASSERT_TRUE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
    EXPECT_EQ(decoded.payloadLen, hwlib::communication::TRANSACTION_MAX_PAYLOAD_LEN);
    EXPECT_EQ(decoded.payload, frame.payload);
}

TEST(TransactionFrameTest, EncodeRejectsAnOversizedPayload)
{
    hwlib::communication::TransactionFrame frame = MakeFrame(0U);
    frame.payloadLen                             = hwlib::communication::TRANSACTION_MAX_PAYLOAD_LEN + 1U;
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};

    EXPECT_EQ(hwlib::communication::EncodeTransactionFrame(frame, buf), 0U);
}

TEST(TransactionFrameTest, EncodeRejectsATooSmallOutputBuffer)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(8U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_HEADER_LEN> buf{};

    EXPECT_EQ(hwlib::communication::EncodeTransactionFrame(frame, buf), 0U);
}

TEST(TransactionFrameTest, DecodeRejectsACorruptedByte)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    buf[hwlib::communication::TRANSACTION_HEADER_LEN] ^= 0xFFU; // flip a payload byte

    hwlib::communication::TransactionFrame decoded{};
    EXPECT_FALSE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsATruncatedFrame)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    hwlib::communication::TransactionFrame decoded{};
    EXPECT_FALSE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len - 1U}, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsATrailingByte)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    hwlib::communication::TransactionFrame decoded{};
    EXPECT_FALSE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len + 1U}, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsAnInputShorterThanTheHeader)
{
    const std::array<std::uint8_t, 4> tooShort{};
    hwlib::communication::TransactionFrame decoded{};
    EXPECT_FALSE(hwlib::communication::DecodeTransactionFrame(tooShort, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsAnInconsistentLengthField)
{
    const hwlib::communication::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = hwlib::communication::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    buf[9]  = 0xFFU; // payloadLen low byte -> claims far more than is present
    buf[10] = 0xFFU;

    hwlib::communication::TransactionFrame decoded{};
    EXPECT_FALSE(hwlib::communication::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
}

} // namespace
