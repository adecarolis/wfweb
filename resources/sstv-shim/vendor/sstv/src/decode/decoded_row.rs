//! A row of an image a decoder recovers.

use alloc::vec::Vec;

use crate::RgbPixel;

/// One row of a decoded image, handed out by [`Rows`](super::Rows) as soon
/// as it is decoded.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DecodedRow {
    index: usize,
    pixels: Vec<RgbPixel>,
}

impl DecodedRow {
    pub(super) const fn new(index: usize, pixels: Vec<RgbPixel>) -> Self {
        Self { index, pixels }
    }

    /// The row's position within the image; `0` is the top row.
    #[must_use]
    pub const fn index(&self) -> usize {
        self.index
    }

    /// The row's pixels, left to right, as many as the mode's image width.
    #[must_use]
    pub fn pixels(&self) -> &[RgbPixel] {
        &self.pixels
    }
}
