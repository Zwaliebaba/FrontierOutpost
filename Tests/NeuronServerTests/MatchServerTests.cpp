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

#include <array>
#include <memory>
#include <string>
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
    return m_socket.Valid();
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
    client.Send(Neuron::Protocol::EncodeHello("echo"));
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
    client.Send(Neuron::Protocol::EncodeHello("foxtrot"));
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
};

} // namespace NeuronServerTests
