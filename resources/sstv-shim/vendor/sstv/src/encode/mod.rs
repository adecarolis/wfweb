//! Encoding an image into SSTV tones, and synthesizing the tones into audio
//! samples.

mod convert;
mod emit;
mod rgb_lines;
mod synthesizer;

#[cfg(test)]
pub use emit::testing;

use crate::image::RgbPixel;
use crate::modes::Mode;
use crate::{Error, Result};

use emit::State;
use rgb_lines::{RgbLines, Storage};

pub use synthesizer::Synthesizer;

/// `Encoder` is the main struct for converting an image into SSTV tones.
///
/// Construct `Encoder` with your desired mode and an iterator over the pixels
/// you want to encode, supplied row by row, top to bottom. The image must be
/// [`Mode::resolution`] pixels in size.
/// ```rust
/// use sstv::{modes::ROBOT_36, Encoder, Error, RgbPixel};
///
/// let image = [RgbPixel::new(0, 0, 0); 320 * 240];
/// let encoder = Encoder::new(ROBOT_36, image.into_iter())?;
/// for tone in encoder {
///     // emit or save the tones
/// }
/// # Ok::<(), Error>(())
/// ```
///
/// It encodes any mode by walking its timing sequence: the header, then for
/// each group of buffered lines one pass through the sequence, emitting fixed
/// tones verbatim and expanding each scan step into one tone per pixel.
pub struct Encoder<'a, I>
where
    I: Iterator<Item = RgbPixel>,
{
    mode: Mode,
    pixels: I,
    lines: RgbLines<'a>,
    state: State,
}

#[cfg(feature = "alloc")]
impl<I> Encoder<'static, I>
where
    I: Iterator<Item = RgbPixel>,
{
    /// Construct an `Encoder` from the mode and a pixel iterator, buffering
    /// lines in an allocation of its own. Available with the `alloc` feature
    /// (on by default); [`Encoder::new_in`] encodes without allocating.
    ///
    /// # Errors
    ///
    /// [`Error::EmptyImage`] if the iterator cannot fill the mode's first
    /// lines.
    pub fn new(mode: Mode, pixels: I) -> Result<Self> {
        let storage = alloc::vec![RgbPixel::new(0, 0, 0); mode.encoder_buffer_len()];
        Self::with_storage(mode, pixels, Storage::Owned(storage))
    }
}

impl<'a, I> Encoder<'a, I>
where
    I: Iterator<Item = RgbPixel>,
{
    /// Construct an `Encoder` that buffers lines in the given storage instead
    /// of allocating. The buffer must hold at least one of the mode's line
    /// groups — [`Mode::encoder_buffer_len`] pixels.
    ///
    /// ```rust
    /// use sstv::{modes::ROBOT_36, Encoder, RgbPixel};
    ///
    /// let image = [RgbPixel::new(0, 0, 0); 320 * 240];
    /// let mut buffer = [RgbPixel::new(0, 0, 0); ROBOT_36.encoder_buffer_len()];
    /// let encoder = Encoder::new_in(ROBOT_36, image.into_iter(), &mut buffer)?;
    /// # Ok::<(), sstv::Error>(())
    /// ```
    ///
    /// # Errors
    ///
    /// [`Error::BufferTooSmall`] if the buffer cannot hold a line group, and
    /// [`Error::EmptyImage`] if the iterator cannot fill the mode's first
    /// lines.
    pub fn new_in(mode: Mode, pixels: I, buffer: &'a mut [RgbPixel]) -> Result<Self> {
        let Some(storage) = buffer.get_mut(..mode.encoder_buffer_len()) else {
            return Err(Error::BufferTooSmall);
        };
        Self::with_storage(mode, pixels, Storage::Borrowed(storage))
    }

    fn with_storage(mode: Mode, mut pixels: I, storage: Storage<'a>) -> Result<Self> {
        let mut lines = RgbLines {
            storage,
            width: mode.resolution.0,
            color: mode.color,
        };

        if lines.fill_next(&mut pixels).is_none() {
            return Err(Error::EmptyImage);
        }

        Ok(Self {
            mode,
            pixels,
            lines,
            state: State::NotStarted,
        })
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec;

    use super::testing::black_image;
    use super::*;
    use crate::modes::ROBOT_36;

    const BLACK: RgbPixel = RgbPixel::new(0, 0, 0);

    #[test]
    fn buffer_shorter_than_a_line_group_is_rejected() {
        let mut short_buffer = vec![BLACK; ROBOT_36.encoder_buffer_len() - 1];

        let encoder = Encoder::new_in(ROBOT_36, black_image(ROBOT_36), &mut short_buffer);

        assert!(matches!(encoder, Err(Error::BufferTooSmall)));
    }

    #[test]
    fn buffer_longer_than_a_line_group_is_accepted() {
        let mut long_buffer = vec![BLACK; ROBOT_36.encoder_buffer_len() + 100];

        let encoder = Encoder::new_in(ROBOT_36, black_image(ROBOT_36), &mut long_buffer);

        assert!(encoder.is_ok());
    }

    #[test]
    fn truncated_image_ends_the_transmission_early() {
        let (width, height) = ROBOT_36.resolution();
        let top_half = black_image(ROBOT_36).take((width * height / 2) as usize);

        let truncated_tone_count = tone_count(ROBOT_36, top_half);

        assert!(truncated_tone_count > ROBOT_36.header_tones().count());
        assert!(truncated_tone_count < tone_count(ROBOT_36, black_image(ROBOT_36)));
    }

    fn tone_count(mode: Mode, pixels: impl Iterator<Item = RgbPixel>) -> usize {
        let mut buffer = vec![BLACK; mode.encoder_buffer_len()];
        Encoder::new_in(mode, pixels, &mut buffer).unwrap().count()
    }
}
