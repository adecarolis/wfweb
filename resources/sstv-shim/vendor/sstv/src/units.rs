use core::iter::Sum;
use core::ops::{Add, Div, Mul, Sub};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
/// A frequency value.
pub struct Frequency {
    hertz: u32,
}

impl Frequency {
    /// Construct a `Frequency` from its value in Hertz.
    #[must_use]
    pub const fn from_hz(hertz: u32) -> Self {
        Self { hertz }
    }

    /// Get the frequency value in Hertz.
    #[must_use]
    pub const fn hz(self) -> u32 {
        self.hertz
    }

    /// Return the absolute difference between this and another Frequency.
    #[must_use]
    pub const fn abs_diff(self, other: Self) -> Self {
        Self::from_hz(self.hz().abs_diff(other.hz()))
    }
}

impl Add for Frequency {
    type Output = Self;

    fn add(self, rhs: Self) -> Self {
        Self::from_hz(self.hz() + rhs.hz())
    }
}

impl Sub for Frequency {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self {
        Self::from_hz(self.hz() - rhs.hz())
    }
}

impl Mul<u32> for Frequency {
    type Output = Self;

    fn mul(self, rhs: u32) -> Self {
        Self::from_hz(self.hz() * rhs)
    }
}

impl Div<u32> for Frequency {
    type Output = Self;

    fn div(self, rhs: u32) -> Self {
        Self::from_hz(self.hz() / rhs)
    }
}

#[macro_export]
/// Construct a `Frequency` from its Hertz value.
///
/// ```rust
/// use sstv::{Frequency, Hz};
///
/// assert_eq!(
///     Hz!(1500),
///     Frequency::from_hz(1500),
/// );
/// ```
macro_rules! Hz {
    ($value:expr) => {
        $crate::Frequency::from_hz($value)
    };
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
/// The difference between two points in time.
pub struct Duration {
    nanoseconds: u64,
}

impl Duration {
    /// Construct a `Duration` from its value in nanoseconds.
    #[must_use]
    pub const fn from_ns(nanoseconds: u64) -> Self {
        Self { nanoseconds }
    }

    /// Construct a `Duration` from its value in microseconds.
    #[must_use]
    pub const fn from_us(microseconds: u64) -> Self {
        Self {
            nanoseconds: microseconds * 1000,
        }
    }

    /// Construct a `Duration` from its value in milliseconds.
    #[must_use]
    pub const fn from_ms(milliseconds: u64) -> Self {
        Self {
            nanoseconds: milliseconds * 1_000_000,
        }
    }

    /// Get the duration value in nanoseconds.
    #[must_use]
    pub const fn ns(self) -> u64 {
        self.nanoseconds
    }

    /// Get the duration value in microseconds.
    #[must_use]
    pub const fn us(self) -> u32 {
        (self.nanoseconds / 1000) as u32
    }
}

impl Add for Duration {
    type Output = Self;

    fn add(self, rhs: Self) -> Self {
        Self::from_ns(self.ns() + rhs.ns())
    }
}

impl Sum for Duration {
    fn sum<I: Iterator<Item = Self>>(durations: I) -> Self {
        durations.fold(Self::from_ns(0), Add::add)
    }
}

impl Mul<u32> for Duration {
    type Output = Self;

    fn mul(self, rhs: u32) -> Self {
        Self::from_ns(self.ns() * u64::from(rhs))
    }
}

impl Div<u32> for Duration {
    type Output = Self;

    fn div(self, rhs: u32) -> Self {
        Self::from_ns(self.ns() / u64::from(rhs))
    }
}

#[macro_export]
/// Construct a `Duration` from its nanosecond value.
///
/// ```rust
/// use sstv::{Duration, ns};
///
/// assert_eq!(
///     ns!(10),
///     Duration::from_ns(10),
/// );
/// ```
macro_rules! ns {
    ($value:expr) => {
        $crate::Duration::from_ns($value)
    };
}

#[macro_export]
/// Construct a `Duration` from its microseconds value.
///
/// ```rust
/// use sstv::{Duration, us};
///
/// assert_eq!(
///     us!(10),
///     Duration::from_us(10),
/// );
/// ```
macro_rules! us {
    ($value:expr) => {
        $crate::Duration::from_us($value)
    };
}

#[macro_export]
/// Construct a `Duration` from its milliseconds value.
///
/// ```rust
/// use sstv::{Duration, ms};
///
/// assert_eq!(
///     ms!(10),
///     Duration::from_ms(10),
/// );
/// ```
macro_rules! ms {
    ($value:expr) => {
        $crate::Duration::from_ms($value)
    };
}

/// A single frequency emitted for a certain duration.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Tone {
    /// The frequency
    pub frequency: Frequency,
    /// The duration
    pub duration: Duration,
}

impl Tone {
    /// Create a new Tone
    #[must_use]
    pub const fn new(frequency: Frequency, duration: Duration) -> Self {
        Self {
            frequency,
            duration,
        }
    }

    /// Whether the tone's frequency and duration both lie within
    /// `tolerance` of `nominal`'s.
    pub(crate) const fn is_near(self, nominal: Self, tolerance: Self) -> bool {
        self.frequency.hz().abs_diff(nominal.frequency.hz()) <= tolerance.frequency.hz()
            && self.duration.ns().abs_diff(nominal.duration.ns()) <= tolerance.duration.ns()
    }
}

#[macro_export]
/// Construct a [`Tone`](crate::Tone) from a frequency in Hertz and a duration
/// in any supported unit (`ns`, `us` or `ms`).
///
/// ```rust
/// use sstv::{tone, Tone, Hz, ms};
///
/// assert_eq!(
///     tone!(1500 Hz, 30 ms),
///     Tone::new(Hz!(1500), ms!(30)),
/// );
/// ```
macro_rules! tone {
    ($frequency:literal Hz, $duration:literal ns) => {
        $crate::Tone::new($crate::Hz!($frequency), $crate::ns!($duration))
    };
    ($frequency:literal Hz, $duration:literal us) => {
        $crate::Tone::new($crate::Hz!($frequency), $crate::us!($duration))
    };
    ($frequency:literal Hz, $duration:literal ms) => {
        $crate::Tone::new($crate::Hz!($frequency), $crate::ms!($duration))
    };
}
