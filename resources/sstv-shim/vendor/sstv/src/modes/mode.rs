//! The [`Mode`] type and the calibration header preceding a mode's image
//! data.

use super::step::{ColorMode, Step};
use super::{ALL, LEADER, SYNC_FREQUENCY, VisCode};
use crate::units::{Duration, Tone};
use crate::{Error, ms, tone};

/// Tuning (VOX) tones customarily sent ahead of the calibration header to
/// open receiver squelch. They are not part of the paper's specification.
const VOX_TONES: [Tone; 8] = [
    tone!(1900 Hz, 100 ms),
    tone!(1500 Hz, 100 ms),
    tone!(1900 Hz, 100 ms),
    tone!(1500 Hz, 100 ms),
    tone!(2300 Hz, 100 ms),
    tone!(1500 Hz, 100 ms),
    tone!(2300 Hz, 100 ms),
    tone!(1500 Hz, 100 ms),
];

/// A specific protocol for encoding an image as a tone sequence.
///
/// The modes themselves are constants in [`modes`](crate::modes)
/// ([`modes::ROBOT_36`](crate::modes::ROBOT_36),
/// [`modes::SCOTTIE_1`](crate::modes::SCOTTIE_1), …), each defined in its own
/// module.
#[derive(Clone, Copy)]
pub struct Mode {
    /// The mode's name, as [`Debug`](core::fmt::Debug) prints it.
    pub(super) name: &'static str,
    /// The VIS code identifying the mode to a receiving system.
    pub(super) vis_code: VisCode,
    /// Whether one extra sync pulse precedes the first line (Scottie modes).
    pub(super) starting_sync_pulse: bool,
    /// The horizontal and vertical resolution in pixels.
    pub(crate) resolution: (usize, usize),
    /// The repeating timing sequence from the paper's table. Its sync pulses
    /// are evenly spaced — the decoder relies on that to acquire and
    /// re-align.
    pub(crate) sequence: &'static [Step],
    /// Image lines carried by one pass through the sequence (2 for Robot 36
    /// and PD modes).
    pub(crate) lines_per_sequence: usize,
    /// How the scans combine into pixels.
    pub(crate) color: ColorMode,
}

impl Mode {
    /// The mode's VIS code, identifying it to a receiving system.
    #[must_use]
    pub const fn vis_code(&self) -> VisCode {
        self.vis_code
    }

    /// The number of pixels the encoder buffers for this mode — one full
    /// line group. This is the length [`Encoder::new_in`](crate::Encoder)
    /// requires of its buffer.
    ///
    /// ```rust
    /// use sstv::{modes::ROBOT_36, RgbPixel};
    ///
    /// const LEN: usize = ROBOT_36.encoder_buffer_len();
    /// let buffer = [RgbPixel::new(0, 0, 0); LEN];
    /// assert_eq!(buffer.len(), 2 * 320);
    /// ```
    #[must_use]
    pub const fn encoder_buffer_len(&self) -> usize {
        self.lines_per_sequence * self.resolution.0
    }

    /// The image resolution in pixels, as (width, height).
    ///
    /// ```rust
    /// use sstv::modes::ROBOT_36;
    ///
    /// assert_eq!(ROBOT_36.resolution(), (320, 240));
    /// ```
    #[must_use]
    pub const fn resolution(&self) -> (u32, u32) {
        (self.resolution.0 as u32, self.resolution.1 as u32)
    }

    /// The duration of one pass through the timing sequence.
    pub(crate) fn sequence_duration(&self) -> Duration {
        self.sequence.iter().map(Step::duration).sum()
    }

    /// Each step of the sequence with the offset at which it begins.
    pub(crate) fn step_offsets(&self) -> impl Iterator<Item = (Duration, &'static Step)> {
        self.sequence.iter().scan(Duration::from_ns(0), |at, step| {
            let offset = *at;
            *at = *at + step.duration();
            Some((offset, step))
        })
    }

    /// The offset at which each sync pulse starts.
    pub(crate) fn sync_offsets(&self) -> impl Iterator<Item = Duration> {
        self.step_offsets().filter_map(|(offset, step)| {
            matches!(step, Step::Control(tone) if tone.frequency == SYNC_FREQUENCY)
                .then_some(offset)
        })
    }

    /// The number of sync pulses in the sequence (2 for Robot 36).
    pub(crate) fn sync_count(&self) -> usize {
        self.sync_offsets().count()
    }

    /// The spacing of the sync pulses — the duration of one transmitted line.
    pub(crate) fn sync_spacing(&self) -> Duration {
        self.sequence_duration() / self.sync_count() as u32
    }

    /// The first sync pulse's offset within the sequence and its duration.
    ///
    /// Zero offset for most modes; Scottie places the sync pulse between the
    /// Blue and Red scans.
    pub(crate) fn sync_pulse(&self) -> (Duration, Duration) {
        for (offset, step) in self.step_offsets() {
            if let Step::Control(tone) = step
                && tone.frequency == SYNC_FREQUENCY
            {
                return (offset, tone.duration);
            }
        }
        // Every mode's sequence contains a sync pulse.
        (Duration::from_ns(0), Duration::from_ns(0))
    }

