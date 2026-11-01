//! Walking the transmission: tone emission and the state machine behind it.

use super::Encoder;
use crate::image::RgbPixel;
use crate::modes::step::Step;
use crate::modes::{Mode, value_frequency};
use crate::units::Tone;

impl<I> Iterator for Encoder<'_, I>
where
    I: Iterator<Item = RgbPixel>,
{
    type Item = Tone;

    fn next(&mut self) -> Option<Tone> {
        self.state.advance(self.mode);

        let pixel_iterator_is_empty =
            self.state.needs_next_lines() && self.lines.fill_next(&mut self.pixels).is_none();
        if pixel_iterator_is_empty {
            self.state = State::Finished;
        }

        self.emit()
    }
}

impl<I> Encoder<'_, I>
where
    I: Iterator<Item = RgbPixel>,
{
    /// The tone belonging to the current state.
    fn emit(&self) -> Option<Tone> {
        match self.state {
            State::NotStarted | State::Finished => None,
            State::Header(index) => self.mode.header_tone(index),
            State::Image { step, pixel, .. } => Some(self.emit_image_tone(step, pixel)),
        }
    }

    fn emit_image_tone(&self, step: usize, pixel: usize) -> Tone {
        let current_step = self.mode.sequence[step];
        match current_step {
            Step::Control(tone) => tone,
            Step::Scan(channel, duration) => {
                let value = self.lines.value(pixel, channel);
                Tone::new(
                    value_frequency(value),
                    duration / self.mode.resolution.0 as u32,
                )
            }
        }
    }
}

/// Where the encoder is within the transmission.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) enum State {
    NotStarted,
    Header(usize),
    Image {
        /// The first image row of the currently buffered line group.
        row: usize,
        /// The position within the timing sequence's steps.
        step: usize,
        /// The horizontal position within a scan step.
        pixel: usize,
    },
    Finished,
}

impl State {
    /// Step to the state that emits the next tone.
    fn advance(&mut self, mode: Mode) {
        *self = match *self {
            Self::NotStarted => Self::Header(0),
            Self::Header(index) => Self::advance_header(mode, index),
            Self::Image { row, step, pixel } => Self::advance_image(mode, row, step, pixel),
            Self::Finished => Self::Finished,
        };
    }

    /// The state after the `index`-th header tone.
    fn advance_header(mode: Mode, index: usize) -> Self {
        let header_has_more_tones = mode.header_tone(index + 1).is_some();

        if header_has_more_tones {
            return Self::Header(index + 1);
        }

        Self::Image {
            row: 0,
            step: 0,
            pixel: 0,
        }
    }

    /// The state after the given position within the image.
    const fn advance_image(
        mode: Mode,
        row_index: usize,
        step_index: usize,
        pixel_index: usize,
    ) -> Self {
        let current_step = mode.sequence[step_index];
        let image_width = mode.resolution().0 as usize;
        let image_height = mode.resolution().1 as usize;

        let scan_is_ongoing = current_step.is_scan() && pixel_index + 1 < image_width;
        if scan_is_ongoing {
            return Self::Image {
                row: row_index,
                step: step_index,
                pixel: pixel_index + 1,
            };
        }

        let sequence_is_ongoing = step_index + 1 < mode.sequence.len();
        if sequence_is_ongoing {
            return Self::Image {
                row: row_index,
                step: step_index + 1,
                pixel: 0,
            };
        }

        let image_is_ongoing = row_index + mode.lines_per_sequence < image_height;
        if image_is_ongoing {
            return Self::Image {
                row: row_index + mode.lines_per_sequence,
                step: 0,
                pixel: 0,
            };
        }

        Self::Finished
    }

    /// Whether the state just moved onto the first tone of a line group whose
    /// lines are not buffered yet. The first group's lines are already
    /// buffered at construction.
    const fn needs_next_lines(&self) -> bool {
        matches!(
            self,
            Self::Image {
                row,
                step: 0,
                pixel: 0,
            } if *row > 0
        )
    }
}

/// Assertions on whole transmissions, shared by every mode's tests.
#[cfg(test)]
pub mod testing {
    extern crate std;
    use std::vec::Vec;

    use super::*;
    use crate::units::Duration;

    const BLACK: RgbPixel = RgbPixel::new(0, 0, 0);

