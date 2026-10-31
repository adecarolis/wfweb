//! Scottie 2.
//!
//! Scottie modes transmit green, blue and red scans of every line. They are
//! unusual in two ways: the sync pulse sits between the blue and the red scan
//! instead of at the line break, and a single extra sync pulse precedes the
//! very first line (emitted here as the last header tone).

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::units::Duration;
use crate::{tone, us, vis_code};

/// A 320x256 colour image in a 71 second transmission: 256 lines of 277.692ms each.
pub const SCOTTIE_2: Mode = Mode {
    name: "Scottie2",
    vis_code: vis_code!(56),
    starting_sync_pulse: true,
    resolution: (320, 256),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 9 ms));
const SYNC_PORCH: Step = Step::Control(tone!(1500 Hz, 1_500 us));
const SEPARATOR_PULSE: Step = Step::Control(tone!(1500 Hz, 1_500 us));
const SCAN: Duration = us!(88_064);

const SEQUENCE: [Step; 7] = [
    SEPARATOR_PULSE,
    Step::Scan(Channel::Green, SCAN),
    SEPARATOR_PULSE,
    Step::Scan(Channel::Blue, SCAN),
    SYNC_PULSE,
    SYNC_PORCH,
    Step::Scan(Channel::Red, SCAN),
];

#[cfg(test)]
mod tests {
    use super::*;
    use crate::encode::testing::{
        assert_one_tone_per_control_step_and_scanned_pixel,
        assert_transmission_lasts_header_plus_every_pass,
    };
    use crate::modes::testing::{
        assert_line_period, assert_mode_can_be_constructed_from_vis_code, assert_transmission_time,
    };

    #[test]
    fn line_period_matches_the_paper() {
        assert_line_period(SCOTTIE_2, crate::us!(277_692));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(SCOTTIE_2, 71.1);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(SCOTTIE_2);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(SCOTTIE_2);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(SCOTTIE_2);
    }

    #[test]
    fn sync_pulse_sits_between_the_blue_and_red_scans() {
        let green_and_blue_scans =
            SEPARATOR_PULSE.duration() + SCAN + SEPARATOR_PULSE.duration() + SCAN;

        assert_eq!(SCOTTIE_2.sync_pulse().0, green_and_blue_scans);
    }
}
