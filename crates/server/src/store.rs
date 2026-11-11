//! Persistence: one embedded SQLite file per world (AGENTS.md section 8).
//!
//! The file holds a world identifier, fixed when it is created, and each
//! pilot's account document. Calls are synchronous and quick; async code runs
//! them on a blocking thread through [`Store::call`].

use std::path::Path;
use std::sync::{Arc, Mutex, MutexGuard};

use rusqlite::{Connection, OptionalExtension, params};
use tracing::info;

use crate::account::{Account, DocumentError, STARTING_CREDITS, valid_callsign};
use crate::events::{GateEvent, Recorded};
use crate::ledger::{Item, Npc, Party, Reason, Transfer, TransferError};

/// The database layout this server writes. Unrelated to the document version.
const SCHEMA_VERSION: u32 = 1;

/// How long a session token signs a pilot in again: a week.
const SESSION_SECONDS: i64 = 7 * 24 * 60 * 60;

fn unix_now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |since| {
            i64::try_from(since.as_secs()).unwrap_or(i64::MAX)
        })
}

fn hash(token: &str) -> String {
    use sha2::{Digest, Sha256};
    Sha256::digest(token.as_bytes())
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect()
}

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
    #[error(transparent)]
    Transfer(#[from] TransferError),
    #[error("a store call panicked; its transaction was rolled back")]
    Panicked,
}

/// Random bytes from the operating system.
pub(crate) fn random_bytes<const N: usize>() -> Result<[u8; N], StoreError> {
    let mut buffer = [0_u8; N];
    getrandom::fill(&mut buffer).map_err(|error| StoreError::Random(error.to_string()))?;
    Ok(buffer)
}

/// Random bytes from the operating system, as lowercase hex.
pub(crate) fn random_hex(bytes: usize) -> Result<String, StoreError> {
    let mut buffer = vec![0_u8; bytes];
    getrandom::fill(&mut buffer).map_err(|error| StoreError::Random(error.to_string()))?;
    Ok(buffer.iter().map(|b| format!("{b:02x}")).collect())
}

/// A pilot's place in a checkpointed run.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Seat {
    pub account: AccountId,
    pub ship: u16,
    /// Their share of the pot, in whole percent.
    pub share: u32,
}

/// A seat as JSON: account, ship and share. Checkpoints written before crews
/// (plan M6) have no share; theirs were solo runs, which take the whole pot.
#[derive(serde::Deserialize)]
#[serde(untagged)]
enum SeatJson {
    Crew(i64, u16, u32),
    Solo(i64, u16),
}

/// A run's latest checkpoint as the store keeps it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SavedRun {
    pub run: String,
    /// Each pilot's account, ship and share.
    pub crew: Vec<Seat>,
    /// The checkpoint, as JSON.
    pub document: String,
}

/// One world's saves. Cheap to clone.
#[derive(Clone)]
pub struct Store {
    connection: Arc<Mutex<Connection>>,
    world: Arc<str>,
    /// Stock granted to every new pilot on top of the starting balance.
    grant: Arc<[(Item, u32)]>,
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

    /// Wipes the world saved at `path` and starts an empty one there, with a
    /// new world identifier: Stage 1 data may be wiped between builds (design
    /// section 9). Stop the server first. Returns the new world.
    ///
    /// # Errors
    /// Fails if the files cannot be removed or the new world cannot be made.
    pub fn wipe(path: &Path) -> Result<Self, StoreError> {
        // SQLite keeps a write-ahead log and an index beside the database.
        for suffix in ["", "-wal", "-shm"] {
            let mut file = path.as_os_str().to_owned();
            file.push(suffix);
            match std::fs::remove_file(&file) {
                Ok(()) => {}
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
                Err(error) => {
                    return Err(StoreError::Sqlite(rusqlite::Error::InvalidPath(
                        format!("{}: {error}", std::path::Path::new(&file).display()).into(),
                    )));
                }
            }
        }
        Self::open(path)
    }

