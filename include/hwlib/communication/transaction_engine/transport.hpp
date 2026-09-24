#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace hwlib::communication
{

/// Transport abstraction the transaction engine sends and receives raw frame
/// bytes through. The engine knows nothing about the underlying link.
class ITransactionTransport
{
public:
    virtual ~ITransactionTransport() = default;

    // Sends a single already-encoded TransactionFrame. Returns false if the
    // transport could not accept it. The caller does not retry here — retry is
    // ReliableEventSenderCore's job.
    [[nodiscard]] virtual bool Send(std::span<const std::uint8_t> frame) = 0;

    // Lets the sender decide between sending/retrying now and parking a
    // transaction as queued.
    [[nodiscard]] virtual bool IsAvailable() const = 0;

    // Registers the callback the sender receives raw frames through. May be
    // called from a different execution context than the application's main
    // loop — implementations must not assume any particular thread.
    //
    // Independent from SetReceiverReceiveHandler(): a transport supporting both
    // lets one sender and one receiver share an instance, each registering its
    // own slot. Every received frame is delivered to BOTH handlers — no demux
    // is needed, because each side ignores frames not meant for it (the sender
    // only acts on TRANSACTION_ACK_TYPE frames, the receiver ignores them).
    virtual void SetSenderReceiveHandler(std::function<void(std::span<const std::uint8_t>)> handler) = 0;

    // Registers the callback the receiver receives raw frames through. See
    // SetSenderReceiveHandler().
    virtual void SetReceiverReceiveHandler(std::function<void(std::span<const std::uint8_t>)> handler) = 0;
};

} // namespace hwlib::communication