    /// Whether the mode transmits one extra sync pulse between the header and
    /// the first line. Only Scottie modes do.
    pub(crate) const fn has_starting_sync_pulse(self) -> bool {
        self.starting_sync_pulse
    }

    /// The tones sent before the image: the VOX tuning tones, the calibration
    /// header carrying the VIS code, and the starting sync pulse for modes
    /// that transmit one. The image data begins immediately after the last
    /// header tone.
    ///
    /// ```rust
    /// use sstv::modes::ROBOT_36;
    /// use sstv::{ms, Duration};
    ///
    /// let total_header_duration: Duration = ROBOT_36
    ///     .header_tones()
    ///     .map(|tone| tone.duration)
    ///     .sum();
    /// assert_eq!(total_header_duration, ms!(1_710));
    /// ```
    pub fn header_tones(&self) -> impl Iterator<Item = Tone> + '_ {
        (0..).map_while(move |index| self.header_tone(index))
    }

    /// The `index`-th header tone, or `None` past the end of the header.
    pub(crate) fn header_tone(self, index: usize) -> Option<Tone> {
        match index {
            0..=7 => Some(VOX_TONES[index]),
            8 | 10 => Some(LEADER),
            9 => Some(Tone::new(SYNC_FREQUENCY, ms!(10))), // break
            11..=20 => self.vis_code.tone(index - 11),
            21 if self.has_starting_sync_pulse() => {
                Some(Tone::new(SYNC_FREQUENCY, self.sync_pulse().1))
            }
            _ => None,
        }
    }
}

/// Look up the mode a [`VisCode`] identifies.
impl TryFrom<VisCode> for Mode {
    type Error = Error;

    /// # Errors
    ///
    /// [`Error::UnknownMode`] if no mode carries the code.
    fn try_from(code: VisCode) -> Result<Self, Error> {
        ALL.into_iter()
            .find(|mode| mode.vis_code == code)
            .ok_or(Error::UnknownMode)
    }
}

// The VIS code is unique to each mode, so it serves as the mode's identity —
// comparing the layouts would walk their timing sequences.
impl PartialEq for Mode {
    fn eq(&self, other: &Self) -> bool {
        self.vis_code == other.vis_code
    }
}

impl Eq for Mode {}

impl core::hash::Hash for Mode {
    fn hash<H: core::hash::Hasher>(&self, state: &mut H) {
        self.vis_code.hash(state);
    }
}

impl core::fmt::Debug for Mode {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(self.name)
    }
}

/// The shared assertions behind every mode's tests, and the tests of the
/// mode-independent header structure.
#[cfg(test)]
pub mod testing {
    extern crate std;
    use std::vec::Vec;

    use super::*;
    use crate::units::Duration;

    #[test]
    fn header_tones_are_vox_leader_and_vis_code() {
        let leader_break_leader = [
            tone!(1900 Hz, 300 ms),
            tone!(1200 Hz, 10 ms),
            tone!(1900 Hz, 300 ms),
        ];
        let expected_header_tones: Vec<_> = VOX_TONES
            .into_iter()
            .chain(leader_break_leader)
            .chain(crate::modes::ROBOT_36.vis_code().tones())
            .collect();

        let header_tones: Vec<_> = crate::modes::ROBOT_36.header_tones().collect();

        assert_eq!(header_tones, expected_header_tones);
    }

    /// Assert that the timing sequence sums to the paper's line period (the
    /// pair period for two-line sequences), and that its sync pulses are
    /// evenly spaced — the decoder relies on that to acquire and re-align.
    pub fn assert_line_period(mode: Mode, expected: Duration) {
        assert_eq!(mode.sequence_duration(), expected);

        let first_sync = mode.sync_pulse().0;
        for (index, offset) in mode.sync_offsets().enumerate() {
            assert_eq!(offset, first_sync + mode.sync_spacing() * index as u32);
        }
    }

    /// Assert that the transcribed steps, summed over all lines, reproduce
    /// the transmission time the paper publishes alongside the per-step
    /// timings — this catches a transcription mistake in any single step.
    pub fn assert_transmission_time(mode: Mode, expected_seconds: f64) {
        let passes = (mode.resolution.1 / mode.lines_per_sequence) as f64;
        let seconds = passes * mode.sequence_duration().ns() as f64 / 1e9;
        assert!(
            (seconds - expected_seconds).abs() < 0.1,
            "{seconds}s instead of {expected_seconds}s",
        );
    }

    /// Assert that looking up the mode's VIS code yields the mode again — a
    /// collision between two modes fails the lookup.
    pub fn assert_mode_can_be_constructed_from_vis_code(mode: Mode) {
        assert_eq!(Mode::try_from(mode.vis_code()), Ok(mode));
    }
}
