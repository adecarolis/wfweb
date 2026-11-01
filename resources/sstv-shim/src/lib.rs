//! wfweb SSTV codec shim: a plain C ABI around the `sstv` crate, compiled to
//! `wasm32-unknown-unknown` and loaded by `resources/web-shared/sstv-worker.js`
//! through `resources/web-standalone/wasm/sstv-codec.mjs`.
//!
//! The crate's decoder *pulls* samples from an iterator. In WebAssembly a pull
//! cannot wait for the sound card, so the module is post-processed with
//! Binaryen Asyncify (`tools/build-sstv-wasm.sh`): `RingIter` pops from a ring
//! buffer and, when it runs dry, calls the imported `sstv_await_samples()`,
//! which the JS glue turns into a Promise that resolves once more audio has
//! been pushed with `sstv_rx_push`. Everything on the call path from
//! `sstv_rx_decode` to that import is instrumented; the encoder is not.
//!
//! No wasm-bindgen: every export takes integers and pointers into linear
//! memory, and the JS side copies typed arrays in and out.

use std::alloc::{alloc, dealloc, Layout};
use std::cell::RefCell;
use std::collections::VecDeque;
use std::fmt::Write as _;

use sstv::modes::ALL;
use sstv::{Decoder, Encoder, Mode, RgbPixel, Synthesizer};

#[cfg(target_arch = "wasm32")]
#[link(wasm_import_module = "env")]
extern "C" {
    /// Block until more samples are available. Returns 0 to retry the ring,
    /// nonzero to abort the decode (the iterator then ends).
    fn sstv_await_samples() -> i32;
    /// The image's mode has been identified: mode id (index into `ALL`),
    /// width and height in pixels.
    fn sstv_on_mode(id: i32, width: u32, height: u32);
    /// One decoded row: its index and `width * 3` RGB bytes.
    fn sstv_on_row(index: u32, rgb: *const u8, len: u32);
}

// Host builds (`cargo check`, IDEs) have nothing to import; stub the callbacks.
#[cfg(not(target_arch = "wasm32"))]
unsafe fn sstv_await_samples() -> i32 {
    1
}
#[cfg(not(target_arch = "wasm32"))]
unsafe fn sstv_on_mode(_id: i32, _width: u32, _height: u32) {}
#[cfg(not(target_arch = "wasm32"))]
unsafe fn sstv_on_row(_index: u32, _rgb: *const u8, _len: u32) {}

thread_local! {
    /// RX samples pushed by JS and not yet consumed by the decoder.
    static RING: RefCell<VecDeque<i16>> = RefCell::new(VecDeque::new());
    /// The transmission being synthesized, drained by `sstv_tx_fill`.
    static TX: RefCell<Option<Box<dyn Iterator<Item = i16>>>> = RefCell::new(None);
}

/// Pulls samples from `RING`, waiting on the host when it is empty.
///
/// The ring is borrowed only for the pop: `sstv_rx_push` runs on the same
/// thread while this iterator is suspended inside `sstv_await_samples`, so no
/// borrow may be held across that call.
struct RingIter;

impl Iterator for RingIter {
    type Item = i16;

    fn next(&mut self) -> Option<i16> {
        loop {
            if let Some(sample) = RING.with(|r| r.borrow_mut().pop_front()) {
                return Some(sample);
            }
            // SAFETY: a plain host import with no pointer arguments.
            if unsafe { sstv_await_samples() } != 0 {
                return None;
            }
        }
    }
}

fn mode_by_id(id: i32) -> Option<Mode> {
    usize::try_from(id).ok().and_then(|i| ALL.get(i).copied())
}

fn mode_id(mode: Mode) -> i32 {
    ALL.iter()
        .position(|m| *m == mode)
        .map_or(-1, |i| i as i32)
}

// ---------------------------------------------------------------- memory ---

/// Allocate `n` bytes for the host to write into (8-byte aligned).
#[no_mangle]
pub extern "C" fn sstv_alloc(n: usize) -> *mut u8 {
    if n == 0 {
        return std::ptr::null_mut();
    }
    match Layout::from_size_align(n, 8) {
        // SAFETY: a valid, nonzero layout.
        Ok(layout) => unsafe { alloc(layout) },
        Err(_) => std::ptr::null_mut(),
    }
}

/// Free a buffer from `sstv_alloc` of the same size.
#[no_mangle]
pub extern "C" fn sstv_free(ptr: *mut u8, n: usize) {
    if ptr.is_null() || n == 0 {
        return;
    }
    if let Ok(layout) = Layout::from_size_align(n, 8) {
        // SAFETY: `ptr` came from `sstv_alloc(n)` with this layout.
        unsafe { dealloc(ptr, layout) };
    }
}

