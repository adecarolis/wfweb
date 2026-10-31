use core::ops::{Add, Shr, Sub};

use crate::units::Frequency;

/// Estimates the instantaneous frequency of a stream of PCM samples.
///
/// `Demodulator` is the inverse of [`Synthesizer`](crate::Synthesizer): it turns
/// 16-bit samples back into a stream of [`Frequency`] estimates. It performs no
/// protocol interpretation — grouping the frequency track into pixels and
/// scanlines is the decoder's job.
///
/// This is a deliberately simple *midline-crossing* estimator. It measures the
/// number of samples between every *other* crossing of the waveform's midline —
/// a full period — and holds that estimate until the next crossing is seen.
/// Measuring a full period rather than a single half-period cancels the
/// alternating long/short bias that appears when the midline is not perfectly
/// centred, which would otherwise make the estimate swing above and below the
/// true frequency every half cycle. The midline is the
/// midpoint of a running minimum and maximum that continuously relax toward it,
/// forming an adaptive envelope: the midline tracks a changing DC offset or
/// signal level, and an early transient (a click, a burst of static) cannot
/// latch it away from the signal — which would otherwise stop the crossings the
/// estimator depends on. The crossing position is linearly interpolated between
/// the two straddling samples for sub-sample accuracy. The approach is cheap and
/// allocation-free, but only moderately noise resistant and less accurate when
/// there are few samples per cycle.
///
/// The iterator yields *nothing* during an initial warm-up rather than a
/// placeholder value: it needs a pair of crossings to measure a period, and it
/// drops the first few crossings while the envelope grows to span a full cycle
/// and its midline is still biased. Once the first estimate is available it
/// yields one estimate per input sample. The dropped warm-up is bounded to a
/// few periods and, for SSTV, always falls inside the leader tone.
///
/// ```rust
/// use sstv::{Demodulator, Synthesizer, Tone, Hz, ms};
///
/// let samples = Synthesizer::new([Tone::new(Hz!(1900), ms!(20))].into_iter(), 48000);
/// for frequency in Demodulator::new(samples, 48000) {
///     // inspect the recovered frequency track
///     let _ = frequency;
/// }
/// ```
pub struct Demodulator<I: Iterator<Item = i16>> {
    samples: I,
    sample_rate: u32,
    previous_sample: i16,
    envelope: Envelope,
    /// Time from the last crossing up to the current sample.
    ticks_since_crossing: Ticks,
    /// Time between the last two crossings.
    previous_half_period: Ticks,
    crossings_seen: u32,
    frequency: Option<Frequency>,
}

impl<I: Iterator<Item = i16>> Demodulator<I> {
    /// Crossings dropped before the first estimate, while the envelope grows to
    /// span a full cycle and its midline is still biased. Must be at least two,
    /// so both half-periods of the first estimate lie between real crossings.
    const WARM_UP_CROSSINGS: u32 = 4;

    /// Create a new `Demodulator` from a sample iterator and a sample rate in Hz.
    ///
    /// `sample_rate` must be greater than zero and at most 16 MHz.
    pub fn new(mut samples: I, sample_rate: u32) -> Self {
        let first_sample = samples.next().unwrap_or_default();

        Self {
            samples,
            sample_rate: sample_rate.max(1),
            previous_sample: first_sample,
            envelope: Envelope::new(first_sample, sample_rate),
            ticks_since_crossing: Ticks::ZERO,
            previous_half_period: Ticks::ZERO,
            crossings_seen: 0,
            frequency: None,
        }
    }

    /// The sample rate the demodulator was constructed with, in Hz.
    pub const fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    /// Feed one sample and return a new frequency estimate if it completes a
    /// midline crossing.
    ///
    /// The estimate is taken over the last two half-periods, i.e. one full
    /// period. Single half-periods are not reliable on their own: an
    /// off-centre midline or harmonic distortion (which real receivers add to
    /// the low tones) makes one half of each cycle longer than the other,
    /// while a full period is the same length whatever the waveform's shape.
    ///
    /// Returns `None` when the sample does not cross the midline, and during
    /// the first [`WARM_UP_CROSSINGS`](Self::WARM_UP_CROSSINGS) crossings.
    fn calculate_frequency(&mut self, sample: i16) -> Option<Frequency> {
        let midline = self.envelope.update(sample);

        let previous_sample = self.previous_sample;
        self.previous_sample = sample;

        if !self.envelope.midline_was_crossed(previous_sample, sample) {
            self.ticks_since_crossing.update(Ticks::SAMPLE);
            return None;
        }

        let crossing_point = Ticks::until_crossing(previous_sample, sample, midline);

        let half_period = self.ticks_since_crossing + crossing_point;
        let period = self.previous_half_period.saturating_add(half_period);

        self.previous_half_period = half_period;
        self.ticks_since_crossing = Ticks::SAMPLE - crossing_point;
        self.crossings_seen = self.crossings_seen.saturating_add(1);

        if self.crossings_seen <= Self::WARM_UP_CROSSINGS {
            return None;
        }

        Some(period.frequency(self.sample_rate))
    }

