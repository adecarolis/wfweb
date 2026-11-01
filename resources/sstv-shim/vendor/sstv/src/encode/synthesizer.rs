include!(concat!(env!("OUT_DIR"), "/sine_table.rs"));

use crate::units::Tone;

/// Use a `Synthesizer` to encode an iterator of `Tone`s into 16-bit PCM samples.
///
/// You would usually use it to encode an image into single samples to emit them via an audio device.
/// ```rust
/// use sstv::{modes::ROBOT_36, Encoder, RgbPixel, Synthesizer};
///
/// let image = [RgbPixel::new(0, 0, 0); 320 * 240];
/// let encoder = Encoder::new(ROBOT_36, image.into_iter())?;
/// for sample in Synthesizer::new(encoder, 8000) {
///     // ...
/// }
/// # Ok::<(), sstv::Error>(())
/// ```
///
/// It can be used with a single `Tone` as well.
/// ```rust
/// use sstv::{Synthesizer, tone};
///
/// let tone = tone!(1500 Hz, 1000 us);
/// for sample in Synthesizer::new([tone].into_iter(), 8000) {
///     // ...
/// }
/// ```
pub struct Synthesizer<I: Iterator<Item = Tone>> {
    tones: I,
    sample_rate: u32,
    phase: u32,
    phase_increment: u32,
    samples_remaining: u32,
    sample_carry: u64,
}

impl<I: Iterator<Item = Tone>> Synthesizer<I> {
    /// Create a new `Synthesizer` from a tone iterator and a sample rate in Hz.
    ///
    /// `sample_rate` must be greater than zero.
    pub fn new(tones: I, sample_rate: u32) -> Self {
        Self {
            tones,
            sample_rate: sample_rate.max(1),
            phase: 0,
            phase_increment: 0,
            samples_remaining: 0,
            sample_carry: 0,
        }
    }

    fn load_next_tone(&mut self) -> Option<()> {
        let tone = self.tones.next()?;
        let numerator = tone.duration.ns() * u64::from(self.sample_rate) + self.sample_carry;
        self.samples_remaining = (numerator / 1_000_000_000) as u32;
        self.sample_carry = numerator % 1_000_000_000;
        self.phase_increment =
            ((u64::from(tone.frequency.hz()) * (1u64 << 32)) / u64::from(self.sample_rate)) as u32;
        Some(())
    }
}

#[cfg(feature = "wav")]
impl<I: Iterator<Item = Tone>> Synthesizer<I> {
    /// The remaining samples as a complete mono 16-bit PCM WAV, ready to be
    /// written wherever the WAV should go.
    ///
    /// This buffers the entire transmission in memory (a few megabytes at
    /// typical sample rates); on memory-constrained systems, emit the samples
    /// one by one instead.
    // expect: writing into an in-memory cursor cannot fail.
    #[allow(clippy::expect_used, clippy::missing_panics_doc)]
    #[must_use]
    pub fn to_wav(mut self) -> alloc::vec::Vec<u8> {
        let spec = hound::WavSpec {
            channels: 1,
            sample_rate: self.sample_rate,
            bits_per_sample: 16,
            sample_format: hound::SampleFormat::Int,
        };
        let mut cursor = std::io::Cursor::new(alloc::vec::Vec::new());
        // Writing into an in-memory cursor cannot fail.
        let mut writer = hound::WavWriter::new(&mut cursor, spec).expect("write to memory");
        for sample in &mut self {
            writer.write_sample(sample).expect("write to memory");
        }
        writer.finalize().expect("write to memory");
        cursor.into_inner()
    }
}

