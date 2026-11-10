import { describe, expect, it } from "vitest";
import { FrameError, FrameReader, HEADER_BYTES, MAX_BODY_BYTES, encodeFrame } from "./frame";

const hello = { type: "hello", protocolVersion: 1 };

describe("frames", () => {
  it("matches the Rust encoding byte for byte", () => {
    // The same bytes as frontier_protocol::frame's test.
    const body = new TextEncoder().encode('{"type":"hello","protocolVersion":1}');
    const frame = encodeFrame(hello);
    expect(new DataView(frame.buffer).getUint32(0)).toBe(body.length);
    expect(frame.subarray(HEADER_BYTES)).toEqual(body);
  });

  it("decodes frames however the stream splits or joins them", () => {
    const a = encodeFrame(hello);
    const b = encodeFrame({ type: "welcome", protocolVersion: 1 });
    const stream = new Uint8Array([...a, ...b]);
    for (let split = 0; split <= stream.length; split += 1) {
      const reader = new FrameReader();
      const messages = [
        ...reader.push(stream.subarray(0, split)),
        ...reader.push(stream.subarray(split)),
      ];
      expect(messages).toEqual([hello, { type: "welcome", protocolVersion: 1 }]);
      expect(reader.pending).toBe(0);
    }
  });

  it("decodes a frame delivered one byte at a time", () => {
    const reader = new FrameReader();
    const frame = encodeFrame(hello);
    const messages = [...frame].flatMap((byte) => reader.push(new Uint8Array([byte])));
    expect(messages).toEqual([hello]);
  });

  it("refuses to encode an oversized body", () => {
    expect(() => encodeFrame({ reason: "x".repeat(MAX_BODY_BYTES) })).toThrow(FrameError);
  });

  it("refuses a header that announces an oversized body", () => {
    const header = new Uint8Array(HEADER_BYTES);
    new DataView(header.buffer).setUint32(0, MAX_BODY_BYTES + 1);
    expect(() => new FrameReader().push(header)).toThrow(FrameError);
  });
});
