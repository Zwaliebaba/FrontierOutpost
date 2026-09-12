// MatchServerTests.cpp -- the server, over a real socket.
//
// Step 3 of Design/Plans/4X-02-ServerAndClient.md. These connect to 127.0.0.1 on a port the OS
// picks, which is slower than a fake and tests something a fake cannot: that the listener listens,
// that a non-blocking read means what this tree thinks it means, and that a connection closing is
// noticed.
//
// They are deterministic despite being network tests, because loopback delivers in order and
// immediately. `Settle` exists for the one thing that is not instant -- the OS completing a
// connection -- and polls rather than sleeping a fixed time.

#include "pch.h"
#include "CppUnitTest.h"

#include "MatchServer.h"

#include "CountingSimulation.h"
#include "FrameStream.h"
#include "Prng.h"
#include "Protocol.h"
#include "Session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

constexpr std::uint32_t SIX_HOUR_INTERVAL = 6 * 60 * 60;
constexpr Neuron::Instant SIX_HOURS = SIX_HOUR_INTERVAL;

/// A client, as a socket and a frame stream. What the executable does, minus the drawing.
class TestClient
{
public:
  [[nodiscard]] bool Connect(std::uint16_t _port)
  {
    m_socket = Neuron::Socket::Connect("127.0.0.1", _port);

    // Settled rather than assumed. A loopback connect usually finishes inside the call, but since
    // ADR-043 it is allowed not to, and a test that sent into a half-open socket would fail on a
    // busy machine and nowhere else.
    for (std::int32_t attempt = 0; attempt < 500; ++attempt)
    {
      const Neuron::Socket::Connection progress = m_socket.Progress();
      if (progress == Neuron::Socket::Connection::Ready)
      {
        return true;
      }
      if (progress == Neuron::Socket::Connection::Failed)
      {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  void Send(std::span<const std::uint8_t> _payload)
  {
    const std::vector<std::uint8_t> framed = Neuron::FrameStream::Frame(_payload);
    std::size_t sent = 0;
    while (sent < framed.size())
    {
      const std::int32_t written = m_socket.Send(std::span{framed.data() + sent, framed.size() - sent});
      if (written < 0)
      {
        return;
      }
      sent += static_cast<std::size_t>(written);
    }
  }

  /// Reads whatever has arrived. Returns false once the peer has gone.
  [[nodiscard]] bool Pump()
  {
    std::array<std::uint8_t, 4096> buffer = {};
    for (;;)
    {
      const std::int32_t read = m_socket.Receive(buffer);
      if (read < 0)
      {
        m_closed = true;
        return false;
      }
      if (read == 0)
      {
        return true;
      }
      if (!m_incoming.Feed(std::span{buffer.data(), static_cast<std::size_t>(read)}))
      {
        return false;
      }

      std::vector<std::uint8_t> payload;
      while (m_incoming.Take(payload))
      {
        m_received.push_back(payload);
      }
    }
  }

  [[nodiscard]] bool Closed() const noexcept
  {
    return m_closed;
  }

  /// The first message of this kind, or false.
  [[nodiscard]] bool Find(Neuron::MessageKind _kind, std::vector<std::uint8_t>& _out) const
  {
    for (const std::vector<std::uint8_t>& message : m_received)
    {
      if (Neuron::Protocol::KindOf(message) == _kind)
      {
        _out = message;
        return true;
      }
    }
    return false;
  }

  /// The LAST message of a kind. `Find` returns the first, which for a client that has been
  /// watching several ticks is the state it was welcomed with -- sent before anything had resolved,
  /// and so the one message guaranteed to say nothing about the match.
  [[nodiscard]] bool FindLast(Neuron::MessageKind _kind, std::vector<std::uint8_t>& _out) const
  {
    bool found = false;
    for (const std::vector<std::uint8_t>& message : m_received)
    {
      if (Neuron::Protocol::KindOf(message) == _kind)
      {
        _out = message;
        found = true;
      }
    }
    return found;
  }

  [[nodiscard]] std::size_t Count(Neuron::MessageKind _kind) const
  {
    std::size_t count = 0;
    for (const std::vector<std::uint8_t>& message : m_received)
    {
      count += Neuron::Protocol::KindOf(message) == _kind ? 1 : 0;
    }
    return count;
  }

  void Forget()
  {
    m_received.clear();
  }

  /// Sends raw bytes, bypassing framing. For the tests that are about a peer that is not this
  /// program.
  void SendRaw(std::span<const std::uint8_t> _bytes)
  {
    std::size_t sent = 0;
    while (sent < _bytes.size())
    {
      const std::int32_t written = m_socket.Send(std::span{_bytes.data() + sent, _bytes.size() - sent});
      if (written < 0)
      {
        return;
      }
      sent += static_cast<std::size_t>(written);
    }
  }

private:
  Neuron::Socket m_socket;
  Neuron::FrameStream m_incoming;
  std::vector<std::vector<std::uint8_t>> m_received;
  bool m_closed = false;
};

/// Runs the server and the client until nothing more is moving.
///
/// Polls rather than sleeping a fixed time: loopback is immediate but the OS still has to complete
/// the connection, and a fixed sleep is either too short on a loaded machine or wasted on an idle
/// one.
void Settle(Neuron::MatchServer& _server, TestClient& _client, Neuron::Instant _now = 0, std::int32_t _rounds = 24)
{
  for (std::int32_t round = 0; round < _rounds; ++round)
  {
    (void)_server.Poll(_now);
    (void)_client.Pump();
  }
}

/// Some bytes to send as an order set. The server never opens one, so what is in it is irrelevant
/// -- but they differ from each other so that a test reading the wire can tell them apart.
[[nodiscard]] std::vector<std::uint8_t> SomeOrders(std::uint8_t _tag)
{
  return {_tag, static_cast<std::uint8_t>(_tag + 1), static_cast<std::uint8_t>(_tag + 2)};
}

[[nodiscard]] bool Mentions(const std::vector<std::string>& _log, const std::string& _text)
{
  return std::ranges::any_of(_log, [&_text](const std::string& _line) { return _line.find(_text) != std::string::npos; });
}

[[nodiscard]] std::size_t CountOf(const std::vector<std::string>& _log, const std::string& _text)
{
  return static_cast<std::size_t>(
    std::ranges::count_if(_log, [&_text](const std::string& _line) { return _line.find(_text) != std::string::npos; }));
}

/// Polls only the server. What a test wants after a client has gone: the disconnect is noticed on
/// the next failed read, and there is no longer a client to pump.
void SettleServer(Neuron::MatchServer& _server, Neuron::Instant _now = 0, std::int32_t _rounds = 24)
{
  for (std::int32_t round = 0; round < _rounds; ++round)
  {
    (void)_server.Poll(_now);
  }
}

[[nodiscard]] std::vector<std::string> SixTokens()
{
  return {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot"};
}

} // namespace

TEST_CLASS(MatchServerTests)
{
public:
  struct Running
  {
    CountingSimulation* fake = nullptr;
    std::unique_ptr<Neuron::MatchServer> server;
  };

  [[nodiscard]] static Running Start(const std::string& _storePath = {})
  {
    auto simulation = std::make_unique<CountingSimulation>();
    CountingSimulation* observed = simulation.get();
    auto session = std::make_unique<Neuron::Session>(std::move(simulation), Neuron::TickSchedule{0, SIX_HOUR_INTERVAL}, _storePath);

    return Running{.fake = observed, .server = std::make_unique<Neuron::MatchServer>(std::move(session), 0, SixTokens())};
  }

  TEST_METHOD(TheServerListensOnAPortItWasGiven)
  {
    Running running = Start();
    Assert::IsTrue(running.server->Listening());
    Assert::IsTrue(running.server->Port() != 0, L"asking for zero means the OS picks one and says which");
  }

  TEST_METHOD(AGoodTokenIsWelcomedAndGetsStateImmediately)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("charlie"));
    Settle(*running.server, client);

    std::vector<std::uint8_t> welcome;
    Assert::IsTrue(client.Find(Neuron::MessageKind::Welcome, welcome), L"no welcome arrived");

    std::int32_t player = -1;
    std::uint32_t tick = 0;
    std::int64_t seconds = 0;
    Assert::IsTrue(Neuron::Protocol::DecodeWelcome(welcome, player, tick, seconds));
    Assert::AreEqual(2, player, L"charlie is the third token, so player two");

    // The state follows at once, so a client that has just joined has something to draw rather
    // than waiting up to six hours for the next lock.
    std::vector<std::uint8_t> state;
    Assert::IsTrue(client.Find(Neuron::MessageKind::State, state), L"no state followed the welcome");
    Assert::AreEqual(1, running.server->Connected());
  }

  // The countdown a joining client is handed is measured from the poll that welcomed it. It used
  // to be measured from instant zero, which is the lock's absolute time, so a player joining an
  // hour into a six-hour tick counted down six hours and kept editing after the lock.
  TEST_METHOD(AJoiningClientIsToldHowLongUntilTheLockAsOfNow)
  {
    Running running = Start();
    constexpr Neuron::Instant AN_HOUR_IN = 3600;

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, client, AN_HOUR_IN);

    std::vector<std::uint8_t> welcome;
    Assert::IsTrue(client.Find(Neuron::MessageKind::Welcome, welcome));
    std::int32_t player = -1;
    std::uint32_t tick = 0;
    std::int64_t seconds = 0;
    Assert::IsTrue(Neuron::Protocol::DecodeWelcome(welcome, player, tick, seconds));
    Assert::AreEqual(static_cast<std::int64_t>(SIX_HOURS - AN_HOUR_IN), seconds, L"the welcome says what is left, not the whole interval");

    std::vector<std::uint8_t> state;
    Assert::IsTrue(client.Find(Neuron::MessageKind::State, state));
    std::vector<std::uint8_t> snapshot;
    std::vector<Neuron::Protocol::TickDigest> digests;
    Assert::IsTrue(Neuron::Protocol::DecodeState(state, tick, seconds, snapshot, digests));
    Assert::AreEqual(static_cast<std::int64_t>(SIX_HOURS - AN_HOUR_IN), seconds, L"and so does the state that follows it");
  }

