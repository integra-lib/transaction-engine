#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <integra/transaction_engine.hpp>
#include <span>

namespace
{

integra::TransactionFrame MakeFrame(std::uint16_t payloadLen)
{
    integra::TransactionFrame frame{};
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
    const integra::TransactionFrame frame = MakeFrame(8U);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};

    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_EQ(len, integra::TRANSACTION_HEADER_LEN + 8U + integra::TRANSACTION_CHECKSUM_LEN);

    integra::TransactionFrame decoded{};
    ASSERT_TRUE(integra::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
    EXPECT_EQ(decoded.txnId, frame.txnId);
    EXPECT_EQ(decoded.type, frame.type);
    EXPECT_EQ(decoded.timestamp, frame.timestamp);
    EXPECT_EQ(decoded.payloadLen, frame.payloadLen);
    EXPECT_EQ(decoded.payload, frame.payload);
}

TEST(TransactionFrameTest, RoundTripsAnEmptyPayload)
{
    const integra::TransactionFrame frame = MakeFrame(0U);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};

    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_EQ(len, integra::TRANSACTION_HEADER_LEN + integra::TRANSACTION_CHECKSUM_LEN);

    integra::TransactionFrame decoded{};
    ASSERT_TRUE(integra::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
    EXPECT_EQ(decoded.payloadLen, 0U);
}

TEST(TransactionFrameTest, RoundTripsAMaximumPayload)
{
    const integra::TransactionFrame frame = MakeFrame(integra::TRANSACTION_MAX_PAYLOAD_LEN);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};

    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_EQ(len, integra::TRANSACTION_MAX_FRAME_LEN);

    integra::TransactionFrame decoded{};
    ASSERT_TRUE(integra::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
    EXPECT_EQ(decoded.payloadLen, integra::TRANSACTION_MAX_PAYLOAD_LEN);
    EXPECT_EQ(decoded.payload, frame.payload);
}

TEST(TransactionFrameTest, EncodeRejectsAnOversizedPayload)
{
    integra::TransactionFrame frame = MakeFrame(0U);
    frame.payloadLen                = integra::TRANSACTION_MAX_PAYLOAD_LEN + 1U;
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};

    EXPECT_EQ(integra::EncodeTransactionFrame(frame, buf), 0U);
}

TEST(TransactionFrameTest, EncodeRejectsATooSmallOutputBuffer)
{
    const integra::TransactionFrame frame = MakeFrame(8U);
    std::array<std::uint8_t, integra::TRANSACTION_HEADER_LEN> buf{};

    EXPECT_EQ(integra::EncodeTransactionFrame(frame, buf), 0U);
}

TEST(TransactionFrameTest, DecodeRejectsACorruptedByte)
{
    const integra::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    buf[integra::TRANSACTION_HEADER_LEN] ^= 0xFFU; // flip a payload byte

    integra::TransactionFrame decoded{};
    EXPECT_FALSE(integra::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsATruncatedFrame)
{
    const integra::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    integra::TransactionFrame decoded{};
    EXPECT_FALSE(integra::DecodeTransactionFrame(std::span{buf.data(), len - 1U}, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsATrailingByte)
{
    const integra::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    integra::TransactionFrame decoded{};
    EXPECT_FALSE(integra::DecodeTransactionFrame(std::span{buf.data(), len + 1U}, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsAnInputShorterThanTheHeader)
{
    const std::array<std::uint8_t, 4> tooShort{};
    integra::TransactionFrame decoded{};
    EXPECT_FALSE(integra::DecodeTransactionFrame(tooShort, decoded));
}

TEST(TransactionFrameTest, DecodeRejectsAnInconsistentLengthField)
{
    const integra::TransactionFrame frame = MakeFrame(4U);
    std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};
    const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
    ASSERT_NE(len, 0U);

    buf[9]  = 0xFFU; // payloadLen low byte -> claims far more than is present
    buf[10] = 0xFFU;

    integra::TransactionFrame decoded{};
    EXPECT_FALSE(integra::DecodeTransactionFrame(std::span{buf.data(), len}, decoded));
}

} // namespace
