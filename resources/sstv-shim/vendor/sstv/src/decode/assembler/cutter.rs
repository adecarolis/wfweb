//! Cutting an image's timing sequences into tones: each sequence is anchored
//! on its sync pulse, stretched to end where the next sequence's sync pulse
//! puts it, and cut into one tone per control step and one per pixel, as the
//! encoder emits them.

use alloc::collections::VecDeque;
use alloc::vec::Vec;

use super::clock::SampleClock;
use super::estimates::Estimates;
use super::sync_lock::SyncLock;
use super::tone_in_progress::CompletedTone;
use crate::modes::step::Step;
use crate::modes::{Mode, SYNC_FREQUENCY};
use crate::units::{Duration, Frequency, Tone};
use crate::{Hz, ms};

/// How long after a sync pulse is expected to end the cutter waits for it
/// before assuming it was lost. A tone is reported only once the next one
/// has lasted a few milliseconds.
const SYNC_GRACE: Duration = ms!(10);
const SYNC_FREQUENCY_TOLERANCE: Frequency = Hz!(50);

/// A stretch of a timing sequence that becomes one tone, measured in samples
/// from the sequence's start.
#[derive(Clone, Copy)]
struct Slot {
    start: f64,
    length: f64,
}

/// A mode's timing sequence, measured in samples.
struct SequenceLayout {
    length: f64,
    /// One slot per tone the encoder emits: each control step and each pixel.
    slots: Vec<Slot>,
    /// Where the last scan begins.
    last_scan_start: f64,
    /// Where the first sync pulse ends.
    sync_end: f64,
    /// The sync pulse as the tone splitter reports it, and how far it may
    /// stray.
    sync: Tone,
    sync_tolerance: Tone,
    /// How far a sync pulse may stray from where it is expected.
    sync_window: f64,
    grace: f64,
}

impl SequenceLayout {
    fn new(mode: Mode, clock: SampleClock) -> Self {
        let (sync_offset, sync_duration) = mode.sync_pulse();
        let width = mode.resolution.0;
        let slots = mode
            .step_offsets()
            .flat_map(|(offset, step)| step_slots(clock.samples(offset), step, width, clock))
            .collect();
        let last_scan_start = mode
            .step_offsets()
            .filter(|(_, step)| step.is_scan())
            .map(|(offset, _)| clock.samples(offset))
            .last()
            .unwrap_or(0.0);

        Self {
            length: clock.samples(mode.sequence_duration()),
            slots,
            last_scan_start,
            sync_end: clock.samples(sync_offset + sync_duration),
            sync: Tone::new(SYNC_FREQUENCY, sync_duration),
            sync_tolerance: Tone::new(SYNC_FREQUENCY_TOLERANCE, sync_duration / 2),
            sync_window: clock.samples(sync_duration),
            grace: clock.samples(SYNC_GRACE),
        }
    }
}

/// The slots of a step beginning at `start`: one for a control step, one
/// per pixel for a scan.
fn step_slots(
    start: f64,
    step: &Step,
    width: usize,
    clock: SampleClock,
) -> impl Iterator<Item = Slot> {
    let (count, length) = match step {
        Step::Control(tone) => (1, clock.samples(tone.duration)),
        Step::Scan(_, duration) => (width, clock.samples(*duration) / width as f64),
    };
    (0..count).map(move |index| Slot {
        start: start + index as f64 * length,
        length,
    })
}

/// Where the image being cut stands.
pub(super) struct SequenceCutter {
    mode: Mode,
    layout: SequenceLayout,
    clock: SampleClock,
    /// The (fractional) sample at which the current sequence begins.
    sequence_start: f64,
    /// Where the next sequence begins, once its sync pulse has been seen.
    next_sequence_start: Option<f64>,
    /// The timing is lost: the last sequence was cut at nominal timing
    /// because its sync pulse did not land where expected, and none has
    /// since. An audio frame dropped or stalled on the way (a sound card
    /// underrun, a lost network packet) moves every later sync pulse off
    /// the nominal timing by the same amount, so it never lands in the
    /// window again and nominal timing garbles the rest of the image.
    lost: bool,
    /// While lost, watches for three evenly spaced sync pulses in the new
    /// timing to re-anchor on, exactly as a headerless signal is acquired.
    relock: Option<SyncLock>,
    sequences_left: usize,
}

impl SequenceCutter {
    /// Cut an image whose first sequence begins at `sequence_start`.
    pub fn new(mode: Mode, clock: SampleClock, sequence_start: f64) -> Self {
        Self {
            mode,
            layout: SequenceLayout::new(mode, clock),
            clock,
            sequence_start,
            next_sequence_start: None,
            lost: false,
            relock: None,
            sequences_left: mode.resolution.1 / mode.lines_per_sequence,
        }
    }