#[cfg(feature = "mp3")]
impl<I: Iterator<Item = Tone>> Synthesizer<I> {
    /// The remaining samples as a complete mono 128 kbps MP3, ready to be
    /// written wherever the MP3 should go.
    ///
    /// This buffers the entire transmission in memory.
    ///
    /// # Errors
    ///
    /// Fails if LAME rejects the sample rate.
    // expect: the buffer is sized via `max_required_buffer_size`.
    #[allow(clippy::expect_used, clippy::missing_panics_doc)]
    pub fn to_mp3(mut self) -> Result<alloc::vec::Vec<u8>, mp3lame_encoder::BuildError> {
        use mp3lame_encoder::{Builder, FlushNoGap, MonoPcm};

        let sample_rate = self.sample_rate;
        let mut samples: alloc::vec::Vec<i16> = self.by_ref().collect();
        // The encoder and any decoder each delay the stream by up to a frame;
        // pad the tail so the transmission's end survives the codec chain.
        samples.resize(samples.len() + 2304, 0);

        let mut builder = Builder::new().ok_or(mp3lame_encoder::BuildError::NoMem)?;
        builder.set_num_channels(1)?;
        builder.set_sample_rate(sample_rate)?;
        builder.set_brate(mp3lame_encoder::Bitrate::Kbps128)?;
        builder.set_quality(mp3lame_encoder::Quality::Best)?;
        let mut encoder = builder.build()?;

        let mut mp3 = alloc::vec::Vec::with_capacity(mp3lame_encoder::max_required_buffer_size(
            samples.len(),
        ));
        let written = encoder
            .encode(MonoPcm(&samples), mp3.spare_capacity_mut())
            .expect("the buffer is sized to fit the whole encoding");
        // The encoder wrote `written` bytes into the spare capacity.
        unsafe { mp3.set_len(mp3.len() + written) };

        let flushed = encoder
            .flush::<FlushNoGap>(mp3.spare_capacity_mut())
            .expect("the buffer is sized to fit the whole encoding");
        unsafe { mp3.set_len(mp3.len() + flushed) };
        Ok(mp3)
    }
}

impl<I: Iterator<Item = Tone>> Iterator for Synthesizer<I> {
    type Item = i16;

    fn next(&mut self) -> Option<i16> {
        while self.samples_remaining == 0 {
            self.load_next_tone()?;
        }
        let sample = SINE_TABLE[(self.phase.wrapping_add(1 << 23) >> 24) as usize];
        self.phase = self.phase.wrapping_add(self.phase_increment);
        self.samples_remaining -= 1;
        Some(sample)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tone;
    use core::f64::consts::PI;

    fn float_reference(tones: &[Tone], sample_rate: u32) -> Vec<i16> {
        let mut samples = Vec::new();
        let mut phase = 0.0f64;
        let mut carry: u64 = 0;

        for tone in tones {
            let numerator = tone.duration.ns() * u64::from(sample_rate) + carry;
            let count = (numerator / 1_000_000_000) as usize;
            carry = numerator % 1_000_000_000;

            let increment = 2.0 * PI * f64::from(tone.frequency.hz()) / f64::from(sample_rate);
            for _ in 0..count {
                samples.push((phase.sin() * f64::from(i16::MAX)).round() as i16);
                phase += increment;
            }
        }
        samples
    }

    #[cfg(feature = "wav")]
    #[test]
    fn to_wav_wraps_the_samples() {
        let tones = [tone!(1500 Hz, 50 ms)];
        let samples: Vec<i16> = Synthesizer::new(tones.into_iter(), 8_000).collect();

        let wav = Synthesizer::new(tones.into_iter(), 8_000).to_wav();

        let reader = hound::WavReader::new(std::io::Cursor::new(wav)).expect("parse wav");
        assert_eq!(reader.spec().channels, 1);
        assert_eq!(reader.spec().sample_rate, 8_000);
        assert_eq!(reader.spec().bits_per_sample, 16);
        let unwrapped: Vec<i16> = reader
            .into_samples()
            .map(|sample| sample.expect("read sample"))
            .collect();
        assert_eq!(unwrapped, samples);
    }

    #[test]
    fn synthesizer_matches_pure_sine_wave() {
        let sample_rate: u32 = 48_000;
        let tones = [
            tone!(1200 Hz, 10 ms),
            tone!(1500 Hz, 10 ms),
            tone!(2300 Hz, 10 ms),
        ];

        let samples: Vec<i16> = Synthesizer::new(tones.into_iter(), sample_rate).collect();
        let reference_samples = float_reference(&tones, sample_rate);

        let difference: Vec<u16> = samples
            .iter()
            .zip(&reference_samples)
            .map(|(sample, reference_sample)| sample.abs_diff(*reference_sample))
            .collect();
        let max_difference = *difference.iter().max().unwrap();
        let max_reference_sample = reference_samples.iter().max().unwrap().unsigned_abs();

        // should not deviate by more than 2%
        assert!(max_difference <= max_reference_sample / 50);
    }
}
