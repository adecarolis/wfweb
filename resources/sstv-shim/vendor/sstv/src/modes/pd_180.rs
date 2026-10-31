//! PD 180.
//!
//! PD modes transmit two image lines per sequence: the first line's
//! luminance, the red and blue colour differences averaged over both lines,
//! and then the second line's luminance. The sub-modes differ only in scan
//! time and resolution.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::units::Duration;
use crate::{tone, us, vis_code};

/// A 640x496 colour image in a 187 second transmission: 248 line pairs of 754.24ms each.
pub const PD_180: Mode = Mode {
    name: "Pd180",
    vis_code: vis_code!(96),
    starting_sync_pulse: false,
    resolution: (640, 496),
    sequence: &SEQUENCE,
    lines_per_sequence: 2,
    color: ColorMode::YuvSharedPair,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 20 ms));
const PORCH: Step = Step::Control(tone!(1500 Hz, 2_080 us));
const SCAN: Duration = us!(183_040);

const SEQUENCE: [Step; 6] = [
    SYNC_PULSE,
    PORCH,
    Step::Scan(Channel::Y, SCAN),
    Step::Scan(Channel::RY, SCAN),
    Step::Scan(Channel::BY, SCAN),
    Step::Scan(Channel::YSecond, SCAN),
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
        assert_line_period(PD_180, crate::us!(754_240));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(PD_180, 187.1);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(PD_180);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(PD_180);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(PD_180);
    }
}
