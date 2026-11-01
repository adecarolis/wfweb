// Build-time code generation: panicking is the intended failure mode, and the
// casts are in range by construction (0..256, sin scaled into i16).
#![allow(
    clippy::unwrap_used,
    clippy::as_conversions,
    clippy::cast_precision_loss,
    clippy::cast_possible_truncation,
    clippy::cast_lossless
)]

use std::f64::consts::PI;
use std::io::Write;

fn main() {
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let dest = std::path::Path::new(&out_dir).join("sine_table.rs");
    let mut f = std::fs::File::create(dest).unwrap();

    write!(f, "const SINE_TABLE: [i16; 256] = [").unwrap();
    for i in 0usize..256 {
        if i % 8 == 0 {
            write!(f, "\n   ").unwrap();
        }
        let value = ((2.0 * PI * i as f64 / 256.0).sin() * i16::MAX as f64).round() as i16;
        write!(f, " {value:6},").unwrap();
    }
    writeln!(f, "\n];").unwrap();
}
