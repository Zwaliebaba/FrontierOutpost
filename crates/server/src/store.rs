//! Persistence: one embedded SQLite file per world (AGENTS.md section 8).
//!
//! The file holds a world identifier, fixed when it is created, and each
//! pilot's account document. Calls are synchronous and quick; async code runs
//! them on a blocking thread through [`Store::call`].

use std::path::Path;
use std::sync::{Arc, Mutex, MutexGuard};

use rusqlite::{Connection, OptionalExtension, params};

use crate::account::{Account, DocumentError, valid_callsign};

/// The database layout this server writes. Unrelated to the document version.
const SCHEMA_VERSION: u32 = 1;

/// A pilot's account, as the store numbers them.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct AccountId(pub i64);

#[derive(Debug, thiserror::Error)]
pub enum StoreError {
    #[error("database: {0}")]
    Sqlite(#[from] rusqlite::Error),
    #[error(transparent)]
    Document(#[from] DocumentError),
    #[error("could not encode a document: {0}")]
    Encode(#[from] serde_json::Error),
    #[error("could not draw random bytes: {0}")]
    Random(String),
    #[error("database schema {found} is not the {SCHEMA_VERSION} this server writes; wipe it")]
    Schema { found: u32 },
    #[error("the callsign {0:?} is taken")]
    CallsignTaken(String),
    #[error("{0:?} is not a callsign: use 3 to 16 letters, digits, - or _")]
    BadCallsign(String),
    #[error("no account {0:?}")]
    NoSuchAccount(AccountId),
    #[error("the database lock was poisoned by a panic")]
    Poisoned,
}

/// Random bytes from the operating system, as lowercase hex.
pub(crate) fn random_hex(bytes: usize) -> Result<String, StoreError> {
    let mut buffer = vec![0_u8; bytes];
    getrandom::fill(&mut buffer).map_err(|error| StoreError::Random(error.to_string()))?;
    Ok(buffer.iter().map(|b| format!("{b:02x}")).collect())
}

/// One world's saves. Cheap to clone.
#[derive(Clone)]
pub struct Store {
    connection: Arc<Mutex<Connection>>,
    world: Arc<str>,
}

impl Store {
    /// Opens the world saved at `path`, creating it if the file is new.
    ///
    /// # Errors
    /// Fails if the file cannot be opened or holds another schema.
    pub fn open(path: &Path) -> Result<Self, StoreError> {
        if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty()) {
            std::fs::create_dir_all(parent).map_err(|error| {
                StoreError::Sqlite(rusqlite::Error::InvalidPath(
                    format!("{}: {error}", parent.display()).into(),
                ))
            })?;
        }
        Self::init(Connection::open(path)?)
    }

    /// A world held in memory, for tests.
    ///
    /// # Errors
    /// Fails only if SQLite cannot start.
    pub fn in_memory() -> Result<Self, StoreError> {
        Self::init(Connection::open_in_memory()?)
    }

    fn init(connection: Connection) -> Result<Self, StoreError> {
        connection.execute_batch(
            "PRAGMA journal_mode = WAL;
             PRAGMA foreign_keys = ON;
             CREATE TABLE IF NOT EXISTS meta (
                 key TEXT PRIMARY KEY,
                 value TEXT NOT NULL
             );
             CREATE TABLE IF NOT EXISTS accounts (
                 id INTEGER PRIMARY KEY,
                 callsign TEXT NOT NULL UNIQUE COLLATE NOCASE,
                 document TEXT NOT NULL
             );",
        )?;
        let schema: Option<String> = connection
            .query_row("SELECT value FROM meta WHERE key = 'schema'", [], |row| {
                row.get(0)
            })
            .optional()?;
        match schema {
            None => {
                connection.execute(
                    "INSERT INTO meta (key, value) VALUES ('schema', ?1), ('world', ?2)",
                    params![SCHEMA_VERSION.to_string(), random_hex(16)?],
                )?;
            }
            Some(found) if found == SCHEMA_VERSION.to_string() => {}
            Some(found) => {
                return Err(StoreError::Schema {
                    found: found.parse().unwrap_or(0),
                });
            }
        }
        let world: String =
            connection.query_row("SELECT value FROM meta WHERE key = 'world'", [], |row| {
                row.get(0)
            })?;
        Ok(Self {
            connection: Arc::new(Mutex::new(connection)),
            world: world.into(),
        })
    }

    /// This world's identifier, which every account document carries.
    #[must_use]
    pub fn world(&self) -> &str {
        &self.world
    }

    pub(crate) fn lock(&self) -> Result<MutexGuard<'_, Connection>, StoreError> {
        self.connection.lock().map_err(|_| StoreError::Poisoned)
    }

    /// Runs `work` against the store on a blocking thread.
    ///
    /// # Errors
    /// Whatever `work` returns; a panic in it is reported as a poisoned store.
    pub async fn call<T, F>(&self, work: F) -> Result<T, StoreError>
    where
        T: Send + 'static,
        F: FnOnce(&Store) -> Result<T, StoreError> + Send + 'static,
    {
        let store = self.clone();
        tokio::task::spawn_blocking(move || work(&store))
            .await
            .map_err(|_| StoreError::Poisoned)?
    }

    /// Creates a pilot's account with the starting balance.
    ///
    /// # Errors
    /// Fails if the callsign is malformed or taken.
    pub fn create_account(&self, callsign: &str) -> Result<AccountId, StoreError> {
        if !valid_callsign(callsign) {
            return Err(StoreError::BadCallsign(callsign.to_owned()));
        }
        let document = Account::new(&self.world, callsign).to_json()?;
        let connection = self.lock()?;
        match connection.execute(
            "INSERT INTO accounts (callsign, document) VALUES (?1, ?2)",
            params![callsign, document],
        ) {
            Ok(_) => Ok(AccountId(connection.last_insert_rowid())),
            Err(rusqlite::Error::SqliteFailure(error, _))
                if error.code == rusqlite::ErrorCode::ConstraintViolation =>
            {
                Err(StoreError::CallsignTaken(callsign.to_owned()))
            }
            Err(error) => Err(error.into()),
        }
    }

    /// The account with this callsign, ignoring case.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn find(&self, callsign: &str) -> Result<Option<AccountId>, StoreError> {
        Ok(self
            .lock()?
            .query_row(
                "SELECT id FROM accounts WHERE callsign = ?1",
                params![callsign],
                |row| row.get(0),
            )
            .optional()?
            .map(AccountId))
    }

