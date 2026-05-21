// C-callable entry points exported from js8.wasm. The JS Worker
// harness in resources/web-shared/js8.js does ccall/cwrap against
// these and nothing else — JS never touches the C++ side directly.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Encoder ----------------------------------------- */

/* Encode a 12-character JS8 message of the given frame type into
 * 79 8-FSK tones. Caller provides `tones_out` as int[79].
 *
 * submode    — Varicode::SubmodeType ID (Normal=0, Fast=1, Turbo=2,
 *              Slow=4, Ultra=8). Selects the Costas sync array
 *              (ORIGINAL for Normal, MODIFIED for the others).
 * frame_type — Varicode::TransmissionType (0..7)
 * msg        — null-terminated 12-char string from the JS8 alphabet
 *              "0-9A-Za-z-+" (no whitespace; spaces map nowhere)
 * tones_out  — int[79], filled with 0..7 on success
 *
 * Returns 0 on success, -1 on bad input (invalid char in msg). */
int js8_encode(int submode, int frame_type, const char* msg, int* tones_out);

/* ---------------- Decoder ----------------------------------------- */

/* Opaque decoder state. One per submode. */
typedef struct js8_decoder js8_decoder;

/* Create a decoder for the given submode.
 *   submode — Varicode::SubmodeType bit (Normal=0, Fast=1, Turbo=2,
 *                                         Slow=4, Ultra=8)
 * Returns NULL on allocation failure. */
js8_decoder* js8_decoder_new(int submode);

/* Free a decoder previously returned by js8_decoder_new.
 * Safe to pass NULL. */
void js8_decoder_free(js8_decoder* dec);

/* Append `n_samples` floats of 12 kHz mono audio to the decoder's
 * input ring buffer.
 *
 * Returns the number of samples actually consumed (may be < n_samples
 * if the ring buffer is full — caller should slow down). */
int js8_decoder_push(js8_decoder* dec, const float* samples, int n_samples);

/* Run a decode pass on the currently buffered samples. Decode
 * results are queued internally; drain with js8_decoder_pop().
 *
 * Call this at slot boundaries — every 6/10/15.6/30 seconds depending
 * on submode. Cheap to call when there's nothing to decode (returns
 * quickly without producing events).
 *
 * Returns the number of decoded messages added to the queue. */
int js8_decoder_run(js8_decoder* dec);

/* Decode a specific slice of buffered audio for one or more submodes.
 * Unlike js8_decoder_run, this gives the caller full control over
 * which modes to scan and where in the staged audio to scan them.
 *
 *   nsubmodes   — bitmask: Normal=1, Fast=2, Turbo(JS8 40)=4, Slow=8,
 *                 Ultra(JS8 60)=16. Use the OR of every mode whose slot
 *                 just ended.
 *   kposX/kszX  — for each enabled mode, the start sample and length
 *                 of that mode's slot within the staged audio. kszX
 *                 must be ≤ Mode::NMAX (slot duration × 12 000); the
 *                 decoder asserts otherwise.
 *
 * Modes with their bit clear in nsubmodes are skipped entirely; their
 * kpos/ksz arguments may be 0.
 *
 * Returns the number of decoded messages added to the queue. */
int js8_decoder_run_modes(js8_decoder* dec, int nsubmodes,
                          int kposA, int kszA,
                          int kposB, int kszB,
                          int kposC, int kszC,
                          int kposE, int kszE,
                          int kposI, int kszI);

/* Pop one decoded-message JSON record from the queue, or return NULL
 * when the queue is empty. Caller must free the returned string with
 * js8_free_string().
 *
 * Format:
 *   {"snr":-12,"dt":0.42,"freq":1500.0,"text":"CQ K1FM EM85",
 *    "type":3,"quality":0.8,"mode":0,"utc":123456}
 */
char* js8_decoder_pop(js8_decoder* dec);

/* Free a string previously returned by js8_decoder_pop. Safe with NULL. */
void js8_free_string(char* s);

/* ---------------- Packer (free-text + compound frames) -------------- */

/* Wraps Varicode::buildMessageFrames — JS8Call's central frame
 * factory. Takes a natural-language line plus the operator's identity
 * and the selected addressee (or empty for non-directed messages), and
 * returns the resulting frame sequence as a JSON array.
 *
 * Each element is {"frame": "<12-char raw>", "type": <FrameType>}.
 * The 12-char raw payload is exactly what js8_encode() expects; the
 * type is the Varicode::FrameType the receiver will see embedded in
 * the bitstream.
 *
 *   mycall       — operator callsign (used for compound/directed framing)
 *   mygrid       — 4- or 6-char locator (may be empty)
 *   selectedCall — current QSO partner (may be empty for CQ/HB/free-text)
 *   text         — UTF-8 input string; JSC handles the English subset
 *   submode      — Varicode::SubmodeType ID (Normal=0..)
 *
 * Returns a malloc'd JSON string (free with js8_free_string), or NULL
 * on allocation failure or empty result.
 */
char* js8_pack(const char* mycall, const char* mygrid,
               const char* selectedCall, const char* text, int submode);

#ifdef __cplusplus
}
#endif