// ----------------------------------------------------------------- modes ---

/// Number of modes; ids are `0..count`, in the Dayton paper's order.
#[no_mangle]
pub extern "C" fn sstv_mode_count() -> i32 {
    ALL.len() as i32
}

/// Image width in pixels, 0 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_width(id: i32) -> u32 {
    mode_by_id(id).map_or(0, |m| m.resolution().0)
}

/// Image height in pixels, 0 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_height(id: i32) -> u32 {
    mode_by_id(id).map_or(0, |m| m.resolution().1)
}

/// Write the mode's name (UTF-8, as the crate prints it) into `out`, at most
/// `cap` bytes. Returns the length written, 0 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_name(id: i32, out: *mut u8, cap: usize) -> usize {
    let Some(mode) = mode_by_id(id) else { return 0 };
    let mut name = String::new();
    if write!(name, "{mode:?}").is_err() {
        return 0;
    }
    let bytes = name.as_bytes();
    let n = bytes.len().min(cap);
    if n > 0 && !out.is_null() {
        // SAFETY: the host handed us `cap` writable bytes at `out`.
        unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n) };
    }
    n
}

/// Length of the header (VOX tones, leader, VIS) in samples at `rate`:
/// the image lines start after it. 0 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_header_samples(id: i32, rate: u32) -> u32 {
    let Some(mode) = mode_by_id(id) else { return 0 };
    let ns: u64 = mode.header_tones().map(|t| t.duration.ns()).sum();
    (ns * u64::from(rate) / 1_000_000_000) as u32
}

/// The mode's VIS code (0..127), -1 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_vis(id: i32) -> i32 {
    mode_by_id(id).map_or(-1, |mode| i32::from(u8::from(mode.vis_code())))
}

/// The mode's timing layout, for a receiver that locates the image itself:
/// writes up to `cap` f64 values at `out` -- sequence duration (s), sync
/// spacing (s, one transmitted line), the first sync pulse's offset in the
/// sequence (s) and duration (s), 1.0 when the mode sends one extra sync
/// pulse between the header and the first line (Scottie), the number of
/// control tones, then per control tone its offset (s), duration (s) and
/// frequency (Hz). Returns the number of values the layout has (more than
/// `cap` means a longer buffer is needed); 0 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_layout(id: i32, out: *mut f64, cap: usize) -> usize {
    let Some(mode) = mode_by_id(id) else { return 0 };
    let secs = |d: sstv::Duration| d.ns() as f64 / 1e9;
    let (sync_offset, sync_duration) = mode.sync_pulse();
    let mut values = vec![
        secs(mode.sequence_duration()),
        secs(mode.sync_spacing()),
        secs(sync_offset),
        secs(sync_duration),
        if mode.has_starting_sync_pulse() { 1.0 } else { 0.0 },
        0.0,
    ];
    let mut count = 0usize;
    for (offset, tone) in mode.control_tones() {
        values.extend([secs(offset), secs(tone.duration), f64::from(tone.frequency.hz())]);
        count += 1;
    }
    values[5] = count as f64;
    if !out.is_null() && cap >= values.len() {
        // SAFETY: the host provided `cap` f64 slots at `out` (from `sstv_alloc`).
        let slice = unsafe { std::slice::from_raw_parts_mut(out, values.len()) };
        slice.copy_from_slice(&values);
    }
    values.len()
}

/// Length of a whole transmission in samples at `rate` (header included),
/// for progress bars. 0 for an unknown id.
#[no_mangle]
pub extern "C" fn sstv_mode_samples(id: i32, rate: u32) -> u32 {
    let Some(mode) = mode_by_id(id) else { return 0 };
    let (w, h) = mode.resolution();
    let black = std::iter::repeat(RgbPixel::new(0, 0, 0)).take((w * h) as usize);
    match Encoder::new(mode, black) {
        Ok(encoder) => Synthesizer::new(encoder, rate).count() as u32,
        Err(_) => 0,
    }
}

// -------------------------------------------------------------------- RX ---

/// Append `n` samples to the ring.
#[no_mangle]
pub extern "C" fn sstv_rx_push(ptr: *const i16, n: usize) {
    if ptr.is_null() || n == 0 {
        return;
    }
    // SAFETY: the host wrote `n` samples at `ptr` (from `sstv_alloc`).
    let samples = unsafe { std::slice::from_raw_parts(ptr, n) };
    RING.with(|r| r.borrow_mut().extend(samples.iter().copied()));
}

