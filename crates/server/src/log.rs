//! Where log lines go: to a writer, through a bounded queue and a thread of
//! their own.
//!
//! `tracing`'s default writer writes to standard output on the thread that
//! logs. Those are the runtime's worker threads, and for the ledger's lines
//! a thread holding the store's lock. Standard output can stop taking bytes:
//! a pipe whose reader has stalled, a console with text selected. Every
//! worker then blocks on its next line and the whole server stops, with no
//! tick and no reply, until the reader comes back.
//!
//! Here the thread that logs only queues the line. If the queue is full the
//! line is dropped and counted, and the count is written when the reader
//! catches up: losing log lines is better than stopping the game.

use std::io::{self, Write};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver, SyncSender, TrySendError};
use std::time::Duration;

use tracing_subscriber::fmt::MakeWriter;

/// Lines that may wait for a slow reader before more are dropped.
pub const QUEUE: usize = 1_024;

/// How long the end of the process waits for the lines queued so far.
const FLUSH_WAIT: Duration = Duration::from_secs(1);

enum Entry {
    Line(Vec<u8>),
    Flush(mpsc::Sender<()>),
}

/// The queue's sending end. Cheap to clone; give one to `tracing` as its
/// writer.
#[derive(Clone)]
pub struct Lines {
    queue: SyncSender<Entry>,
    dropped: Arc<AtomicU64>,
}

impl Lines {
    /// Starts the thread that writes queued lines to `out`.
    ///
    /// # Errors
    /// Fails if the thread cannot be started.
    pub fn to(out: impl Write + Send + 'static) -> io::Result<Self> {
        let (queue, entries) = mpsc::sync_channel(QUEUE);
        let dropped = Arc::new(AtomicU64::new(0));
        let counter = Arc::clone(&dropped);
        std::thread::Builder::new()
            .name("log".to_owned())
            .spawn(move || write_lines(out, &entries, &counter))?;
        Ok(Self { queue, dropped })
    }

    /// Waits, briefly, until every line queued so far has been written. A
    /// reader that has stalled is not waited for.
    pub fn flush(&self) {
        let (done, written) = mpsc::channel();
        if self.queue.try_send(Entry::Flush(done)).is_ok() {
            let _ = written.recv_timeout(FLUSH_WAIT);
        }
    }

    /// Flushes when the returned value is dropped: hold it for the length of
    /// `main`, so the last lines of a stopping server are not lost with it.
    #[must_use]
    pub fn flush_on_drop(&self) -> FlushOnDrop {
        FlushOnDrop(self.clone())
    }
}

/// See [`Lines::flush_on_drop`].
pub struct FlushOnDrop(Lines);

impl Drop for FlushOnDrop {
    fn drop(&mut self) {
        self.0.flush();
    }
}

fn write_lines(mut out: impl Write, entries: &Receiver<Entry>, dropped: &AtomicU64) {
    for entry in entries {
        // A write that fails has nowhere to be reported; the next line tries
        // again.
        let lost = dropped.swap(0, Ordering::Relaxed);
        if lost > 0 {
            let _ = writeln!(
                out,
                "{lost} log lines were dropped: the log's reader fell behind"
            );
        }
        match entry {
            Entry::Line(line) => {
                let _ = out.write_all(&line);
            }
            Entry::Flush(done) => {
                let _ = out.flush();
                let _ = done.send(());
            }
        }
    }
}

/// One log line being formatted. It is queued whole when dropped.
pub struct Line<'a> {
    text: Vec<u8>,
    lines: &'a Lines,
}

impl Write for Line<'_> {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        self.text.extend_from_slice(bytes);
        Ok(bytes.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl Drop for Line<'_> {
    fn drop(&mut self) {
        if self.text.is_empty() {
            return;
        }
        let line = Entry::Line(std::mem::take(&mut self.text));
        if let Err(TrySendError::Full(_)) = self.lines.queue.try_send(line) {
            self.lines.dropped.fetch_add(1, Ordering::Relaxed);
        }
    }
}

impl<'a> MakeWriter<'a> for Lines {
    type Writer = Line<'a>;

    fn make_writer(&'a self) -> Self::Writer {
        Line {
            text: Vec::new(),
            lines: self,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;
    use std::time::Instant;

    /// What reached the writer, shared with the test.
    #[derive(Clone, Default)]
    struct Written(Arc<Mutex<Vec<u8>>>);

    impl Written {
        fn text(&self) -> String {
            String::from_utf8(self.0.lock().unwrap().clone()).unwrap()
        }
    }

    impl Write for Written {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            self.0.lock().unwrap().extend_from_slice(bytes);
            Ok(bytes.len())
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    /// A writer that takes nothing until the test lets it, as a pipe does
    /// when nobody reads it.
    struct Stalled {
        open: Receiver<()>,
        opened: bool,
        to: Written,
    }

    impl Write for Stalled {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            if !self.opened {
                let _ = self.open.recv();
                self.opened = true;
            }
            self.to.write(bytes)
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    fn log(lines: &Lines, text: &str) {
        writeln!(lines.make_writer(), "{text}").unwrap();
    }

    #[test]
    fn lines_are_written_whole_and_in_order() {
        let written = Written::default();
        let lines = Lines::to(written.clone()).unwrap();
        for n in 0..200 {
            let mut line = lines.make_writer();
            // As the formatter may: one line in several writes.
            write!(line, "line ").unwrap();
            writeln!(line, "{n}").unwrap();
        }
        lines.flush();
        let expected: String = (0..200).map(|n| format!("line {n}\n")).collect();
        assert_eq!(written.text(), expected);
    }

    #[test]
    fn a_stalled_reader_costs_lines_and_never_blocks_the_thread_that_logs() {
        let written = Written::default();
        let (open, gate) = mpsc::channel();
        let lines = Lines::to(Stalled {
            open: gate,
            opened: false,
            to: written.clone(),
        })
        .unwrap();

        // Far more than the queue holds, while nothing is being read.
        let started = Instant::now();
        let total = QUEUE * 3;
        for n in 0..total {
            log(&lines, &format!("line {n}"));
        }
        assert!(
            started.elapsed() < Duration::from_secs(2),
            "logging waited for the reader: {:?}",
            started.elapsed()
        );
        lines.flush();
        assert_eq!(written.text(), "", "nothing read yet");

        // The reader comes back: what was queued arrives, with the count of
        // what was not, then new lines as before.
        open.send(()).unwrap();
        let drained = Instant::now();
        while written.text().lines().count() < QUEUE && drained.elapsed().as_secs() < 5 {
            std::thread::sleep(Duration::from_millis(5));
        }
        log(&lines, "after");
        lines.flush();
        let text = written.text();
        let kept: Vec<usize> = text
            .lines()
            .filter_map(|line| line.strip_prefix("line ")?.parse().ok())
            .collect();
        assert_eq!(kept[0], 0, "the oldest lines are kept");
        assert!(kept.windows(2).all(|pair| pair[0] < pair[1]), "in order");
        assert!(
            (QUEUE..=QUEUE + 1).contains(&kept.len()),
            "{} lines kept",
            kept.len()
        );
        // Counted in one notice or, if the writer woke part-way, in two.
        let counted: usize = text
            .lines()
            .filter_map(|line| {
                line.split_once(" log lines were dropped")?
                    .0
                    .parse::<usize>()
                    .ok()
            })
            .sum();
        assert_eq!(kept.len() + counted, total, "each line written or counted");
        assert!(text.ends_with("after\n"));
    }
}
