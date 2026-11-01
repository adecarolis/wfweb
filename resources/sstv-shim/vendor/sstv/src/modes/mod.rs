//! The SSTV modes specified in the Dayton paper — JL Barber (N7CXI),
//! "Proposal for SSTV Mode Specifications", presented at the Dayton SSTV
//! forum, 20 May 2000.
//!
//! Each mode lives in its own module, transcribing its timing table from the
//! paper into a [`Mode`]. Everything shared between modes — the frequency
//! range, the calibration header and the VIS code — is defined here, as in
//! the paper's common sections.

mod mode;
pub(crate) mod step;
mod vis_code;

#[cfg(test)]
pub(crate) use mode::testing;

mod martin_1;
mod martin_2;
mod pasokon_p3;
mod pasokon_p5;
mod pasokon_p7;
mod pd_120;
mod pd_160;
mod pd_180;
mod pd_240;
mod pd_290;
mod pd_50;
mod pd_90;
mod robot_36;
mod robot_72;
mod scottie_1;
mod scottie_2;
mod scottie_dx;
mod wrasse_sc2_180;

use crate::units::{Frequency, Tone};
use crate::{Hz, ms};

pub use mode::Mode;
pub use vis_code::VisCode;

pub use martin_1::MARTIN_1;
pub use martin_2::MARTIN_2;
pub use pasokon_p3::PASOKON_P3;
pub use pasokon_p5::PASOKON_P5;
pub use pasokon_p7::PASOKON_P7;
pub use pd_50::PD_50;
pub use pd_90::PD_90;
pub use pd_120::PD_120;
pub use pd_160::PD_160;
pub use pd_180::PD_180;
pub use pd_240::PD_240;
pub use pd_290::PD_290;
pub use robot_36::ROBOT_36;
pub use robot_72::ROBOT_72;
pub use scottie_1::SCOTTIE_1;
pub use scottie_2::SCOTTIE_2;
pub use scottie_dx::SCOTTIE_DX;
pub use wrasse_sc2_180::WRASSE_SC2_180;

/// Every transmission mode, in the paper's order.
///
/// ```rust
/// use sstv::modes;
///
/// let mode = modes::ALL
///     .into_iter()
///     .find(|mode| format!("{mode:?}").eq_ignore_ascii_case("robot36"));
/// assert_eq!(mode, Some(modes::ROBOT_36));
/// ```
pub const ALL: [Mode; 18] = [
    SCOTTIE_1,
    SCOTTIE_2,
    SCOTTIE_DX,
    MARTIN_1,
    MARTIN_2,
    ROBOT_36,
    ROBOT_72,
    WRASSE_SC2_180,
    PASOKON_P3,
    PASOKON_P5,
    PASOKON_P7,
    PD_50,
    PD_90,
    PD_120,
    PD_160,
    PD_180,
    PD_240,
    PD_290,
];

/// The sync pulse frequency, shared by every mode.
pub(crate) const SYNC_FREQUENCY: Frequency = Hz!(1200);
/// Pure black — the lower end of the luminance range.
pub(crate) const BLACK_FREQUENCY: Frequency = Hz!(1500);
/// Pure white — the upper end of the luminance range.
pub(crate) const WHITE_FREQUENCY: Frequency = Hz!(2300);
/// The leader tone of the calibration header.
pub(crate) const LEADER_FREQUENCY: Frequency = Hz!(1900);
/// Each of the calibration header's two leader tones.
pub(crate) const LEADER: Tone = Tone::new(LEADER_FREQUENCY, ms!(300));

/// The frequency representing a pixel value, mapped linearly onto the
/// luminance range.
pub(crate) fn value_frequency(value: u8) -> Frequency {
    BLACK_FREQUENCY + (WHITE_FREQUENCY - BLACK_FREQUENCY) * u32::from(value) / 255
}

/// The pixel value a frequency represents: the inverse of
/// [`value_frequency`], rounded to the nearest value and clamped to the
/// luminance range.
pub(crate) fn frequency_value(frequency: Frequency) -> u8 {
    let black = i64::from(BLACK_FREQUENCY.hz());
    let range = i64::from(WHITE_FREQUENCY.hz()) - black;
    let value = ((i64::from(frequency.hz()) - black) * 255 + range / 2).div_euclid(range);
    value.clamp(0, 255) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn value_range_maps_onto_the_luminance_range() {
        assert_eq!(value_frequency(0), BLACK_FREQUENCY);
        assert_eq!(value_frequency(255), WHITE_FREQUENCY);
    }

    #[test]
    fn value_frequency_rises_with_the_value() {
        for value in 0..255 {
            assert!(value_frequency(value) <= value_frequency(value + 1));
        }
    }

    #[test]
    fn frequency_value_inverts_value_frequency() {
        for value in [0, 1, 127, 128, 254, 255] {
            assert_eq!(frequency_value(value_frequency(value)), value);
        }
    }

    #[test]
    fn frequencies_outside_the_luminance_range_clamp() {
        assert_eq!(frequency_value(Hz!(1200)), 0);
        assert_eq!(frequency_value(Hz!(2500)), 255);
    }
}
