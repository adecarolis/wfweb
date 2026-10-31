use core::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
/// An error generated while encoding or decoding.
pub enum Error {
    /// Emitted if not enough pixels could be fetched from the image.
    ///
    /// ```rust
    /// use sstv::{modes::ROBOT_36, Encoder, Error, RgbPixel};
    ///
    /// let empty_image: Vec<RgbPixel> = vec![];
    ///
    /// assert!(matches!(
    ///     Encoder::new(ROBOT_36, empty_image.into_iter()),
    ///     Err(Error::EmptyImage)
    /// ));
    /// ```
    EmptyImage,
    /// Emitted by [`Encoder::new_in`](crate::Encoder::new_in) if the buffer
    /// cannot hold one of the mode's line groups.
    ///
    /// ```rust
    /// use sstv::{modes::ROBOT_36, Encoder, Error, RgbPixel};
    ///
    /// let image = [RgbPixel::new(0, 0, 0); 320 * 240];
    /// let mut buffer = [RgbPixel::new(0, 0, 0); 10];
    ///
    /// assert!(matches!(
    ///     Encoder::new_in(ROBOT_36, image.into_iter(), &mut buffer),
    ///     Err(Error::BufferTooSmall)
    /// ));
    /// ```
    BufferTooSmall,
    /// Emitted by [`VisCode::try_new`](crate::VisCode::try_new) if the value
    /// does not fit in 7 bits.
    BadVisCode,
    /// Emitted by `Mode::try_from` if the VIS code does not identify a known
    /// mode.
    UnknownMode,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyImage => write!(
                f,
                "The supplied image is empty. Was the pixel iterator already used?"
            ),
            Self::BufferTooSmall => write!(
                f,
                "The buffer cannot hold one of the mode's line groups. Size it \
                 to the mode's encoder_buffer_len()."
            ),
            Self::BadVisCode => write!(f, "VIS codes are 7 bit; the value does not fit."),
            Self::UnknownMode => write!(f, "The VIS code does not identify a known mode."),
        }
    }
}

impl core::error::Error for Error {}

/// Result with the custom sstv Error.
pub type Result<T> = core::result::Result<T, Error>;