    /// Assert that the transmission holds one tone per header tone, one per
    /// control step and one per scanned pixel.
    pub fn assert_one_tone_per_control_step_and_scanned_pixel(mode: Mode) {
        let expected_tone_count =
            mode.header_tones().count() + pass_count(mode) * tones_per_pass(mode);

        assert_eq!(encode_uniform_image(mode, BLACK).len(), expected_tone_count);
    }

    /// Assert that the transmission lasts as long as its header plus one pass
    /// through the timing sequence per line group.
    pub fn assert_transmission_lasts_header_plus_every_pass(mode: Mode) {
        let header_duration: Duration = mode.header_tones().map(|tone| tone.duration).sum();
        let expected_duration =
            header_duration + transmitted_pass_duration(mode) * pass_count(mode) as u32;

        let tones = encode_uniform_image(mode, BLACK);
        let actual_duration: Duration = tones.into_iter().map(|tone| tone.duration).sum();

        assert_eq!(actual_duration, expected_duration);
    }

    /// A black image at the mode's resolution, row by row.
    pub fn black_image(mode: Mode) -> impl Iterator<Item = RgbPixel> {
        let (width, height) = mode.resolution();
        core::iter::repeat_n(BLACK, (width * height) as usize)
    }

    /// Every tone of the transmission of an image filled with one colour.
    pub fn encode_uniform_image(mode: Mode, pixel: RgbPixel) -> Vec<Tone> {
        let (width, height) = mode.resolution();
        let pixels = core::iter::repeat_n(pixel, (width * height) as usize);
        let mut buffer = std::vec![BLACK; mode.encoder_buffer_len()];
        Encoder::new_in(mode, pixels, &mut buffer)
            .unwrap()
            .collect()
    }

    /// The number of passes through the timing sequence the image takes.
    fn pass_count(mode: Mode) -> usize {
        mode.resolution.1 / mode.lines_per_sequence
    }

    /// The number of tones one pass emits: one per control step, one per
    /// pixel of each scan.
    fn tones_per_pass(mode: Mode) -> usize {
        let width = mode.resolution.0;
        mode.sequence
            .iter()
            .map(|step| if step.is_scan() { width } else { 1 })
            .sum()
    }

