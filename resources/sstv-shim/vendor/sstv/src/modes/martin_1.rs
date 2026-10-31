//! Martin 1.
//!
//! Martin modes transmit green, blue and red scans of every line, with the
//! sync pulse at the line break and a short separator pulse after each scan.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::units::Duration;
use crate::{tone, us, vis_code};

/// A 320x256 colour image in a 114 second transmission: 256 lines of 446.446ms each.
pub const MARTIN_1: Mode = Mode {
    name: "Martin1",
    vis_code: vis_code!(44),
    starting_sync_pulse: false,
    resolution: (320, 256),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 4_862 us));
const SYNC_PORCH: Step = Step::Control(tone!(1500 Hz, 572 us));
const SEPARATOR_PULSE: Step = Step::Control(tone!(1500 Hz, 572 us));
const SCAN: Duration = us!(146_432);

const SEQUENCE: [Step; 8] = [
    SYNC_PULSE,
    SYNC_PORCH,
    Step::Scan(Channel::Green, SCAN),
    SEPARATOR_PULSE,
    Step::Scan(Channel::Blue, SCAN),
    SEPARATOR_PULSE,
    Step::Scan(Channel::Red, SCAN),
    SEPARATOR_PULSE,
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
        assert_line_period(MARTIN_1, crate::us!(446_446));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(MARTIN_1, 114.3);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(MARTIN_1);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(MARTIN_1);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(MARTIN_1);
    }
}
