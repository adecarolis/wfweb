//! Assembles the demodulated frequency track into the tones of the first
//! image it carries. [`ImageSearch`] splits the track into tones where the
//! frequency changes, to find where the image starts: at its calibration
//! header, or at its line sync pulses when the mode is known. From there,
//! [`ImageTones`] cuts the image by the mode's timing, anchored on the line
//! sync pulses, into one tone per control step and one per pixel, as the
//! encoder emits them.

mod clock;
mod cutter;
mod estimates;
mod header;
mod sync_lock;
mod tone_in_progress;

use alloc::collections::VecDeque;

use crate::modes::Mode;
use crate::ms;
use crate::units::{Duration, Frequency, Tone};
use clock::SampleClock;
use cutter::SequenceCutter;
use estimates::Estimates;
use header::HeaderWindow;
use sync_lock::SyncLock;
use tone_in_progress::{CompletedTone, ToneInProgress};

/// How long the estimates are kept while searching for a header. The header
/// is identified a few milliseconds after its stop bit and the first sync
/// pulse ended, and the first sequence begins at most that sync pulse
/// earlier.
const HEADER_LOOKBACK: Duration = ms!(200);
/// How many sequences of estimates are kept while locking on to sync pulses:
/// the three pulses span two lines, and the first sequence may begin up to
/// one sequence before the first of them.
const SYNC_LOCK_LOOKBACK_SEQUENCES: u32 = 3;

/// The frequency track as it is read: the frequencies still to come, the
/// estimates kept, and the tones split from them where the frequency
/// changes.
struct Track<I: Iterator<Item = Frequency>> {
    frequencies: I,
    clock: SampleClock,
    estimates: Estimates,
    tone_in_progress: ToneInProgress,
}

/// What reading the next frequency produced.
enum Reading {
    Frequency,
    /// The frequency completed a tone.
    Tone(CompletedTone),
    /// The frequencies have run out.
    End,
}

impl<I: Iterator<Item = Frequency>> Track<I> {
    fn new(frequencies: I, sample_rate: u32) -> Self {
        let clock = SampleClock::new(sample_rate);
        Self {
            frequencies,
            clock,
            estimates: Estimates::default(),
            tone_in_progress: ToneInProgress::new(clock),
        }
    }

    fn read(&mut self) -> Reading {
        let Some(frequency) = self.frequencies.next() else {
            return Reading::End;
        };
        self.estimates.push(frequency);
        self.tone_in_progress
            .push(frequency)
            .map_or(Reading::Frequency, Reading::Tone)
    }
}

/// Searches the frequency track for where the image starts.
pub(super) struct ImageSearch<I: Iterator<Item = Frequency>> {
    track: Track<I>,
    header: HeaderWindow,
    /// Locks on to the line sync pulses when the mode is known in advance.
    sync_lock: Option<SyncLock>,
    /// How many samples of estimates are kept while searching.
    lookback: u64,
}

impl<I: Iterator<Item = Frequency>> ImageSearch<I> {
    /// Search for an image starting at its header or, with `mode` given, at
    /// a header announcing it or else at three of its line sync pulses
    /// spaced one line apart.
    pub fn new(frequencies: I, sample_rate: u32, mode: Option<Mode>) -> Self {
        let track = Track::new(frequencies, sample_rate);
        let clock = track.clock;
        let sync_lock_lookback = mode.map_or(Duration::from_ns(0), |mode| {
            mode.sequence_duration() * SYNC_LOCK_LOOKBACK_SEQUENCES
        });
        Self {
            track,
            header: HeaderWindow::new(mode),
            sync_lock: mode.map(|mode| SyncLock::new(mode, clock)),
            lookback: clock.whole_samples(HEADER_LOOKBACK + sync_lock_lookback),
        }
    }

    /// Read until the image starts, and hand over to cutting it. `None` if
    /// the frequencies run out first.
    pub fn find_image(mut self) -> Option<ImageTones<I>> {
        loop {
            match self.track.read() {
                Reading::End => return None,
                Reading::Frequency => {}
                Reading::Tone(completed) => {
                    if let Some(cutter) = self.image_starting_at(completed) {
                        return Some(ImageTones::new(self.track, cutter));
                    }
                }
            }
            self.track.estimates.keep_last(self.lookback);
        }
    }

    /// The image, if this tone completes its header or the sync lock.
    fn image_starting_at(&mut self, completed: CompletedTone) -> Option<SequenceCutter> {
        let clock = self.track.clock;
        if let Some(mode) = self.header.push(completed.tone) {
            return Some(SequenceCutter::after_header(mode, clock, completed.end));
        }

        let lock = self.sync_lock.as_mut()?;
        let first_sync_end = lock.push(completed)?;
        let sequence_start = lock.first_sequence_start(first_sync_end, &self.track.estimates);
        Some(SequenceCutter::new(lock.mode(), clock, sequence_start))
    }
}

/// The tones of the image, cut by its mode's timing. Reading stops shortly
/// after the image's last tone.
pub(super) struct ImageTones<I: Iterator<Item = Frequency>> {
    track: Track<I>,
    cutter: SequenceCutter,
    /// Tones cut but not handed out yet, oldest first.
    queue: VecDeque<Tone>,
    done: bool,
}

impl<I: Iterator<Item = Frequency>> ImageTones<I> {
    /// The tones of an image whose first sequence begins at the first
    /// sample.
    pub fn at_first_sample(frequencies: I, sample_rate: u32, mode: Mode) -> Self {
        let track = Track::new(frequencies, sample_rate);
        let cutter = SequenceCutter::new(mode, track.clock, 0.0);
        Self::new(track, cutter)
    }