    /// The duration of one pass as transmitted: each scan is split into one
    /// tone per pixel, whose durations are truncated to whole nanoseconds.
    fn transmitted_pass_duration(mode: Mode) -> Duration {
        let width = mode.resolution.0 as u32;
        mode.sequence
            .iter()
            .map(|step| match step {
                Step::Control(tone) => tone.duration,
                Step::Scan(_, scan_duration) => *scan_duration / width * width,
            })
            .sum()
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec::Vec;

    use super::testing::encode_uniform_image;
    use super::*;
    use crate::modes::step::Channel;
    use crate::modes::{BLACK_FREQUENCY, MARTIN_1, ROBOT_36, WHITE_FREQUENCY};
    use crate::units::Frequency;

    /// Martin 1's sequence opens with its sync pulse.
    const MARTIN_1_SYNC_PULSE_STEP: usize = 0;
    /// Martin 1's green scan follows the sync pulse and porch.
    const MARTIN_1_GREEN_SCAN_STEP: usize = 2;

    #[test]
    fn header_states_precede_the_first_image_state() {
        let mut state = State::NotStarted;

        for header_index in 0..MARTIN_1.header_tones().count() {
            state.advance(MARTIN_1);
            assert_eq!(state, State::Header(header_index));
        }

        state.advance(MARTIN_1);
        assert_eq!(state, image_state(0, 0, 0));
    }

    #[test]
    fn control_step_advances_to_the_next_step() {
        let next_state = State::advance_image(MARTIN_1, 0, MARTIN_1_SYNC_PULSE_STEP, 0);

        assert_eq!(next_state, image_state(0, MARTIN_1_SYNC_PULSE_STEP + 1, 0));
    }

    #[test]
    fn scan_step_advances_to_the_next_pixel() {
        let next_state = State::advance_image(MARTIN_1, 0, MARTIN_1_GREEN_SCAN_STEP, 0);

        assert_eq!(next_state, image_state(0, MARTIN_1_GREEN_SCAN_STEP, 1));
    }

    #[test]
    fn last_pixel_of_a_scan_advances_to_the_next_step() {
        let last_pixel = MARTIN_1.resolution.0 - 1;

        let next_state = State::advance_image(MARTIN_1, 0, MARTIN_1_GREEN_SCAN_STEP, last_pixel);

        assert_eq!(next_state, image_state(0, MARTIN_1_GREEN_SCAN_STEP + 1, 0));
    }

    #[test]
    fn end_of_a_single_line_sequence_advances_one_row() {
        let last_step = MARTIN_1.sequence.len() - 1;

        let next_state = State::advance_image(MARTIN_1, 0, last_step, 0);

        assert_eq!(next_state, image_state(1, 0, 0));
    }

    #[test]
    fn end_of_a_line_pair_sequence_advances_two_rows() {
        let last_step = ROBOT_36.sequence.len() - 1;
        let last_pixel = ROBOT_36.resolution.0 - 1;

        let next_state = State::advance_image(ROBOT_36, 0, last_step, last_pixel);

        assert_eq!(next_state, image_state(2, 0, 0));
    }

    #[test]
    fn end_of_the_last_line_group_finishes() {
        let last_row = MARTIN_1.resolution.1 - 1;
        let last_step = MARTIN_1.sequence.len() - 1;

        let next_state = State::advance_image(MARTIN_1, last_row, last_step, 0);

        assert_eq!(next_state, State::Finished);
    }

    #[test]
    fn next_lines_are_needed_at_the_start_of_a_later_line_group() {
        assert!(image_state(1, 0, 0).needs_next_lines());
    }

    #[test]
    fn first_line_group_is_already_buffered() {
        assert!(!image_state(0, 0, 0).needs_next_lines());
    }

    #[test]
    fn next_lines_are_not_needed_within_a_line_group() {
        assert!(!image_state(1, 1, 0).needs_next_lines());
        assert!(!image_state(1, MARTIN_1_GREEN_SCAN_STEP, 5).needs_next_lines());
    }

    #[test]
    fn header_needs_no_lines() {
        assert!(!State::Header(0).needs_next_lines());
    }

    #[test]
    fn black_image_scans_colours_at_the_black_frequency() {
        let black = RgbPixel::new(0, 0, 0);

        let frequencies = brightness_scan_frequencies(MARTIN_1, black);

        assert!(
            frequencies
                .iter()
                .all(|frequency| *frequency == BLACK_FREQUENCY)
        );
    }

    #[test]
    fn white_image_scans_colours_at_the_white_frequency() {
        let white = RgbPixel::new(255, 255, 255);

        let frequencies = brightness_scan_frequencies(MARTIN_1, white);

        assert!(
            frequencies
                .iter()
                .all(|frequency| *frequency == WHITE_FREQUENCY)
        );
    }

    #[test]
    fn white_image_scans_both_lines_luminance_at_the_white_frequency() {
        let white = RgbPixel::new(255, 255, 255);

        let frequencies = brightness_scan_frequencies(ROBOT_36, white);

        assert!(
            frequencies
                .iter()
                .all(|frequency| *frequency == WHITE_FREQUENCY)
        );
    }

    fn image_state(row: usize, step: usize, pixel: usize) -> State {
        State::Image { row, step, pixel }
    }

    /// The frequencies of all image tones scanning a brightness channel: a
    /// colour or a luminance, but not a colour difference.
    fn brightness_scan_frequencies(mode: Mode, pixel: RgbPixel) -> Vec<Frequency> {
        let image_tones = encode_uniform_image(mode, pixel)
            .into_iter()
            .skip(mode.header_tones().count());

        image_tones
            .zip(step_of_each_image_tone(mode))
            .filter(|(_, step)| scans_brightness(*step))
            .map(|(tone, _)| tone.frequency)
            .collect()
    }

    /// The step behind each image tone: every control step once and every
    /// scan step once per pixel, repeating with each pass.
    fn step_of_each_image_tone(mode: Mode) -> impl Iterator<Item = Step> {
        mode.sequence
            .iter()
            .flat_map(move |step| {
                let tone_count = if step.is_scan() { mode.resolution.0 } else { 1 };
                core::iter::repeat_n(*step, tone_count)
            })
            .cycle()
    }

    const fn scans_brightness(step: Step) -> bool {
        matches!(
            step,
            Step::Scan(
                Channel::Red | Channel::Green | Channel::Blue | Channel::Y | Channel::YSecond,
                _
            )
        )
    }
}
