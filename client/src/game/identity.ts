/**
 * Signing in (design §9, AGENTS.md §8): a pilot registers or signs in with a passkey, and a
 * session token signs them in again without one while the tab stays open.
 *
 * The server starts each passkey ceremony and sends the browser's options as JSON; the browser
 * asks its authenticator and the result goes back as JSON. The server checks everything.
 */
import type { ClientMessage, ServerMessage } from "../protocol";
import type { Connection } from "../net/connection";

export type SignedIn = Extract<ServerMessage, { type: "signedIn" }>;

export class SignInRefusedError extends Error {
  override name = "SignInRefusedError";
}

/**
 * Where the session token is kept: the tab's session storage, so a reload or a dropped connection
 * signs straight back in, and closing the tab forgets it.
 */
const TOKEN_KEY = "frontier.session";

export function storedToken(): string | null {
  try {
    return sessionStorage.getItem(TOKEN_KEY);
  } catch {
    return null;
  }
}

export function storeToken(token: string | null): void {
  try {
    if (token === null) sessionStorage.removeItem(TOKEN_KEY);
    else sessionStorage.setItem(TOKEN_KEY, token);
  } catch {
    // Without storage the pilot signs in with their passkey each time.
  }
}

/**
 * Whether a failed resumption means the token is no good. Only the server's refusal says so; a
 * connection that drops mid-way (a reload, a lost network) leaves the token to try again.
 */
export function tokenRefused(error: unknown): boolean {
  return error instanceof SignInRefusedError;
}

/** Registers a new pilot called `callsign` with a new passkey. */
export function register(connection: Connection, callsign: string): Promise<SignedIn> {
  return exchange(connection, { type: "register", callsign }, "create");
}

/** Signs in the pilot called `callsign` with their passkey. */
export function signIn(connection: Connection, callsign: string): Promise<SignedIn> {
  return exchange(connection, { type: "signIn", callsign }, "get");
}

/** Signs in again with a session token. */
export function resume(connection: Connection, token: string): Promise<SignedIn> {
  return exchange(connection, { type: "resume", token }, null);
}

/** Sends `first`, answers the passkey ceremony it starts, and waits for the outcome. */
function exchange(
  connection: Connection,
  first: ClientMessage,
  ceremony: "create" | "get" | null,
): Promise<SignedIn> {
  return new Promise((resolve, reject) => {
    let settled = false;
    const fail = (error: unknown) => {
      if (settled) return;
      settled = true;
      reject(error instanceof Error ? error : new Error(String(error)));
    };
    connection.onMessage((message: ServerMessage) => {
      if (settled) return;
      if (message.type === "signedIn") {
        settled = true;
        resolve(message);
      } else if (message.type === "signInRefused") {
        fail(new SignInRefusedError(message.reason));
      } else if (message.type === "passkeyOptions" && ceremony !== null) {
        answer(ceremony, message.options)
          .then((credential) => connection.send({ type: "passkeyCredential", credential }))
          .catch(fail);
      }
    });
    void connection.closed.then((reason) => {
      fail(new Error(`disconnected before signing in: ${reason}`));
    });
    connection.send(first).catch(fail);
  });
}

/** Asks the browser's authenticator, and returns its credential as JSON text. */
async function answer(ceremony: "create" | "get", options: string): Promise<string> {
  const parsed = JSON.parse(options) as { publicKey: unknown };
  const credential =
    ceremony === "create"
      ? await navigator.credentials.create({
          publicKey: PublicKeyCredential.parseCreationOptionsFromJSON(
            parsed.publicKey as PublicKeyCredentialCreationOptionsJSON,
          ),
        })
      : await navigator.credentials.get({
          publicKey: PublicKeyCredential.parseRequestOptionsFromJSON(
            parsed.publicKey as PublicKeyCredentialRequestOptionsJSON,
          ),
        });
  if (!(credential instanceof PublicKeyCredential)) {
    throw new SignInRefusedError("The passkey was cancelled.");
  }
  return JSON.stringify(credential.toJSON());
}
