/**
 * What was last written to each named place on the page, so the frame loop writes only what
 * changed. Setting a node's text to the string it already holds still replaces its text node,
 * and the browser lays the page out again: written every frame, the HUD cost a layout and a
 * style pass a frame for lines that mostly said the same thing.
 */
export class Written {
  readonly #last = new Map<string, string>();

  /** Whether `value` differs from what `place` last held. It is remembered either way. */
  changed(place: string, value: string): boolean {
    if (this.#last.get(place) === value) return false;
    this.#last.set(place, value);
    return true;
  }

  /** Forgets every place whose name starts with `prefix`: their nodes are gone. */
  forget(prefix: string): void {
    for (const place of this.#last.keys()) {
      if (place.startsWith(prefix)) this.#last.delete(place);
    }
  }
}
