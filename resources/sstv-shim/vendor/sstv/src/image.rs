#[derive(Debug, Clone, Copy, PartialEq, Eq)]
/// A single pixel in an image represented by red, green, blue components.
pub struct RgbPixel {
    red: u8,
    green: u8,
    blue: u8,
}
impl RgbPixel {
    /// Construct from the color components.
    #[must_use]
    pub const fn new(red: u8, green: u8, blue: u8) -> Self {
        Self { red, green, blue }
    }
    /// Return the red value.
    #[must_use]
    pub const fn red(self) -> u8 {
        self.red
    }
    /// Return the green value.
    #[must_use]
    pub const fn green(self) -> u8 {
        self.green
    }
    /// Return the blue value.
    #[must_use]
    pub const fn blue(self) -> u8 {
        self.blue
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
/// A single pixel in an image represented by separate brightness and color information.
pub struct YuvPixel {
    /// Brightness of the pixel.
    luma: u8,
    /// Red color component.
    chroma_red: u8,
    /// Blue color component.
    chroma_blue: u8,
}
impl YuvPixel {
    /// Construct from the individual components.
    #[must_use]
    pub const fn new(luma: u8, chroma_red: u8, chroma_blue: u8) -> Self {
        Self {
            luma,
            chroma_red,
            chroma_blue,
        }
    }
    /// Return the brightness of the pixel.
    #[must_use]
    pub const fn luma(self) -> u8 {
        self.luma
    }
    /// Return the red color component.
    #[must_use]
    pub const fn chroma_red(self) -> u8 {
        self.chroma_red
    }
    /// Return the blue color component.
    #[must_use]
    pub const fn chroma_blue(self) -> u8 {
        self.chroma_blue
    }
}

impl From<RgbPixel> for YuvPixel {
    fn from(rgb: RgbPixel) -> Self {
        let luma =
            (30 * u32::from(rgb.red()) + 59 * u32::from(rgb.green()) + 11 * u32::from(rgb.blue()))
                / 100;
        let luma = luma.min(255) as u8;

        let red_difference = i32::from(rgb.red) - i32::from(luma);
        let chroma_red = ((10 * red_difference + 7 * 255) / 14).clamp(0, 255) as u8;

        let blue_difference = i32::from(rgb.blue) - i32::from(luma);
        let chroma_blue = ((100 * blue_difference + 89 * 255) / 178).clamp(0, 255) as u8;

        Self {
            luma,
            chroma_red,
            chroma_blue,
        }
    }
}

impl From<YuvPixel> for RgbPixel {
    fn from(yuv: YuvPixel) -> Self {
        let luma = i32::from(yuv.luma());

        // Inverse of the clamped conversions in `From<RgbPixel>`.
        let red_difference = (14 * i32::from(yuv.chroma_red()) - 7 * 255) / 10;
        let blue_difference = (178 * i32::from(yuv.chroma_blue()) - 89 * 255) / 100;

        let red = (luma + red_difference).clamp(0, 255);
        let blue = (luma + blue_difference).clamp(0, 255);
        let green = ((100 * luma - 30 * red - 11 * blue) / 59).clamp(0, 255);

        Self::new(red as u8, green as u8, blue as u8)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rgb_to_yuv() {
        assert_eq!(
            YuvPixel::from(RgbPixel::new(67, 69, 42)),
            YuvPixel::new(65, 128, 114)
        );
    }

    #[test]
    fn rgb_survives_yuv_round_trip() {
        // The YUV conversion quantizes and clamps, so a small deviation is
        // expected rather than exact equality.
        for rgb in [
            RgbPixel::new(0, 0, 0),
            RgbPixel::new(255, 255, 255),
            RgbPixel::new(200, 100, 50),
            RgbPixel::new(30, 180, 220),
        ] {
            let restored = RgbPixel::from(YuvPixel::from(rgb));
            let deviation = |a: u8, b: u8| (i32::from(a) - i32::from(b)).abs();
            assert!(
                deviation(restored.red(), rgb.red()) <= 6
                    && deviation(restored.green(), rgb.green()) <= 6
                    && deviation(restored.blue(), rgb.blue()) <= 6,
                "{rgb:?} -> {restored:?}",
            );
        }
    }
}
