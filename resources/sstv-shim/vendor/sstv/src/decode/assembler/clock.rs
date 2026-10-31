//! Converting between durations and positions on the frequency track, which
//! holds one estimate per sample.

use crate::units::Duration;

/// The sample rate of the frequency track.
#[derive(Clone, Copy)]
pub(super) struct SampleClock {
    rate: u32,
}

impl SampleClock {
    /// `rate` must be greater than zero; zero counts as one.
    pub const fn new(rate: u32) -> Self {
        Self {
            rate: if rate == 0 { 1 } else { rate },
        }
    }

    /// The duration in (fractional) samples.
    pub fn samples(self, duration: Duration) -> f64 {
        duration.ns() as f64 * f64::from(self.rate) / 1e9
    }

    /// The whole samples the duration spans, rounded down.
    pub const fn whole_samples(self, duration: Duration) -> u64 {
        duration.ns() * self.rate as u64 / 1_000_000_000
    }

    /// The duration of a (fractional) number of samples.
    pub fn duration(self, samples: f64) -> Duration {
        Duration::from_ns((samples * 1e9 / f64::from(self.rate)) as u64)
    }
}
