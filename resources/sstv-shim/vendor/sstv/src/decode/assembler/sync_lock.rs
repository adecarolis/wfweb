//! Locating an image in a known mode by its line sync pulses, for signals
//! whose header is missing or unreadable.

use alloc::collections::VecDeque;

use super::clock::SampleClock;
use super::estimates::Estimates;
use super::tone_in_progress::CompletedTone;
use crate::Hz;
use crate::modes::step::Step;
use crate::modes::{Mode, SYNC_FREQUENCY};
use crate::units::{Duration, Frequency};

/// How far a sync pulse or control tone may stray from its frequency.
const FREQUENCY_TOLERANCE: Frequency = Hz!(50);

/// Watches the tones for three sync pulses spaced exactly one line apart,
/// which noise does not produce.
pub(super) struct SyncLock {
    mode: Mode,
    clock: SampleClock,
    sync_duration: Duration,
    /// The line spacing of the sync pulses, in samples.
    spacing: f64,
    /// How far the spacing may deviate, in samples.
    window: f64,
    /// Where recent sync pulse candidates ended, oldest first.
    candidates: VecDeque<u64>,
}

impl SyncLock {
    pub fn new(mode: Mode, clock: SampleClock) -> Self {
        let (_, sync_duration) = mode.sync_pulse();
        Self {
            mode,
            clock,
            sync_duration,
            spacing: clock.samples(mode.sync_spacing()),
            window: clock.samples(sync_duration),
            candidates: VecDeque::new(),
        }
    }

    pub const fn mode(&self) -> Mode {
        self.mode
    }

    /// Take in a tone split where the frequency changes. Returns where the
    /// first of three evenly spaced sync pulses ended once this tone
    /// completes them.
    ///
    /// A sync pulse is at least half its nominal duration but may be longer:
    /// the VIS stop bit, at the same frequency, merges with the first line's
    /// sync pulse. Measuring from the pulses' ends covers both.
    pub fn push(&mut self, completed: CompletedTone) -> Option<u64> {
        let tone = completed.tone;
        let is_sync = tone.frequency.abs_diff(SYNC_FREQUENCY) <= FREQUENCY_TOLERANCE
            && tone.duration >= self.sync_duration / 2;
        if !is_sync {
            return None;
        }

        let first = self.first_of_three_ending_at(completed.end);
        self.remember(completed.end);
        first
    }

    /// Where the first sequence begins, given the end of the sync pulse the
    /// lock found first. Sequences with several sync pulses (Robot 36's line
    /// pair) could have been entered at any of them: the alignment whose
    /// control tones match the mode best wins, so entering at a pair's second
    /// line does not swap the colour differences. An alignment that begins
    /// before the oldest kept estimate moves on by one sequence; within one
    /// sync pulse of it, the alignment is the stream's very start, which the
    /// demodulator's warm-up shortens slightly.
    pub fn first_sequence_start(&self, first_sync_end: u64, estimates: &Estimates) -> f64 {
        let sync_duration = self.clock.samples(self.sync_duration);
        let sync_start = first_sync_end as f64 - sync_duration;
        let sequence = self.clock.samples(self.mode.sequence_duration());
        let earliest = estimates.start() as f64 - sync_duration;

        let mut best = (0, sync_start);
        for offset in self.mode.sync_offsets() {
            let mut start = sync_start - self.clock.samples(offset);
            if start < earliest {
                start += sequence;
            }
            let score = self.matching_control_tones(start, estimates);
            if score > best.0 {
                best = (score, start);
            }
        }
        best.1
    }

    /// The first of two earlier candidates that form an evenly spaced triple
    /// with a sync pulse ending at `end`.
    fn first_of_three_ending_at(&self, end: u64) -> Option<u64> {
        let middle = *self
            .candidates
            .iter()
            .rev()
            .find(|&&middle| self.one_line_apart(middle, end))?;
        self.candidates
            .iter()
            .find(|&&first| first < middle && self.one_line_apart(first, middle))
            .copied()
    }

    /// Remember a candidate, forgetting those too old to be the first of
    /// three any more.
    fn remember(&mut self, end: u64) {
        self.candidates.push_back(end);
        let horizon = self.spacing * 2.0 + self.window;
        self.candidates
            .retain(|&candidate| (end - candidate) as f64 <= horizon);
    }

    fn one_line_apart(&self, earlier: u64, later: u64) -> bool {
        ((later - earlier) as f64 - self.spacing).abs() <= self.window
    }

    /// How many of the sequence's control tones other than sync pulses the
    /// estimates match, if the sequence begins at `start`. Only each tone's
    /// middle half counts: its edges carry the demodulator's smeared
    /// transitions to the neighbouring tones. A tone past the newest estimate
    /// is checked a whole sequence earlier, where it repeats.
    fn matching_control_tones(&self, start: f64, estimates: &Estimates) -> usize {
        let sequence = self.clock.samples(self.mode.sequence_duration());
        let end = estimates.end() as f64;
        self.mode
            .step_offsets()
            .filter(|(offset, step)| {
                let Step::Control(tone) = step else {
                    return false;
                };
                let length = self.clock.samples(tone.duration);
                let middle_start = start + self.clock.samples(*offset) + length / 4.0;
                let overshoot = (middle_start + length / 2.0 - end).max(0.0);
                let sequences_back = (overshoot / sequence).ceil();
                let received =
                    estimates.mean(middle_start - sequences_back * sequence, length / 2.0);
                tone.frequency != SYNC_FREQUENCY
                    && received.abs_diff(tone.frequency) <= FREQUENCY_TOLERANCE
            })
            .count()
    }
}
