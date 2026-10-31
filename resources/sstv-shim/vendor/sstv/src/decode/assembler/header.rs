//! Identifying the mode from the calibration header among the tones.

use crate::modes::{LEADER, Mode, SYNC_FREQUENCY, VisCode};
use crate::units::Tone;
use crate::{ms, tone};

/// Generous in duration, so a leader clipped by a trimmed recording still
/// counts.
const LEADER_TOLERANCE: Tone = tone!(50 Hz, 150 ms);
/// The stop bit closing the VIS code merges with the first line's sync
/// pulse, which has the same frequency and lasts up to 20 ms.
const STOP_BIT_AND_SYNC: Tone = Tone::new(SYNC_FREQUENCY, ms!(40));
/// Covers the stop bit alone (30 ms) up to the stop bit plus the longest
/// sync pulse (50 ms), with 10 ms of slack either side.
const STOP_BIT_AND_SYNC_TOLERANCE: Tone = tone!(50 Hz, 20 ms);
/// The calibration header spans at most this many tones: leader, break,
/// leader, start bit, eight data bits and stop bit.
const HEADER_TONES: usize = 13;

/// The most recent tones, watched for a calibration header.
pub(super) struct HeaderWindow {
    /// Oldest first.
    tones: [Tone; HEADER_TONES],
    /// The mode a header must announce to count; `None` accepts any.
    expected_mode: Option<Mode>,
}

impl HeaderWindow {
    pub const fn new(expected_mode: Option<Mode>) -> Self {
        Self {
            tones: [tone!(0 Hz, 0 ns); HEADER_TONES],
            expected_mode,
        }
    }

    /// Take in the next tone. Returns the mode once this tone completes a
    /// header announcing it.
    pub fn push(&mut self, tone: Tone) -> Option<Mode> {
        self.tones.copy_within(1.., 0);
        self.tones[HEADER_TONES - 1] = tone;

        let mode = identify_header(&self.tones)?;
        self.expected_mode
            .is_none_or(|expected| expected == mode)
            .then_some(mode)
    }
}

/// The mode announced by a header ending at the newest tone.
fn identify_header(tones: &[Tone]) -> Option<Mode> {
    let data_bits = find_vis_data_bits(tones)?;
    let code = VisCode::from_received_tones(data_bits)?;
    Mode::try_from(code).ok()
}

/// The VIS data-bit tones of a header ending at the newest tone: the tones
/// between a start bit that follows a leader and the stop bit. The break and
/// first leader are not required, so a recording trimmed at the front is
/// still identified.
fn find_vis_data_bits(tones: &[Tone]) -> Option<&[Tone]> {
    let (stop_bit, earlier) = tones.split_last()?;
    if !stop_bit.is_near(STOP_BIT_AND_SYNC, STOP_BIT_AND_SYNC_TOLERANCE) {
        return None;
    }

    let start_bit = earlier
        .iter()
        .rposition(|tone| tone.is_near(VisCode::FRAMING_BIT, VisCode::BIT_TOLERANCE))?;
    let leader = earlier.get(..start_bit)?.last()?;
    if !leader.is_near(LEADER, LEADER_TOLERANCE) {
        return None;
    }

    earlier
        .get(start_bit + 1..)
        .filter(|data_bits| !data_bits.is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::modes::{MARTIN_1, ROBOT_36};

    #[test]
    fn identifies_the_mode_at_the_headers_last_tone() {
        let mut window = HeaderWindow::new(None);

        let identified = push_header(&mut window, ROBOT_36);

        assert_eq!(identified, Some(ROBOT_36));
    }

    #[test]
    fn ignores_headers_announcing_another_mode_than_expected() {
        let mut window = HeaderWindow::new(Some(MARTIN_1));

        let identified = push_header(&mut window, ROBOT_36);

        assert_eq!(identified, None);
    }

    #[test]
    fn identifies_a_header_trimmed_down_to_its_second_leader() {
        let mut window = HeaderWindow::new(None);
        let second_leader_on = ROBOT_36
            .header_tones()
            .skip_while(|tone| *tone != LEADER)
            .skip(2);

        let identified = second_leader_on
            .map(|tone| window.push(tone))
            .last()
            .flatten();

        assert_eq!(identified, Some(ROBOT_36));
    }

    /// Push the mode's header tones, returning what the last one identified.
    fn push_header(window: &mut HeaderWindow, mode: Mode) -> Option<Mode> {
        mode.header_tones()
            .map(|tone| window.push(tone))
            .last()
            .flatten()
    }
}
