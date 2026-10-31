//! Pasokon P5.
//!
//! High-resolution modes transmitting red, green and blue scans of every
//! line, with a porch after every scan. Sync pulse and porch lengths vary
//! with the sub-mode: they were chosen to divide evenly into standard RS232
//! clock rates.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::units::Duration;
use crate::{tone, us, vis_code};

/// A 640x496 colour image in a 305 second transmission: 496 lines of 614.065ms each.
pub const PASOKON_P5: Mode = Mode {
    name: "PasokonP5",
    vis_code: vis_code!(114),
    starting_sync_pulse: false,
    resolution: (640, 496),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 7_813 us));
const PORCH: Step = Step::Control(tone!(1500 Hz, 1_563 us));
const SCAN: Duration = us!(200_000);

const SEQUENCE: [Step; 8] = [
    SYNC_PULSE,
    PORCH,
    Step::Scan(Channel::Red, SCAN),
    PORCH,
    Step::Scan(Channel::Green, SCAN),
    PORCH,
    Step::Scan(Channel::Blue, SCAN),
    PORCH,
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
        assert_line_period(PASOKON_P5, crate::us!(614_065));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(PASOKON_P5, 304.6);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(PASOKON_P5);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(PASOKON_P5);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(PASOKON_P5);
    }
}
