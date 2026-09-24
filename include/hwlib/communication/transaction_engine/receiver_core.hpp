#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <hwlib/communication/transaction_engine/frame.hpp>
#include <hwlib/data_structures/dedup_cache.hpp>
#include <span>

namespace hwlib::communication
{

// Pure, platform-agnostic transaction responder. Validates the frame,
// suppresses re-delivery of an already-processed txn_id (dedup),
// dispatches new transactions to the business handler, and always replies
// with an ACK carrying the same txn_id.
template<std::size_t DEDUP_CAPACITY = 16U>
class ReliableEventReceiverCore
{
    static_assert(DEDUP_CAPACITY > 0U, "DEDUP_CAPACITY must be at least 1");

public:
    using HandlerFn = std::function<void(std::uint8_t type, std::span<const std::uint8_t> payload)>;
    using SendAckFn = std::function<void(std::uint32_t txnId)>;

    ReliableEventReceiverCore(HandlerFn handler, SendAckFn sendAck)
        : m_handler{std::move(handler)}
        , m_sendAck{std::move(sendAck)}
    {}

    ReliableEventReceiverCore(const ReliableEventReceiverCore&)            = default;
    ReliableEventReceiverCore& operator=(const ReliableEventReceiverCore&) = default;
    ReliableEventReceiverCore(ReliableEventReceiverCore&&)                 = default;
    ReliableEventReceiverCore& operator=(ReliableEventReceiverCore&&)      = default;
    ~ReliableEventReceiverCore()                                           = default;

    void OnFrameReceived(std::span<const std::uint8_t> raw)
    {
        TransactionFrame frame{};
        if (!DecodeTransactionFrame(raw, frame))
        {
            return; // invalid checksum or malformed frame — silently dropped
        }

        if (frame.type == TRANSACTION_ACK_TYPE)
        {
            return; // TE-level ACK is not a business event — never dispatched or re-acked
        }

        if (!m_dedup.Contains(frame.txnId))
        {
            m_handler(frame.type, std::span{frame.payload.data(), frame.payloadLen});
            m_dedup.Insert(frame.txnId);
        }
        m_sendAck(frame.txnId);
    }

private:
    HandlerFn m_handler;
    SendAckFn m_sendAck;
    hwlib::data_structures::DedupCache<DEDUP_CAPACITY> m_dedup;
};

} // namespace hwlib::communication