    /// Cut an image whose header ended at sample `header_end`, with the stop
    /// bit merged into the following sync pulse: the first sequence's own
    /// sync pulse or, for modes with a starting sync pulse, the one that
    /// precedes the first sequence.
    pub fn after_header(mode: Mode, clock: SampleClock, header_end: u64) -> Self {
        let sequence_start = if mode.has_starting_sync_pulse() {
            header_end as f64
        } else {
            header_end as f64 - clock.samples(mode.sync_pulse().1)
        };
        Self::new(mode, clock, sequence_start)
    }

    pub const fn mode(&self) -> Mode {
        self.mode
    }

    pub const fn sequence_start(&self) -> f64 {
        self.sequence_start
    }

    pub const fn is_finished(&self) -> bool {
        self.sequences_left == 0
    }

    pub const fn is_lost(&self) -> bool {
        self.lost
    }

    /// The length of a timing sequence, in samples.
    pub const fn sequence_length(&self) -> f64 {
        self.layout.length
    }

    /// Take a tone split where the frequency changes as the next sequence's
    /// sync pulse, if it is one and ends about where expected.
    pub fn sync_found(&mut self, completed: CompletedTone) {
        let layout = &self.layout;
        if self.next_sequence_start.is_some()
            || !completed.tone.is_near(layout.sync, layout.sync_tolerance)
        {
            return;
        }
        let sequence_start = completed.end as f64 - layout.sync_end;
        if (sequence_start - self.expected_next_start()).abs() <= layout.sync_window {
            self.next_sequence_start = Some(sequence_start);
            self.lost = false;
            self.relock = None;
        }
    }

    /// While the timing is lost, take in a tone split where the frequency
    /// changes: once three sync pulses one line apart have been seen in the
    /// signal's new timing, the current sequence is re-anchored on them.
    /// Sequences with several sync pulses (Robot 36's line pair) take the
    /// alignment whose control tones match, so the colour differences do
    /// not swap. The sequences cut at nominal timing meanwhile are garbled;
    /// everything after follows the signal again.
    pub fn relock(&mut self, completed: CompletedTone, estimates: &Estimates) {
        if !self.lost {
            return;
        }
        let lock = self
            .relock
            .get_or_insert_with(|| SyncLock::new(self.mode, self.clock));
        let Some(first_sync_end) = lock.push(completed) else {
            return;
        };
        let start = lock.first_sequence_start(first_sync_end, estimates);
        // The sequence the signal is in now: the start, in the new timing,
        // nearest the one assumed for the current sequence.
        let length = self.layout.length;
        let sequences = ((self.sequence_start - start) / length).round();
        self.sequence_start = start + sequences * length;
        self.next_sequence_start = None;
        self.lost = false;
        self.relock = None;
    }

    /// Where the next sequence begins once the current one can be cut: at
    /// its sync pulse once seen, or at the nominal timing once the sync pulse
    /// is overdue. `None` while it may still arrive.
    pub fn next_start_if_ready(&self, estimates_end: u64) -> Option<f64> {
        if self.next_sequence_start.is_some() {
            return self.next_sequence_start;
        }
        let layout = &self.layout;
        let expected = self.expected_next_start();
        let overdue = expected + layout.sync_end + layout.sync_window + layout.grace;
        (estimates_end as f64 >= overdue).then_some(expected)
    }

    /// Where the next sequence begins once the frequencies have run out.
    pub fn next_start(&self) -> f64 {
        self.next_sequence_start
            .unwrap_or_else(|| self.expected_next_start())
    }

    /// Whether the estimates up to `estimates_end` reach into the current
    /// sequence's last scan.
    pub fn last_scan_begun(&self, estimates_end: u64) -> bool {
        self.sequence_start + self.layout.last_scan_start < estimates_end as f64
    }

    /// Cut the current sequence, stretched to end at `next_start`, into
    /// tones, and move on to the next sequence.
    pub fn cut(&mut self, next_start: f64, estimates: &Estimates, tones: &mut VecDeque<Tone>) {
        let stretch = (next_start - self.sequence_start) / self.layout.length;
        tones.extend(self.layout.slots.iter().map(|slot| {
            let start = self.sequence_start + slot.start * stretch;
            let length = slot.length * stretch;
            Tone::new(estimates.mean(start, length), self.clock.duration(length))
        }));

        self.lost = self.next_sequence_start.is_none();
        self.sequence_start = next_start;
        self.next_sequence_start = None;
        self.sequences_left -= 1;
    }

    /// Where the next sequence begins if the signal keeps its nominal timing.
    fn expected_next_start(&self) -> f64 {
        self.sequence_start + self.layout.length
    }
}
