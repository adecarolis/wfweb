/*
 *  wfweb_dw_server_shim.h
 *
 *  C interface used by the host application (AX25LinkProcessor) to plug
 *  Qt-signal trampolines into the server_*() hooks ax25_link.c calls.
 */

#ifndef WFWEB_DW_SERVER_SHIM_H
#define WFWEB_DW_SERVER_SHIM_H 1

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wfweb_link_event_cb)(int chan, int client,
                                    const char *remote_call,
                                    const char *own_call,
                                    int param);

typedef void (*wfweb_rec_data_cb)(int chan, int client,
                                  const char *remote_call,
                                  const char *own_call,
                                  int pid,
                                  const char *data, int len);

typedef void (*wfweb_outstanding_cb)(int chan, int client,
                                     const char *own_call,
                                     const char *remote_call,
                                     int count);

typedef int  (*wfweb_callsign_lookup_cb)(const char *callsign);

void wfweb_dw_register_server_callbacks(wfweb_link_event_cb established,
                                        wfweb_link_event_cb terminated,
                                        wfweb_rec_data_cb   recdata,
                                        wfweb_outstanding_cb outstanding,
                                        wfweb_callsign_lookup_cb lookup);

#ifdef __cplusplus
}
#endif

#endif /* WFWEB_DW_SERVER_SHIM_H */
