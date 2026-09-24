#include <gtest/gtest.h>

#include <cstdint>
#include <hwlib/communication/transaction_engine.hpp>
#include <vector>

namespace
{

constexpr std::uint16_t DEVICE_ID = 0x00ABU;

class SenderFixture : public ::testing::Test
{
protected:
    std::vector<hwlib::communication::TransactionFrame> sent;
    std::vector<std::uint32_t> confirmed;
    std::vector<std::uint32_t> failed;

    template<std::size_t MAX_IN_FLIGHT = 2U>
    hwlib::communication::ReliableEventSenderCore<MAX_IN_FLIGHT> MakeSender(
        hwlib::communication::ReliableEventSenderConfig cfg = {})
    {
        hwlib::communication::ReliableEventSenderCore<MAX_IN_FLIGHT> sender{
            DEVICE_ID, [this](const hwlib::communication::TransactionFrame& frame) { sent.push_back(frame); }, cfg};
        sender.SetOnConfirmed([this](std::uint32_t txnId) { confirmed.push_back(txnId); });
        sender.SetOnFailed([this](std::uint32_t txnId) { failed.push_back(txnId); });
        return sender;
    }
};

TEST_F(SenderFixture, SubmitSendsImmediatelyAndReturnsATxnId)
{
    auto sender               = MakeSender();
    const std::uint8_t data[] = {1U, 2U, 3U};
    const auto txnId          = sender.Submit(0x10U, data, 0U);

    ASSERT_TRUE(txnId.has_value());
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].txnId, txnId.value());
    EXPECT_EQ(sent[0].type, 0x10U);
    EXPECT_EQ(sent[0].payloadLen, 3U);
}

TEST_F(SenderFixture, TxnIdCarriesTheDeviceIdInTheHighHalf)
{
    auto sender      = MakeSender();
    const auto txnId = sender.Submit(0x10U, {}, 0U);

    ASSERT_TRUE(txnId.has_value());
    EXPECT_EQ(txnId.value() >> 16U, DEVICE_ID);
}

TEST_F(SenderFixture, EachSubmitGetsADistinctTxnId)
{
    auto sender       = MakeSender();
    const auto first  = sender.Submit(0x10U, {}, 0U);
    const auto second = sender.Submit(0x11U, {}, 0U);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first.value(), second.value());
}

TEST_F(SenderFixture, RejectsAPayloadOverTheLimit)
{
    auto sender = MakeSender();
    const std::array<std::uint8_t, hwlib::communication::TRANSACTION_MAX_PAYLOAD_LEN + 1U> tooBig{};

    EXPECT_FALSE(sender.Submit(0x10U, tooBig, 0U).has_value());
    EXPECT_TRUE(sent.empty());
}

TEST_F(SenderFixture, RejectsASubmitWhenEverySlotIsBusy)
{
    auto sender = MakeSender<1U>();
    ASSERT_TRUE(sender.Submit(0x10U, {}, 0U).has_value());

    EXPECT_FALSE(sender.Submit(0x11U, {}, 0U).has_value());
    EXPECT_EQ(sent.size(), 1U);
}

TEST_F(SenderFixture, AnAckConfirmsAndFreesTheSlot)
{
    auto sender      = MakeSender<1U>();
    const auto txnId = sender.Submit(0x10U, {}, 0U);
    ASSERT_TRUE(txnId.has_value());

    sender.OnAckReceived(txnId.value());
    ASSERT_EQ(confirmed.size(), 1U);
    EXPECT_EQ(confirmed[0], txnId.value());
    EXPECT_TRUE(sender.Submit(0x11U, {}, 0U).has_value());
}

TEST_F(SenderFixture, AnUnknownAckIsIgnored)
{
    auto sender = MakeSender();
    std::ignore = sender.Submit(0x10U, {}, 0U);

    sender.OnAckReceived(0xDEADBEEFU);
    EXPECT_TRUE(confirmed.empty());
}

TEST_F(SenderFixture, RetransmitsAfterTheBaseTimeout)
{
    auto sender = MakeSender({.baseTimeoutMs = 500U, .maxRetry = 4U});
    std::ignore = sender.Submit(0x10U, {}, 0U);
    ASSERT_EQ(sent.size(), 1U);

    sender.OnTick(499U);
    EXPECT_EQ(sent.size(), 1U);

    sender.OnTick(500U);
    EXPECT_EQ(sent.size(), 2U);
}

TEST_F(SenderFixture, BacksOffExponentially)
{
    auto sender = MakeSender({.baseTimeoutMs = 100U, .maxRetry = 4U});
    std::ignore = sender.Submit(0x10U, {}, 0U);

    sender.OnTick(100U); // first retry, next timeout at 100 + 200
    ASSERT_EQ(sent.size(), 2U);

    sender.OnTick(299U);
    EXPECT_EQ(sent.size(), 2U);
    sender.OnTick(300U); // second retry, next timeout at 300 + 400
    EXPECT_EQ(sent.size(), 3U);

    sender.OnTick(699U);
    EXPECT_EQ(sent.size(), 3U);
    sender.OnTick(700U);
    EXPECT_EQ(sent.size(), 4U);
}