    /// Whether the world holds nothing: no pilots, ledger or runs.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn is_empty(&self) -> Result<bool, StoreError> {
        let connection = self.lock();
        let count = |table: &str| -> Result<i64, StoreError> {
            Ok(
                connection.query_row(&format!("SELECT COUNT(*) FROM {table}"), [], |row| {
                    row.get(0)
                })?,
            )
        };
        Ok(count("accounts")? == 0
            && count("ledger")? == 0
            && count("passkeys")? == 0
            && count("sessions")? == 0
            && count("checkpoints")? == 0
            && count("events")? == 0)
    }

    /// A world held in memory, for tests.
    ///
    /// # Errors
    /// Fails only if SQLite cannot start.
    pub fn in_memory() -> Result<Self, StoreError> {
        Self::init(Connection::open_in_memory()?)
    }

    fn init(connection: Connection) -> Result<Self, StoreError> {
        // `frontier-server grant` may write while the server runs: each waits
        // for the other's transaction rather than failing.
        connection.busy_timeout(std::time::Duration::from_secs(5))?;
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
             );
             CREATE TABLE IF NOT EXISTS ledger (
                 id INTEGER PRIMARY KEY,
                 at INTEGER NOT NULL,
                 item TEXT NOT NULL,
                 amount INTEGER NOT NULL CHECK (amount > 0),
                 source TEXT NOT NULL,
                 sink TEXT NOT NULL,
                 reason TEXT NOT NULL,
                 run TEXT
             );
             CREATE TRIGGER IF NOT EXISTS ledger_no_update BEFORE UPDATE ON ledger
                 BEGIN SELECT RAISE(ABORT, 'the ledger is append-only'); END;
             CREATE TRIGGER IF NOT EXISTS ledger_no_delete BEFORE DELETE ON ledger
                 BEGIN SELECT RAISE(ABORT, 'the ledger is append-only'); END;
             CREATE TABLE IF NOT EXISTS passkeys (
                 id INTEGER PRIMARY KEY,
                 account INTEGER NOT NULL REFERENCES accounts(id),
                 credential TEXT NOT NULL
             );
             CREATE TABLE IF NOT EXISTS sessions (
                 token_hash TEXT PRIMARY KEY,
                 account INTEGER NOT NULL REFERENCES accounts(id),
                 expires INTEGER NOT NULL
             );
             CREATE TABLE IF NOT EXISTS events (
                 id INTEGER PRIMARY KEY,
                 at INTEGER NOT NULL,
                 account INTEGER NOT NULL,
                 run TEXT,
                 kind TEXT NOT NULL,
                 data TEXT NOT NULL
             );
             CREATE INDEX IF NOT EXISTS events_by_account ON events (account, at);
             CREATE TRIGGER IF NOT EXISTS events_no_update BEFORE UPDATE ON events
                 BEGIN SELECT RAISE(ABORT, 'the gate events are append-only'); END;
             CREATE TRIGGER IF NOT EXISTS events_no_delete BEFORE DELETE ON events
                 BEGIN SELECT RAISE(ABORT, 'the gate events are append-only'); END;
             CREATE TABLE IF NOT EXISTS checkpoints (
                 run TEXT PRIMARY KEY,
                 crew TEXT NOT NULL,
                 document TEXT NOT NULL,
                 saved INTEGER NOT NULL
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
            grant: Arc::new([]),
        })
    }

    /// This world's identifier, which every account document carries.
    #[must_use]
    pub fn world(&self) -> &str {
        &self.world
    }

    /// The connection, also after a panic under the lock. One panic must not
    /// stop every pilot and every instance from saving: each write of more
    /// than one statement runs in a transaction, which rolls back as the
    /// panic unwinds, so the connection is as it was before that call.
    pub(crate) fn lock(&self) -> MutexGuard<'_, Connection> {
        self.connection.lock().unwrap_or_else(|poisoned| {
            self.connection.clear_poison();
            let connection = poisoned.into_inner();
            let rolled_back = if connection.is_autocommit() {
                Ok(())
            } else {
                connection.execute_batch("ROLLBACK")
            };
            match rolled_back {
                Ok(()) => tracing::error!(
                    "a store call panicked under the lock; its transaction was rolled back"
                ),
                Err(error) => tracing::error!(
                    %error,
                    "a store call panicked under the lock, and its transaction would not roll back"
                ),
            }
            connection
        })
    }

    /// Runs `work` against the store on a blocking thread.
    ///
    /// # Errors
    /// Whatever `work` returns; a panic in it fails this call alone, and the
    /// store carries on.
    pub async fn call<T, F>(&self, work: F) -> Result<T, StoreError>
    where
        T: Send + 'static,
        F: FnOnce(&Store) -> Result<T, StoreError> + Send + 'static,
    {
        let store = self.clone();
        tokio::task::spawn_blocking(move || work(&store))
            .await
            .map_err(|_| StoreError::Panicked)?
    }

    /// The same store, granting `grant` to every new pilot on top of the
    /// starting balance.
    #[must_use]
    pub fn with_grant(mut self, grant: &[(Item, u32)]) -> Self {
        self.grant = grant.into();
        self
    }

    /// Creates a pilot's account and grants the starting balance through the
    /// ledger, in one transaction.
    ///
    /// # Errors
    /// Fails if the callsign is malformed or taken.
    pub fn create_account(&self, callsign: &str) -> Result<AccountId, StoreError> {
        self.create_pilot(callsign, None)
    }

    /// Creates a pilot's account, with the passkey they registered if any, and
    /// grants the starting balance through the ledger, all in one transaction.
    ///
    /// # Errors
    /// Fails if the callsign is malformed or taken.
    pub fn create_pilot(
        &self,
        callsign: &str,
        passkey: Option<&str>,
    ) -> Result<AccountId, StoreError> {
        if !valid_callsign(callsign) {
            return Err(StoreError::BadCallsign(callsign.to_owned()));
        }
        let document = Account::new(&self.world, callsign).to_json()?;
        let mut connection = self.lock();
        let transaction = connection.transaction()?;
        let id = match transaction.execute(
            "INSERT INTO accounts (callsign, document) VALUES (?1, ?2)",
            params![callsign, document],
        ) {
            Ok(_) => AccountId(transaction.last_insert_rowid()),
            Err(rusqlite::Error::SqliteFailure(error, _))
                if error.code == rusqlite::ErrorCode::ConstraintViolation =>
            {
                return Err(StoreError::CallsignTaken(callsign.to_owned()));
            }
            Err(error) => return Err(error.into()),
        };
        if let Some(passkey) = passkey {
            transaction.execute(
                "INSERT INTO passkeys (account, credential) VALUES (?1, ?2)",
                params![id.0, passkey],
            )?;
        }
        let grants: Vec<Transfer> = std::iter::once((Item::Credits, STARTING_CREDITS))
            .chain(self.grant.iter().copied())
            .map(|(item, amount)| Transfer {
                item,
                amount,
                from: Party::Npc(Npc::Grant),
                to: Party::Account(id),
                reason: Reason::StartingBalance,
                run: None,
            })
            .collect();
        apply(&transaction, &self.world, &grants)?;
        transaction.commit()?;
        drop(connection);
        announce(&grants);
        Ok(id)
    }

    /// The passkeys an account registered, as webauthn-rs serialises them.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn passkeys(&self, id: AccountId) -> Result<Vec<String>, StoreError> {
        let connection = self.lock();
        let mut statement =
            connection.prepare("SELECT credential FROM passkeys WHERE account = ?1 ORDER BY id")?;
        let rows = statement.query_map(params![id.0], |row| row.get(0))?;
        Ok(rows.collect::<Result<_, _>>()?)
    }

    /// Replaces an account's passkeys, after a sign-in updated their counters.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn set_passkeys(&self, id: AccountId, passkeys: &[String]) -> Result<(), StoreError> {
        let mut connection = self.lock();
        let transaction = connection.transaction()?;
        transaction.execute("DELETE FROM passkeys WHERE account = ?1", params![id.0])?;
        for passkey in passkeys {
            transaction.execute(
                "INSERT INTO passkeys (account, credential) VALUES (?1, ?2)",
                params![id.0, passkey],
            )?;
        }
        transaction.commit()?;
        Ok(())
    }

    /// Issues a session token that signs the account in again without a passkey
    /// until it expires, so a reconnecting pilot is not asked again. Only its
    /// hash is stored, so a copy of the database cannot be used to sign in.
    ///
    /// # Errors
    /// Fails if no random bytes can be drawn or the database fails.
    pub fn issue_token(&self, id: AccountId) -> Result<String, StoreError> {
        let token = random_hex(32)?;
        let expires = unix_now() + SESSION_SECONDS;
        let connection = self.lock();
        connection.execute(
            "DELETE FROM sessions WHERE expires < ?1",
            params![unix_now()],
        )?;
        connection.execute(
            "INSERT INTO sessions (token_hash, account, expires) VALUES (?1, ?2, ?3)",
            params![hash(&token), id.0, expires],
        )?;
        Ok(token)
    }

    /// Saves a run's latest checkpoint, replacing the one before, with each
    /// pilot's seat in the run.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn save_checkpoint(
        &self,
        run: &str,
        crew: &[Seat],
        document: &str,
    ) -> Result<(), StoreError> {
        let crew: Vec<(i64, u16, u32)> = crew
            .iter()
            .map(|seat| (seat.account.0, seat.ship, seat.share))
            .collect();
        self.lock().execute(
            "INSERT INTO checkpoints (run, crew, document, saved) VALUES (?1, ?2, ?3, ?4)
             ON CONFLICT (run) DO UPDATE SET crew = ?2, document = ?3, saved = ?4",
            params![run, serde_json::to_string(&crew)?, document, unix_now()],
        )?;
        Ok(())
    }

    /// Forgets a run's checkpoint, once the run is over.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn delete_checkpoint(&self, run: &str) -> Result<(), StoreError> {
        self.lock()
            .execute("DELETE FROM checkpoints WHERE run = ?1", params![run])?;
        Ok(())
    }

    /// Every run checkpointed and not yet over: the runs a restarted server
    /// restores.
    ///
    /// # Errors
    /// Fails if the database does or a crew list is malformed.
    pub fn checkpoints(&self) -> Result<Vec<SavedRun>, StoreError> {
        let connection = self.lock();
        let mut statement =
            connection.prepare("SELECT run, crew, document FROM checkpoints ORDER BY saved")?;
        let rows = statement.query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
            ))
        })?;
        let mut runs = Vec::new();
        for row in rows {
            let (run, crew, document) = row?;
            // One unreadable row loses one run. It must not stop the server
            // starting, with every other run and every pilot's home behind it.
            let crew: Vec<SeatJson> = match serde_json::from_str(&crew) {
                Ok(crew) => crew,
                Err(error) => {
                    tracing::warn!(run, %error, "checkpoint skipped: its crew is unreadable");
                    continue;
                }
            };
            runs.push(SavedRun {
                run,
                crew: crew
                    .into_iter()
                    .map(|seat| match seat {
                        SeatJson::Crew(account, ship, share) => Seat {
                            account: AccountId(account),
                            ship,
                            share,
                        },
                        SeatJson::Solo(account, ship) => Seat {
                            account: AccountId(account),
                            ship,
                            share: 100,
                        },
                    })
                    .collect(),
                document,
            });
        }
        Ok(runs)
    }

    /// The account a session token signs in, if it is known and unexpired.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn resume(&self, token: &str) -> Result<Option<AccountId>, StoreError> {
        Ok(self
            .lock()
            .query_row(
                "SELECT account FROM sessions WHERE token_hash = ?1 AND expires >= ?2",
                params![hash(token), unix_now()],
                |row| row.get(0),
            )
            .optional()?
            .map(AccountId))
    }

    /// Applies `transfers` to the accounts they name and appends them to the
    /// ledger, all in one transaction: the only way stock changes. If any
    /// transfer fails, none is applied.
    ///
    /// # Errors
    /// Fails if an account is missing or cannot cover a transfer.
    pub fn transfer(&self, transfers: &[Transfer]) -> Result<(), StoreError> {
        let mut connection = self.lock();
        let transaction = connection.transaction()?;
        apply(&transaction, &self.world, transfers)?;
        transaction.commit()?;
        drop(connection);
        announce(transfers);
        Ok(())
    }

    /// Changes one account in one transaction: `change` edits the document's
    /// outpost, processor or hangar and returns the transfers that pay for the
    /// edit; both are saved together, or neither. A refusal from `change`, or
    /// a transfer the account cannot cover, leaves everything as it was and
    /// comes back as `Ok(Err(reason))`.
    ///
    /// # Errors
    /// Fails if the account is missing or the database fails.
    pub fn update<F>(&self, id: AccountId, change: F) -> Result<Result<Account, String>, StoreError>
    where
        F: FnOnce(&mut Account) -> Result<Vec<Transfer>, String>,
    {
        let mut connection = self.lock();
        let transaction = connection.transaction()?;
        let mut account = load(&transaction, id, &self.world)?;
        let before = account.clone();
        let transfers = match change(&mut account) {
            Ok(transfers) => transfers,
            Err(reason) => return Ok(Err(reason)),
        };
        // Stock moves only through transfers.
        account.credits = before.credits;
        account.stock = before.stock;
        save(&transaction, id, &account)?;
        match apply(&transaction, &self.world, &transfers) {
            Ok(()) => {}
            Err(StoreError::Transfer(error)) => return Ok(Err(error.to_string())),
            Err(error) => return Err(error),
        }
        let account = load(&transaction, id, &self.world)?;
        transaction.commit()?;
        drop(connection);
        announce(&transfers);
        Ok(Ok(account))
    }

    /// Records gate events (plan 7.1), all at `at` (Unix milliseconds).
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn record(&self, events: &[Recorded]) -> Result<(), StoreError> {
        let mut connection = self.lock();
        let transaction = connection.transaction()?;
        for recorded in events {
            transaction.execute(
                "INSERT INTO events (at, account, run, kind, data) VALUES (?1, ?2, ?3, ?4, ?5)",
                params![
                    recorded.at,
                    recorded.account,
                    recorded.run,
                    recorded.event.kind(),
                    serde_json::to_string(&recorded.event)?,
                ],
            )?;
        }
        transaction.commit()?;
        Ok(())
    }

    /// Every gate event, oldest first. Rows this server cannot read (from a
    /// newer build) are skipped.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn events(&self) -> Result<Vec<Recorded>, StoreError> {
        let connection = self.lock();
        let mut statement =
            connection.prepare("SELECT at, account, run, data FROM events ORDER BY at, id")?;
        let rows = statement.query_map([], |row| {
            Ok((
                row.get::<_, i64>(0)?,
                row.get::<_, i64>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, String>(3)?,
            ))
        })?;
        let mut events = Vec::new();
        for row in rows {
            let (at, account, run, data) = row?;
            if let Ok(event) = serde_json::from_str::<GateEvent>(&data) {
                events.push(Recorded {
                    at,
                    account,
                    run,
                    event,
                });
            }
        }
        Ok(events)
    }

    /// Every account's callsign, by account.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn callsigns(&self) -> Result<Vec<(i64, String)>, StoreError> {
        let connection = self.lock();
        let mut statement = connection.prepare("SELECT id, callsign FROM accounts ORDER BY id")?;
        let rows = statement.query_map([], |row| Ok((row.get(0)?, row.get(1)?)))?;
        Ok(rows.collect::<Result<_, _>>()?)
    }

    /// The ledger folded for one account: what every transfer in and out of it
    /// adds up to, by item. Equals the account's stock.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn ledger_balance(&self, id: AccountId) -> Result<Vec<(Item, i64)>, StoreError> {
        let connection = self.lock();
        let party = Party::Account(id).to_string();
        Item::ALL
            .iter()
            .map(|&item| {
                let total: i64 = connection.query_row(
                    "SELECT COALESCE(SUM(CASE WHEN sink = ?1 THEN amount ELSE 0 END), 0)
                          - COALESCE(SUM(CASE WHEN source = ?1 THEN amount ELSE 0 END), 0)
                     FROM ledger WHERE item = ?2",
                    params![party, item.name()],
                    |row| row.get(0),
                )?;
                Ok((item, total))
            })
            .collect()
    }

    /// The account with this callsign, ignoring case.
    ///
    /// # Errors
    /// Fails only if the database does.
    pub fn find(&self, callsign: &str) -> Result<Option<AccountId>, StoreError> {
        Ok(self
            .lock()
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
        let connection = self.lock();
        load(&connection, id, &self.world)
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

/// Applies transfers within an open transaction: loads each account once,
/// changes it, saves it, and appends every transfer to the ledger.
fn apply(connection: &Connection, world: &str, transfers: &[Transfer]) -> Result<(), StoreError> {
    let mut accounts: Vec<(AccountId, Account)> = Vec::new();
    for transfer in transfers {
        for id in transfer.accounts() {
            if !accounts.iter().any(|(held, _)| *held == id) {
                accounts.push((id, load(connection, id, world)?));
            }
        }
        for (id, account) in &mut accounts {
            transfer.apply(*id, account)?;
        }
    }
    let now = unix_now();
    for transfer in transfers {
        connection.execute(
            "INSERT INTO ledger (at, item, amount, source, sink, reason, run)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                now,
                transfer.item.name(),
                transfer.amount,
                transfer.from.to_string(),
                transfer.to.to_string(),
                transfer.reason.name(),
                transfer.run,
            ],
        )?;
    }
    for (id, account) in &accounts {
        save(connection, *id, account)?;
    }
    Ok(())
}

/// Logs transfers that have been committed: the economy's instrumentation
/// (AGENTS.md section 4.7). Called once the transaction is in and the lock
/// released, so a batch that rolled back is never announced, and nothing
/// that slows the log holds the store.
fn announce(transfers: &[Transfer]) {
    for transfer in transfers {
        info!(
            target: "frontier::ledger",
            item = transfer.item.name(),
            amount = transfer.amount,
            source = %transfer.from,
            sink = %transfer.to,
            reason = transfer.reason.name(),
            run = transfer.run.as_deref().unwrap_or(""),
            "transfer"
        );
    }
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
    fn a_panic_under_the_lock_rolls_back_and_leaves_the_store_working() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let spend = |amount| Transfer {
            item: Item::Credits,
            amount,
            from: Party::Account(id),
            to: Party::Npc(Npc::Market),
            reason: Reason::Bought,
            run: None,
        };
        let panicked = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            store.update(id, |account| {
                account.hangar.saved.rig = Some("half-written".to_owned());
                panic!("a bug in a game rule");
            })
        }));
        assert!(panicked.is_err());

        // Nothing of the panicked call was kept, and every later call works,
        // from this handle and from a clone of it.
        let account = store.clone().load(id).unwrap();
        assert_eq!((account.credits, account.hangar.saved.rig), (100, None));
        let after = store.update(id, |_| Ok(vec![spend(30)])).unwrap().unwrap();
        assert_eq!(after.credits, 70);
        assert!(
            store
                .ledger_balance(id)
                .unwrap()
                .contains(&(Item::Credits, 70))
        );
    }

    #[tokio::test]
    async fn a_panicking_call_fails_alone() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let failed = store
            .call(move |store| {
                store
                    .update(id, |_| panic!("a bug in a game rule"))
                    .map(drop)
            })
            .await;
        assert!(matches!(failed, Err(StoreError::Panicked)), "{failed:?}");
        let loaded = store.call(move |store| store.load(id)).await;
        assert_eq!(loaded.unwrap().credits, 100);
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
        let account = store.load(id).unwrap();
        assert_eq!(account.world, store.world());
        assert_eq!(account.credits, 100, "the starting balance");
    }

    #[test]
    fn a_grant_reaches_new_pilots_through_the_ledger() {
        let store = Store::in_memory()
            .unwrap()
            .with_grant(&[(Item::Credits, 900), (Item::ORE, 40)]);
        let id = store.create_account("Vega").unwrap();
        let account = store.load(id).unwrap();
        assert_eq!((account.credits, account.stock.ore), (1_000, 40));
        assert!(store.ledger_balance(id).unwrap().contains(&(Item::ORE, 40)));
    }

    #[test]
    fn gate_events_are_recorded_in_order_and_cannot_be_changed() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let at = |ms, event| Recorded {
            at: ms,
            account: id.0,
            run: Some("r".into()),
            event,
        };
        store
            .record(&[
                at(2_000, GateEvent::ScanStarted),
                at(1_000, GateEvent::SessionStarted),
            ])
            .unwrap();
        let events = store.events().unwrap();
        assert_eq!(events.len(), 2);
        assert_eq!(events[0].event, GateEvent::SessionStarted);
        assert!(!store.is_empty().unwrap());
        let changed = store.lock().execute("DELETE FROM events", []).is_err();
        assert!(changed, "append-only");
        assert_eq!(store.callsigns().unwrap(), [(id.0, "Vega".to_owned())]);
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

    fn stock_of(account: &Account) -> Vec<(Item, i64)> {
        Item::ALL
            .iter()
            .map(|&item| (item, i64::from(item.held(account))))
            .collect()
    }

    #[test]
    fn the_ledger_folded_equals_the_stock_saved() {
        let store = Store::in_memory().unwrap();
        let pilots: Vec<AccountId> = ["Vega", "Rigel", "Deneb"]
            .iter()
            .map(|callsign| store.create_account(callsign).unwrap())
            .collect();
        // A seeded walk through launches, banked cargo, payouts, refused
        // overdrafts and the home's actions, checking conservation after each.
        let mut seed: u32 = 2_266;
        let mut next = |below: u32| {
            seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            (seed >> 8) % below
        };
        for round in 0..600 {
            let pilot = pilots[next(3) as usize];
            let run = Some(format!("run-{round}"));
            let account = store.load(pilot).unwrap();
            if next(2) == 0 {
                use crate::home::{self, Action};
                use frontier_sim::goods::Good;
                let good = Good::ALL[next(5) as usize];
                let amount = 1 + next(6);
                let action = match next(8) {
                    0 => Some(Action::Install {
                        socket: ["a", "b"][next(2) as usize].into(),
                        facility: ["sensor_array", "refinery", "fabrication_bay"][next(3) as usize]
                            .into(),
                    }),
                    1 => Some(Action::Upgrade { socket: "a".into() }),
                    2 => Some(Action::Remove {
                        socket: ["a", "b"][next(2) as usize].into(),
                    }),
                    3 => Some(Action::Sell { good, amount }),
                    4 => Some(Action::Buy { good, amount }),
                    5 => Some(Action::Process {
                        recipe: ["alloy", "components"][next(2) as usize].into(),
                        batches: amount,
                    }),
                    6 => Some(Action::BuyFitting {
                        fitting: frontier_sim::fitting::FITTINGS[next(7) as usize].id.into(),
                    }),
                    _ => None,
                };
                let _ = match &action {
                    Some(action) => store
                        .update(pilot, |account| home::act(pilot, account, action))
                        .unwrap(),
                    None => {
                        let run = format!("run-{round}");
                        store
                            .update(pilot, |account| {
                                Ok(home::finish_processing(pilot, account, &run))
                            })
                            .unwrap()
                    }
                };
                for &id in &pilots {
                    assert_eq!(
                        store.ledger_balance(id).unwrap(),
                        stock_of(&store.load(id).unwrap()),
                        "account {id:?} after round {round}: {action:?}"
                    );
                }
                continue;
            }
            let batch = match next(4) {
                0 => {
                    let due = account.credits.min(frontier_sim::run::LAUNCH_COST);
                    if due == 0 {
                        continue;
                    }
                    vec![Transfer {
                        item: Item::Credits,
                        amount: due,
                        from: Party::Account(pilot),
                        to: Party::Npc(Npc::Supplies),
                        reason: Reason::LaunchSupplies,
                        run,
                    }]
                }
                1 => vec![
                    Transfer {
                        item: Item::ORE,
                        amount: 1 + next(20),
                        from: Party::Site,
                        to: Party::Account(pilot),
                        reason: Reason::Banked,
                        run: run.clone(),
                    },
                    Transfer {
                        item: Item::Credits,
                        amount: 100,
                        from: Party::Npc(Npc::Contract),
                        to: Party::Account(pilot),
                        reason: Reason::ContractPayout,
                        run,
                    },
                ],
                2 => vec![Transfer {
                    item: Item::VOLATILES,
                    amount: 1 + next(5),
                    from: Party::Site,
                    to: Party::Account(pilot),
                    reason: Reason::Banked,
                    run,
                }],
                _ => vec![
                    // A batch that cannot be covered: none of it may apply.
                    Transfer {
                        item: Item::ORE,
                        amount: 1,
                        from: Party::Site,
                        to: Party::Account(pilot),
                        reason: Reason::Banked,
                        run: run.clone(),
                    },
                    Transfer {
                        item: Item::SALVAGE,
                        amount: account.stock.salvage + 1,
                        from: Party::Account(pilot),
                        to: Party::Npc(Npc::Supplies),
                        reason: Reason::LaunchSupplies,
                        run,
                    },
                ],
            };
            let _ = store.transfer(&batch);
            for &id in &pilots {
                assert_eq!(
                    store.ledger_balance(id).unwrap(),
                    stock_of(&store.load(id).unwrap()),
                    "account {id:?} after round {round}"
                );
            }
        }
    }

    #[test]
    fn a_batch_that_cannot_be_covered_changes_nothing() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let before = store.load(id).unwrap();
        let result = store.transfer(&[
            Transfer {
                item: Item::ORE,
                amount: 5,
                from: Party::Site,
                to: Party::Account(id),
                reason: Reason::Banked,
                run: None,
            },
            Transfer {
                item: Item::Credits,
                amount: 500,
                from: Party::Account(id),
                to: Party::Npc(Npc::Supplies),
                reason: Reason::LaunchSupplies,
                run: None,
            },
        ]);
        assert!(matches!(
            result,
            Err(StoreError::Transfer(TransferError::Insufficient { .. }))
        ));
        assert_eq!(store.load(id).unwrap(), before);
        let rows: i64 = store
            .lock()
            .query_row("SELECT COUNT(*) FROM ledger", [], |row| row.get(0))
            .unwrap();
        assert_eq!(rows, 1, "only the starting balance");
    }

    #[test]
    fn the_ledger_refuses_to_be_edited() {
        let store = Store::in_memory().unwrap();
        store.create_account("Vega").unwrap();
        let connection = store.lock();
        assert!(
            connection
                .execute("UPDATE ledger SET amount = 1", [])
                .is_err()
        );
        assert!(connection.execute("DELETE FROM ledger", []).is_err());
    }

    #[test]
    fn a_pilot_is_created_with_their_passkey_and_signs_in_again_by_token() {
        let store = Store::in_memory().unwrap();
        let id = store.create_pilot("Vega", Some("{\"cred\":1}")).unwrap();
        assert_eq!(store.passkeys(id).unwrap(), ["{\"cred\":1}"]);
        store
            .set_passkeys(id, &["{\"cred\":2}".to_owned()])
            .unwrap();
        assert_eq!(store.passkeys(id).unwrap(), ["{\"cred\":2}"]);

        let token = store.issue_token(id).unwrap();
        assert_eq!(token.len(), 64);
        assert_eq!(store.resume(&token).unwrap(), Some(id));
        assert_eq!(store.resume("not a token").unwrap(), None);
        let stored: String = store
            .lock()
            .query_row("SELECT token_hash FROM sessions", [], |row| row.get(0))
            .unwrap();
        assert_ne!(stored, token, "only the hash is kept");
    }

    #[test]
    fn an_expired_token_signs_nobody_in() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let token = store.issue_token(id).unwrap();
        store
            .lock()
            .execute("UPDATE sessions SET expires = 0", [])
            .unwrap();
        assert_eq!(store.resume(&token).unwrap(), None);
    }

    #[test]
    fn keeps_the_latest_checkpoint_of_each_run_until_it_is_over() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        let seat = |ship, share| Seat {
            account: id,
            ship,
            share,
        };
        store
            .save_checkpoint("run-a", &[seat(0, 100)], "{\"v\":1}")
            .unwrap();
        store
            .save_checkpoint("run-a", &[seat(0, 60)], "{\"v\":2}")
            .unwrap();
        store
            .save_checkpoint("run-b", &[seat(3, 100)], "{}")
            .unwrap();
        let saved = store.checkpoints().unwrap();
        assert_eq!(saved.len(), 2);
        assert_eq!(saved[0].document, "{\"v\":2}");
        assert_eq!(saved[0].crew, [seat(0, 60)]);
        store.delete_checkpoint("run-a").unwrap();
        assert_eq!(store.checkpoints().unwrap().len(), 1);
    }

    #[test]
    fn a_checkpoint_with_an_unreadable_crew_is_skipped_and_the_rest_are_kept() {
        let store = Store::in_memory().unwrap();
        let pilot = store.create_account("Vega").unwrap();
        let seat = Seat {
            account: pilot,
            ship: 0,
            share: 100,
        };
        store.save_checkpoint("first", &[seat], "{}").unwrap();
        store
            .lock()
            .execute(
                "INSERT INTO checkpoints (run, crew, document, saved) VALUES ('bad', 'not json', '{}', 0)",
                [],
            )
            .unwrap();
        store.save_checkpoint("last", &[seat], "{}").unwrap();
        let runs: Vec<String> = store
            .checkpoints()
            .unwrap()
            .into_iter()
            .map(|saved| saved.run)
            .collect();
        assert_eq!(runs.len(), 2, "{runs:?}");
        assert!(runs.contains(&"first".to_owned()) && runs.contains(&"last".to_owned()));
    }

    #[test]
    fn a_checkpoint_saved_before_crews_reads_as_a_solo_seat() {
        let store = Store::in_memory().unwrap();
        let id = store.create_account("Vega").unwrap();
        store
            .lock()
            .execute(
                "INSERT INTO checkpoints (run, crew, document, saved) VALUES ('old', ?1, '{}', 0)",
                params![format!("[[{},2]]", id.0)],
            )
            .unwrap();
        let saved = store.checkpoints().unwrap();
        assert_eq!(
            saved[0].crew,
            [Seat {
                account: id,
                ship: 2,
                share: 100
            }]
        );
    }

    #[test]
    fn a_wipe_leaves_an_empty_world_with_a_new_identity() {
        let dir = std::env::temp_dir().join(format!("frontier-wipe-{}", random_hex(6).unwrap()));
        let path = dir.join("world.db");
        let store = Store::open(&path).unwrap();
        let id = store.create_account("Vega").unwrap();
        store.issue_token(id).unwrap();
        store
            .save_checkpoint(
                "run",
                &[Seat {
                    account: id,
                    ship: 0,
                    share: 100,
                }],
                "{}",
            )
            .unwrap();
        let old_world = store.world().to_owned();
        assert!(!store.is_empty().unwrap());
        drop(store);

        let wiped = Store::wipe(&path).unwrap();
        assert!(wiped.is_empty().unwrap());
        assert_ne!(wiped.world(), old_world);
        assert_eq!(wiped.find("Vega").unwrap(), None);
        let _ = std::fs::remove_dir_all(dir);
    }
}
