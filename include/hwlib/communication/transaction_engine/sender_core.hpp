#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <hwlib/communication/transaction_engine/frame.hpp>
#include <limits>
#include <optional>
#include <span>

namespace hwlib::communication
{

struct ReliableEventSenderConfig
{
    std::uint32_t baseTimeoutMs{500U};
    // Total send attempts allowed for a transaction, including the initial
    // send (i.e. NOT "retries in addition to the first send"). matches the protocol spec
    // §6.5 "max_retry" semantics (loop condition retry_count < max_retry is
    // checked before each send) — with the default of 4, exactly 4 frames go
    // out before FAILED, not 5.
    std::uint8_t maxRetry{4U};
};

// Pure, platform-agnostic transaction initiator state machine (see
// the transaction protocol spec). Time is supplied by
// the caller (nowMs) rather than read internally, so it is host-testable with
// a fake clock; a thin platform adapter drives OnTick() from a timer/work queue.
template<std::size_t MAX_IN_FLIGHT = 2U>
class ReliableEventSenderCore
{
    static_assert(MAX_IN_FLIGHT > 0U, "MAX_IN_FLIGHT must be at least 1");

public:
    using SendFn      = std::function<void(const TransactionFrame&)>;
    using ConfirmedFn = std::function<void(std::uint32_t txnId)>;
    using FailedFn    = std::function<void(std::uint32_t txnId)>;

    // Hard upper bound on ReliableEventSenderConfig::maxRetry. AttemptSend() computes
    // `1U << slot.retryCount`, and slot.retryCount only ever reaches values below whatever
    // maxRetry is configured to (see OnTick()) — an unbounded maxRetry would let retryCount
    // reach 32, where the shift amount equals the width of `unsigned int` and the expression
    // is undefined behavior, not just a large timeout. 31 keeps the shift always valid.
    static constexpr std::uint8_t MAX_RETRY_CAP = 31U;

    // Upper bound on the computed backoff delay, matching the ~24.8-day bound the
    // wrap-safe timeout check in OnTick() depends on (see the comment there). Delays
    // above this are saturated rather than wrapped — see ComputeBackoffDelayMs().
    static constexpr std::uint32_t MAX_BACKOFF_MS =
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());

    ReliableEventSenderCore(std::uint16_t deviceId, SendFn send, ReliableEventSenderConfig cfg = {})
        : m_deviceId{deviceId}
        , m_send{std::move(send)}
        , m_cfg{cfg}
    {
        if (m_cfg.maxRetry > MAX_RETRY_CAP)
        {
            m_cfg.maxRetry = MAX_RETRY_CAP;
        }
    }

    ReliableEventSenderCore(const ReliableEventSenderCore&)            = default;
    ReliableEventSenderCore& operator=(const ReliableEventSenderCore&) = default;
    ReliableEventSenderCore(ReliableEventSenderCore&&)                 = default;
    ReliableEventSenderCore& operator=(ReliableEventSenderCore&&)      = default;
    ~ReliableEventSenderCore()                                         = default;

    void SetOnConfirmed(ConfirmedFn cb)
    {
        m_onConfirmed = std::move(cb);
    }

    void SetOnFailed(FailedFn cb)
    {
        m_onFailed = std::move(cb);
    }

    void SetTransportAvailable(bool available)
    {
        m_transportAvailable = available;
    }

    // Returns the assigned txn_id, or std::nullopt if no free slot (reject-on-full,
    // see the protocol spec §6.6.2) or payload exceeds
    // TRANSACTION_MAX_PAYLOAD_LEN.
    [[nodiscard]] std::optional<std::uint32_t> Submit(std::uint8_t type, std::span<const std::uint8_t> payload,
                                                      std::uint32_t nowMs)
    {
        if (payload.size() > TRANSACTION_MAX_PAYLOAD_LEN)
        {
            return std::nullopt;
        }

        Slot* slot = FindFreeSlot();
        if (slot == nullptr)
        {
            return std::nullopt;
        }

        const std::uint32_t txnId = (static_cast<std::uint32_t>(m_deviceId) << 16) | m_nextLocalCounter;
        ++m_nextLocalCounter;

        slot->inUse            = true;
        slot->queued           = false;
        slot->txnId            = txnId;
        slot->retryCount       = 0U;
        slot->frame.txnId      = txnId;
        slot->frame.type       = type;
        slot->frame.timestamp  = nowMs;
        slot->frame.payloadLen = static_cast<std::uint16_t>(payload.size());
        std::ranges::copy(payload, slot->frame.payload.begin());

        AttemptSend(*slot, nowMs);
        return txnId;
    }

