//! Conversions between the transmission and common file formats.

#[cfg(any(feature = "wav", feature = "mp3", feature = "image"))]
use alloc::vec::Vec;

use super::Encoder;
#[cfg(feature = "image")]
use crate::Result;
use crate::image::RgbPixel;
#[cfg(feature = "image")]
use crate::modes::Mode;

/// The transmission as a whole, packed into common audio containers.
impl<I> Encoder<'_, I>
where
    I: Iterator<Item = RgbPixel>,
{
    /// The transmission as a complete mono 16-bit PCM WAV at the given sample
    /// rate; see [`Synthesizer::to_wav`](crate::Synthesizer::to_wav).
    ///
    /// ```no_run
    /// use sstv::{modes::ROBOT_36, Encoder, RgbPixel};
    ///
    /// let image = [RgbPixel::new(0, 0, 0); 320 * 240];
    /// let wav = Encoder::new(ROBOT_36, image.into_iter())?.to_wav(48_000);
    /// std::fs::write("transmission.wav", wav)?;
    /// # Ok::<(), Box<dyn std::error::Error>>(())
    /// ```
    #[cfg(feature = "wav")]
    #[must_use]
    pub fn to_wav(self, sample_rate: u32) -> Vec<u8> {
        crate::Synthesizer::new(self, sample_rate).to_wav()
    }

    /// The transmission as a complete mono 128 kbps MP3 at the given sample
    /// rate; see [`Synthesizer::to_mp3`](crate::Synthesizer::to_mp3).
    ///
    /// # Errors
    ///
    /// Fails if LAME rejects the sample rate.
    #[cfg(feature = "mp3")]
    pub fn to_mp3(
        self,
        sample_rate: u32,
    ) -> core::result::Result<Vec<u8>, mp3lame_encoder::BuildError> {
        crate::Synthesizer::new(self, sample_rate).to_mp3()
    }
}

#[cfg(feature = "image")]
impl Encoder<'static, alloc::vec::IntoIter<RgbPixel>> {
    /// Encode an image loaded with the `image` crate.
    ///
    /// The image is resized to the mode's resolution if it does not match,
    /// stretching it to fit.
    ///
    /// ```no_run
    /// use sstv::{modes::ROBOT_36, Encoder};
    ///
    /// let image = image::open("image.png")?;
    /// let encoder = Encoder::from_image(ROBOT_36, &image)?;
    /// # Ok::<(), Box<dyn std::error::Error>>(())
    /// ```
    ///
    /// # Errors
    ///
    /// [`Error::EmptyImage`](crate::Error::EmptyImage) if the mode has no
    /// pixels, which cannot happen for the supported modes.
    pub fn from_image(mode: Mode, image: &image::DynamicImage) -> Result<Self> {
        let mut image = image.to_rgb8();

        let image_resolution = (image.width(), image.height());
        if image_resolution != mode.resolution() {
            image = image::imageops::resize(
                &image,
                mode.resolution().0,
                mode.resolution().1,
                image::imageops::FilterType::Triangle,
            );
        }

        // The pixels must outlive the image buffer this function drops, so
        // collecting them is not needless.
        #[allow(clippy::needless_collect)]
        let pixels: Vec<RgbPixel> = image
            .pixels()
            .map(|pixel| RgbPixel::new(pixel[0], pixel[1], pixel[2]))
            .collect();

        Self::new(mode, pixels.into_iter())
    }
}
