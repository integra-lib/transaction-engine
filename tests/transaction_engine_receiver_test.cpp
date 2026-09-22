#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <integra/transaction_engine.hpp>
#include <span>
#include <vector>

namespace
{

struct Delivered
{
    std::uint8_t type{};
    std::vector<std::uint8_t> payload;
};

class ReceiverFixture : public ::testing::Test
{
protected:
    std::vector<Delivered> delivered;
    std::vector<std::uint32_t> acked;

    template<std::size_t DEDUP_CAPACITY = 16U>
    integra::ReliableEventReceiverCore<DEDUP_CAPACITY> MakeReceiver()
    {
        return integra::ReliableEventReceiverCore<DEDUP_CAPACITY>{
            [this](std::uint8_t type, std::span<const std::uint8_t> payload) {
                delivered.push_back({
                    type, {payload.begin(), payload.end()}
                });
            },
            [this](std::uint32_t txnId) { acked.push_back(txnId); }};
    }

    static std::vector<std::uint8_t> EncodeFrame(std::uint32_t txnId, std::uint8_t type,
                                                 std::span<const std::uint8_t> payload)
    {
        integra::TransactionFrame frame{};
        frame.txnId      = txnId;
        frame.type       = type;
        frame.payloadLen = static_cast<std::uint16_t>(payload.size());
        std::ranges::copy(payload, frame.payload.begin());

        std::array<std::uint8_t, integra::TRANSACTION_MAX_FRAME_LEN> buf{};
        const std::size_t len = integra::EncodeTransactionFrame(frame, buf);
        return {buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(len)};
    }
};

TEST_F(ReceiverFixture, DispatchesAndAcksANewTransaction)
{
    auto receiver                = MakeReceiver();
    const std::uint8_t payload[] = {0xAAU, 0xBBU};
    const auto raw               = EncodeFrame(42U, 0x10U, payload);

    receiver.OnFrameReceived(raw);

    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered[0].type, 0x10U);
    EXPECT_EQ(delivered[0].payload, std::vector<std::uint8_t>({0xAAU, 0xBBU}));
    ASSERT_EQ(acked.size(), 1U);
    EXPECT_EQ(acked[0], 42U);
}

TEST_F(ReceiverFixture, ARetransmissionIsAckedButNotDispatchedTwice)
{
    auto receiver  = MakeReceiver();
    const auto raw = EncodeFrame(42U, 0x10U, {});

    receiver.OnFrameReceived(raw);
    receiver.OnFrameReceived(raw);

    EXPECT_EQ(delivered.size(), 1U);
    EXPECT_EQ(acked.size(), 2U);
}

TEST_F(ReceiverFixture, DistinctTransactionsAreAllDispatched)
{
    auto receiver = MakeReceiver();
    receiver.OnFrameReceived(EncodeFrame(1U, 0x10U, {}));
    receiver.OnFrameReceived(EncodeFrame(2U, 0x10U, {}));

    EXPECT_EQ(delivered.size(), 2U);
    EXPECT_EQ(acked.size(), 2U);
}

TEST_F(ReceiverFixture, DropsAFrameWithABrokenChecksum)
{
    auto receiver  = MakeReceiver();
    auto raw       = EncodeFrame(42U, 0x10U, {});
    raw[0]        ^= 0xFFU;

    receiver.OnFrameReceived(raw);

    EXPECT_TRUE(delivered.empty());
    EXPECT_TRUE(acked.empty());
}

TEST_F(ReceiverFixture, DropsAMalformedFrame)
{
    auto receiver = MakeReceiver();
    const std::array<std::uint8_t, 3> tooShort{};

    receiver.OnFrameReceived(tooShort);

    EXPECT_TRUE(delivered.empty());
    EXPECT_TRUE(acked.empty());
}

// An ACK is a transport-level frame, never a business event: it must not be
// dispatched and must not be acked back, or two receivers would ping-pong.
TEST_F(ReceiverFixture, IgnoresAnAckFrame)
{
    auto receiver  = MakeReceiver();
    const auto raw = EncodeFrame(42U, integra::TRANSACTION_ACK_TYPE, {});

    receiver.OnFrameReceived(raw);

    EXPECT_TRUE(delivered.empty());
    EXPECT_TRUE(acked.empty());
}

TEST_F(ReceiverFixture, DispatchesAgainOnceTheIdIsEvictedFromTheCache)
{
    auto receiver  = MakeReceiver<2U>();
    const auto raw = EncodeFrame(1U, 0x10U, {});

    receiver.OnFrameReceived(raw);
    receiver.OnFrameReceived(EncodeFrame(2U, 0x10U, {}));
    receiver.OnFrameReceived(EncodeFrame(3U, 0x10U, {}));
    ASSERT_EQ(delivered.size(), 3U);

    receiver.OnFrameReceived(raw); // id 1 no longer remembered
    EXPECT_EQ(delivered.size(), 4U);
}

TEST_F(ReceiverFixture, DeliversAMaximumPayload)
{
    auto receiver = MakeReceiver();
    const std::array<std::uint8_t, integra::TRANSACTION_MAX_PAYLOAD_LEN> payload{};

    receiver.OnFrameReceived(EncodeFrame(7U, 0x10U, payload));

    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered[0].payload.size(), integra::TRANSACTION_MAX_PAYLOAD_LEN);
}

} // namespace