    // Called by the transport layer when an explicit ACK for txnId arrives.
    // Unknown or already-completed txn_id are ignored.
    void OnAckReceived(std::uint32_t txnId)
    {
        Slot* slot = FindByTxnId(txnId);
        if (slot == nullptr)
        {
            return;
        }

        slot->inUse = false;
        if (m_onConfirmed)
        {
            m_onConfirmed(txnId);
        }
    }

    // Drives timeouts, retries/backoff and QUEUED recovery. Call periodically
    // with the current monotonic time.
    void OnTick(std::uint32_t nowMs)
    {
        for (Slot& slot : m_slots)
        {
            if (!slot.inUse)
            {
                continue;
            }

            if (slot.queued)
            {
                // Resuming from QUEUED is not itself a retry — retryCount is
                // intentionally left untouched here (per the protocol spec §6.6.1: "повтор
                // цикла отправки выше с текущим retry_count"). The budget is
                // only spent when THIS resumed send later times out below.
                if (m_transportAvailable)
                {
                    AttemptSend(slot, nowMs);
                }
                continue;
            }

            // Wrap-safe "not yet elapsed" check: nowMs wraps every ~49.7 days
            // (uint32_t ms, as produced by a platform uptime source truncated by the
            // adapter). A plain `nowMs < nextTimeoutAtMs` misfires right at
            // the wrap boundary; the signed-difference form stays correct as
            // long as the actual elapsed time never exceeds ~24.8 days.
            // AttemptSend() enforces that bound by saturating the backoff
            // delay to MAX_BACKOFF_MS — with maxRetry clamped only to
            // MAX_RETRY_CAP (not the old default of 4), baseTimeoutMs *
            // 2^retryCount routinely exceeds it and must not be trusted to
            // "never approach it in practice" anymore.
            if (static_cast<std::int32_t>(nowMs - slot.nextTimeoutAtMs) < 0)
            {
                continue;
            }

            if (!m_transportAvailable)
            {
                slot.queued = true;
                continue;
            }

            ++slot.retryCount;
            if (slot.retryCount >= m_cfg.maxRetry)
            {
                const std::uint32_t txnId = slot.txnId;
                slot.inUse                = false;
                if (m_onFailed)
                {
                    m_onFailed(txnId);
                }
            }
            else
            {
                AttemptSend(slot, nowMs);
            }
        }
    }

private:
    struct Slot
    {
        bool inUse{false};
        bool queued{false};
        std::uint32_t txnId{};
        TransactionFrame frame{};
        std::uint8_t retryCount{0U};
        std::uint32_t nextTimeoutAtMs{0U};
    };

    [[nodiscard]] Slot* FindFreeSlot()
    {
        for (Slot& slot : m_slots)
        {
            if (!slot.inUse)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Slot* FindByTxnId(std::uint32_t txnId)
    {
        for (Slot& slot : m_slots)
        {
            if (slot.inUse && slot.txnId == txnId)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    // Sends (or re-sends) the slot's frame if the transport is available,
    // otherwise parks the slot in QUEUED without touching retryCount.
    void AttemptSend(Slot& slot, std::uint32_t nowMs)
    {
        if (!m_transportAvailable)
        {
            slot.queued = true;
            return;
        }
        slot.queued = false;
        m_send(slot.frame);
        slot.nextTimeoutAtMs = nowMs + ComputeBackoffDelayMs(slot.retryCount);
    }

    // baseTimeoutMs * 2^retryCount computed in 64 bits and saturated to MAX_BACKOFF_MS.
    // A plain 32-bit `baseTimeoutMs * (1U << retryCount)` silently wraps for large
    // retryCount/baseTimeoutMs combinations — e.g. baseTimeoutMs=500, retryCount=30
    // wraps to exactly 0, turning the intended multi-day backoff into an immediate
    // retry storm — and even where it does not literally overflow, the raw product can
    // still exceed the ~24.8-day bound the wrap-safe timeout check above depends on.
    // MAX_RETRY_CAP keeps the shift itself always valid (retryCount never reaches 32).
    [[nodiscard]] std::uint32_t ComputeBackoffDelayMs(std::uint8_t retryCount) const
    {
        const std::uint64_t rawDelay = static_cast<std::uint64_t>(m_cfg.baseTimeoutMs) << retryCount;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(rawDelay, MAX_BACKOFF_MS));
    }

    std::uint16_t m_deviceId;
    SendFn m_send;
    ReliableEventSenderConfig m_cfg;
    ConfirmedFn m_onConfirmed;
    FailedFn m_onFailed;
    bool m_transportAvailable{true};
    std::uint16_t m_nextLocalCounter{0U};
    std::array<Slot, MAX_IN_FLIGHT> m_slots{};
};

} // namespace hwlib::communication
