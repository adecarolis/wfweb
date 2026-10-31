//! Conversions from common audio formats into the decoder's input, and from
//! its output into `image` crate buffers.

#[cfg(any(feature = "wav", feature = "mp3", feature = "image"))]
use alloc::vec::Vec;

#[cfg(feature = "image")]
use super::DecodedImage;
#[cfg(any(feature = "wav", feature = "mp3", feature = "image"))]
use super::Decoder;

#[cfg(feature = "wav")]
impl Decoder<alloc::vec::IntoIter<i16>> {
    /// Decode a transmission from in-memory WAV data.
    ///
    /// Only the first channel of multi-channel audio is used; integer samples
    /// of any bit depth and float samples are converted to 16 bit.
    ///
    /// Data cut short relative to the length declared in the header, common
    /// in recordings whose writer was interrupted, is decoded up to the cut;
    /// pixels the audio did not carry are left black.
    ///
    /// # Errors
    ///
    /// Fails if the WAV data is malformed.
    pub fn from_wav(wav: &[u8]) -> core::result::Result<Self, hound::Error> {
        // `hound` reports running out of data mid-sample as an I/O error;
        // treat that as end of stream to tolerate truncated recordings.
        fn or_eof<T>(
            sample: core::result::Result<T, hound::Error>,
        ) -> core::result::Result<Option<T>, hound::Error> {
            match sample {
                Ok(sample) => Ok(Some(sample)),
                Err(hound::Error::IoError(_)) => Ok(None),
                Err(error) => Err(error),
            }
        }

        let reader = hound::WavReader::new(std::io::Cursor::new(wav))?;
        let spec = reader.spec();
        let channels = spec.channels.max(1) as usize;

        let mut samples = Vec::with_capacity(reader.len() as usize / channels);
        match spec.sample_format {
            hound::SampleFormat::Int => {
                for (index, sample) in reader.into_samples::<i32>().enumerate() {
                    let Some(sample) = or_eof(sample)? else { break };
                    if index % channels == 0 {
                        let scaled = if spec.bits_per_sample >= 16 {
                            sample >> (spec.bits_per_sample - 16)
                        } else {
                            sample << (16 - spec.bits_per_sample)
                        };
                        samples.push(scaled as i16);
                    }
                }
            }
            hound::SampleFormat::Float => {
                for (index, sample) in reader.into_samples::<f32>().enumerate() {
                    let Some(sample) = or_eof(sample)? else { break };
                    if index % channels == 0 {
                        samples.push((sample * f32::from(i16::MAX)) as i16);
                    }
                }
            }
        }

        Ok(Self::new(samples.into_iter(), spec.sample_rate))
    }
}

#[cfg(feature = "mp3")]
impl Decoder<alloc::vec::IntoIter<i16>> {
    /// Decode a transmission from in-memory MP3 data.
    ///
    /// Only the first channel of multi-channel audio is used.
    ///
    /// # Errors
    ///
    /// Fails if the MP3 data is malformed.
    pub fn from_mp3(mp3: &[u8]) -> core::result::Result<Self, minimp3::Error> {
        let mut frames = minimp3::Decoder::new(std::io::Cursor::new(mp3));
        let mut samples = Vec::new();
        let mut sample_rate = 0u32;
        loop {
            match frames.next_frame() {
                Ok(frame) => {
                    sample_rate = frame.sample_rate as u32;
                    let channels = frame.channels.max(1);
                    samples.extend(frame.data.iter().step_by(channels));
                }
                Err(minimp3::Error::Eof) => break,
                Err(error) => return Err(error),
            }
        }

        Ok(Self::new(samples.into_iter(), sample_rate))
    }
}

#[cfg(feature = "image")]
impl<I: Iterator<Item = i16>> Decoder<I> {
    /// Decode the image as an `image` crate buffer, ready for its processing
    /// and saving APIs.
    ///
    /// This drops the mode and completeness metadata; use
    /// [`decode`](Self::decode) to keep it.
    ///
    /// ```no_run
    /// use sstv::Decoder;
    ///
    /// # let samples = std::vec::Vec::<i16>::new().into_iter();
    /// if let Some(image) = Decoder::new(samples, 48000).rgb_image() {
    ///     image.save("decoded.png")?;
    /// }
    /// # Ok::<(), image::ImageError>(())
    /// ```
    pub fn rgb_image(self) -> Option<image::RgbImage> {
        self.decode().map(|decoded| image::RgbImage::from(&decoded))
    }
}

#[cfg(feature = "image")]
impl From<&DecodedImage> for image::RgbImage {
    // expect: `pixels` always holds `width * height` entries.
    #[allow(clippy::expect_used)]
    fn from(decoded: &DecodedImage) -> Self {
        let mut bytes = Vec::with_capacity(decoded.pixels().len() * 3);
        for pixel in decoded.pixels() {
            bytes.extend_from_slice(&[pixel.red(), pixel.green(), pixel.blue()]);
        }
        Self::from_raw(decoded.width() as u32, decoded.height() as u32, bytes)
            .expect("pixel count always matches the dimensions")
    }
}
