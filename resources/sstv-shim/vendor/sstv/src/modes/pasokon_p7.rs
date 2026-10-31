//! Pasokon P7.
//!
//! High-resolution modes transmitting red, green and blue scans of every
//! line, with a porch after every scan. Sync pulse and porch lengths vary
//! with the sub-mode: they were chosen to divide evenly into standard RS232
//! clock rates.

use super::Mode;
use super::step::{Channel, ColorMode, Step};
use crate::units::Duration;
use crate::{tone, us, vis_code};

/// A 640x496 colour image in a 406 second transmission: 496 lines of 818.747ms each.
pub const PASOKON_P7: Mode = Mode {
    name: "PasokonP7",
    vis_code: vis_code!(115),
    starting_sync_pulse: false,
    resolution: (640, 496),
    sequence: &SEQUENCE,
    lines_per_sequence: 1,
    color: ColorMode::Rgb,
};

const SYNC_PULSE: Step = Step::Control(tone!(1200 Hz, 10_417 us));
const PORCH: Step = Step::Control(tone!(1500 Hz, 2_083 us));
const SCAN: Duration = us!(266_666);

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
        assert_line_period(PASOKON_P7, crate::us!(818_747));
    }

    #[test]
    fn transmission_time_matches_the_paper() {
        assert_transmission_time(PASOKON_P7, 406.1);
    }

    #[test]
    fn mode_constructed_from_vis_code() {
        assert_mode_can_be_constructed_from_vis_code(PASOKON_P7);
    }

    #[test]
    fn encodes_one_tone_per_control_step_and_scanned_pixel() {
        assert_one_tone_per_control_step_and_scanned_pixel(PASOKON_P7);
    }

    #[test]
    fn transmission_lasts_header_plus_every_pass() {
        assert_transmission_lasts_header_plus_every_pass(PASOKON_P7);
    }
}
