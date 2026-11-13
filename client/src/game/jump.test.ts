import { describe, expect, it } from "vitest";
import type { PocketKind } from "../protocol";
import { CANCEL_TEXT, JumpDrive, REFUSAL_TEXT, markerOf, targetName, targetOf } from "./jump";

const pockets: { kind: PocketKind; locked: boolean }[] = [
  { kind: "oreField", locked: false },
  { kind: "derelict", locked: true },
];

describe("targetOf and markerOf", () => {
  it("names the dock and each pocket by its marker, and back", () => {
    expect(targetOf("dock", pockets)).toEqual({ type: "dock" });
    expect(targetOf("derelict", pockets)).toEqual({ type: "pocket", index: 1 });
    expect(targetOf("nestCore", pockets)).toBeNull();
    expect(markerOf({ type: "pocket", index: 0 }, pockets)).toBe("oreField");
    expect(markerOf({ type: "dock" }, pockets)).toBe("dock");
  });

  it("calls targets by name", () => {
    expect(targetName({ type: "dock" }, pockets)).toBe("the dock");
    expect(targetName({ type: "pocket", index: 0 }, pockets)).toBe("Ore field");
  });
});

describe("JumpDrive", () => {
  it("does nothing on J until a destination is chosen, then jumps to it", () => {
    const drive = new JumpDrive();
    expect(drive.press(pockets)).toBeNull();
    expect(drive.readout(0, pockets)).toEqual({
      state: "idle",
      target: null,
      text: "No destination",
      hint: "M map",
      share: 0,
    });
    drive.toggle("oreField");
    expect(drive.readout(0, pockets)).toEqual({
      state: "chosen",
      target: "oreField",
      text: "Ore field",
      hint: "J jumps",
      share: 0,
    });
    expect(drive.press(pockets)).toEqual({ jump: { type: "pocket", index: 0 } });
  });

  it("clears a choice chosen again", () => {
    const drive = new JumpDrive();
    drive.toggle("dock");
    drive.toggle("dock");
    expect(drive.selected).toBeNull();
    drive.toggle("dock");
    drive.toggle("oreField");
    expect(drive.selected).toBe("oreField");
  });

  it("counts a spool down and cancels it on J", () => {
    const drive = new JumpDrive();
    drive.toggle("oreField");
    drive.spooling({ type: "pocket", index: 0 }, 6, 1_000);
    expect(drive.secondsLeft(1_000)).toBe(6);
    expect(drive.secondsLeft(5_500)).toBe(2);
    expect(drive.readout(5_500, pockets)).toEqual({
      state: "spooling",
      target: "oreField",
      text: "Jumping to Ore field in 2 s",
      hint: "J cancels",
      share: 0.75,
    });
    expect(drive.press(pockets)).toEqual({ jump: null });
    drive.ended();
    expect(drive.secondsLeft(5_500)).toBeNull();
    expect(drive.press(pockets)).toEqual({ jump: { type: "pocket", index: 0 } });
  });

  it("names the dock as a place, and holds the spool's share between its ends", () => {
    const drive = new JumpDrive();
    drive.toggle("dock");
    expect(drive.readout(0, pockets).text).toBe("Dock");
    drive.spooling({ type: "dock" }, 6, 1_000);
    expect(drive.readout(1_000, pockets).share).toBe(0);
    expect(drive.readout(9_000, pockets).share).toBe(1);
    expect(drive.readout(9_000, pockets).text).toBe("Jumping to the dock in 0 s");
  });

  it("forgets everything on a new run", () => {
    const drive = new JumpDrive();
    drive.toggle("dock");
    drive.spooling({ type: "dock" }, 6, 0);
    drive.reset();
    expect(drive.selected).toBeNull();
    expect(drive.spool).toBeNull();
  });
});

describe("refusal and cancel text", () => {
  it("says why the drive did not spool or stopped", () => {
    expect(REFUSAL_TEXT.hunted).toContain("locked");
    expect(REFUSAL_TEXT.extracting).toContain("X");
    expect(CANCEL_TEXT.hunted).toContain("locked");
  });
});
