// Orders.cpp -- what one player decided, on the wire and back.
//
// Validation is NOT here. It needs the match to check against, so it lives with `Match`; this file
// is the record itself and the only thing it knows how to do is turn into bytes.

#include "pch.h"
#include "Orders.h"

namespace Lockstep
{

namespace
{

/// Ids go over as their underlying index, invalid included. `Id::NONE` is -1 and `WriteI32` is
/// defined for it, so an unset id survives the trip as an unset id rather than as zero -- which
/// would be system 0, the sealed region, and a plausible-looking lie.
void WriteId(Neuron::ByteWriter& _writer, std::int32_t _index)
{
  _writer.WriteI32(_index);
}

/// A count written as 32 bits and read back with a sanity bound.
///
/// The bound is the whole reason this is a function. A truncated or hostile record can declare a
/// vector of four billion entries, and a `reserve` on that number is an allocation failure at
/// best. Nothing in this game legitimately sends more orders than a player has fleets and systems,
/// so a count past this is a broken record, not a big turn.
constexpr std::uint32_t MAXIMUM_ORDERS_PER_LIST = 4096;

[[nodiscard]] std::uint32_t ReadCount(Neuron::ByteReader& _reader)
{
  const std::uint32_t count = _reader.ReadU32();
  if (count > MAXIMUM_ORDERS_PER_LIST)
  {
    return 0;
  }
  return count;
}

} // namespace

const char* Describe(OrderRejection _rejection) noexcept
{
  switch (_rejection)
  {
  case OrderRejection::None:
    return "accepted";
  case OrderRejection::NoSuchPlayer:
    return "no such player in this match";
  case OrderRejection::NotYourFleet:
    return "that is not your fleet";
  case OrderRejection::FleetInTransit:
    return "the fleet is already under way and cannot be redirected";
  case OrderRejection::NoLaneToDestination:
    return "no lane runs there from where the fleet is";
  case OrderRejection::FleetOrderedTwice:
    return "the fleet was given two orders";
  case OrderRejection::NotYourSystem:
    return "you do not hold that system";
  case OrderRejection::AlreadyBuilt:
    return "that system already has one";
  case OrderRejection::CannotAfford:
    return "not enough credits";
  case OrderRejection::NoSuchRecipient:
    return "no such player to propose to";
  case OrderRejection::LaneNotBetweenYou:
    return "that lane does not join your two empires";
  case OrderRejection::BadHoldWindow:
    return "a hold has to be between one tick and the proposal window";
  case OrderRejection::NoSuchProposal:
    return "that proposal is no longer open";
  case OrderRejection::NotYoursToAnswer:
    return "that offer was not made to you";
  case OrderRejection::NotYoursToWithdraw:
    return "you did not make that offer";
  case OrderRejection::AlreadyConceded:
    return "you have conceded this match";
  case OrderRejection::NotYourTradeLane:
    return "no trade lane of yours is open there";
  case OrderRejection::ConditionalLaneNotBetweenYou:
    return "the conditional lane does not join your two empires";
  case OrderRejection::YouAreACustodian:
    return "your territory is in custody and defends only; log in to resume";
  default:
    return "unknown";
  }
}

void OrderSet::Write(Neuron::ByteWriter& _writer) const
{
  WriteId(_writer, player.Index());

  _writer.WriteU32(static_cast<std::uint32_t>(fleetOrders.size()));
  for (const FleetOrder& order : fleetOrders)
  {
    WriteId(_writer, order.fleet.Index());
    WriteId(_writer, order.destination.Index());
  }

  _writer.WriteU32(static_cast<std::uint32_t>(builds.size()));
  for (const BuildOrder& order : builds)
  {
    WriteId(_writer, order.system.Index());
    _writer.WriteU8(static_cast<std::uint8_t>(order.kind));
  }

  _writer.WriteU32(static_cast<std::uint32_t>(proposals.size()));
  for (const ProposalOrder& order : proposals)
  {
    WriteId(_writer, order.to.Index());
    _writer.WriteU8(static_cast<std::uint8_t>(order.kind));
    WriteId(_writer, order.lane.Index());
    _writer.WriteU32(order.ticks);
    WriteId(_writer, order.conditionalLane.Index());
  }

  _writer.WriteU32(static_cast<std::uint32_t>(answers.size()));
  for (const AnswerOrder& order : answers)
  {
    WriteId(_writer, order.proposal.Index());
    _writer.WriteU8(static_cast<std::uint8_t>(order.answer));
  }

  _writer.WriteU32(static_cast<std::uint32_t>(withdrawals.size()));
  for (const WithdrawOrder& order : withdrawals)
  {
    WriteId(_writer, order.proposal.Index());
  }

  _writer.WriteU32(static_cast<std::uint32_t>(cancellations.size()));
  for (const CancelLaneOrder& order : cancellations)
  {
    WriteId(_writer, order.lane.Index());
  }

  _writer.WriteBool(concede);
}

OrderSet OrderSet::Read(Neuron::ByteReader& _reader)
{
  OrderSet set;
  set.player = PlayerId{_reader.ReadI32()};

  const std::uint32_t fleetCount = ReadCount(_reader);
  set.fleetOrders.reserve(fleetCount);
  for (std::uint32_t index = 0; index < fleetCount; ++index)
  {
    FleetOrder order;
    order.fleet = FleetId{_reader.ReadI32()};
    order.destination = SystemId{_reader.ReadI32()};
    set.fleetOrders.push_back(order);
  }

  const std::uint32_t buildCount = ReadCount(_reader);
  set.builds.reserve(buildCount);
  for (std::uint32_t index = 0; index < buildCount; ++index)
  {
    BuildOrder order;
    order.system = SystemId{_reader.ReadI32()};
    order.kind = _reader.ReadEnum(BuildKind::MiningStation);
    set.builds.push_back(order);
  }

  const std::uint32_t proposalCount = ReadCount(_reader);
  set.proposals.reserve(proposalCount);
  for (std::uint32_t index = 0; index < proposalCount; ++index)
  {
    ProposalOrder order;
    order.to = PlayerId{_reader.ReadI32()};
    order.kind = _reader.ReadEnum(ProposalKind::HoldForTicks);
    order.lane = LaneId{_reader.ReadI32()};
    order.ticks = _reader.ReadU32();
    order.conditionalLane = LaneId{_reader.ReadI32()};
    set.proposals.push_back(order);
  }

  const std::uint32_t answerCount = ReadCount(_reader);
  set.answers.reserve(answerCount);
  for (std::uint32_t index = 0; index < answerCount; ++index)
  {
    AnswerOrder order;
    order.proposal = ProposalId{_reader.ReadI32()};
    order.answer = _reader.ReadEnum(Answer::Decline);
    set.answers.push_back(order);
  }

  const std::uint32_t withdrawCount = ReadCount(_reader);
  set.withdrawals.reserve(withdrawCount);
  for (std::uint32_t index = 0; index < withdrawCount; ++index)
  {
    WithdrawOrder order;
    order.proposal = ProposalId{_reader.ReadI32()};
    set.withdrawals.push_back(order);
  }

  const std::uint32_t cancelCount = ReadCount(_reader);
  set.cancellations.reserve(cancelCount);
  for (std::uint32_t index = 0; index < cancelCount; ++index)
  {
    CancelLaneOrder order;
    order.lane = LaneId{_reader.ReadI32()};
    set.cancellations.push_back(order);
  }

  set.concede = _reader.ReadBool();
  return set;
}

} // namespace Lockstep
