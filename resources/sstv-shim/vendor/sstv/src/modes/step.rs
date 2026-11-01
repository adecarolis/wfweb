//! Building blocks for describing a mode's transmission as the Dayton paper
//! does: each mode is a repeating timing sequence of fixed tones (sync
//! pulses, porches, separator pulses) and channel scans.

use crate::units::{Duration, Tone};

/// The image values carried by a scan step.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Channel {
    Red,
    Green,
    Blue,
    /// Luminance. In sequences carrying two lines (Robot 36 and PD modes),
    /// the first line's.
    Y,
    /// The second line's luminance (Robot 36 and PD modes).
    YSecond,
    /// The red colour difference.
    RY,
    /// The blue colour difference.
    BY,
}

/// One entry of a mode's timing sequence.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Step {
    /// A fixed control tone: sync pulse, sync porch, separator pulse or porch.
    Control(Tone),
    /// A channel scan: one line of pixels spread evenly over the duration.
    Scan(Channel, Duration),
}

impl Step {
    pub(crate) const fn duration(&self) -> Duration {
        match self {
            Self::Control(tone) => tone.duration,
            Self::Scan(_, duration) => *duration,
        }
    }

    pub(crate) const fn is_scan(&self) -> bool {
        match self {
            Self::Control(_) => false,
            Self::Scan(..) => true,
        }
    }
}

/// How the scans of one timing sequence combine into image pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ColorMode {
    /// Red, Green and Blue scans of a single line (Martin, Scottie, Wrasse,
    /// Pasokon).
    Rgb,
    /// Y, R-Y and B-Y scans of a single line (Robot 72).
    Yuv,
    /// Y scans of two consecutive lines sharing pair-averaged R-Y and B-Y
    /// scans (Robot 36 and PD modes).
    YuvSharedPair,
}