/// Drop every sample not yet consumed.
#[no_mangle]
pub extern "C" fn sstv_rx_reset() {
    RING.with(|r| r.borrow_mut().clear());
}

/// Samples waiting in the ring (diagnostics).
#[no_mangle]
pub extern "C" fn sstv_rx_pending() -> usize {
    RING.with(|r| r.borrow().len())
}

/// Decode the next image from the ring at `rate` Hz, pinned to `pin`
/// when it names a mode, otherwise detected from the VIS header; with
/// `headerless` nonzero the image data starts at the next sample. Calls
/// `sstv_on_mode` once and `sstv_on_row` per row. Returns 1 when the image
/// completed, 0 when the samples ended first (abort), -1 when no image was
/// found before the samples ended.
///
/// Asyncified: from JS this call returns a Promise.
#[no_mangle]
pub extern "C" fn sstv_rx_decode(rate: u32, pin: i32, headerless: i32) -> i32 {
    let decoder = Decoder::new(RingIter, rate.max(1));
    let decoder = match mode_by_id(pin) {
        Some(mode) => decoder.with_mode(mode),
        None => decoder,
    };
    // A start by hand: the image data is taken to begin at the next sample
    // (no header search, the panel's START button), in `pin` or Robot 36.
    let decoder = if headerless != 0 { decoder.without_header() } else { decoder };
    let Some(rows) = decoder.rows() else { return -1 };

    let mode = rows.mode();
    let (width, height) = mode.resolution();
    // SAFETY: plain integers.
    unsafe { sstv_on_mode(mode_id(mode), width, height) };

    let mut rgb = Vec::with_capacity(width as usize * 3);
    let mut count = 0u32;
    for row in rows {
        rgb.clear();
        for p in row.pixels() {
            rgb.extend_from_slice(&[p.red(), p.green(), p.blue()]);
        }
        // SAFETY: `rgb` outlives the call; the host copies the bytes out.
        unsafe { sstv_on_row(row.index() as u32, rgb.as_ptr(), rgb.len() as u32) };
        count += 1;
    }
    i32::from(count == height)
}

// -------------------------------------------------------------------- TX ---

/// Start synthesizing an image: `rgb` holds `width * height * 3` bytes at
/// the mode's resolution, rows top to bottom. Returns the transmission's
/// length in samples at `rate` (0 on error); drain it with `sstv_tx_fill`.
#[no_mangle]
pub extern "C" fn sstv_tx_begin(mode_id: i32, rgb: *const u8, rate: u32) -> u32 {
    let Some(mode) = mode_by_id(mode_id) else { return 0 };
    if rgb.is_null() {
        return 0;
    }
    let (w, h) = mode.resolution();
    let n = (w * h) as usize;
    // SAFETY: the host wrote `n * 3` bytes at `rgb` (from `sstv_alloc`).
    let bytes = unsafe { std::slice::from_raw_parts(rgb, n * 3) };
    let pixels: Vec<RgbPixel> = bytes
        .chunks_exact(3)
        .map(|c| RgbPixel::new(c[0], c[1], c[2]))
        .collect();

    let rate = rate.max(1);
    let total = match Encoder::new(mode, pixels.clone().into_iter()) {
        Ok(encoder) => Synthesizer::new(encoder, rate).count() as u32,
        Err(_) => return 0,
    };
    let Ok(encoder) = Encoder::new(mode, pixels.into_iter()) else { return 0 };
    TX.with(|t| *t.borrow_mut() = Some(Box::new(Synthesizer::new(encoder, rate))));
    total
}

/// Write up to `n` samples of the transmission into `out`; returns how many
/// were written, 0 when it is finished.
#[no_mangle]
pub extern "C" fn sstv_tx_fill(out: *mut i16, n: usize) -> usize {
    if out.is_null() || n == 0 {
        return 0;
    }
    // SAFETY: the host handed us `n` writable samples at `out`.
    let out = unsafe { std::slice::from_raw_parts_mut(out, n) };
    TX.with(|t| {
        let mut slot = t.borrow_mut();
        let Some(synth) = slot.as_mut() else { return 0 };
        let mut written = 0;
        for cell in out.iter_mut() {
            match synth.next() {
                Some(s) => {
                    *cell = s;
                    written += 1;
                }
                None => break,
            }
        }
        if written < n {
            *slot = None;
        }
        written
    })
}

/// Discard a transmission in progress.
#[no_mangle]
pub extern "C" fn sstv_tx_end() {
    TX.with(|t| *t.borrow_mut() = None);
}
