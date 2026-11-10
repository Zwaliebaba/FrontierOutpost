/**
 * Framing for JSON messages on a WebTransport stream, matching
 * `frontier_protocol::frame`: a 4-byte big-endian body length, then that many bytes of JSON.
 */

export const HEADER_BYTES = 4;
/** Largest JSON body either side may send. */
export const MAX_BODY_BYTES = 64 * 1024;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export class FrameError extends Error {
  override name = "FrameError";
}

/** Encodes one message as a frame. */
export function encodeFrame(message: unknown): Uint8Array {
  const body = encoder.encode(JSON.stringify(message));
  if (body.length > MAX_BODY_BYTES) {
    throw new FrameError(
      `frame body of ${String(body.length)} bytes exceeds ${String(MAX_BODY_BYTES)}`,
    );
  }
  const frame = new Uint8Array(HEADER_BYTES + body.length);
  new DataView(frame.buffer).setUint32(0, body.length);
  frame.set(body, HEADER_BYTES);
  return frame;
}

/**
 * Collects stream chunks, which can split or join frames anywhere, and returns each
 * complete message once all of its bytes have arrived.
 */
export class FrameReader {
  #buffer = new Uint8Array(0);

  /** Adds a chunk and returns the messages it completed, in order. */
  push(chunk: Uint8Array): unknown[] {
    const joined = new Uint8Array(this.#buffer.length + chunk.length);
    joined.set(this.#buffer);
    joined.set(chunk, this.#buffer.length);

    const messages: unknown[] = [];
    let offset = 0;
    while (joined.length - offset >= HEADER_BYTES) {
      const length = new DataView(joined.buffer, offset).getUint32(0);
      if (length > MAX_BODY_BYTES) {
        throw new FrameError(
          `frame announces ${String(length)} bytes, over ${String(MAX_BODY_BYTES)}`,
        );
      }
      const end = offset + HEADER_BYTES + length;
      if (end > joined.length) break;
      const body = joined.subarray(offset + HEADER_BYTES, end);
      messages.push(JSON.parse(decoder.decode(body)) as unknown);
      offset = end;
    }
    this.#buffer = joined.slice(offset);
    return messages;
  }

  /** Bytes held back while waiting for the rest of a frame. */
  get pending(): number {
    return this.#buffer.length;
  }
}