    /// Loads an account's document, upgrading an earlier version.
    ///
    /// # Errors
    /// Fails if there is no such account or its document cannot be read.
    pub fn load(&self, id: AccountId) -> Result<Account, StoreError> {
        let connection = self.lock()?;
        load(&connection, id, &self.world)
    }

    /// Saves an account's document at the current version.
    ///
    /// # Errors
    /// Fails if there is no such account.
    pub fn save(&self, id: AccountId, account: &Account) -> Result<(), StoreError> {
        let connection = self.lock()?;
        save(&connection, id, account)
    }
}

pub(crate) fn load(
    connection: &Connection,
    id: AccountId,
    world: &str,
) -> Result<Account, StoreError> {
    let document: String = connection
        .query_row(
            "SELECT document FROM accounts WHERE id = ?1",
            params![id.0],
            |row| row.get(0),
        )
        .optional()?
        .ok_or(StoreError::NoSuchAccount(id))?;
    Ok(Account::from_json(&document, world)?)
}

pub(crate) fn save(
    connection: &Connection,
    id: AccountId,
    account: &Account,
) -> Result<(), StoreError> {
    let changed = connection.execute(
        "UPDATE accounts SET document = ?1 WHERE id = ?2",
        params![account.to_json()?, id.0],
    )?;
    if changed == 0 {
        return Err(StoreError::NoSuchAccount(id));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_new_world_gets_an_identifier_that_survives_reopening() {
        let dir = std::env::temp_dir().join(format!("frontier-store-{}", random_hex(6).unwrap()));
        let path = dir.join("world.db");
        let first = Store::open(&path).unwrap().world().to_owned();
        assert_eq!(first.len(), 32);
        let again = Store::open(&path).unwrap().world().to_owned();
        assert_eq!(first, again);
        let other = Store::in_memory().unwrap();
        assert_ne!(other.world(), first);
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn creates_loads_and_saves_accounts() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        assert_eq!(
            store.find("vega").unwrap(),
            Some(id),
            "callsigns ignore case"
        );
        let mut account = store.load(id).unwrap();
        assert_eq!(account.world, store.world());
        account.stock.ore = 7;
        store.save(id, &account).unwrap();
        assert_eq!(store.load(id).unwrap().stock.ore, 7);
    }

    #[test]
    fn refuses_taken_and_malformed_callsigns() {
        let store = Store::in_memory().unwrap();
        store.create_account("Vega").unwrap();
        assert!(matches!(
            store.create_account("VEGA"),
            Err(StoreError::CallsignTaken(_))
        ));
        assert!(matches!(
            store.create_account("no"),
            Err(StoreError::BadCallsign(_))
        ));
    }

    #[test]
    fn refuses_a_document_saved_in_another_world() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let foreign = Account::new("elsewhere", "Vega").to_json().unwrap();
        store
            .lock()
            .unwrap()
            .execute(
                "UPDATE accounts SET document = ?1 WHERE id = ?2",
                params![foreign, id.0],
            )
            .unwrap();
        assert!(matches!(
            store.load(id),
            Err(StoreError::Document(DocumentError::WrongWorld { .. }))
        ));
    }
}
