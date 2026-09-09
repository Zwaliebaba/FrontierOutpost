#pragma once

#include "MessageQueue.h"
#include "Protocol.h"

#include <cstddef>
#include <cstdint>

namespace Neuron
{

/// The connection between the client and the server when both are in one process.
///
/// Two queues, one each way. The client pushes orders and pops states; the server pops orders and
/// pushes states. Neither ever blocks on the other, which is the property MVP-01 section 2 asks
/// for: the client thread never waits for a tick and the server thread never touches D3D12.
///
/// R2: there is no `Transport` base class above this and no `ITransport`. There is one transport
/// and a base class for one derived class is ceremony; the day a UdpTransport exists, the concept
/// gets named and both implement it. The shape of the interface here is what that concept will be.
///
/// The two directions have different overflow policies on purpose, and ADR-006 is why: an order
/// is something a player meant and a state is a fact that is about to be superseded.
class LoopbackTransport
{
public:
  /// Sized for the failure they are protecting against rather than for the traffic. At 20 Hz the
  /// order queue holds thirteen seconds of a player clicking as fast as anyone can, and the state
  /// queue three seconds of a client that has stopped drawing. Neither ever fills in this game;
  /// both are a bound on what happens when something else has already gone wrong.
  static constexpr std::size_t ORDER_CAPACITY = 256;
  static constexpr std::size_t STATE_CAPACITY = 64;

  // The client end.
  bool SendOrder(const MoveToOrder& _order);
  [[nodiscard]] bool ReceiveState(ShipState& _outState);

  // The server end.
  [[nodiscard]] bool ReceiveOrder(MoveToOrder& _outOrder);
  bool SendState(const ShipState& _state);

  [[nodiscard]] std::uint64_t DroppedOrderCount() const;
  [[nodiscard]] std::uint64_t DroppedStateCount() const;

private:
  MessageQueue<MoveToOrder, ORDER_CAPACITY, OverflowPolicy::DropNewest> m_orders;
  MessageQueue<ShipState, STATE_CAPACITY, OverflowPolicy::DropOldest> m_states;
};

} // namespace Neuron