    /// Consume samples until the first frequency can be determined.
    ///
    /// Returns if a frequency could be determined or no more samples are available.
    fn warm_up(&mut self) {
        while self.frequency.is_none() {
            match self.samples.next() {
                Some(sample) => self.frequency = self.calculate_frequency(sample),
                None => break,
            }
        }
    }
}

impl<I: Iterator<Item = i16>> Iterator for Demodulator<I> {
    type Item = Frequency;

    fn next(&mut self) -> Option<Frequency> {
        if self.frequency.is_none() {
            self.warm_up();
        }

        let sample = self.samples.next()?;

        if let Some(frequency) = self.calculate_frequency(sample) {
            self.frequency = Some(frequency);
        }

        self.frequency
    }
}

/// Running minimum and maximum of the waveform whose midpoint is the midline
/// the demodulator measures crossings against.
///
/// Both extremes continuously relax toward the midline and re-expand to include
/// each new sample. This keeps the midline centred on the *current* waveform, so
/// a DC offset, a level change, or an early transient cannot latch it away from
/// the signal.
struct Envelope {
    minimum: Level,
    maximum: Level,
    /// The extremes relax by `distance >> decay_shift` per sample, a time
    /// constant of `1 << decay_shift` samples.
    decay_shift: u32,
}

impl Envelope {
    fn new(first_sample: i16, sample_rate: u32) -> Self {
        let decay_shift = (sample_rate / 10).max(1).ilog2();
        let first_sample = Level::from_sample(first_sample);

        Self {
            minimum: first_sample,
            maximum: first_sample,
            decay_shift,
        }
    }

    const fn midline(&self) -> Level {
        self.minimum.midpoint(self.maximum)
    }

    /// Relax the envelope toward the midline, widen it to include `sample`, and
    /// return the updated midline.
    fn update(&mut self, sample: i16) -> Level {
        let sample = Level::from_sample(sample);
        let midline = self.midline();
        self.maximum = (self.maximum - ((self.maximum - midline) >> self.decay_shift)).max(sample);
        self.minimum = (self.minimum + ((midline - self.minimum) >> self.decay_shift)).min(sample);
        self.midline()
    }

    fn midline_was_crossed(&self, previous_sample: i16, current_sample: i16) -> bool {
        let previous_offset = Level::from_sample(previous_sample) - self.midline();
        let current_offset = Level::from_sample(current_sample) - self.midline();

        (previous_offset >= Level::ZERO) != (current_offset >= Level::ZERO)
    }
}

/// A waveform level in sample units, as fixed point with
/// [`FRACTION_BITS`](Self::FRACTION_BITS) fractional bits.
///
/// The fraction lets the envelope keep relaxing for quiet signals, where a
/// per-sample step is far below one sample unit.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
struct Level(i32);

impl Level {
    /// 15 rather than 16 so the difference of two levels fits in `i32`.
    const FRACTION_BITS: u32 = 15;
    const ZERO: Self = Self(0);

    fn from_sample(sample: i16) -> Self {
        Self(i32::from(sample) << Self::FRACTION_BITS)
    }

    const fn midpoint(self, other: Self) -> Self {
        Self(i32::midpoint(self.0, other.0))
    }
}

impl Add for Level {
    type Output = Self;

    fn add(self, rhs: Self) -> Self {
        Self(self.0 + rhs.0)
    }
}

impl Sub for Level {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self {
        Self(self.0 - rhs.0)
    }
}

impl Shr<u32> for Level {
    type Output = Self;

    fn shr(self, rhs: u32) -> Self {
        Self(self.0 >> rhs)
    }
}

/// A span of time in samples, as fixed point with
/// [`FRACTION_BITS`](Self::FRACTION_BITS) fractional bits.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
struct Ticks(i32);

impl Ticks {
    const FRACTION_BITS: u32 = 8;
    const ZERO: Self = Self(0);
    const SAMPLE: Self = Self(1 << Self::FRACTION_BITS);

    /// Time after `previous_sample` at which the line from it to `sample`
    /// crosses `midline`, found by linear interpolation.
    ///
    /// The samples must straddle the midline, which puts the result in
    /// [`ZERO`](Self::ZERO)..=[`SAMPLE`](Self::SAMPLE).
    fn until_crossing(previous_sample: i16, sample: i16, midline: Level) -> Self {
        let previous_offset = Level::from_sample(previous_sample) - midline;
        let sample_step = i32::from(previous_sample) - i32::from(sample);

        Self((previous_offset.0 >> (Level::FRACTION_BITS - Self::FRACTION_BITS)) / sample_step)
    }

    const fn update(&mut self, ticks: Self) {
        self.0 = self.0.saturating_add(ticks.0);
    }

    const fn saturating_add(self, other: Self) -> Self {
        Self(self.0.saturating_add(other.0))
    }

    /// The frequency of a waveform with this period.
    fn frequency(self, sample_rate: u32) -> Frequency {
        let ticks_per_second = sample_rate.saturating_mul(Self::SAMPLE.0.unsigned_abs());
        Frequency::from_hz(ticks_per_second / self.0.max(1).unsigned_abs())
    }
}

