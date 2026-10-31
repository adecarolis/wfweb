//! Turns the sampled scans of one timing sequence into rows of pixels,
//! according to the mode's colour system.

use alloc::vec::Vec;

use crate::image::{RgbPixel, YuvPixel};
use crate::modes::step::{Channel, ColorMode};

/// The rows completed by one sequence pass, in transmission order, from each
/// scan's channel and sampled pixel values.
// expect: the layouts guarantee every scan their colour mode relies on.
#[allow(clippy::expect_used)]
pub(super) fn assemble_rows(color: ColorMode, scans: &[(Channel, Vec<u8>)]) -> Vec<Vec<RgbPixel>> {
    let scan = |channel: Channel| {
        scans
            .iter()
            .find(|(scanned, _)| *scanned == channel)
            .map(|(_, values)| values.as_slice())
            .expect("the mode's timing sequence contains this scan")
    };

    match color {
        ColorMode::Rgb => {
            let (red, green, blue) = (
                scan(Channel::Red),
                scan(Channel::Green),
                scan(Channel::Blue),
            );
            let row = (0..red.len())
                .map(|column| RgbPixel::new(red[column], green[column], blue[column]))
                .collect();
            alloc::vec![row]
        }
        ColorMode::Yuv => alloc::vec![yuv_row(
            scan(Channel::Y),
            scan(Channel::RY),
            scan(Channel::BY)
        )],
        ColorMode::YuvSharedPair => {
            let (ry, by) = (scan(Channel::RY), scan(Channel::BY));
            alloc::vec![
                yuv_row(scan(Channel::Y), ry, by),
                yuv_row(scan(Channel::YSecond), ry, by),
            ]
        }
    }
}

fn yuv_row(luma: &[u8], chroma_red: &[u8], chroma_blue: &[u8]) -> Vec<RgbPixel> {
    luma.iter()
        .zip(chroma_red.iter().zip(chroma_blue))
        .map(|(&y, (&ry, &by))| RgbPixel::from(YuvPixel::new(y, ry, by)))
        .collect()
}
