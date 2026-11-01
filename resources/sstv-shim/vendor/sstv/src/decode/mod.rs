//! Decoding SSTV audio into an image: the [`Demodulator`] turns samples into
//! frequencies, the assembler turns frequencies into tones, and the
//! [`Decoder`] walks the tones through the mode's timing sequence.
//!
//! The demodulator needs no allocator; everything after it does.

#[cfg(feature = "alloc")]
mod assembler;
#[cfg(feature = "alloc")]
mod convert;
#[cfg(feature = "alloc")]
mod decoded_image;
#[cfg(feature = "alloc")]
mod decoded_row;
#[cfg(feature = "alloc")]
mod decoder;
mod demodulator;
#[cfg(feature = "alloc")]
mod rows;
#[cfg(feature = "alloc")]
#[cfg(test)]
mod testing;
#[cfg(feature = "alloc")]
mod walk;

#[cfg(feature = "alloc")]
pub use decoded_image::DecodedImage;
#[cfg(feature = "alloc")]
pub use decoded_row::DecodedRow;
#[cfg(feature = "alloc")]
pub use decoder::{Decoder, Rows};
pub use demodulator::Demodulator;