  TEST_METHOD(AnUnknownTokenIsRefusedAndTheConnectionClosed)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("not-on-the-list"));
    Settle(*running.server, client);

    std::vector<std::uint8_t> refused;
    Assert::IsTrue(client.Find(Neuron::MessageKind::Refused, refused));

    Neuron::RefusalReason reason = Neuron::RefusalReason::None;
    Assert::IsTrue(Neuron::Protocol::DecodeRefused(refused, reason));
    Assert::IsTrue(reason == Neuron::RefusalReason::UnknownToken);
    Assert::AreEqual(0, running.server->Connected());
  }

  // A token is the only secret in this protocol, and a log file is the likeliest place for one to
  // end up somewhere it should not.
  TEST_METHOD(ARefusedTokenIsNeverWrittenToTheLog)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("hunter2-is-a-secret"));
    Settle(*running.server, client);

    for (const std::string& line : running.server->TakeLog())
    {
      Assert::IsTrue(line.find("hunter2") == std::string::npos, L"a token reached the log");
    }
  }

  TEST_METHOD(TheSameTokenTwiceIsRefusedTheSecondTime)
  {
    Running running = Start();

    TestClient first;
    Assert::IsTrue(first.Connect(running.server->Port()));
    first.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, first);

    TestClient second;
    Assert::IsTrue(second.Connect(running.server->Port()));
    second.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, second);

    std::vector<std::uint8_t> refused;
    Assert::IsTrue(second.Find(Neuron::MessageKind::Refused, refused));

    Neuron::RefusalReason reason = Neuron::RefusalReason::None;
    Assert::IsTrue(Neuron::Protocol::DecodeRefused(refused, reason));
    Assert::IsTrue(reason == Neuron::RefusalReason::AlreadyConnected);
    Assert::AreEqual(1, running.server->Connected(), L"and the first one is untouched");
  }

  // Nothing before a hello. An unidentified connection that could submit orders would be one that
  // could play somebody else's turn.
  TEST_METHOD(OrdersBeforeAHelloAreRefused)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeOrders(std::vector<std::uint8_t>{1, 2, 3}));
    Settle(*running.server, client);

    std::vector<std::uint8_t> refused;
    Assert::IsTrue(client.Find(Neuron::MessageKind::Refused, refused));
    Assert::AreEqual(0U, running.fake->submissions, L"nothing reached the simulation");
  }

  TEST_METHOD(OrdersReachTheSimulationUnread)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("bravo"));
    Settle(*running.server, client);

    const std::vector<std::uint8_t> orders = {7, 7, 7, 7};
    client.Send(Neuron::Protocol::EncodeOrders(orders));
    Settle(*running.server, client);

    Assert::AreEqual(1U, running.fake->submissions);

    (void)running.server->Poll(SIX_HOURS);
    const std::vector<Neuron::PlayerTurn> locked = running.fake->LockedTurn();
    Assert::AreEqual(static_cast<size_t>(4), locked[1].orders.size(), L"bravo is player one");
    Assert::AreEqual(static_cast<std::uint8_t>(7), locked[1].orders[0]);
  }

  // A client that sends orders has not thereby been seen. A ping is what says somebody is there,
  // and getting this backwards makes quiet players custodians.
  TEST_METHOD(APingMarksPresenceWithoutSubmittingAnything)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("delta"));
    Settle(*running.server, client);

    client.Send(Neuron::Protocol::EncodePing());
    Settle(*running.server, client);

    (void)running.server->Poll(SIX_HOURS);
    const std::vector<Neuron::PlayerTurn> locked = running.fake->LockedTurn();

    Assert::IsTrue(locked[3].present, L"delta is player three, and was seen");
    Assert::IsTrue(locked[3].orders.empty(), L"and sent nothing");
    Assert::AreEqual(0U, running.fake->submissions);
  }

  TEST_METHOD(StateGoesOutAfterEveryLock)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("delta"));
    Settle(*running.server, client);
    client.Forget();

    for (std::uint32_t tick = 1; tick <= 3; ++tick)
    {
      (void)running.server->Poll(static_cast<Neuron::Instant>(tick) * SIX_HOURS);
      (void)client.Pump();
    }

    Assert::AreEqual(static_cast<size_t>(3), client.Count(Neuron::MessageKind::State), L"one per lock, no more and no fewer");
  }

  TEST_METHOD(ThreeMissedLocksProduceThreeResolutionsAndTheStateThatFollows)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, client);
    client.Forget();

    Assert::AreEqual(3U, running.server->Poll(3 * SIX_HOURS));
    (void)client.Pump();

    Assert::AreEqual(3U, running.fake->Tick());
    Assert::IsTrue(client.Count(Neuron::MessageKind::State) >= 1, L"and the client is told where the match got to");
  }

  // ---- Things a peer that is not this program would send -------------------------------------

  TEST_METHOD(AGarbageFrameLengthDropsTheConnection)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));

    // Four bytes claiming four billion, straight onto the socket.
    const std::vector<std::uint8_t> lie = {0xFF, 0xFF, 0xFF, 0xFF};
    client.SendRaw(lie);
    Settle(*running.server, client);

    Assert::AreEqual(0, running.server->Connected());
  }

  TEST_METHOD(AMalformedHelloDropsTheConnection)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));

    // A Hello whose string length runs past the message.
    Neuron::ByteWriter writer;
    writer.WriteU8(static_cast<std::uint8_t>(Neuron::MessageKind::Hello));
    writer.WriteU16(9000);
    writer.WriteU8('x');
    client.Send(writer.Bytes());
    Settle(*running.server, client);

    Assert::AreEqual(0, running.server->Connected());
  }

  // A client sending a message only a server sends is confused, or is not a client.
  TEST_METHOD(AClientSendingAServerMessageIsDropped)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("delta"));
    Settle(*running.server, client);
    Assert::AreEqual(1, running.server->Connected());

    client.Send(Neuron::Protocol::EncodeWelcome(0, 0, 0));
    Settle(*running.server, client);
    Assert::AreEqual(0, running.server->Connected());
  }

  TEST_METHOD(NoiseOnTheSocketNeverCrashesTheServer)
  {
    Running running = Start();

    for (std::uint64_t seed = 0; seed < 12; ++seed)
    {
      TestClient client;
      Assert::IsTrue(client.Connect(running.server->Port()));

      Neuron::Prng prng{seed};
      std::vector<std::uint8_t> noise;
      noise.reserve(64);
      for (std::int32_t index = 0; index < 64; ++index)
      {
        noise.push_back(static_cast<std::uint8_t>(prng.Next() & 0xFFU));
      }
      client.SendRaw(noise);
      Settle(*running.server, client, 0, 8);
    }

    // Still up, still listening, and still able to take a real client.
    Assert::IsTrue(running.server->Listening());

    TestClient honest;
    Assert::IsTrue(honest.Connect(running.server->Port()));
    honest.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, honest);

    std::vector<std::uint8_t> welcome;
    Assert::IsTrue(honest.Find(Neuron::MessageKind::Welcome, welcome), L"the server survived and still works");
  }

  TEST_METHOD(TheLogRecordsLoginsAndIsTakenRatherThanKept)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, client);

    const std::vector<std::string> log = running.server->TakeLog();
    bool sawLogin = false;
    for (const std::string& line : log)
    {
      sawLogin = sawLogin || line.find("login player 0") != std::string::npos;
    }
    Assert::IsTrue(sawLogin, L"the login curve is the test plan's primary instrument");
    Assert::IsTrue(running.server->TakeLog().empty(), L"and taking it clears it");
  }

  // A lobby issues a token for every seat it could have; the host then starts a match with fewer.
  // A token past that names a seat in no galaxy -- no capital, no snapshot, nothing to order -- and
  // is refused as what it now is. Two tests in this file used to rely on the opposite, logging into
  // seat five of a four-seat match and being welcomed.
  TEST_METHOD(ATokenPastTheMatchsSeatsIsRefused)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("echo"));
    Settle(*running.server, client);

    std::vector<std::uint8_t> refused;
    Assert::IsTrue(client.Find(Neuron::MessageKind::Refused, refused), L"the fifth token of a four-seat match names nobody");

    Neuron::RefusalReason reason = Neuron::RefusalReason::None;
    Assert::IsTrue(Neuron::Protocol::DecodeRefused(refused, reason));
    Assert::IsTrue(reason == Neuron::RefusalReason::UnknownToken);
    Assert::AreEqual(0, running.server->Connected());
  }

  // ---- H4: the order edit ------------------------------------------------------------------------
  //
  // "At least 80% of sessions include an order edit." The server counts envelopes and never opens
  // one (ADR-031), so what these tests drive is exactly what a client does: an order set per tap.

  TEST_METHOD(TheFirstOrderSetOfATickIsATurnAndTheNextIsAnEdit)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, client);

    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(1)));
    Settle(*running.server, client);
    Assert::IsFalse(Mentions(running.server->TakeLog(), "order-edit"), L"a turn given once is not an edit");

    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(2)));
    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(3)));
    Settle(*running.server, client);

    const std::vector<std::string> log = running.server->TakeLog();
    Assert::AreEqual(std::size_t{2}, CountOf(log, "order-edit"), L"two replacements, two edits");
    Assert::IsTrue(Mentions(log, "order-edit player=0 this-tick=2"));
    Assert::IsTrue(Mentions(log, "order-edit player=0 this-tick=3"));
  }

  // The lock is what makes the next set a turn again. Without this the second day of a match would
  // read as one enormous edit, and H4 would be answered off a number that only ever goes up.
  TEST_METHOD(TheLockMakesTheNextOrderSetATurnAgain)
  {
    Running running = Start();

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, client);

    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(1)));
    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(2)));
    Settle(*running.server, client);
    (void)running.server->TakeLog();

    // Six hours later, which is one lock.
    Settle(*running.server, client, SIX_HOURS);
    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(3)));
    Settle(*running.server, client, SIX_HOURS);

    Assert::IsFalse(Mentions(running.server->TakeLog(), "order-edit"), L"the first set after a lock is a new turn");
  }

  // H4 counts sessions, not order sets, so the totals go on the line that ends one. Otherwise the
  // question "what share of sessions included an edit" is a join across a forty-eight-hour log that
  // somebody has to do by hand and will do differently each time.
  TEST_METHOD(ASessionEndsWithItsOwnTotals)
  {
    Running running = Start();

    {
      TestClient client;
      Assert::IsTrue(client.Connect(running.server->Port()));
      client.Send(Neuron::Protocol::EncodeHello("bravo"));
      Settle(*running.server, client);

      client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(1)));
      client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(2)));
      Settle(*running.server, client);
      (void)running.server->TakeLog();
    }

    // The client is gone. The server notices on the next failed read.
    SettleServer(*running.server);

    Assert::IsTrue(Mentions(running.server->TakeLog(), "player 1 disconnected orders=2 edits=1"));
  }

  // A second session starts its own count. A player who edits on Saturday and not on Sunday has one
  // session of each, and H4 is a fraction of sessions.
  TEST_METHOD(ASecondSessionCountsFromZero)
  {
    Running running = Start();

    {
      TestClient first;
      Assert::IsTrue(first.Connect(running.server->Port()));
      first.Send(Neuron::Protocol::EncodeHello("alpha"));
      Settle(*running.server, first);
      first.Send(Neuron::Protocol::EncodeOrders(SomeOrders(1)));
      first.Send(Neuron::Protocol::EncodeOrders(SomeOrders(2)));
      Settle(*running.server, first);
    }

    SettleServer(*running.server);
    (void)running.server->TakeLog();

    // Back after the lock, so the new session's first set is a turn rather than a replacement.
    {
      TestClient second;
      Assert::IsTrue(second.Connect(running.server->Port()));
      second.Send(Neuron::Protocol::EncodeHello("alpha"));
      Settle(*running.server, second, SIX_HOURS);
      second.Send(Neuron::Protocol::EncodeOrders(SomeOrders(3)));
      Settle(*running.server, second, SIX_HOURS);
    }
    SettleServer(*running.server, SIX_HOURS);

    Assert::IsTrue(Mentions(running.server->TakeLog(), "player 0 disconnected orders=1 edits=0"), L"the second session is its own session");
  }

  // ---- A match that has ended takes no more orders -----------------------------------------------
  //
  // Found by a rehearsal rather than by reading: a match ended early on dominance, the client went
  // on offering its buttons, and every tap was logged as an order edit into a tick that would never
  // resolve. H4 is a fraction of the edits somebody made while the game was still a game.
  TEST_METHOD(AFinishedMatchTakesNoMoreOrders)
  {
    Running running = Start();
    running.fake->SetLength(1);

    TestClient client;
    Assert::IsTrue(client.Connect(running.server->Port()));
    client.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, client);

    // One lock, and the fake is finished.
    Settle(*running.server, client, SIX_HOURS);
    Assert::IsTrue(running.server->Match().Match().IsFinished(), L"the fixture has to actually be over");
    (void)running.server->TakeLog();

    const std::uint32_t resolvedBefore = running.fake->resolves;
    const std::uint32_t submittedBefore = running.fake->submissions;

    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(1)));
    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(2)));
    client.Send(Neuron::Protocol::EncodeOrders(SomeOrders(3)));
    Settle(*running.server, client, SIX_HOURS);

    Assert::AreEqual(submittedBefore, running.fake->submissions, L"a finished match must not take an order");
    Assert::AreEqual(resolvedBefore, running.fake->resolves, L"and must not resolve another tick");

    const std::vector<std::string> log = running.server->TakeLog();
    Assert::IsFalse(Mentions(log, "order-edit"), L"an order that was refused is not an edit");
    Assert::IsTrue(Mentions(log, "ordered after the match ended"), L"but it is worth knowing somebody tried");

    // Once per session, however many times they tap.
    Assert::AreEqual(std::size_t{1}, CountOf(log, "ordered after the match ended"));
  }

  // The one that survives a reconnect. Whether a submission REPLACED one is a fact about the
  // player's turn, not about the socket it arrived on -- so somebody who submits, drops and comes
  // back inside the same tick has still edited their turn.
  TEST_METHOD(AReplacementAfterAReconnectIsStillAnEdit)
  {
    Running running = Start();

    {
      TestClient first;
      Assert::IsTrue(first.Connect(running.server->Port()));
      first.Send(Neuron::Protocol::EncodeHello("charlie"));
      Settle(*running.server, first);
      first.Send(Neuron::Protocol::EncodeOrders(SomeOrders(1)));
      Settle(*running.server, first);
    }

    SettleServer(*running.server);
    (void)running.server->TakeLog();

    TestClient second;
    Assert::IsTrue(second.Connect(running.server->Port()));
    second.Send(Neuron::Protocol::EncodeHello("charlie"));
    Settle(*running.server, second);
    second.Send(Neuron::Protocol::EncodeOrders(SomeOrders(2)));
    Settle(*running.server, second);

    Assert::IsTrue(Mentions(running.server->TakeLog(), "order-edit player=2 this-tick=2"),
                   L"the same tick had two order sets, however many sockets carried them");
  }

  // ---- What a player who was not looking is owed (ADR-044) --------------------------------------

  /// The digests a client's most recent `State` carried.
  [[nodiscard]] static std::vector<Neuron::Protocol::TickDigest> DigestsSeen(TestClient& _client)
  {
    std::vector<std::uint8_t> state;
    if (!_client.FindLast(Neuron::MessageKind::State, state))
    {
      return {};
    }

    std::uint32_t tick = 0;
    std::int64_t seconds = 0;
    std::vector<std::uint8_t> snapshot;
    std::vector<Neuron::Protocol::TickDigest> digests;
    return Neuron::Protocol::DecodeState(state, tick, seconds, snapshot, digests) ? digests : std::vector<Neuron::Protocol::TickDigest>{};
  }

  TEST_METHOD(AClientThatWasAwayIsSentWhatItMissed)
  {
    // **The finding this closes**: a player who closed a lid for a night came back to a count of
    // the ticks they had missed and the events of only the last one.
    Running running = Start();

    {
      TestClient present;
      Assert::IsTrue(present.Connect(running.server->Port()));
      present.Send(Neuron::Protocol::EncodeHello("alpha"));
      Settle(*running.server, present);
    }

    // Three ticks with nobody watching.
    for (std::int32_t tick = 1; tick <= 3; ++tick)
    {
      (void)running.server->Poll(SIX_HOURS * static_cast<Neuron::Instant>(tick));
    }

    TestClient returning;
    Assert::IsTrue(returning.Connect(running.server->Port()));
    returning.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, returning);

    const std::vector<Neuron::Protocol::TickDigest> digests = DigestsSeen(returning);
    Assert::AreEqual(std::size_t{3}, digests.size(), L"a returning client was not sent the ticks it missed");
    Assert::AreEqual(1U, digests.front().tick, L"the backlog does not start at the oldest tick kept");
    Assert::AreEqual(3U, digests.back().tick, L"the backlog does not end at the tick that just resolved");
  }

  TEST_METHOD(AClientThatStayedGetsOnlyTheNewTick)
  {
    // The other half, and the reason the backlog is not simply sent every time: a client that has
    // been watching already has the rest, and four ticks a day times twelve people is a match
    // re-sent to everybody who did not miss it.
    Running running = Start();

    TestClient watching;
    Assert::IsTrue(watching.Connect(running.server->Port()));
    watching.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, watching);

    for (std::int32_t tick = 1; tick <= 3; ++tick)
    {
      (void)running.server->Poll(SIX_HOURS * static_cast<Neuron::Instant>(tick));
      Settle(*running.server, watching);
    }

    const std::vector<Neuron::Protocol::TickDigest> digests = DigestsSeen(watching);
    Assert::AreEqual(std::size_t{1}, digests.size(), L"a client that never left was sent the whole backlog");
    Assert::AreEqual(3U, digests.front().tick);
  }

  TEST_METHOD(TheBacklogIsBoundedByWhatTheSessionKeeps)
  {
    // A player away longer than the history is shown what is kept and told nothing about the rest.
    // Bounded on purpose: a three-week match is eighty-four ticks and keeping every digest for
    // twelve players would be keeping the match twice.
    Running running = Start();

    {
      TestClient present;
      Assert::IsTrue(present.Connect(running.server->Port()));
      present.Send(Neuron::Protocol::EncodeHello("alpha"));
      Settle(*running.server, present);
    }

    const std::uint32_t away = Neuron::Session::DIGEST_HISTORY + 4;
    for (std::uint32_t tick = 1; tick <= away; ++tick)
    {
      (void)running.server->Poll(SIX_HOURS * static_cast<Neuron::Instant>(tick));
    }

    TestClient returning;
    Assert::IsTrue(returning.Connect(running.server->Port()));
    returning.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, returning);

    const std::vector<Neuron::Protocol::TickDigest> digests = DigestsSeen(returning);
    Assert::AreEqual(static_cast<std::size_t>(Neuron::Session::DIGEST_HISTORY), digests.size(),
                     L"the backlog is not bounded by what the session keeps");
    Assert::AreEqual(away, digests.back().tick, L"the newest tick is not the one that just resolved");
    Assert::AreEqual(away - Neuron::Session::DIGEST_HISTORY + 1, digests.front().tick, L"the oldest kept tick is wrong");
  }

  TEST_METHOD(ABacklogArrivesBeforeTheFirstTick)
  {
    // A client joining a match that has resolved nothing gets a state with no digest in it at all,
    // which is not the same as a digest with nothing in it: there has been no tick to report.
    Running running = Start();

    TestClient early;
    Assert::IsTrue(early.Connect(running.server->Port()));
    early.Send(Neuron::Protocol::EncodeHello("alpha"));
    Settle(*running.server, early);

    Assert::IsTrue(DigestsSeen(early).empty(), L"a match with no resolved tick produced a digest");
  }
};

} // namespace NeuronServerTests
