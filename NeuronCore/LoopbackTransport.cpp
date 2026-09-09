// LoopbackTransport.cpp -- two queues, one each way. See the header for why it is not more.

#include "pch.h"
#include "LoopbackTransport.h"

namespace Neuron
{

bool LoopbackTransport::SendOrder(const MoveToOrder& _order)
{
  return m_orders.Push(_order);
}

bool LoopbackTransport::ReceiveState(ShipState& _outState)
{
  return m_states.Pop(_outState);
}

bool LoopbackTransport::ReceiveOrder(MoveToOrder& _outOrder)
{
  return m_orders.Pop(_outOrder);
}

bool LoopbackTransport::SendState(const ShipState& _state)
{
  return m_states.Push(_state);
}

std::uint64_t LoopbackTransport::DroppedOrderCount() const
{
  return m_orders.DroppedCount();
}

std::uint64_t LoopbackTransport::DroppedStateCount() const
{
  return m_states.DroppedCount();
}

} // namespace Neuron
