//! The VIS code identifying a mode within the calibration header.

use super::SYNC_FREQUENCY;
use crate::units::{Duration, Frequency, Tone};
use crate::{Error, Hz, ms, tone};

const ONE_FREQUENCY: Frequency = Hz!(1100);
const ZERO_FREQUENCY: Frequency = Hz!(1300);
/// Every VIS bit (start, data, parity, stop) lasts 30ms.
const BIT_DURATION: Duration = ms!(30);
/// The seven code bits and the parity bit.
const DATA_BITS: usize = 8;

/// A 7-bit VIS (Vertical Interval Signaling) code, transmitted in the
/// calibration header to identify the mode to a receiving system.
///
/// Construct one with [`VisCode::try_new`], or with
/// [`vis_code!`](crate::vis_code) to validate at compile time.
///
/// ```rust
/// use sstv::{modes::ROBOT_36, Mode, vis_code};
///
/// assert_eq!(
///     Mode::try_from(vis_code!(8)),
///     Ok(ROBOT_36),
/// );
/// ```
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct VisCode(u8);

impl VisCode {
    /// The start and stop bits' tone.
    pub(crate) const FRAMING_BIT: Tone = Tone::new(SYNC_FREQUENCY, BIT_DURATION);
    /// How far a received bit may stray from its nominal frequency and
    /// duration.
    pub(crate) const BIT_TOLERANCE: Tone = tone!(50 Hz, 10 ms);

    /// Whether the value fits in the 7 bits of a VIS code.
    ///
    /// ```rust
    /// use sstv::VisCode;
    ///
    /// assert!(VisCode::is_valid(8));
    /// assert!(!VisCode::is_valid(200));
    /// ```
    #[must_use]
    pub const fn is_valid(code: u8) -> bool {
        code < 128
    }

    /// Construct a `VisCode` from its value.
    ///
    /// ```rust
    /// use sstv::{Error, VisCode, vis_code};
    ///
    /// assert_eq!(
    ///     VisCode::try_new(8),
    ///     Ok(vis_code!(8)),
    /// );
    /// assert_eq!(
    ///     VisCode::try_new(200),
    ///     Err(Error::BadVisCode),
    /// );
    /// ```
    ///
    /// # Errors
    ///
    /// [`Error::BadVisCode`] if the value does not fit in 7 bits.
    pub const fn try_new(code: u8) -> Result<Self, Error> {
        if Self::is_valid(code) {
            Ok(Self(code))
        } else {
            Err(Error::BadVisCode)
        }
    }

    /// `vis_code!` machinery; use [`VisCode::try_new`] or
    /// [`vis_code!`](crate::vis_code) instead. Masks the value to 7 bits —
    /// the macro asserts validity beforehand, so the mask never alters it.
    #[doc(hidden)]
    #[must_use]
    pub const fn new_masked(code: u8) -> Self {
        Self(code & 0x7F)
    }

    /// The code's ten transmitted tones: the start bit, the seven data bits
    /// (least significant first), the even parity bit and the stop bit.
    ///
    /// ```rust
    /// use sstv::vis_code;
    ///
    /// assert_eq!(vis_code!(8).tones().count(), 10);
    /// ```
    pub fn tones(self) -> impl Iterator<Item = Tone> {
        (0..).map_while(move |index| self.tone(index))
    }

    /// The `index`-th of the code's ten transmitted tones — the start bit,
    /// the seven data bits (least significant first), the even parity bit
    /// and the stop bit — or `None` past the end.
    pub(crate) fn tone(self, index: usize) -> Option<Tone> {
        let bit = |one: bool| {
            let frequency = if one { ONE_FREQUENCY } else { ZERO_FREQUENCY };
            Tone::new(frequency, BIT_DURATION)
        };
        match index {
            0 | 9 => Some(Self::FRAMING_BIT),
            1..=7 => Some(bit((self.0 >> (index - 1)) & 1 == 1)),
            8 => Some(bit(self.0.count_ones() % 2 == 1)),
            _ => None,
        }
    }

    /// Decode the data-bit tones received between a code's start and stop
    /// bits.
    ///
    /// Neighbouring bits of equal value arrive as one tone lasting several
    /// bit durations, so the tones need not match the data bits of
    /// [`tones`](Self::tones) one to one.
    ///
    /// `None` unless the tones carry exactly eight bits (the seven code bits,
    /// least significant first, then the parity bit) and the parity is even.
    pub(crate) fn from_received_tones(data_bits: &[Tone]) -> Option<Self> {
        let mut bits = [false; DATA_BITS];
        let mut received = 0;
        for tone in data_bits {
            let (value, count) = read_bit_run(*tone)?;
            bits.get_mut(received..received + count)?.fill(value);
            received += count;
        }

        let ones = bits.iter().filter(|&&bit| bit).count();
        if received != DATA_BITS || !ones.is_multiple_of(2) {
            return None;
        }
        let (code_bits, _parity) = bits.split_at(DATA_BITS - 1);
        let code = code_bits
            .iter()
            .rev()
            .fold(0, |code, &bit| (code << 1) | u8::from(bit));
        Some(Self(code))
    }
}

