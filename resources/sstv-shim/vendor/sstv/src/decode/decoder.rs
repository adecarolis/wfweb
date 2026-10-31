//! The decoder: from samples to the image.

use super::Demodulator;
use super::assembler::{ImageSearch, ImageTones};
use super::walk::{RowWalk, decode_image};
use super::{DecodedImage, DecodedRow};
use crate::modes::{Mode, ROBOT_36};

/// Decodes an SSTV transmission from an audio sample stream.
///
/// `Decoder` is the inverse of [`Encoder`](crate::Encoder). A [`Demodulator`]
/// turns the 16-bit PCM samples into frequencies, which are assembled into
/// tones: the calibration header identifies the mode and where the image
/// starts, and every line sync pulse re-aligns the mode's timing sequence.
/// Walking that sequence recovers the image, one tone per pixel.
///
/// The decoder decodes the first image in the stream and reads only a little
/// past its end, so the remaining samples can be handed to another decoder
/// for the next image. [`decode`](Self::decode) returns the whole image;
/// [`rows`](Self::rows) hands out each row as soon as it is decoded, for
/// rendering a transmission while it is still coming in.
///
/// ```no_run
/// use sstv::Decoder;
///
/// # let samples = std::vec::Vec::<i16>::new().into_iter();
/// if let Some(image) = Decoder::new(samples, 48000).decode() {
///     let _ = (image.mode(), image.pixels());
/// }
/// ```
pub struct Decoder<I: Iterator<Item = i16>> {
    demodulator: Demodulator<I>,
    /// The mode given via [`with_mode`](Self::with_mode); `None` detects it
    /// from the header.
    mode: Option<Mode>,
    /// Begin decoding right away instead of searching for the image.
    without_header: bool,
}

impl<I: Iterator<Item = i16>> Decoder<I> {
    /// Decode a stream of PCM samples.
    ///
    /// `sample_rate` must be greater than zero. Construction never fails:
    /// finding the image is deferred to [`decode`](Self::decode).
    pub fn new(samples: I, sample_rate: u32) -> Self {
        Self::from_demodulator(Demodulator::new(samples, sample_rate))
    }

    /// Decode the frequencies of an existing demodulator.
    pub const fn from_demodulator(demodulator: Demodulator<I>) -> Self {
        Self {
            demodulator,
            mode: None,
            without_header: false,
        }
    }

    /// Decode in the given mode instead of detecting it from the header.
    ///
    /// The image starts at a header announcing this mode or, if none comes
    /// first, at three of the mode's line sync pulses spaced exactly one line
    /// apart, so a signal whose header is missing or unreadable still
    /// decodes. Headers announcing another mode are ignored.
    ///
    /// ```no_run
    /// use sstv::{modes::ROBOT_36, Decoder};
    ///
    /// # let samples = std::vec::Vec::<i16>::new().into_iter();
    /// let image = Decoder::new(samples, 48000).with_mode(ROBOT_36).decode();
    /// ```
    #[must_use]
    pub const fn with_mode(mut self, mode: Mode) -> Self {
        self.mode = Some(mode);
        self
    }

    /// Assume the samples begin directly at the image data and skip searching
    /// for its start.
    ///
    /// Decoding starts immediately at the first line's timing sequence. Use
    /// this when acquisition has already been performed upstream. Without a
    /// header there is no VIS code to detect a mode from, so unless
    /// [`with_mode`](Self::with_mode) names one, the image decodes as
    /// [`modes::ROBOT_36`](crate::modes::ROBOT_36).
    ///
    /// ```no_run
    /// use sstv::{modes::PD_120, Decoder};
    ///
    /// # let samples = std::vec::Vec::<i16>::new().into_iter();
    /// let image = Decoder::new(samples, 48000)
    ///     .with_mode(PD_120)
    ///     .without_header()
    ///     .decode();
    /// ```
    #[must_use]
    pub const fn without_header(mut self) -> Self {
        self.without_header = true;
        self
    }

