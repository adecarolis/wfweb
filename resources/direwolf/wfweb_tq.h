/*
 *  wfweb_tq.h
 *
 *  Host-side callback interface for the wfweb tq shim.  AX25LinkProcessor
 *  registers these to receive frames produced by ax25_link's data-link
 *  state machine and to gate channel seize on PTT.
 */

#ifndef WFWEB_TQ_H
#define WFWEB_TQ_H 1

#include "ax25_pad.h"   /* for packet_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wfweb_tq_data_cb)(int chan, int prio, packet_t pp);
typedef void (*wfweb_tq_seize_cb)(int chan);

void wfweb_dw_register_tq_callbacks(wfweb_tq_data_cb data, wfweb_tq_seize_cb seize);

#ifdef __cplusplus
}
#endif

#endif /* WFWEB_TQ_H */
