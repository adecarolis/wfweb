#![doc = "Slow-Scan Television Encoding and Decoding With Minimal Memory Usage"]
#![cfg_attr(not(any(feature = "std", test)), no_std)]
#![warn(missing_docs)]

#[cfg(feature = "alloc")]
extern crate alloc;

mod error;

mod decode;
mod encode;
mod image;
pub mod modes;
mod units;

pub use decode::Demodulator;
#[cfg(feature = "alloc")]
pub use decode::{DecodedImage, DecodedRow, Decoder, Rows};
pub use encode::{Encoder, Synthesizer};
pub use error::{Error, Result};
pub use image::{RgbPixel, YuvPixel};
pub use modes::{Mode, VisCode};
pub use units::{Duration, Frequency, Tone};