    /// Decode the first image in the stream, or `None` if the samples run
    /// out before one is found. Reading stops shortly after the image's end.
    ///
    /// The image is incomplete if the samples end before it does. With
    /// [`without_header`](Self::without_header), decoding starts at the first
    /// sample, so there is always an image, however little of it the samples
    /// carry.
    pub fn decode(self) -> Option<DecodedImage> {
        let tones = self.find_image()?;
        Some(decode_image(tones.mode(), tones))
    }

    /// Decode the first image in the stream row by row, handing out each row
    /// as soon as it is decoded, or `None` if the samples run out before an
    /// image is found.
    ///
    /// The returned [`Rows`] know the image's [`mode`](Rows::mode), and with
    /// it the resolution, before the first row arrives. A row is decoded
    /// once the timing sequence that carries it has been received, about one
    /// line after the row itself. Fed from a live signal, the image can so
    /// be shown while it is still being transmitted.
    ///
    /// If the samples end before the image does, the rows stop early.
    ///
    /// ```no_run
    /// use sstv::Decoder;
    ///
    /// # let samples = std::vec::Vec::<i16>::new().into_iter();
    /// if let Some(rows) = Decoder::new(samples, 48000).rows() {
    ///     let (width, height) = rows.mode().resolution();
    ///     for row in rows {
    ///         // draw row.pixels() as line row.index() of a width x height canvas
    ///         let _ = (width, height, row.index(), row.pixels());
    ///     }
    /// }
    /// ```
    pub fn rows(self) -> Option<Rows<I>> {
        let tones = self.find_image()?;
        Some(Rows {
            walk: RowWalk::new(tones.mode(), tones),
            next_index: 0,
        })
    }

    /// Read until the image starts, and hand over its tones.
    fn find_image(self) -> Option<ImageTones<Demodulator<I>>> {
        let sample_rate = self.demodulator.sample_rate();
        if self.without_header {
            let mode = self.mode.unwrap_or(ROBOT_36);
            return Some(ImageTones::at_first_sample(
                self.demodulator,
                sample_rate,
                mode,
            ));
        }
        ImageSearch::new(self.demodulator, sample_rate, self.mode).find_image()
    }
}

/// The rows of an image, handed out by [`Decoder::rows`] top to bottom as
/// soon as each is decoded. Ends after the image's last row, or early if
/// the samples end before the image does.
pub struct Rows<I: Iterator<Item = i16>> {
    walk: RowWalk<ImageTones<Demodulator<I>>>,
    next_index: usize,
}

impl<I: Iterator<Item = i16>> Rows<I> {
    /// The SSTV mode the image is transmitted in, which gives its
    /// [`resolution`](Mode::resolution).
    #[must_use]
    pub const fn mode(&self) -> Mode {
        self.walk.mode()
    }
}

impl<I: Iterator<Item = i16>> Iterator for Rows<I> {
    type Item = DecodedRow;

