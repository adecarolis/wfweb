/*
 *  wfweb_dw_server_shim.c
 *
 *  Implements the server_*() entry points that ax25_link.c expects.
 *  Forwards each call to a C callback registered by the host application
 *  (AX25LinkProcessor in M2).  Until a callback is registered, all hooks
 *  are no-ops and inbound connections are rejected.
 *
 *  Callback signatures intentionally use plain C types so the Qt wrapper
 *  can install them from C++ without dragging Qt into the vendored tree.
 */

#include <stddef.h>
#include "wfweb_dw_server_shim.h"

static wfweb_link_event_cb        cb_link_established = NULL;
static wfweb_link_event_cb        cb_link_terminated  = NULL;
static wfweb_rec_data_cb          cb_rec_conn_data    = NULL;
static wfweb_outstanding_cb       cb_outstanding      = NULL;
static wfweb_callsign_lookup_cb   cb_callsign_lookup  = NULL;
static wfweb_data_acked_cb        cb_data_acked       = NULL;

void wfweb_dw_register_data_acked_cb(wfweb_data_acked_cb acked)
{
    cb_data_acked = acked;
}

void server_data_acked(int chan, int client,
                       const char *own_call, const char *remote_call,
                       int count)
{
    if (cb_data_acked && count > 0)
        cb_data_acked(chan, client, own_call, remote_call, count);
}

void wfweb_dw_register_server_callbacks(wfweb_link_event_cb established,
                                        wfweb_link_event_cb terminated,
                                        wfweb_rec_data_cb   recdata,
                                        wfweb_outstanding_cb outstanding,
                                        wfweb_callsign_lookup_cb lookup)
{
    cb_link_established = established;
    cb_link_terminated  = terminated;
    cb_rec_conn_data    = recdata;
    cb_outstanding      = outstanding;
    cb_callsign_lookup  = lookup;
}

void server_link_established (int chan, int client, char *remote_call,
                              char *own_call, int incoming)
{
    if (cb_link_established)
        cb_link_established(chan, client, remote_call, own_call, incoming);
}

void server_link_terminated  (int chan, int client, char *remote_call,
                              char *own_call, int timeout)
{
    if (cb_link_terminated)
        cb_link_terminated(chan, client, remote_call, own_call, timeout);
}

void server_rec_conn_data    (int chan, int client, char *remote_call,
                              char *own_call, int pid,
                              char *data_ptr, int data_len)
{
    if (cb_rec_conn_data)
        cb_rec_conn_data(chan, client, remote_call, own_call, pid,
                         data_ptr, data_len);
}

void server_outstanding_frames_reply (int chan, int client, char *own_call,
                                      char *remote_call, int count)
{
    if (cb_outstanding)
        cb_outstanding(chan, client, own_call, remote_call, count);
}

int server_callsign_registered_by_client (char *callsign)
{
    if (cb_callsign_lookup)
        return cb_callsign_lookup(callsign);
    return -1;      /* No client owns this callsign — reject incoming. */
}