// maxRetry counts total attempts including the first send, so with maxRetry=4
// exactly four frames go out before the transaction fails.
TEST_F(SenderFixture, FailsAfterMaxRetryAttempts)
{
    auto sender      = MakeSender({.baseTimeoutMs = 100U, .maxRetry = 4U});
    const auto txnId = sender.Submit(0x10U, {}, 0U);
    ASSERT_TRUE(txnId.has_value());

    sender.OnTick(100U);
    sender.OnTick(300U);
    sender.OnTick(700U);
    EXPECT_EQ(sent.size(), 4U);
    EXPECT_TRUE(failed.empty());

    sender.OnTick(1500U);
    EXPECT_EQ(sent.size(), 4U);
    ASSERT_EQ(failed.size(), 1U);
    EXPECT_EQ(failed[0], txnId.value());
}

TEST_F(SenderFixture, AFailedTransactionFreesItsSlot)
{
    auto sender = MakeSender<1U>({.baseTimeoutMs = 100U, .maxRetry = 1U});
    std::ignore = sender.Submit(0x10U, {}, 0U);

    sender.OnTick(100U);
    ASSERT_EQ(failed.size(), 1U);
    EXPECT_TRUE(sender.Submit(0x11U, {}, 0U).has_value());
}

TEST_F(SenderFixture, ParksTheTransactionWhileTheTransportIsDown)
{
    auto sender = MakeSender({.baseTimeoutMs = 100U, .maxRetry = 4U});
    sender.SetTransportAvailable(false);

    const auto txnId = sender.Submit(0x10U, {}, 0U);
    ASSERT_TRUE(txnId.has_value());
    EXPECT_TRUE(sent.empty());

    sender.OnTick(50U);
    EXPECT_TRUE(sent.empty());

    sender.SetTransportAvailable(true);
    sender.OnTick(60U);
    EXPECT_EQ(sent.size(), 1U);
}

// Resuming from the queued state is not itself a retry: the retry budget is
// only spent when the resumed send later times out.
TEST_F(SenderFixture, ResumingFromQueuedDoesNotSpendTheRetryBudget)
{
    auto sender = MakeSender({.baseTimeoutMs = 100U, .maxRetry = 2U});
    sender.SetTransportAvailable(false);
    std::ignore = sender.Submit(0x10U, {}, 0U);

    for (std::uint32_t now = 0U; now < 1000U; now += 100U)
    {
        sender.OnTick(now);
    }
    EXPECT_TRUE(failed.empty());

    sender.SetTransportAvailable(true);
    sender.OnTick(1000U);
    EXPECT_EQ(sent.size(), 1U);
    EXPECT_TRUE(failed.empty());
}

TEST_F(SenderFixture, MaxRetryIsClampedToTheCap)
{
    using Sender = hwlib::communication::ReliableEventSenderCore<1U>;
    auto sender  = MakeSender<1U>({.baseTimeoutMs = 1U, .maxRetry = 200U});
    std::ignore  = sender.Submit(0x10U, {}, 0U);

    // With maxRetry clamped to MAX_RETRY_CAP the transaction must still end,
    // rather than retry forever.
    std::uint32_t now = 0U;
    for (int i = 0; i < Sender::MAX_RETRY_CAP + 5; ++i)
    {
        now += 1U << 30U;
        sender.OnTick(now);
    }
    EXPECT_EQ(failed.size(), 1U);
}

// baseTimeoutMs << retryCount is computed in 64 bits and saturated. With a
// plain 32-bit `baseTimeoutMs * (1U << retryCount)`, 500 << 30 wraps to
// exactly 0, so the next tick at an unchanged `now` would already be "timed
// out" and the transaction would burn its last attempt instantly.
TEST_F(SenderFixture, BackoffSaturatesInsteadOfWrappingToZero)
{
    using Sender       = hwlib::communication::ReliableEventSenderCore<1U>;
    constexpr auto CAP = Sender::MAX_RETRY_CAP; // 31
    auto sender        = MakeSender<1U>({.baseTimeoutMs = 500U, .maxRetry = CAP});
    std::ignore        = sender.Submit(0x10U, {}, 0U);

    // Drive retryCount up to 30, the value at which the 32-bit product wraps.
    std::uint32_t now = 0U;
    for (int i = 0; i < CAP - 1; ++i)
    {
        now += Sender::MAX_BACKOFF_MS;
        sender.OnTick(now);
    }
    ASSERT_EQ(sent.size(), static_cast<std::size_t>(CAP));
    ASSERT_TRUE(failed.empty());

    // Time has not moved: a saturated backoff means nothing is due yet.
    sender.OnTick(now);
    EXPECT_TRUE(failed.empty());
    EXPECT_EQ(sent.size(), static_cast<std::size_t>(CAP));
}

} // namespace
