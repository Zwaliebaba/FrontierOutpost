import { describe, expect, it } from "vitest";
import { SignInRefusedError, tokenRefused } from "./identity";

describe("a failed resumption", () => {
  it("forgets the token only when the server refuses it", () => {
    expect(tokenRefused(new SignInRefusedError("Your session has expired."))).toBe(true);
    // A reload or a lost network mid-way: the token stays for the reconnect.
    expect(tokenRefused(new Error("disconnected before signing in: closed"))).toBe(false);
    expect(tokenRefused("anything else")).toBe(false);
  });
});
