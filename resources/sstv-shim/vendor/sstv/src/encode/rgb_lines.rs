//! The buffered image lines the encoder's scan steps sample from.

#[cfg(feature = "alloc")]
use alloc::vec::Vec;

use crate::image::{RgbPixel, YuvPixel};
use crate::modes::step::{Channel, ColorMode};

/// The image lines the scan steps sample — one line for most modes, the line
/// pair for Robot 36 and PD modes — stored line after line.
pub(super) struct RgbLines<'a> {
    pub(super) storage: Storage<'a>,
    /// The width of one line in pixels.
    pub(super) width: usize,
    /// How the buffered lines combine into colour channels.
    pub(super) color: ColorMode,
}

impl RgbLines<'_> {
    /// The number of buffered rows.
    fn row_count(&self) -> usize {
        self.storage.as_slice().len() / self.width
    }

    /// Replace the buffered lines with the next ones from the pixel iterator.
    /// Returns `None` once the image runs out of complete line groups.
    pub(super) fn fill_next(&mut self, pixels: &mut impl Iterator<Item = RgbPixel>) -> Option<()> {
        for slot in self.storage.as_mut_slice() {
            *slot = pixels.next()?;
        }
        Some(())
    }

    /// The pixel value a scan step transmits at horizontal position `column`.
    /// The channel determines which buffered row it reads.
    pub(super) fn value(&self, column: usize, channel: Channel) -> u8 {
        match channel {
            Channel::Red => self.rgb(0, column).red(),
            Channel::Green => self.rgb(0, column).green(),
            Channel::Blue => self.rgb(0, column).blue(),
            Channel::Y => self.yuv(0, column).luma(),
            Channel::YSecond => self.yuv(1, column).luma(),
            Channel::RY => self.chroma(column, YuvPixel::chroma_red),
            Channel::BY => self.chroma(column, YuvPixel::chroma_blue),
        }
    }

    fn rgb(&self, row: usize, column: usize) -> RgbPixel {
        self.storage.as_slice()[row * self.width + column]
    }

    fn yuv(&self, row: usize, column: usize) -> YuvPixel {
        YuvPixel::from(self.rgb(row, column))
    }

    /// One colour-difference component, averaged over all buffered lines
    /// where the mode calls for it (Robot 36 and PD modes).
    fn chroma(&self, column: usize, component: fn(YuvPixel) -> u8) -> u8 {
        match self.color {
            ColorMode::YuvSharedPair => {
                let row_count = self.row_count();
                let sum: u16 = (0..row_count)
                    .map(|row| u16::from(component(self.yuv(row, column))))
                    .sum();
                (sum / row_count as u16) as u8
            }
            _ => component(self.yuv(0, column)),
        }
    }
}

/// The backing storage of the buffered lines: allocated by
/// [`Encoder::new`](super::Encoder::new), or caller-provided through
/// [`Encoder::new_in`](super::Encoder::new_in).
pub(super) enum Storage<'a> {
    #[cfg(feature = "alloc")]
    Owned(Vec<RgbPixel>),
    Borrowed(&'a mut [RgbPixel]),
}

impl Storage<'_> {
    fn as_slice(&self) -> &[RgbPixel] {
        match self {
            #[cfg(feature = "alloc")]
            Self::Owned(pixels) => pixels,
            Self::Borrowed(pixels) => pixels,
        }
    }

    fn as_mut_slice(&mut self) -> &mut [RgbPixel] {
        match self {
            #[cfg(feature = "alloc")]
            Self::Owned(pixels) => pixels,
            Self::Borrowed(pixels) => pixels,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TOP_ROW: [RgbPixel; 2] = [RgbPixel::new(200, 40, 10), RgbPixel::new(1, 2, 3)];
    const BOTTOM_ROW: [RgbPixel; 2] = [RgbPixel::new(10, 90, 250), RgbPixel::new(4, 5, 6)];

    #[test]
    fn colour_channels_read_the_top_row() {
        let mut pixels = two_rows();
        let lines = lines_over(&mut pixels, ColorMode::Rgb);

        assert_eq!(lines.value(0, Channel::Red), TOP_ROW[0].red());
        assert_eq!(lines.value(1, Channel::Green), TOP_ROW[1].green());
        assert_eq!(lines.value(1, Channel::Blue), TOP_ROW[1].blue());
    }

    #[test]
    fn first_luminance_reads_the_top_row() {
        let mut pixels = two_rows();
        let lines = lines_over(&mut pixels, ColorMode::YuvSharedPair);

        assert_eq!(
            lines.value(0, Channel::Y),
            YuvPixel::from(TOP_ROW[0]).luma()
        );
    }

    #[test]
    fn second_luminance_reads_the_bottom_row() {
        let mut pixels = two_rows();
        let lines = lines_over(&mut pixels, ColorMode::YuvSharedPair);

        assert_eq!(
            lines.value(0, Channel::YSecond),
            YuvPixel::from(BOTTOM_ROW[0]).luma()
        );
    }

    #[test]
    fn shared_pair_colour_differences_average_both_rows() {
        let mut pixels = two_rows();
        let lines = lines_over(&mut pixels, ColorMode::YuvSharedPair);
        let top = YuvPixel::from(TOP_ROW[0]);
        let bottom = YuvPixel::from(BOTTOM_ROW[0]);

        let red_average = average(top.chroma_red(), bottom.chroma_red());
        let blue_average = average(top.chroma_blue(), bottom.chroma_blue());
        assert_eq!(lines.value(0, Channel::RY), red_average);
        assert_eq!(lines.value(0, Channel::BY), blue_average);
    }

    #[test]
    fn single_line_colour_differences_read_the_top_row() {
        let mut pixels = two_rows();
        let lines = lines_over(&mut pixels, ColorMode::Yuv);
        let top = YuvPixel::from(TOP_ROW[0]);

        assert_eq!(lines.value(0, Channel::RY), top.chroma_red());
        assert_eq!(lines.value(0, Channel::BY), top.chroma_blue());
    }

    #[test]
    fn filling_from_too_few_pixels_fails() {
        let mut pixels = two_rows();
        let mut lines = lines_over(&mut pixels, ColorMode::Rgb);

        assert!(lines.fill_next(&mut TOP_ROW.into_iter()).is_none());
    }

    #[test]
    fn filling_from_enough_pixels_succeeds() {
        let mut pixels = two_rows();
        let mut lines = lines_over(&mut pixels, ColorMode::Rgb);
        let mut both_rows = TOP_ROW.into_iter().chain(BOTTOM_ROW);

        assert!(lines.fill_next(&mut both_rows).is_some());
    }

    fn two_rows() -> [RgbPixel; 4] {
        [TOP_ROW[0], TOP_ROW[1], BOTTOM_ROW[0], BOTTOM_ROW[1]]
    }

    fn lines_over(pixels: &mut [RgbPixel], color: ColorMode) -> RgbLines<'_> {
        RgbLines {
            storage: Storage::Borrowed(pixels),
            width: 2,
            color,
        }
    }

    fn average(first: u8, second: u8) -> u8 {
        u16::midpoint(u16::from(first), u16::from(second)) as u8
    }
}
