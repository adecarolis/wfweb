//! Splits the frequency track into tones where the frequency changes.

use super::clock::SampleClock;
use crate::units::{Duration, Frequency, Tone};
use crate::{Hz, ms};

/// How far an estimate may stray from the current tone's frequency and still
/// belong to it, and from the candidate for the next tone and still continue
/// it. Half the 100 Hz step between the VIS start bit and a data bit.
const SPLIT_THRESHOLD: Frequency = Hz!(50);
/// How long the frequency must stay away from the current tone, and
/// consistently at one frequency, before a new tone begins. Shorter or
/// erratic off-tone stretches are noise and are absorbed into the current
/// tone; the consistency also keeps the demodulator's smeared estimates at a
/// transition out of the new tone's frequency.
const MIN_NEW_TONE: Duration = ms!(3);

/// A tone split from the frequency track, and the sample at which it ended.
/// Tones follow each other without gaps: each one begins where the previous
/// one ended.
#[derive(Clone, Copy)]
pub(super) struct CompletedTone {
    pub tone: Tone,
    pub end: u64,
}

/// The estimates collected for the tone currently being received.
pub(super) struct ToneInProgress {
    /// The estimates that set the tone's frequency.
    tone: RunningMean,
    /// Every estimate attributed to the tone, including absorbed off-tone
    /// stretches.
    length: u64,
    /// The number of estimates since the frequency last matched the tone.
    off_tone_length: u64,
    /// The most recent off-tone estimates that agree with each other: the
    /// candidate for the next tone.
    candidate: RunningMean,
    /// Candidate length at which the candidate becomes the next tone.
    min_new_tone: u64,
    /// The number of estimates pushed so far.
    pushed: u64,
    clock: SampleClock,
}

impl ToneInProgress {
    pub fn new(clock: SampleClock) -> Self {
        Self {
            tone: RunningMean::default(),
            length: 0,
            off_tone_length: 0,
            candidate: RunningMean::default(),
            min_new_tone: clock.whole_samples(MIN_NEW_TONE).max(1),
            pushed: 0,
            clock,
        }
    }

    /// Add an estimate. Returns the previous tone if this estimate completes
    /// an off-tone stretch long enough to start a new one.
    pub fn push(&mut self, frequency: Frequency) -> Option<CompletedTone> {
        self.pushed += 1;
        let tone_frequency = self.tone.frequency().unwrap_or(frequency);

        let no_tone_switch_has_been_detected =
            frequency.abs_diff(tone_frequency) <= SPLIT_THRESHOLD;
        if no_tone_switch_has_been_detected {
            self.absorb_off_tone_stretch(frequency);
            return None;
        }

        self.extend_candidate(frequency);
        (self.candidate.count >= self.min_new_tone)
            .then(|| self.switch_to_candidate(tone_frequency))
    }

    /// The frequency is back at the tone: the off-tone stretch was noise.
    fn absorb_off_tone_stretch(&mut self, frequency: Frequency) {
        self.length += self.off_tone_length + 1;
        self.off_tone_length = 0;
        self.candidate = RunningMean::default();
        self.tone.add(frequency);
    }

    /// Add an off-tone estimate to the candidate, which restarts if the
    /// estimate disagrees with it.
    fn extend_candidate(&mut self, frequency: Frequency) {
        self.off_tone_length += 1;

        let candidate_frequency_is_distinct_enough =
            frequency.abs_diff(self.candidate.frequency().unwrap_or(frequency)) > SPLIT_THRESHOLD;
        if candidate_frequency_is_distinct_enough {
            self.candidate = RunningMean::default();
        }
        self.candidate.add(frequency);
    }

    /// Complete the tone, and continue with the candidate as the next one.
    /// The tone ends where the off-tone stretch began.
    fn switch_to_candidate(&mut self, tone_frequency: Frequency) -> CompletedTone {
        let completed = CompletedTone {
            tone: Tone::new(tone_frequency, self.clock.duration(self.length as f64)),
            end: self.pushed - self.off_tone_length,
        };
        self.tone = core::mem::take(&mut self.candidate);
        self.length = self.off_tone_length;
        self.off_tone_length = 0;
        completed
    }
}

/// The mean of a run of frequency estimates.
#[derive(Default)]
struct RunningMean {
    sum: u64,
    count: u64,
}

impl RunningMean {
    fn frequency(&self) -> Option<Frequency> {
        let mean = self.sum.checked_div(self.count)?;
        Some(Frequency::from_hz(u32::try_from(mean).unwrap_or(u32::MAX)))
    }

    fn add(&mut self, frequency: Frequency) {
        self.sum += u64::from(frequency.hz());
        self.count += 1;
    }
}
