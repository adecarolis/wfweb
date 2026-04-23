/*
 *  config.h  (wfweb trimmed version)
 *
 *  Upstream Dire Wolf's config.h pulls in digipeater, igate, aprs_tt,
 *  cdigipeater — none of which wfweb vendors.  ax25_link.c only needs the
 *  AX.25 connected-mode subset of struct misc_config_s, so this trimmed
 *  header carries just those fields plus the limit constants ax25_link.c
 *  actually reads.
 *
 *  If a future import requires more of the upstream config.h, extend this
 *  file rather than vendoring the full upstream version.
 */

#ifndef CONFIG_H
#define CONFIG_H 1

#include "audio.h"      /* for struct audio_s, MAX_RADIO_CHANS, MEDIUM_* */

struct misc_config_s {

    /* AX.25 connected mode parameters consumed by ax25_link.c. */

    int frack;              /* T1 / FRACK seconds before retry. */
    int retry;              /* N2 retry count. */
    int paclen;             /* N1, max info part bytes. */
    int maxframe_basic;     /* k window size, mod 8. */
    int maxframe_extended;  /* k window size, mod 128. */
    int maxv22;             /* SABME tries before falling back to SABM. */

    char **v20_addrs;       /* Stations known to be v2.0-only. */
    int v20_count;

    char **noxid_addrs;     /* Stations known not to support XID. */
    int noxid_count;
};

#endif /* CONFIG_H */
