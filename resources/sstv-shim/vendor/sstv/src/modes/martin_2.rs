//! Martin 2.
//!
//! Martin modes transmit green, blue and red scans of every line, with the
//! sync pulse at the line break and a short separator pulse after each scan.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::units::Duration;
use crate::{tone, us, vis_code};

/// A 320x256 colour image in a 58 second transmission: 256 lines of 226.798ms each.
pub const MARTIN_2: Mode = Mode {
    name: "Martin2",
    vis_code: vis_code!(40),
    starting_sync_pulse: false,
    resolution: (320, 256),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 4_862 us));
const SYNC_PORCH: Step = Step::Control(tone!(1500 Hz, 572 us));
const SEPARATOR_PULSE: Step = Step::Control(tone!(1500 Hz, 572 us));
const SCAN: Duration = us!(73_216);

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
        assert_line_period(MARTIN_2, crate::us!(226_798));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(MARTIN_2, 58.06);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(MARTIN_2);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(MARTIN_2);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(MARTIN_2);
    }
}