impl Add for Ticks {
    type Output = Self;

    fn add(self, rhs: Self) -> Self {
        Self(self.0 + rhs.0)
    }
}

impl Sub for Ticks {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self {
        Self(self.0 - rhs.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::units::Duration;
    use crate::{Synthesizer, Tone};
    use rand::SeedableRng;
    use rand_distr::{Distribution, Normal};

    #[test]
    fn pure_1500hz_at_48000() {
        let actual_frequency = Frequency::from_hz(1500);
        let sample_rate: u32 = 48_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn pure_2300hz_at_48000() {
        let actual_frequency = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn pure_1200hz_at_8000() {
        let actual_frequency = Frequency::from_hz(1200);
        let sample_rate: u32 = 8_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn pure_1000hz_at_8000() {
        let actual_frequency = Frequency::from_hz(1000);
        let sample_rate: u32 = 8_000;
        let samples = synthesize(vec![actual_frequency], sample_rate);
        test_frequency(actual_frequency, sample_rate, &samples, vec![]);
    }

    #[test]
    fn dc_offset_2300hz_at_48000() {
        let actual_frequency = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;

        // Synthesize at half scale to leave headroom, then shift the whole
        // waveform up so it is strictly positive and never touches zero.
        let samples = synthesize(vec![actual_frequency], sample_rate);
        let dc_offsets = vec![-samples.iter().min().unwrap() + 1; samples.len()];

        test_frequency(actual_frequency, sample_rate, &samples, dc_offsets);
    }

    #[test]
    fn noisy_2300hz_at_48000() {
        let actual_frequency = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;
        let signal_to_noise_ratio_db = 25.0;

        let samples = synthesize(vec![actual_frequency], sample_rate);
        let offsets = noise(samples.len(), signal_to_noise_ratio_db);
        test_frequency(actual_frequency, sample_rate, &samples, offsets);
    }

    #[test]
    fn switch_between_two_frequencies() {
        let first_actual = Frequency::from_hz(1500);
        let second_actual = Frequency::from_hz(2300);
        let sample_rate: u32 = 48_000;

        let samples = synthesize(vec![first_actual, second_actual], sample_rate);
        let estimates: Vec<Frequency> =
            Demodulator::new(samples.into_iter(), sample_rate).collect();

        // Every estimate should fall within the band spanned by the two tones
        // (plus tolerance). At the switch a single full-period measurement
        // straddles both frequencies and lands between them, which is fine; a
        // wild excursion outside the band is not.
        let low = first_actual.hz() - first_actual.hz() / 20;
        let high = second_actual.hz() + second_actual.hz() / 20;
        for estimated in estimates {
            assert!(
                (low..=high).contains(&estimated.hz()),
                "{} Hz outside [{}, {}]",
                estimated.hz(),
                low,
                high,
            );
        }
    }

    #[test]
    fn empty_samples_yields_empty_frequencies() {
        let mut estimates = Demodulator::new(core::iter::empty(), 48_000);

        assert!(estimates.next().is_none());
    }

    fn synthesize(frequencies: Vec<Frequency>, sample_rate: u32) -> Vec<i16> {
        let tones = frequencies
            .into_iter()
            .map(|freq| Tone::new(freq, Duration::from_ms(10)));
        Synthesizer::new(tones, sample_rate)
            .map(|sample| sample / 2)
            .collect()
    }

    fn test_frequency(
        actual_frequency: Frequency,
        sample_rate: u32,
        samples: &[i16],
        offsets: Vec<i16>,
    ) {
        let samples_with_offset = samples
            .iter()
            .zip(offsets.into_iter().chain(core::iter::repeat(0)))
            .map(|(s, o)| s.saturating_add(o));

        let estimates: Vec<Frequency> =
            Demodulator::new(samples_with_offset, sample_rate).collect();

        assert_ne!(estimates, Vec::new());

        for estimate in estimates {
            assert!(
                frequencies_match(estimate, actual_frequency),
                "median {} Hz ≉ {} Hz",
                estimate.hz(),
                actual_frequency.hz(),
            );
        }
    }

    fn frequencies_match(estimated: Frequency, actual: Frequency) -> bool {
        let allowed_deviation = actual / 20;
        let deviation = estimated.abs_diff(actual);
        deviation <= allowed_deviation
    }

    fn noise(len: usize, snr_db: f64) -> Vec<i16> {
        let signal_rms = f64::from(i16::MAX) / core::f64::consts::SQRT_2 / 2.0;
        let sigma = signal_rms / 10f64.powf(snr_db / 20.0);

        let mut rng = rand::rngs::StdRng::seed_from_u64(0x5EED);
        let normal = Normal::new(0.0, sigma).unwrap();
        (0..len)
            .map(|_| {
                normal
                    .sample(&mut rng)
                    .round()
                    .clamp(f64::from(i16::MIN), f64::from(i16::MAX)) as i16
            })
            .collect()
    }
}
