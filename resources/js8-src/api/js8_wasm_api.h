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

#ifdef __cplusplus
}
#endif
