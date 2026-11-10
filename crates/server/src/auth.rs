//! Passkeys (WebAuthn): how a pilot registers and signs in (design section 9,
//! AGENTS.md section 8).
//!
//! Each ceremony has two halves. The server starts it and sends the browser
//! options as JSON; the browser asks the authenticator and returns a
//! credential as JSON; the server finishes it. The state between the halves
//! stays in the session's memory, so a challenge can only be answered on the
//! connection that asked for it.
//!
//! A pilot signs in by callsign, so passkeys need not be discoverable: every
//! tester has their own operating-system account (the owner's decision), whose
//! authenticator holds their passkey.

use std::sync::Arc;

use webauthn_rs::prelude::{
    Passkey, PasskeyAuthentication, PasskeyRegistration, PublicKeyCredential,
    RegisterPublicKeyCredential, Url, Uuid, Webauthn, WebauthnBuilder, WebauthnError,
};

use crate::account::valid_callsign;
use crate::store::{AccountId, Store, StoreError, random_bytes};

/// Why a ceremony failed. [`AuthError::reason`] is what the pilot is shown.
#[derive(Debug, thiserror::Error)]
pub enum AuthError {
    #[error(transparent)]
    Store(#[from] StoreError),
    #[error("WebAuthn: {0}")]
    Webauthn(#[from] WebauthnError),
    #[error("the credential is not valid JSON: {0}")]
    Malformed(#[from] serde_json::Error),
    #[error("{0:?} is not a callsign")]
    BadCallsign(String),
    #[error("the callsign {0:?} is taken")]
    CallsignTaken(String),
    #[error("no pilot is called {0:?}")]
    NoSuchPilot(String),
    #[error("the relying party is misconfigured: {0}")]
    Configuration(String),
}

impl AuthError {
    /// A sentence for the pilot. Details stay in the server's log.
    #[must_use]
    pub fn reason(&self) -> String {
        match self {
            Self::BadCallsign(_) => "Callsigns are 3 to 16 letters, digits, - or _.".into(),
            Self::CallsignTaken(callsign) => format!("The callsign {callsign} is taken."),
            Self::NoSuchPilot(callsign) => format!("No pilot is called {callsign}."),
            Self::Store(StoreError::CallsignTaken(callsign)) => {
                format!("The callsign {callsign} is taken.")
            }
            Self::Webauthn(_) | Self::Malformed(_) => "The passkey was not accepted.".into(),
            Self::Store(_) | Self::Configuration(_) => "The server could not sign you in.".into(),
        }
    }
}

/// A ceremony started and not yet finished.
pub enum Ceremony {
    Register {
        callsign: String,
        state: PasskeyRegistration,
    },
    SignIn {
        account: AccountId,
        passkeys: Vec<Passkey>,
        state: PasskeyAuthentication,
    },
}

/// The relying party: this server, as browsers know it. Cheap to clone.
#[derive(Clone)]
pub struct Auth {
    webauthn: Arc<Webauthn>,
}

impl Auth {
    /// The relying party for pages served from `origin`, whose host is the
    /// relying party ID. Passkeys need a domain name: `localhost` works, an IP
    /// address does not.
    ///
    /// # Errors
    /// Fails if `origin` has no domain name.
    pub fn new(origin: &Url) -> Result<Self, AuthError> {
        let rp_id = origin
            .domain()
            .ok_or_else(|| AuthError::Configuration(format!("{origin} has no domain name")))?;
        let webauthn = WebauthnBuilder::new(rp_id, origin)
            .map_err(|error| AuthError::Configuration(error.to_string()))?
            .rp_name("Frontier Outpost")
            .build()
            .map_err(|error| AuthError::Configuration(error.to_string()))?;
        Ok(Self {
            webauthn: Arc::new(webauthn),
        })
    }

    /// Starts registering a new pilot. Returns the options for the browser.
    ///
    /// # Errors
    /// Fails if the callsign is malformed or taken.
    pub fn begin_registration(
        &self,
        store: &Store,
        callsign: &str,
    ) -> Result<(String, Ceremony), AuthError> {
        if !valid_callsign(callsign) {
            return Err(AuthError::BadCallsign(callsign.to_owned()));
        }
        if store.find(callsign)?.is_some() {
            return Err(AuthError::CallsignTaken(callsign.to_owned()));
        }
        // The user handle is random and never shown; WebAuthn keys passkeys by it.
        let (options, state) = self.webauthn.start_passkey_registration(
            Uuid::from_bytes(random_bytes::<16>()?),
            callsign,
            callsign,
            None,
        )?;
        let ceremony = Ceremony::Register {
            callsign: callsign.to_owned(),
            state,
        };
        Ok((serde_json::to_string(&options)?, ceremony))
    }

    /// Starts signing in the pilot called `callsign`. Returns the options for
    /// the browser.
    ///
    /// # Errors
    /// Fails if no pilot has that callsign.
    pub fn begin_sign_in(
        &self,
        store: &Store,
        callsign: &str,
    ) -> Result<(String, Ceremony), AuthError> {
        let account = store
            .find(callsign)?
            .ok_or_else(|| AuthError::NoSuchPilot(callsign.to_owned()))?;
        let passkeys = store
            .passkeys(account)?
            .iter()
            .map(|json| serde_json::from_str(json))
            .collect::<Result<Vec<Passkey>, _>>()?;
        if passkeys.is_empty() {
            return Err(AuthError::NoSuchPilot(callsign.to_owned()));
        }
        let (options, state) = self.webauthn.start_passkey_authentication(&passkeys)?;
        let ceremony = Ceremony::SignIn {
            account,
            passkeys,
            state,
        };
        Ok((serde_json::to_string(&options)?, ceremony))
    }

    /// Finishes a ceremony with the browser's credential, creating the pilot
    /// for a registration, and returns their account.
    ///
    /// # Errors
    /// Fails if the credential does not answer the ceremony's challenge, or
    /// the callsign was taken meanwhile.
    pub fn finish(
        &self,
        store: &Store,
        ceremony: Ceremony,
        credential: &str,
    ) -> Result<AccountId, AuthError> {
        match ceremony {
            Ceremony::Register { callsign, state } => {
                let credential: RegisterPublicKeyCredential = serde_json::from_str(credential)?;
                let passkey = self
                    .webauthn
                    .finish_passkey_registration(&credential, &state)?;
                let json = serde_json::to_string(&passkey)?;
                Ok(store.create_pilot(&callsign, Some(&json))?)
            }
            Ceremony::SignIn {
                account,
                mut passkeys,
                state,
            } => {
                let credential: PublicKeyCredential = serde_json::from_str(credential)?;
                let result = self
                    .webauthn
                    .finish_passkey_authentication(&credential, &state)?;
                // Keep the signature counter current, so a cloned key is noticed.
                let mut changed = false;
                for passkey in &mut passkeys {
                    changed |= passkey.update_credential(&result) == Some(true);
                }
                if changed {
                    let json = passkeys
                        .iter()
                        .map(serde_json::to_string)
                        .collect::<Result<Vec<_>, _>>()?;
                    store.set_passkeys(account, &json)?;
                }
                Ok(account)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use webauthn_authenticator_rs::WebauthnAuthenticator;
    use webauthn_authenticator_rs::softpasskey::SoftPasskey;
    use webauthn_rs::prelude::{CreationChallengeResponse, RequestChallengeResponse};

    fn origin() -> Url {
        Url::parse("http://localhost:8080").unwrap()
    }

    fn register(auth: &Auth, store: &Store, key: &mut SoftPasskey, callsign: &str) -> AccountId {
        let (options, ceremony) = auth.begin_registration(store, callsign).unwrap();
        let options: CreationChallengeResponse = serde_json::from_str(&options).unwrap();
        let credential = key.do_registration(origin(), options).unwrap();
        auth.finish(
            store,
            ceremony,
            &serde_json::to_string(&credential).unwrap(),
        )
        .unwrap()
    }

    #[test]
    fn a_pilot_registers_a_passkey_then_signs_in_with_it() {
        let (auth, store) = (Auth::new(&origin()).unwrap(), Store::in_memory().unwrap());
        let mut key = SoftPasskey::new(true);
        let id = register(&auth, &store, &mut key, "Vega");
        assert_eq!(store.load(id).unwrap().credits, 100);

        let (options, ceremony) = auth.begin_sign_in(&store, "vega").unwrap();
        let options: RequestChallengeResponse = serde_json::from_str(&options).unwrap();
        let credential = key.do_authentication(origin(), options).unwrap();
        let signed_in = auth
            .finish(
                &store,
                ceremony,
                &serde_json::to_string(&credential).unwrap(),
            )
            .unwrap();
        assert_eq!(signed_in, id);
    }

    #[test]
    fn another_authenticator_cannot_sign_in_as_the_pilot() {
        let (auth, store) = (Auth::new(&origin()).unwrap(), Store::in_memory().unwrap());
        register(&auth, &store, &mut SoftPasskey::new(true), "Vega");
        let (options, ceremony) = auth.begin_sign_in(&store, "Vega").unwrap();
        let options: RequestChallengeResponse = serde_json::from_str(&options).unwrap();
        // A stranger's key holds no credential the server allows.
        assert!(
            SoftPasskey::new(true)
                .do_authentication(origin(), options)
                .is_err()
        );
        assert!(auth.finish(&store, ceremony, "{}").is_err());
    }

    #[test]
    fn a_credential_for_another_origin_is_refused() {
        let (auth, store) = (Auth::new(&origin()).unwrap(), Store::in_memory().unwrap());
        let (options, ceremony) = auth.begin_registration(&store, "Vega").unwrap();
        let options: CreationChallengeResponse = serde_json::from_str(&options).unwrap();
        let phished = Url::parse("http://localhost:6666").unwrap();
        let credential = SoftPasskey::new(true)
            .do_registration(phished, options)
            .unwrap();
        let result = auth.finish(
            &store,
            ceremony,
            &serde_json::to_string(&credential).unwrap(),
        );
        assert!(matches!(result, Err(AuthError::Webauthn(_))));
        assert_eq!(store.find("Vega").unwrap(), None);
    }

    #[test]
    fn refuses_taken_unknown_and_malformed_callsigns() {
        let (auth, store) = (Auth::new(&origin()).unwrap(), Store::in_memory().unwrap());
        store.create_account("Vega").unwrap();
        assert!(matches!(
            auth.begin_registration(&store, "vega"),
            Err(AuthError::CallsignTaken(_))
        ));
        assert!(matches!(
            auth.begin_registration(&store, "x"),
            Err(AuthError::BadCallsign(_))
        ));
        assert!(matches!(
            auth.begin_sign_in(&store, "Nobody"),
            Err(AuthError::NoSuchPilot(_))
        ));
    }

    #[test]
    fn an_ip_address_cannot_be_a_relying_party() {
        assert!(Auth::new(&Url::parse("http://127.0.0.1:8080").unwrap()).is_err());
    }
}
