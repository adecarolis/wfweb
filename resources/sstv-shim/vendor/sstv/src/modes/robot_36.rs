//! Robot 36.
//!
//! Robot modes encode colour as luminance plus two colour differences, with
//! the colour differences scanned at half the luminance scan period. Robot 36
//! halves the colour information further by sharing it over a line pair: the
//! even line carries the red difference, the odd line the blue one, each
//! averaged over the pair and marked by a separator pulse (1500hz on even
//! lines, 2300hz on odd lines). The pair is one timing sequence of two
//! 150ms lines — and thus two sync pulses.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::{ms, tone, vis_code};

/// A 320x240 colour image in a 36 second transmission: 240 lines of 150ms each.
pub const ROBOT_36: Mode = Mode {
    name: "Robot36",
    vis_code: vis_code!(8),
    starting_sync_pulse: false,
    resolution: (320, 240),
    sequence: &SEQUENCE,
    lines_per_sequence: 2,
    color: ColorMode::YuvSharedPair,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 9 ms));
const SYNC_PORCH: Step = Step::Control(tone!(1500 Hz, 3 ms));
const PORCH: Step = Step::Control(tone!(1900 Hz, 1_500 us));

const SEQUENCE: [Step; 12] = [
    // The even line of the pair.
    SYNC_PULSE,
    SYNC_PORCH,
    Step::Scan(Channel::Y, ms!(88)),
    Step::Control(tone!(1500 Hz, 4_500 us)), // even-line separator pulse
    PORCH,
    Step::Scan(Channel::RY, ms!(44)),
    // The odd line of the pair.
    SYNC_PULSE,
    SYNC_PORCH,
    Step::Scan(Channel::YSecond, ms!(88)),
    Step::Control(tone!(2300 Hz, 4_500 us)), // odd-line separator pulse
    PORCH,
    Step::Scan(Channel::BY, ms!(44)),
];

#[cfg(test)]
mod tests {
    use super::*;
    use crate::encode::testing::{
        assert_one_tone_per_control_step_and_scanned_pixel,
        assert_transmission_lasts_header_plus_every_pass, black_image,
    };
    use crate::modes::testing::{assert_line_period, assert_mode_can_be_constructed_from_vis_code};
    use crate::{Encoder, Synthesizer};

    const SAMPLE_RATE: u32 = 48_000;
    /// The VOX tones (800ms) plus the calibration header with the VIS code
    /// (910ms), in seconds.
    const HEADER_DURATION: f64 = 1.71;

    #[test]
    fn line_period_matches_the_paper() {
        // The paper's 150ms per line; the sequence carries the pair.
        assert_line_period(ROBOT_36, crate::ms!(300));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        let encoder = Encoder::new(ROBOT_36, black_image(ROBOT_36)).unwrap();
        let samples: Vec<i16> = Synthesizer::new(encoder, SAMPLE_RATE).collect();

        let transmission_duration = samples.len() as f64 / f64::from(SAMPLE_RATE);

        let expected_duration = 36.0 + HEADER_DURATION;
        assert!(
            (transmission_duration - expected_duration).abs() < 0.001,
            "{transmission_duration}s instead of {expected_duration}s",
        );
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(ROBOT_36);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(ROBOT_36);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(ROBOT_36);
    }

    #[test]
    fn line_pair_sequence_carries_two_sync_pulses() {
        assert_eq!(ROBOT_36.sync_count(), 2);
    }
}
