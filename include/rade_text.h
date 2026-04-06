/*---------------------------------------------------------------------------*\

  rade_text.h

  Encode/decode callsign text in RADE EOO (End-of-Over) bits.

  Uses LDPC(112,56) FEC, CRC-8 validation, 6-bit character set.
  Wire-compatible with freedv-gui's rade_text implementation.

  Self-contained — no codec2 dependency.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2024 Mooneer Salem (original freedv-gui implementation)
  Copyright (C) 2026 Alain De Carolis (self-contained port for radae_nopy)

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef RADE_TEXT_H
#define RADE_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

    /* Opaque handle. */
    typedef void *rade_text_t;

    /* Callback fired when a valid callsign has been decoded from EOO bits. */
    typedef void (*on_text_rx_t)(rade_text_t rt, const char *txt_ptr,
                                 int length, void *state);

    /* Create rade_text object. */
    rade_text_t rade_text_create(void);

    /* Destroy rade_text object. */
    void rade_text_destroy(rade_text_t ptr);

    /* Encode callsign into float symbols for rade_tx_set_eoo_bits().
       str:       callsign string (max 8 chars, A-Z 0-9 and a few punctuation)
       strlength: length of str
       syms:      output float array (I,Q pairs — size must be >= symSize)
       symSize:   total number of floats available in the EOO (from rade_n_eoo_bits) */
    void rade_text_generate_tx_string(rade_text_t ptr, const char *str,
                                      int strlength, float *syms, int symSize);

    /* Set RX callback. */
    void rade_text_set_rx_callback(rade_text_t ptr, on_text_rx_t text_rx_fn,
                                   void *state);

    /* Decode received soft symbols from rade_rx() eoo_out[].
       syms:    float array of soft-decision symbols (I,Q pairs)
       symSize: number of floats (same as rade_n_eoo_bits) */
    void rade_text_rx(rade_text_t ptr, float *syms, int symSize);

#ifdef __cplusplus
}
#endif

#endif /* RADE_TEXT_H */