    fn new(mut track: Track<I>, cutter: SequenceCutter) -> Self {
        track
            .estimates
            .forget_before(cutter.sequence_start() as u64);
        Self {
            track,
            cutter,
            queue: VecDeque::new(),
            done: false,
        }
    }

    pub const fn mode(&self) -> Mode {
        self.cutter.mode()
    }

    /// Read the next frequency, and cut the current sequence once it is
    /// ready.
    fn read(&mut self) {
        match self.track.read() {
            Reading::End => {
                self.cut_remaining();
                return;
            }
            Reading::Tone(completed) => {
                self.cutter.sync_found(completed);
                self.cutter.relock(completed, &self.track.estimates);
            }
            Reading::Frequency => {}
        }
        if let Some(next_start) = self.cutter.next_start_if_ready(self.track.estimates.end()) {
            self.cut(next_start);
        }
    }

    /// Cut what the track still holds once the frequencies have run out:
    /// every sequence whose scans have all begun, with the missing tail
    /// standing in from the newest estimate.
    fn cut_remaining(&mut self) {
        while !self.done && self.cutter.last_scan_begun(self.track.estimates.end()) {
            self.cut(self.cutter.next_start());
        }
        self.done = true;
    }

    fn cut(&mut self, next_start: f64) {
        self.cutter
            .cut(next_start, &self.track.estimates, &mut self.queue);
        // With the timing lost, the previous sequence is kept too: the
        // re-lock scores the sync pulses it has seen against its control
        // tones.
        let keep_from = if self.cutter.is_lost() {
            next_start - self.cutter.sequence_length()
        } else {
            next_start
        };
        self.track.estimates.forget_before(keep_from.max(0.0) as u64);
        self.done = self.cutter.is_finished();
    }
}

impl<I: Iterator<Item = Frequency>> Iterator for ImageTones<I> {
    type Item = Tone;

    fn next(&mut self) -> Option<Tone> {
        loop {
            if let Some(tone) = self.queue.pop_front() {
                return Some(tone);
            }
            if self.done {
                return None;
            }
            self.read();
        }
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::vec::Vec;

    use super::super::testing::{gradient_image, transmit};
    use super::*;
    use crate::modes::{MARTIN_1, PD_120, ROBOT_36, SCOTTIE_1};
    use crate::{Demodulator, Encoder, RgbPixel};

    #[test]
    fn finds_the_image_of_our_own_transmission() {
        let samples = transmit(ROBOT_36, &gradient_image(ROBOT_36), 48_000);

        let image = find_image(samples, 48_000).unwrap();

        assert_eq!(image.mode(), ROBOT_36);
    }

    #[test]
    fn finds_the_image_when_the_front_of_the_header_is_trimmed() {
        let samples = transmit(PD_120, &gradient_image(PD_120), 48_000);
        // The VOX tones, the first leader and the break: 1110 ms.
        let vox_leader_and_break = 48_000 * 1110 / 1000;
        let trimmed = samples[vox_leader_and_break..].to_vec();

        let image = find_image(trimmed, 48_000).unwrap();

        assert_eq!(image.mode(), PD_120);
    }

    #[test]
    fn finds_no_image_in_silence() {
        let silence = std::vec![0i16; 48_000];

        assert!(find_image(silence, 48_000).is_none());
    }

    #[test]
    fn image_tones_mirror_the_encoders_tones() {
        let image = gradient_image(MARTIN_1);
        let samples = transmit(MARTIN_1, &image, 48_000);

        let assembled: Vec<Tone> = find_image(samples, 48_000).unwrap().collect();
        let encoded = encoded_image_tones(MARTIN_1, &image);

        assert_eq!(assembled.len(), encoded.len());
        let close_in_frequency = assembled
            .iter()
            .zip(&encoded)
            .filter(|(assembled, encoded)| {
                assembled.frequency.abs_diff(encoded.frequency).hz() <= 50
            })
            .count();
        assert!(
            close_in_frequency * 100 >= encoded.len() * 95,
            "only {close_in_frequency} of {} tones close in frequency",
            encoded.len()
        );
    }

    #[test]
    fn image_tones_last_as_long_as_the_image() {
        let image = gradient_image(SCOTTIE_1);
        let samples = transmit(SCOTTIE_1, &image, 48_000);

        let assembled: Duration = find_image(samples, 48_000)
            .unwrap()
            .map(|tone| tone.duration)
            .sum();
        let encoded: Duration = encoded_image_tones(SCOTTIE_1, &image)
            .iter()
            .map(|tone| tone.duration)
            .sum();

        assert!(
            assembled.ns().abs_diff(encoded.ns()) < ms!(1).ns(),
            "{} ns assembled vs {} ns encoded",
            assembled.ns(),
            encoded.ns()
        );
    }

    #[test]
    fn hands_out_only_the_first_images_tones() {
        let robot_36_image = gradient_image(ROBOT_36);
        let mut samples = transmit(ROBOT_36, &robot_36_image, 48_000);
        samples.extend(transmit(MARTIN_1, &gradient_image(MARTIN_1), 48_000));

        let tone_count = find_image(samples, 48_000).unwrap().count();

        assert_eq!(
            tone_count,
            encoded_image_tones(ROBOT_36, &robot_36_image).len()
        );
    }

    fn find_image(
        samples: Vec<i16>,
        sample_rate: u32,
    ) -> Option<ImageTones<Demodulator<std::vec::IntoIter<i16>>>> {
        let frequencies = Demodulator::new(samples.into_iter(), sample_rate);
        ImageSearch::new(frequencies, sample_rate, None).find_image()
    }

    /// The tones the encoder emits for the image, after the header.
    fn encoded_image_tones(mode: Mode, image: &[RgbPixel]) -> Vec<Tone> {
        Encoder::new(mode, image.iter().copied())
            .unwrap()
            .skip(mode.header_tones().count())
            .collect()
    }
}