    fn next(&mut self) -> Option<DecodedRow> {
        let pixels = self.walk.next()?;
        let row = DecodedRow::new(self.next_index, pixels);
        self.next_index += 1;
        Some(row)
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec::Vec;

    use core::cell::Cell;

    use super::super::testing::{gradient_image, header_length, mean_abs_error, transmit};
    use super::*;
    use crate::RgbPixel;
    use crate::modes::{MARTIN_1, PD_120, ROBOT_36, ROBOT_72, SCOTTIE_1};

    #[test]
    fn decodes_our_own_robot_36_transmission() {
        assert_round_trip(ROBOT_36, 48_000, 5.0);
    }

    #[test]
    fn decodes_our_own_robot_72_transmission() {
        assert_round_trip(ROBOT_72, 48_000, 5.0);
    }

    /// Martin opens every sequence with a short (4.862 ms) sync pulse.
    #[test]
    fn decodes_our_own_martin_1_transmission() {
        assert_round_trip(MARTIN_1, 48_000, 3.0);
    }

    /// Scottie sends a starting sync pulse and places each line's sync
    /// pulse in the middle of the sequence.
    #[test]
    fn decodes_our_own_scottie_1_transmission() {
        assert_round_trip(SCOTTIE_1, 48_000, 3.0);
    }

    #[test]
    fn decodes_our_own_pd_120_transmission() {
        assert_round_trip(PD_120, 48_000, 5.0);
    }

    /// At 8 kHz a pixel spans only a couple of samples, so the error is
    /// higher, but the image still decodes completely.
    #[test]
    fn decodes_a_transmission_sampled_at_8_khz() {
        assert_round_trip(ROBOT_36, 8_000, 15.0);
    }

    #[test]
    fn decodes_in_the_given_mode_without_a_header() {
        let image = gradient_image(SCOTTIE_1);
        let samples = transmit(SCOTTIE_1, &image, 48_000);
        let without_header = samples[header_length(SCOTTIE_1, 48_000)..].to_vec();

        let decoded = Decoder::new(without_header.into_iter(), 48_000)
            .with_mode(SCOTTIE_1)
            .decode()
            .unwrap();

        assert_decodes_completely(&decoded, SCOTTIE_1);
        let error = mean_abs_error(&image, decoded.pixels());
        assert!(error < 5.0, "mean abs error {error} too high");
    }

    /// Entering the stream at a line pair's second line must not swap the
    /// colour differences: decoding aligns to the next full pair, so the
    /// image shifts up by the two lines skipped.
    #[test]
    fn decoding_from_the_second_line_of_a_pair_keeps_the_colours() {
        let image = gradient_image(ROBOT_36);
        let samples = transmit(ROBOT_36, &image, 48_000);
        let first_line = 48_000 * 150 / 1000;
        let from_second_line = samples[header_length(ROBOT_36, 48_000) + first_line..].to_vec();

        let decoded = Decoder::new(from_second_line.into_iter(), 48_000)
            .with_mode(ROBOT_36)
            .decode()
            .unwrap();

        let (width, height) = (320, 240);
        let error = mean_abs_error(
            &image[width * 2..],
            &decoded.pixels()[..width * (height - 2)],
        );
        assert!(error < 5.0, "mean abs error {error} too high");
    }

    #[test]
    fn decodes_samples_that_begin_at_the_first_line() {
        let image = gradient_image(PD_120);
        let samples = transmit(PD_120, &image, 48_000);
        let first_line_on = samples[header_length(PD_120, 48_000)..].to_vec();

        let decoded = Decoder::new(first_line_on.into_iter(), 48_000)
            .with_mode(PD_120)
            .without_header()
            .decode()
            .unwrap();

        assert_decodes_completely(&decoded, PD_120);
        let error = mean_abs_error(&image, decoded.pixels());
        assert!(error < 5.0, "mean abs error {error} too high");
    }

    #[test]
    fn decoding_a_demodulator_matches_decoding_its_samples() {
        let samples = transmit(ROBOT_36, &gradient_image(ROBOT_36), 48_000);
        let demodulator = Demodulator::new(samples.clone().into_iter(), 48_000);

        let from_demodulator = Decoder::from_demodulator(demodulator).decode();

        assert_eq!(from_demodulator, decode(samples, 48_000));
    }

    #[test]
    fn decodes_only_the_first_image() {
        let mut samples = transmit(ROBOT_36, &gradient_image(ROBOT_36), 48_000);
        samples.extend(transmit(MARTIN_1, &gradient_image(MARTIN_1), 48_000));

        let decoded = decode(samples, 48_000).unwrap();

        assert_decodes_completely(&decoded, ROBOT_36);
    }

    /// A decoder stops reading shortly after its image, so the samples left
    /// over hold the next transmission for a fresh decoder.
    #[test]
    fn decodes_consecutive_images_with_one_decoder_each() {
        let image = gradient_image(ROBOT_36);
        let mut samples = transmit(ROBOT_36, &image, 48_000);
        samples.extend(transmit(ROBOT_36, &image, 48_000));
        let mut samples = samples.into_iter();

        let first = Decoder::new(samples.by_ref(), 48_000).decode().unwrap();
        let second = Decoder::new(samples.by_ref(), 48_000).decode().unwrap();
        let after_the_last = Decoder::new(samples.by_ref(), 48_000).decode();

        assert_decodes_completely(&first, ROBOT_36);
        assert_decodes_completely(&second, ROBOT_36);
        assert!(after_the_last.is_none());
    }

    #[test]
    fn transmission_cut_short_decodes_an_incomplete_image() {
        let image = gradient_image(ROBOT_36);
        let samples = transmit(ROBOT_36, &image, 48_000);
        let first_half = samples[..samples.len() / 2].to_vec();

        let decoded = decode(first_half, 48_000).unwrap();

        assert!(!decoded.complete());
    }

    #[test]
    fn rows_carry_the_image_decode_returns() {
        let samples = transmit(PD_120, &gradient_image(PD_120), 48_000);
        let image = decode(samples.clone(), 48_000).unwrap();

        let rows = Decoder::new(samples.into_iter(), 48_000).rows().unwrap();
        let mode = rows.mode();
        let rows: Vec<DecodedRow> = rows.collect();

        assert_eq!(mode, PD_120);
        let indices: Vec<usize> = rows.iter().map(DecodedRow::index).collect();
        assert_eq!(indices, (0..image.height()).collect::<Vec<_>>());
        let pixels: Vec<RgbPixel> = rows.iter().flat_map(|row| row.pixels().to_vec()).collect();
        assert_eq!(pixels, image.pixels());
    }

    /// Fed from a live signal, rows must be available long before the
    /// transmission ends: about one line after they were received.
    #[test]
    fn first_row_arrives_while_the_transmission_is_still_coming_in() {
        let samples = transmit(ROBOT_36, &gradient_image(ROBOT_36), 48_000);
        let samples_read = Cell::new(0);
        let live = samples
            .iter()
            .copied()
            .inspect(|_| samples_read.set(samples_read.get() + 1));

        let mut rows = Decoder::new(live, 48_000).rows().unwrap();
        rows.next().unwrap();

        let header_and_first_lines = header_length(ROBOT_36, 48_000) + 48_000 / 2;
        assert!(
            samples_read.get() < header_and_first_lines,
            "read {} of {} samples for the first row",
            samples_read.get(),
            samples.len()
        );
    }

    #[test]
    fn rows_stop_early_when_the_transmission_is_cut_short() {
        let samples = transmit(ROBOT_36, &gradient_image(ROBOT_36), 48_000);
        let first_half = samples[..samples.len() / 2].to_vec();

        let row_count = Decoder::new(first_half.into_iter(), 48_000)
            .rows()
            .unwrap()
            .count();

        assert!(row_count > 0 && row_count < 240, "{row_count} rows");
    }

    #[test]
    fn silence_decodes_no_image() {
        let silence = std::vec![0i16; 48_000];

        assert!(decode(silence, 48_000).is_none());
    }

    fn decode(samples: Vec<i16>, sample_rate: u32) -> Option<DecodedImage> {
        Decoder::new(samples.into_iter(), sample_rate).decode()
    }

    fn assert_round_trip(mode: Mode, sample_rate: u32, max_error: f64) {
        let image = gradient_image(mode);
        let decoded = decode(transmit(mode, &image, sample_rate), sample_rate).unwrap();

        assert_decodes_completely(&decoded, mode);
        let error = mean_abs_error(&image, decoded.pixels());
        assert!(error < max_error, "mean abs error {error} too high");
    }

    fn assert_decodes_completely(image: &DecodedImage, mode: Mode) {
        assert_eq!(image.mode(), mode);
        assert!(image.complete(), "image should decode completely");
    }
}
