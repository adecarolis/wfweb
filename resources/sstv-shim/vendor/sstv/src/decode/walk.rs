//! Walking the image's tones through the mode's timing sequence, as the
//! encoder does: one tone per control step and one per pixel.

use alloc::collections::VecDeque;
use alloc::vec::Vec;

use super::DecodedImage;
use super::rows::assemble_rows;
use crate::RgbPixel;
use crate::modes::step::Step;
use crate::modes::{Mode, frequency_value};
use crate::units::Tone;

/// The image the tones carry, incomplete if they run out first. Rows the
/// tones did not carry are black.
pub(super) fn decode_image(mode: Mode, tones: impl Iterator<Item = Tone>) -> DecodedImage {
    let (width, height) = mode.resolution;
    let rows: Vec<Vec<RgbPixel>> = RowWalk::new(mode, tones).collect();
    let complete = rows.len() == height;

    let mut pixels: Vec<RgbPixel> = rows.into_iter().flatten().collect();
    pixels.resize(width * height, RgbPixel::new(0, 0, 0));
    DecodedImage::new(mode, complete, pixels)
}

/// The rows the tones carry, top to bottom, each as soon as the pass through
/// the mode's timing sequence that carries it is complete. Ends after the
/// image's last row, or when the tones run out.
pub(super) struct RowWalk<T: Iterator<Item = Tone>> {
    mode: Mode,
    tones: T,
    /// Rows the last pass completed but that have not been handed out yet.
    completed: VecDeque<Vec<RgbPixel>>,
    rows_left: usize,
}

impl<T: Iterator<Item = Tone>> RowWalk<T> {
    pub const fn new(mode: Mode, tones: T) -> Self {
        Self {
            mode,
            tones,
            completed: VecDeque::new(),
            rows_left: mode.resolution.1,
        }
    }

    pub const fn mode(&self) -> Mode {
        self.mode
    }
}

impl<T: Iterator<Item = Tone>> Iterator for RowWalk<T> {
    type Item = Vec<RgbPixel>;

    fn next(&mut self) -> Option<Vec<RgbPixel>> {
        if self.rows_left == 0 {
            return None;
        }
        if self.completed.is_empty() {
            self.completed
                .extend(decode_sequence(self.mode, &mut self.tones)?);
        }
        let row = self.completed.pop_front()?;
        self.rows_left -= 1;
        Some(row)
    }
}

/// The rows completed by one pass through the mode's timing sequence, or
/// `None` if the tones run out first.
fn decode_sequence(
    mode: Mode,
    tones: &mut impl Iterator<Item = Tone>,
) -> Option<Vec<Vec<RgbPixel>>> {
    let width = mode.resolution.0;
    let mut scans = Vec::new();
    for step in mode.sequence {
        let Step::Scan(channel, _) = step else {
            tones.next()?;
            continue;
        };

        let values = (0..width)
            .map(|_| tones.next().map(|tone| frequency_value(tone.frequency)))
            .collect::<Option<Vec<u8>>>()?;

        scans.push((*channel, values));
    }
    Some(assemble_rows(mode.color, &scans))
}
