# WebTransport spike

A throwaway test for one open decision in `AGENTS.md` §8: whether the game's network transport
can be WebTransport, served from Rust by [quinn](https://crates.io/crates/quinn) and
[web-transport-quinn](https://crates.io/crates/web-transport-quinn).

It is a workspace of its own, so its dependencies stay out of the game's `Cargo.lock` and CI.
Delete this directory once the decision is recorded.

## Run it

```sh
cd spikes/webtransport
cargo run
```

Then open the two addresses it prints, in each browser under test:

- `http://127.0.0.1:4444/` connects straight to the server.
- `http://127.0.0.1:4444/?impaired` connects through a relay in the same process that delays
  every packet by 40 ms each way and drops 3% each way.

The server generates a fresh self-signed certificate on every start and hands its SHA-256 hash to
the page, which pins it with `serverCertificateHashes`. Nothing has to be installed or trusted.
The page reports each check as pass or fail and prints a JSON block to copy into the table below.

## What it checks

1. The browser has WebTransport and connects with a pinned local certificate.
2. Bidirectional streams echo 200, 6,000 and 60,000 bytes intact.
3. Datagrams echo at 20 per second.
4. The server pushes 100 messages at 20 per second, the rate the game would send snapshots, in
   three ways:
   - as 1,000-byte datagrams, which may be lost;
   - as 6,000-byte messages, each on a stream of its own;
   - as 6,000-byte messages on one ordered stream, which is how a WebSocket would carry them.
5. What happens to a datagram larger than the browser's limit.

6,000 bytes is the Revision 3 review's estimate of an unoptimised JSON snapshot. A "hitch" is a
gap between arrivals of more than two snapshot periods (100 ms).

## Results

### Microsoft Edge, direct loopback
{
  "userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36 Edg/157.0.0.0",
  "link": "direct loopback",
  "supported": true,
  "connect": {
    "ok": true,
    "ms": 701
  },
  "maxDatagramBytes": 1211,
  "streamEchoSmall": {
    "bytes": 200,
    "repeats": 20,
    "intact": true,
    "roundTripMs": {
      "median": 0.8,
      "p95": 29.3,
      "max": 29.3
    }
  },
  "streamEchoSnapshot": {
    "bytes": 6000,
    "repeats": 20,
    "intact": true,
    "roundTripMs": {
      "median": 1,
      "p95": 1.3,
      "max": 1.3
    }
  },
  "streamEchoLarge": {
    "bytes": 60000,
    "repeats": 5,
    "intact": true,
    "roundTripMs": {
      "median": 3.2,
      "p95": 7.5,
      "max": 7.5
    }
  },
  "datagramEcho": {
    "sent": 100,
    "returned": 100,
    "roundTripMs": {
      "median": 1.1,
      "p95": 1.6,
      "max": 2
    }
  },
  "oversizeDatagram": {
    "bytes": 6000,
    "outcome": "silently dropped"
  },
  "pushedDatagrams": {
    "bytes": 1000,
    "server": "sent 100",
    "received": 100,
    "inOrder": true,
    "intervalMs": {
      "median": 46.8,
      "p95": 62.9,
      "max": 64
    },
    "hitches": 0
  },
  "pushedStreams": {
    "bytes": 6000,
    "server": "sent 100",
    "received": 100,
    "complete": true,
    "intervalMs": {
      "median": 47.1,
      "p95": 62.6,
      "max": 63.1
    },
    "hitches": 0
  },
  "pushedOneStream": {
    "bytes": 6000,
    "server": "sent 100",
    "received": 100,
    "intervalMs": {
      "median": 47.1,
      "p95": 62.9,
      "max": 64
    },
    "hitches": 0
  }
}

### Microsoft Edge, impaired link
{
  "userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36 Edg/157.0.0.0",
  "link": "impaired: 40 ms each way, 3% loss each way",
  "supported": true,
  "connect": {
    "ok": true,
    "ms": 714
  },
  "maxDatagramBytes": 1211,
  "streamEchoSmall": {
    "bytes": 200,
    "repeats": 20,
    "intact": true,
    "roundTripMs": {
      "median": 92.8,
      "p95": 108.1,
      "max": 108.1
    }
  },
  "streamEchoSnapshot": {
    "bytes": 6000,
    "repeats": 20,
    "intact": true,
    "roundTripMs": {
      "median": 201.2,
      "p95": 402.3,
      "max": 402.3
    }
  },
  "streamEchoLarge": {
    "bytes": 60000,
    "repeats": 5,
    "intact": true,
    "roundTripMs": {
      "median": 1474.8,
      "p95": 1695.1,
      "max": 1695.1
    }
  },
  "datagramEcho": {
    "sent": 100,
    "returned": 94,
    "roundTripMs": {
      "median": 93.7,
      "p95": 96.3,
      "max": 102.1
    }
  },
  "oversizeDatagram": {
    "bytes": 6000,
    "outcome": "silently dropped"
  },
  "pushedDatagrams": {
    "bytes": 1000,
    "server": "sent 100",
    "received": 97,
    "inOrder": true,
    "intervalMs": {
      "median": 46.8,
      "p95": 65.1,
      "max": 110.6
    },
    "hitches": 3
  },
  "pushedStreams": {
    "bytes": 6000,
    "server": "sent 100",
    "received": 100,
    "complete": true,
    "intervalMs": {
      "median": 32.1,
      "p95": 276,
      "max": 469.4
    },
    "hitches": 23
  },
  "pushedOneStream": {
    "bytes": 6000,
    "server": "sent 100",
    "received": 100,
    "intervalMs": {
      "median": 93,
      "p95": 221.1,
      "max": 466.9
    },
    "hitches": 28
  }
}

### Chromium 153 (headless, Linux), direct loopback

Every check passes. Connecting takes about 10 ms, echoes take 2 to 5 ms, and all three push
patterns deliver 100 of 100 messages at a steady 50 ms with no hitches. The browser's datagram
limit is 1,024 bytes; a 6,000-byte datagram is dropped without an error.

### Chromium 153 (headless, Linux), impaired link

Two runs of 100 messages each.

| Pattern                              | Delivered | Hitches  | Longest gap     |
| ------------------------------------ | --------- | -------- | --------------- |
| 1,000-byte datagrams                 | 95 to 97  | 3 to 4   | about 105 ms    |
| 6,000-byte messages, one stream each | 100       | 17 to 24 | 650 to 1,000 ms |
| 6,000-byte messages, one stream      | 95 to 100 | 32 to 40 | 270 to 400 ms   |

One of the two single-stream runs had delivered only 95 messages when the page stopped waiting.

### Firefox and Safari

Not yet run. They need a machine with those browsers; this one has only Chromium.

| Browser | Direct | Impaired | Notes |
| ------- | ------ | -------- | ----- |
| Firefox |        |          |       |
| Safari  |        |          |       |

## Findings

- **The stack works.** `web-transport-quinn` 0.13.6 on quinn 0.11 accepted Chromium's sessions,
  streams and datagrams on the first attempt, with about 150 lines of server code.
- **Local development needs no certificate setup in Chromium.** Pinning the hash of a
  certificate generated at start-up was enough. Such a certificate may be valid for at most 14
  days, so it cannot be a file checked into the repository.
- **Datagrams are what make the difference on a lossy link, not streams.** A lost datagram costs
  one snapshot period. Large reliable messages hitch several times a second whether they share a
  stream or not, because each spans several packets and waits for retransmission.
- **So snapshots have to fit in a datagram.** With a 1,024-byte limit, a 6,000-byte JSON snapshot
  cannot be sent as one. The snapshot path needs a compact encoding from the start; JSON can stay
  for reliable messages on streams.
- **One stream per message was better than a single stream, but not good.** It roughly halved
  the hitches and did nothing for the longest gap.

## Limits of this spike

- One browser, on loopback, on one machine. Nothing here was measured over a real network.
- The relay drops packets uniformly at random. Real links lose packets in bursts, and 3% each way
  is a poor connection, so the impaired figures show the direction of the effect, not its size.
- 100 messages per pattern and two runs: enough to see the pattern, not to quote a number.
- The server uses quinn's default congestion control. `web-transport-quinn` can select another;
  that was not tried, and it may change the stream results.
- Reliable messages small enough for one packet were not tried.
- The spike builds with the `ring` crypto backend. The crate's default, `aws-lc-rs`, needs extra
  build tools on Windows; a Windows build was not attempted here.
- A tablet on the local network cannot use this page as it stands: browsers only allow
  WebTransport from a secure context, and a plain `http://` address other than loopback is not
  one.

