/*
 * MSVC shim — empty <unistd.h>.
 *
 * Several vendored Direwolf .c files (demod.c, demod_afsk.c, demod_9600.c,
 * dsp.c, dlq.c, gen_tone.c, multi_modem.c, xid.c) include <unistd.h>
 * unconditionally even though the modem subset wfweb vendors never calls
 * any of the POSIX functions it would declare.  MSVC has no native
 * <unistd.h>; this empty stub lets those translation units parse so the
 * rest of the file compiles via direwolf.h's __WIN32__ branch.
 */
#ifndef WFWEB_MSVC_SHIM_UNISTD_H
#define WFWEB_MSVC_SHIM_UNISTD_H
#endif
