#pragma once

#include "Galaxy.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct FleetTag;
struct ProposalTag;

using FleetId = Neuron::Id<FleetTag>;
using ProposalId = Neuron::Id<ProposalTag>;

/// What can be built. A trade lane is in this list and not in a diplomacy screen, because the
/// one-pager puts it in the build menu with *Propose* where *Build* would be -- it is a building
/// with two owners (decision three).
enum class BuildKind : std::uint8_t
{
  Shipyard,
  MiningStation
};

/// The three kinds of offer, and no more. The one-pager fixes the list, and the reason it is fixed
/// is the reason there is no free text: every offer has to be answerable with one tap by somebody
/// who will not write a sentence back.
enum class ProposalKind : std::uint8_t
{
  OpenLane,
  ShareScouting,
  HoldForTicks
};

enum class Answer : std::uint8_t
{
  Accept,
  Decline
};

/// What an accepted proposal becomes, when it is not a trade lane.
///
/// `OpenLane` has no entry here because it becomes an `ActiveTradeLane` instead -- it is the one
/// kind that pays, and the one kind the game enforces.
enum class AgreementKind : std::uint8_t
{
  ShareScouting,
  HoldFire
};

/// Move a fleet, or hold it.
///
/// The destination is a SYSTEM, not a lane and not a path. A fleet moves along one lane per order
/// (the picker on the main page is lane-constrained for exactly this reason), so `destination`
/// must be one lane from where the fleet is. Holding is `destination` equal to the fleet's current
/// system, which is a real order rather than the absence of one: the one-pager's incumbent gets
/// the defender bonus, and choosing to stay is how a player claims it.
struct FleetOrder
{
  FleetId fleet;
  SystemId destination;
};

struct BuildOrder
{
  SystemId system;
  BuildKind kind = BuildKind::Shipyard;
};

/// An offer to another player. `lane` is used by `OpenLane`, `ticks` by `HoldForTicks`; the unused
/// field is ignored rather than made a variant, because this is about to become a wire record and
/// a fixed shape decodes in one read.
struct ProposalOrder
{
  PlayerId to;
  ProposalKind kind = ProposalKind::OpenLane;
  LaneId lane;
  std::uint32_t ticks = 0;

  /// A lane to open if this offer is accepted, whatever else the offer was about.
  ///
  /// The one-pager's example, in as many words: a proposal "can carry a conditional order -- *if
  /// accepted, open lane* -- so the effect lands without a second round trip." Without it, agreeing
  /// to share scouting and then opening a lane between the same two empires costs two ticks and two
  /// digests, at four ticks a day. Unset on an `OpenLane` proposal, whose whole subject is already
  /// a lane.
  LaneId conditionalLane;
};

/// Close a trade lane. Either party, any tick, no notice.
///
/// The one-pager makes that unilateral and instant on purpose -- "either can cancel it at any
/// tick... Lanes are public; cancelling one is a tell." The sanction for walking away is that
/// everybody can see you did.
struct CancelLaneOrder
{
  LaneId lane;
};

/// Answering an offer. It is ITSELF AN ORDER (one-pager, decision three): it locks with the rest,
/// which is what stops a proposal being a side channel that moves faster than the tick.
struct AnswerOrder
{
  ProposalId proposal;
  Answer answer = Answer::Accept;
};

struct WithdrawOrder
{
  ProposalId proposal;
};

/// Everything one player has decided for one tick.
///
/// It is the unit that locks, the unit that is sent, and -- because a match is a seed and a list
/// of these -- the unit that a replay is made of.
struct OrderSet
{
  PlayerId player;
  std::vector<FleetOrder> fleetOrders;
  std::vector<BuildOrder> builds;
  std::vector<ProposalOrder> proposals;
  std::vector<AnswerOrder> answers;
  std::vector<WithdrawOrder> withdrawals;
  std::vector<CancelLaneOrder> cancellations;
  /// Hand the empire to a custodian, permanently (one-pager, *Player states*).
  bool concede = false;

  void Write(Neuron::ByteWriter& _writer) const;
  [[nodiscard]] static OrderSet Read(Neuron::ByteReader& _reader);
};

/// Why one order was refused.
///
/// The one-pager's "nobody is ever shown a dead offer as acceptable" is a rule about the whole
/// system, so a refusal carries a reason and the reason reaches the digest. An order that vanished
/// silently is indistinguishable, from the player's side, from an order that was obeyed and did
/// nothing -- and they would do completely different things next tick.
enum class OrderRejection : std::uint8_t
{
  None,
  /// The order set names a player who is not in this match.
  NoSuchPlayer,
  /// A fleet id that does not exist, or one belonging to somebody else.
  NotYourFleet,
  /// The fleet is already in transit; it cannot be redirected mid-lane.
  FleetInTransit,
  /// No lane runs from where the fleet is to where it was told to go.
  NoLaneToDestination,
  /// Two orders for the same fleet in one set.
  FleetOrderedTwice,
  /// Building on a system this player does not hold.
  NotYourSystem,
  /// A second building of a kind the system already has.
  AlreadyBuilt,
  /// The builds in this set cost more than the player has.
  CannotAfford,
  /// Proposing to yourself, or to a player who is not in the match.
  NoSuchRecipient,
  /// `OpenLane` naming a lane that does not exist, or that neither player is on.
  LaneNotBetweenYou,
  /// `HoldForTicks` with a window of zero, or longer than the proposal window.
  BadHoldWindow,
  /// Answering or withdrawing a proposal that is not open.
  NoSuchProposal,
  /// Answering an offer that was not made to you.
  NotYoursToAnswer,
  /// Withdrawing an offer you did not make.
  NotYoursToWithdraw,
  /// A conceded player has no orders left to give.
  AlreadyConceded,
  /// Cancelling a trade lane that is not open, or is not yours.
  NotYourTradeLane,
  /// A conditional lane that does not join the two parties.
  ConditionalLaneNotBetweenYou
};

[[nodiscard]] const char* Describe(OrderRejection _rejection) noexcept;

/// Which order was refused and why.
///
/// `index` is the position within its own list, so a client can point at the row the player typed
/// rather than at "an order".
struct RejectedOrder
{
  OrderRejection reason = OrderRejection::None;
  std::int32_t index = -1;
};

} // namespace Frontier
