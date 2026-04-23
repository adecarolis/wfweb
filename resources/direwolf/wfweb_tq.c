/*
 *  wfweb_tq.c
 *
 *  Minimal replacement for upstream Dire Wolf tq.c.  ax25_link.c only
 *  calls lm_data_request() and lm_seize_request() from the TX queue API;
 *  the rest of tq.h (tq_append / tq_remove / tq_count / tq_peek /
 *  tq_wait_while_empty) is exposed for header completeness but is no-op
 *  in wfweb because the existing DireWolfProcessor TX path already owns
 *  audio scheduling.
 *
 *  lm_data_request() forwards the constructed packet_t to a host-side
 *  callback (registered by AX25LinkProcessor in M2) which feeds the
 *  existing DireWolfProcessor::transmitPacket pipeline.
 *
 *  lm_seize_request() asks the channel to be seized; once PTT is on and
 *  the modem is idle, the host calls dlq_seize_confirm(chan) to let
 *  ax25_link drain its pending frames.  For M1 (compile-only) we stub
 *  lm_seize_request to call dlq_seize_confirm immediately so an internal
 *  loopback works without a real PTT path.
 */

#include <stddef.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "tq.h"
#include "dlq.h"
#include "wfweb_tq.h"

static wfweb_tq_data_cb  cb_data  = NULL;
static wfweb_tq_seize_cb cb_seize = NULL;

void wfweb_dw_register_tq_callbacks(wfweb_tq_data_cb data, wfweb_tq_seize_cb seize)
{
    cb_data  = data;
    cb_seize = seize;
}

void tq_init (struct audio_s *audio_config_p)
{
    (void)audio_config_p;
}

void tq_append (int chan, int prio, packet_t pp)
{
    /* APRS UI frames in wfweb already go through DireWolfProcessor::transmitFrame.
     * If anything in ax25_link or its helpers ever reaches here, route through
     * the same callback so the frame still gets sent. */
    if (cb_data) {
        cb_data(chan, prio, pp);
    } else {
        ax25_delete(pp);
    }
}

void lm_data_request (int chan, int prio, packet_t pp)
{
    if (cb_data) {
        cb_data(chan, prio, pp);
    } else {
        ax25_delete(pp);
    }
}

void lm_seize_request (int chan)
{
    if (cb_seize) {
        cb_seize(chan);
    } else {
        /* No host wired up — fake an immediate confirm so internal
         * state machines do not stall during build/link verification. */
        dlq_seize_confirm(chan);
    }
}

void tq_wait_while_empty (int chan)
{
    (void)chan;
}

packet_t tq_remove (int chan, int prio)
{
    (void)chan; (void)prio;
    return NULL;
}

packet_t tq_peek (int chan, int prio)
{
    (void)chan; (void)prio;
    return NULL;
}

int tq_count (int chan, int prio, char *source, char *dest, int bytes)
{
    (void)chan; (void)prio; (void)source; (void)dest; (void)bytes;
    return 0;
}
