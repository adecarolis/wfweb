//! The image a decoder recovers.

use alloc::vec::Vec;

use crate::RgbPixel;
use crate::modes::Mode;

/// An image decoded by a [`Decoder`](super::Decoder).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DecodedImage {
    mode: Mode,
    width: usize,
    height: usize,
    complete: bool,
    pixels: Vec<RgbPixel>,
}

impl DecodedImage {
    /// `pixels` must hold the mode's full resolution, row by row.
    pub(super) const fn new(mode: Mode, complete: bool, pixels: Vec<RgbPixel>) -> Self {
        let (width, height) = mode.resolution;
        Self {
            mode,
            width,
            height,
            complete,
            pixels,
        }
    }

    /// The SSTV mode the image was transmitted in.
    #[must_use]
    pub const fn mode(&self) -> Mode {
        self.mode
    }

    /// The image width in pixels.
    #[must_use]
    pub const fn width(&self) -> usize {
        self.width
    }

    /// The image height in pixels.
    #[must_use]
    pub const fn height(&self) -> usize {
        self.height
    }

    /// Whether every row was decoded. `false` if the signal ended before the
    /// image did.
    #[must_use]
    pub const fn complete(&self) -> bool {
        self.complete
    }

    /// The pixels in row-major order, always `width() * height()` of them.
    /// Rows the signal did not carry are black.
    #[must_use]
    pub fn pixels(&self) -> &[RgbPixel] {
        &self.pixels
    }
}
