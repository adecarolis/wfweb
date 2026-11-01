//! The frequency estimates kept for cutting an image by its timing.

use alloc::collections::VecDeque;

use crate::units::Frequency;

/// A window onto the frequency track, addressed by sample index since the
/// start of the stream.
#[derive(Default)]
pub(super) struct Estimates {
    buffer: VecDeque<Frequency>,
    /// The sample index of `buffer[0]`.
    start: u64,
}

impl Estimates {
    pub fn push(&mut self, frequency: Frequency) {
        self.buffer.push_back(frequency);
    }

    /// The sample index of the oldest estimate kept.
    pub const fn start(&self) -> u64 {
        self.start
    }

    /// The sample index after the newest estimate.
    pub fn end(&self) -> u64 {
        self.start + self.buffer.len() as u64
    }

    /// Drop the estimates before sample `sample`.
    pub fn forget_before(&mut self, sample: u64) {
        let count = sample
            .saturating_sub(self.start)
            .min(self.buffer.len() as u64);
        self.buffer.drain(..count as usize);
        self.start += count;
    }

    /// Keep only the newest `samples` estimates.
    pub fn keep_last(&mut self, samples: u64) {
        self.forget_before(self.end().saturating_sub(samples));
    }

    /// The mean of the estimates from (fractional) sample `start` over
    /// `length` samples, but at least one. Past the newest estimate, the
    /// newest one stands in: the demodulator's warm-up ends the track
    /// slightly before the signal does.
    pub fn mean(&self, start: f64, length: f64) -> Frequency {
        let first = (start.round().max(0.0) as u64).max(self.start);
        let last = ((start + length).round() as u64).max(first + 1);
        let available = first.min(self.end())..last.min(self.end());
        let missing = (last - first) - (available.end - available.start);

        let index = |sample: u64| (sample - self.start) as usize;
        let sum: u64 = self
            .buffer
            .range(index(available.start)..index(available.end))
            .map(|estimate| u64::from(estimate.hz()))
            .sum();
        let newest = self
            .buffer
            .back()
            .map_or(0, |estimate| u64::from(estimate.hz()));
        let mean = (sum + newest * missing) / (last - first);
        Frequency::from_hz(u32::try_from(mean).unwrap_or(u32::MAX))
    }
}
