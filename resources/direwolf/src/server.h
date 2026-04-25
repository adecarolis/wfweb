/*
 *  server.h  (wfweb shim)
 *
 *  Upstream server.h declares the AGW TCP server API.  wfweb does not
 *  vendor server.c; instead the link-layer event hooks called by
 *  ax25_link.c are implemented in resources/direwolf/wfweb_dw_server_shim.c
 *  and forwarded into a Qt-side callback registered by the application.
 */

#ifndef SERVER_H
#define SERVER_H 1

#include "ax25_pad.h"
#include "config.h"

void server_link_established (int chan, int client, char *remote_call, char *own_call, int incoming);

void server_link_terminated  (int chan, int client, char *remote_call, char *own_call, int timeout);

void server_rec_conn_data    (int chan, int client, char *remote_call, char *own_call, int pid, char *data_ptr, int data_len);

void server_outstanding_frames_reply (int chan, int client, char *own_call, char *remote_call, int count);

int  server_callsign_registered_by_client (char *callsign);

/* wfweb extension: notified by ax25_link.c when N(R) advances and
   one or more outbound I-frames are now acknowledged. */
void server_data_acked (int chan, int client, const char *own_call,
                        const char *remote_call, int count);

#endif /* SERVER_H */
