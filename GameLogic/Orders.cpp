// Orders.cpp -- what one player decided, on the wire and back.
//
// Validation is NOT here. It needs the match to check against, so it lives with `Match`; this file
// is the record itself and the only thing it knows how to do is turn into bytes.

#include "pch.h"
#include "Orders.h"

#include "Archive.h"

namespace Lockstep
{

namespace
{

/// The bound every list in an order set is read back under.
///
/// A truncated or hostile record can declare a vector of four billion entries, and a `reserve` on
/// that number is an allocation failure at best. Nothing in this game legitimately sends more
/// orders than a player has fleets and systems, so a count past this is a broken record rather than
/// a big turn.
constexpr std::uint32_t MAXIMUM_ORDERS_PER_LIST = 4096;

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

namespace
{

/// Every list in an order set, described once (ADR-049).
///
/// Read this beside `OrderSet::Write` and `OrderSet::Read` as they were: the same fields in the
/// same order, twice, and a field added to one and forgotten in the other decoded the rest of the
/// record shifted by four bytes -- a valid-looking record of nonsense, and no test that round-trips
/// with itself can see it.
///
/// The byte layout is unchanged and `OrderWireFormatTests` pins it. It has to be: the match store
/// holds locked order sets (ADR-024), so this is the file format of every match in progress.
void Visit(Neuron::Archive& _archive, OrderSet& _orders)
{
  _archive.Identity(_orders.player);

  const std::uint32_t fleetCount = _archive.Count(_orders.fleetOrders.size(), MAXIMUM_ORDERS_PER_LIST);
  _orders.fleetOrders.resize(fleetCount);
  for (FleetOrder& order : _orders.fleetOrders)
  {
    _archive.Identity(order.fleet);
    _archive.Identity(order.destination);
  }

  const std::uint32_t buildCount = _archive.Count(_orders.builds.size(), MAXIMUM_ORDERS_PER_LIST);
  _orders.builds.resize(buildCount);
  for (BuildOrder& order : _orders.builds)
  {
    _archive.Identity(order.system);
    _archive.Enumerator(order.kind, BuildKind::MiningStation);
  }

  const std::uint32_t proposalCount = _archive.Count(_orders.proposals.size(), MAXIMUM_ORDERS_PER_LIST);
  _orders.proposals.resize(proposalCount);
  for (ProposalOrder& order : _orders.proposals)
  {
    _archive.Identity(order.to);
    _archive.Enumerator(order.kind, ProposalKind::HoldForTicks);
    _archive.Identity(order.lane);
    _archive.U32(order.ticks);
    _archive.Identity(order.conditionalLane);
  }

  const std::uint32_t answerCount = _archive.Count(_orders.answers.size(), MAXIMUM_ORDERS_PER_LIST);
  _orders.answers.resize(answerCount);
  for (AnswerOrder& order : _orders.answers)
  {
    _archive.Identity(order.proposal);
    _archive.Enumerator(order.answer, Answer::Decline);
  }

  const std::uint32_t withdrawCount = _archive.Count(_orders.withdrawals.size(), MAXIMUM_ORDERS_PER_LIST);
  _orders.withdrawals.resize(withdrawCount);
  for (WithdrawOrder& order : _orders.withdrawals)
  {
    _archive.Identity(order.proposal);
  }

  const std::uint32_t cancelCount = _archive.Count(_orders.cancellations.size(), MAXIMUM_ORDERS_PER_LIST);
  _orders.cancellations.resize(cancelCount);
  for (CancelLaneOrder& order : _orders.cancellations)
  {
    _archive.Identity(order.lane);
  }

  _archive.Boolean(_orders.concede);
}

} // namespace

void OrderSet::Write(Neuron::ByteWriter& _writer) const
{
  Neuron::Archive archive{_writer};

  // The description takes a mutable record because it serves both directions. Writing does not
  // change anything, and a const_cast here is the cost of not having the list twice.
  Visit(archive, const_cast<OrderSet&>(*this));
}

OrderSet OrderSet::Read(Neuron::ByteReader& _reader)
{
  OrderSet set;
  Neuron::Archive archive{_reader};
  Visit(archive, set);

  // An archive that refused a count has produced a record that is not what was sent, and it has
  // stopped short of the end -- which is what every caller already checks for, because a record
  // with bytes left over is as wrong as one with bytes missing.
  return archive.Failed() ? OrderSet{} : set;
}

} // namespace Lockstep
