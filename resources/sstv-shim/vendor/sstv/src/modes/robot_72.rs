//! Robot 72.
//!
//! Robot modes encode colour as luminance plus two colour differences, with
//! the colour differences scanned at half the luminance scan period. Robot 72
//! transmits both differences on every line.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::{ms, tone, vis_code};

/// A 320x240 colour image in a 72 second transmission: 240 lines of 300ms each.
pub const ROBOT_72: Mode = Mode {
    name: "Robot72",
    vis_code: vis_code!(12),
    starting_sync_pulse: false,
    resolution: (320, 240),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Yuv,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 9 ms));
const SYNC_PORCH: Step = Step::Control(tone!(1500 Hz, 3 ms));
const PORCH: Step = Step::Control(tone!(1900 Hz, 1_500 us));

const SEQUENCE: [Step; 9] = [
    SYNC_PULSE,
    SYNC_PORCH,
    Step::Scan(Channel::Y, ms!(138)),
    Step::Control(tone!(1500 Hz, 4_500 us)), // even separator pulse
    PORCH,
    Step::Scan(Channel::RY, ms!(69)),
    Step::Control(tone!(2300 Hz, 4_500 us)), // odd separator pulse
    Step::Control(tone!(1500 Hz, 1_500 us)),
    Step::Scan(Channel::BY, ms!(69)),
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
        assert_line_period(ROBOT_72, crate::ms!(300));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(ROBOT_72, 72.0);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(ROBOT_72);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(ROBOT_72);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(ROBOT_72);
    }
}
