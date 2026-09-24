#pragma once

/// Umbrella header for the transaction engine. The component ships as one
/// indivisible unit — the initiator, the responder, the wire format and the
/// transport contract are one protocol and are versioned together — so pulling
/// the whole interface in is the normal way to use it. The individual headers
/// under hwlib/communication/transaction_engine/ remain available for narrower includes.

#include <hwlib/communication/transaction_engine/frame.hpp>
#include <hwlib/communication/transaction_engine/receiver_core.hpp>
#include <hwlib/communication/transaction_engine/sender_core.hpp>
#include <hwlib/communication/transaction_engine/transport.hpp>
