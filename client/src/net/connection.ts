import type { ClientMessage, ConnectInfo, ServerMessage } from "../protocol";
import { PROTOCOL_VERSION } from "../protocol";
import { FrameReader, encodeFrame } from "./frame";

/** Where the server that served this page publishes `ConnectInfo`. */
export const CONNECT_INFO_PATH = "/api/connect";

export class ConnectionError extends Error {
  override name = "ConnectionError";
}

/** An open session that has completed the handshake. */
export interface Connection {
  /** The protocol version the server accepted. */
  readonly protocolVersion: number;
  /** Sends a reliable message on the control stream. */
  send(message: ClientMessage): Promise<void>;
  /**
   * Calls `listener` for every later message on the control stream. Messages that arrived
   * before the first listener was added are delivered to it at once.
   */
  onMessage(listener: (message: ServerMessage) => void): void;
  /**
   * Sends an unreliable datagram. It may be lost and must fit the browser's limit,
   * `maxDatagramBytes`.
   */
  sendDatagram(bytes: Uint8Array): Promise<void>;
  /** Calls `listener` for every datagram the server sends. */
  onDatagram(listener: (bytes: Uint8Array) => void): void;
  readonly maxDatagramBytes: number;
  /** Resolves when the session ends, with the reason if the server gave one. */
  readonly closed: Promise<string>;
  close(): void;
}

/** Asks the server that served this page how to reach its WebTransport endpoint. */
export async function fetchConnectInfo(base: string = location.origin): Promise<ConnectInfo> {
  const response = await fetch(new URL(CONNECT_INFO_PATH, base));
  if (!response.ok) {
    throw new ConnectionError(`connection details unavailable (HTTP ${String(response.status)})`);
  }
  return (await response.json()) as ConnectInfo;
}

function hexToBytes(hex: string): Uint8Array<ArrayBuffer> {
  const pairs = hex.match(/../g) ?? [];
  return Uint8Array.from(pairs, (pair) => Number.parseInt(pair, 16));
}

/** Reads a stream to its end, handing each chunk to `onChunk`. */
async function pump(stream: ReadableStream<Uint8Array>, onChunk: (chunk: Uint8Array) => void) {
  const reader = stream.getReader();
  for (;;) {
    const { value, done } = await reader.read();
    if (done) return;
    onChunk(value);
  }
}

/**
 * Opens a WebTransport session, opens the control stream and completes the handshake.
 * Rejects if the server refuses the protocol version or the session fails.
 */
export async function connect(info: ConnectInfo): Promise<Connection> {
  const options: WebTransportOptions = {};
  if (info.certificateSha256 !== undefined) {
    options.serverCertificateHashes = [
      { algorithm: "sha-256", value: hexToBytes(info.certificateSha256) },
    ];
  }
  const transport = new WebTransport(info.webTransportUrl, options);
  await transport.ready;

  const control = await transport.createBidirectionalStream();
  const writer = control.writable.getWriter();
  const reader = new FrameReader();
  const messageListeners: ((message: ServerMessage) => void)[] = [];
  const datagramListeners: ((bytes: Uint8Array) => void)[] = [];

  // The first message from the server answers the handshake; later ones go to listeners,
  // or wait for one: `joined` can arrive in the same chunk as `welcome`.
  const early: ServerMessage[] = [];
  let answer: ((message: ServerMessage) => void) | null = null;
  const handshake = new Promise<ServerMessage>((resolve) => {
    answer = resolve;
  });
  // As with datagrams, the typings leave the stream's chunk type open.
  void pump(control.readable as ReadableStream<Uint8Array>, (chunk) => {
    for (const message of reader.push(chunk) as ServerMessage[]) {
      if (answer) {
        answer(message);
        answer = null;
      } else if (messageListeners.length === 0) {
        early.push(message);
      } else {
        for (const listener of messageListeners) listener(message);
      }
    }
  }).catch(() => undefined);
  // The DOM typings leave the datagram stream's chunk type open; it carries bytes.
  const incoming = transport.datagrams.readable as ReadableStream<Uint8Array>;
  void pump(incoming, (bytes) => {
    for (const listener of datagramListeners) listener(bytes);
  }).catch(() => undefined);

  const closed = transport.closed.then(
    (info) => info.reason ?? "",
    (error: unknown) => String(error),
  );

  const hello: ClientMessage = { type: "hello", protocolVersion: PROTOCOL_VERSION };
  await writer.write(encodeFrame(hello));
  const reply = await Promise.race([
    handshake,
    closed.then((reason) => {
      throw new ConnectionError(`session closed during the handshake: ${reason}`);
    }),
  ]);
  if (reply.type === "rejected") {
    transport.close();
    throw new ConnectionError(reply.reason);
  }
  if (reply.type !== "welcome") {
    transport.close();
    throw new ConnectionError(`the server answered the handshake with '${reply.type}'`);
  }

  const datagrams = transport.datagrams.writable.getWriter();
  return {
    protocolVersion: reply.protocolVersion,
    send: (message) => writer.write(encodeFrame(message)),
    onMessage: (listener) => {
      messageListeners.push(listener);
      for (const message of early.splice(0)) listener(message);
    },
    sendDatagram: (bytes) => datagrams.write(bytes),
    onDatagram: (listener) => datagramListeners.push(listener),
    maxDatagramBytes: transport.datagrams.maxDatagramSize,
    closed,
    close: () => {
      transport.close();
    },
  };
}