/// The value and number of the equal data bits a tone carries, if it is one
/// or more data bits.
fn read_bit_run(tone: Tone) -> Option<(bool, usize)> {
    let bit = BIT_DURATION.ns();
    let count = (tone.duration.ns() + bit / 2) / bit;
    let run = Tone::new(ONE_FREQUENCY, Duration::from_ns(count.checked_mul(bit)?));
    let value = if tone.is_near(run, VisCode::BIT_TOLERANCE) {
        true
    } else if tone.is_near(
        Tone {
            frequency: ZERO_FREQUENCY,
            ..run
        },
        VisCode::BIT_TOLERANCE,
    ) {
        false
    } else {
        return None;
    };
    (count > 0).then_some((value, usize::try_from(count).ok()?))
}

/// The code's value.
///
/// ```rust
/// use sstv::vis_code;
///
/// assert_eq!(u8::from(vis_code!(8)), 8);
/// ```
impl From<VisCode> for u8 {
    fn from(code: VisCode) -> Self {
        code.0
    }
}

#[macro_export]
/// Construct a [`VisCode`](crate::VisCode), validated at compile time.
///
/// ```rust
/// use sstv::vis_code;
///
/// let code = vis_code!(8);
/// ```
///
/// A value that does not fit in 7 bits fails to compile:
///
/// ```compile_fail
/// use sstv::vis_code;
///
/// let code = vis_code!(200);
/// ```
macro_rules! vis_code {
    ($code:expr) => {
        const {
            assert!($crate::VisCode::is_valid($code), "VIS codes are 7 bit");
            $crate::VisCode::new_masked($code)
        }
    };
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec::Vec;

    use super::*;
    use crate::tone;

    /// Robot 36's code 8 (`0b000_1000`): a single set bit, so the even parity
    /// bit is a one.
    #[test]
    fn tones_encode_the_bits_least_significant_first() {
        assert_eq!(
            vis_code!(8).tones().collect::<Vec<_>>(),
            std::vec![
                tone!(1200 Hz, 30 ms), // start bit
                tone!(1300 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1100 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1100 Hz, 30 ms), // parity bit
                tone!(1200 Hz, 30 ms), // stop bit
            ]
        );
    }

    /// Scottie 1's code 60 (`0b011_1100`): four set bits, so the even parity
    /// bit is a zero.
    #[test]
    fn parity_bit_is_even() {
        assert_eq!(
            vis_code!(60).tones().collect::<Vec<_>>(),
            std::vec![
                tone!(1200 Hz, 30 ms), // start bit
                tone!(1300 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1100 Hz, 30 ms),
                tone!(1100 Hz, 30 ms),
                tone!(1100 Hz, 30 ms),
                tone!(1100 Hz, 30 ms),
                tone!(1300 Hz, 30 ms),
                tone!(1300 Hz, 30 ms), // parity bit
                tone!(1200 Hz, 30 ms), // stop bit
            ]
        );
    }

    #[test]
    fn transmitted_data_bits_are_read_back() {
        let data_bits: Vec<Tone> = vis_code!(8).tones().skip(1).take(8).collect();

        assert_eq!(VisCode::from_received_tones(&data_bits), Some(vis_code!(8)));
    }

    /// PD 180's code 96 (`0b110_0000`): five zeros, then two ones, then a zero
    /// parity bit.
    #[test]
    fn merged_bits_are_counted_by_their_duration() {
        let data_bits = [
            tone!(1300 Hz, 150 ms), // five zeros
            tone!(1100 Hz, 60 ms),  // two ones
            tone!(1300 Hz, 30 ms),  // parity bit
        ];

        assert_eq!(
            VisCode::from_received_tones(&data_bits),
            Some(vis_code!(96))
        );
    }

    /// Robot 36's code 8 with its parity bit flipped to a zero.
    #[test]
    fn odd_parity_is_rejected() {
        let data_bits = [
            tone!(1300 Hz, 90 ms),  // three zeros
            tone!(1100 Hz, 30 ms),  // a one
            tone!(1300 Hz, 120 ms), // three zeros and the flipped parity bit
        ];

        assert_eq!(VisCode::from_received_tones(&data_bits), None);
    }

    #[test]
    fn missing_bits_are_rejected() {
        let five_bits = [tone!(1300 Hz, 150 ms)];

        assert_eq!(VisCode::from_received_tones(&five_bits), None);
    }

    #[test]
    fn surplus_bits_are_rejected() {
        let nine_bits = [tone!(1300 Hz, 270 ms)];

        assert_eq!(VisCode::from_received_tones(&nine_bits), None);
    }

    #[test]
    fn tones_that_are_neither_one_nor_zero_are_rejected() {
        let data_bits = [tone!(1300 Hz, 120 ms), tone!(1500 Hz, 120 ms)];

        assert_eq!(VisCode::from_received_tones(&data_bits), None);
    }
}
