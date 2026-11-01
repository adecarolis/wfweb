//! Synthetic images and transmissions shared by the decoder's unit tests.
//! Tests against recordings live in `tests/`.

extern crate std;
use std::vec::Vec;

use crate::modes::Mode;
use crate::{Encoder, RgbPixel, Synthesizer};

/// An image at the mode's resolution, brightening to the right in red,
/// downwards in green and diagonally in blue.
pub fn gradient_image(mode: Mode) -> Vec<RgbPixel> {
    let (width, height) = mode.resolution();
    let pixel_at = |row: u32, column: u32| {
        RgbPixel::new(
            (column * 255 / width) as u8,
            (row * 255 / height) as u8,
            ((row + column) * 255 / (width + height)) as u8,
        )
    };
    (0..height)
        .flat_map(|row| (0..width).map(move |column| pixel_at(row, column)))
        .collect()
}

/// The samples of the image's whole transmission, header included.
pub fn transmit(mode: Mode, image: &[RgbPixel], sample_rate: u32) -> Vec<i16> {
    let encoder = Encoder::new(mode, image.iter().copied()).unwrap();
    Synthesizer::new(encoder, sample_rate).collect()
}

/// The number of samples the header occupies, up to the first line.
pub fn header_length(mode: Mode, sample_rate: u32) -> usize {
    let header_ns: u64 = mode.header_tones().map(|tone| tone.duration.ns()).sum();
    (header_ns * u64::from(sample_rate) / 1_000_000_000) as usize
}

/// Mean absolute per-channel difference between two images of equal size.
pub fn mean_abs_error(expected: &[RgbPixel], actual: &[RgbPixel]) -> f64 {
    assert_eq!(expected.len(), actual.len());
    let difference = |a: u8, b: u8| f64::from(a.abs_diff(b));
    let total: f64 = expected
        .iter()
        .zip(actual)
        .map(|(e, a)| {
            difference(e.red(), a.red())
                + difference(e.green(), a.green())
                + difference(e.blue(), a.blue())
        })
        .sum();
    total / (expected.len() as f64 * 3.0)
}
