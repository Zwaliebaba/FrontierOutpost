import { describe, expect, it } from "vitest";

import { onHidden } from "./hidden";

/** A page whose visibility a test can change, as a browser does on a tab switch. */
function page() {
  const listeners: (() => void)[] = [];
  const state = { hidden: false };
  return {
    get hidden() {
      return state.hidden;
    },
    addEventListener: (_type: "visibilitychange", listener: () => void) => {
      listeners.push(listener);
    },
    show(hidden: boolean) {
      state.hidden = hidden;
      for (const listener of listeners) listener();
    },
  };
}

describe("onHidden", () => {
  it("releases each time the page is hidden, and not when it is shown again", () => {
    const tab = page();
    let released = 0;
    onHidden(tab, () => {
      released += 1;
    });
    expect(released).toBe(0);
    tab.show(true);
    expect(released).toBe(1);
    tab.show(false);
    expect(released).toBe(1);
    tab.show(true);
    expect(released).toBe(2);
  });
});
