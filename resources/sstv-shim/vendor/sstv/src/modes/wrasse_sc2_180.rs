//! Wrasse SC2-180.
//!
//! The simplest colour mode: red, green and blue scans of every line follow
//! the sync pulse and porch directly, without separators.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::{ms, tone, vis_code};

/// A 320x256 colour image in a 182 second transmission: 256 lines of 711.0225ms each.
pub const WRASSE_SC2_180: Mode = Mode {
    name: "WrasseSc2180",
    vis_code: vis_code!(55),
    starting_sync_pulse: false,
    resolution: (320, 256),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const SEQUENCE: [Step; 5] = [
    Step::Control(tone!(1200 Hz, 5_522_500 ns)),
    Step::Control(tone!(1500 Hz, 500 us)),
    Step::Scan(Channel::Red, ms!(235)),
    Step::Scan(Channel::Green, ms!(235)),
    Step::Scan(Channel::Blue, ms!(235)),
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
        assert_line_period(WRASSE_SC2_180, crate::ns!(711_022_500));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(WRASSE_SC2_180, 182.0);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(WRASSE_SC2_180);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(WRASSE_SC2_180);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(WRASSE_SC2_180);
    }
}
