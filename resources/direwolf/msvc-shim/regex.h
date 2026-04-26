/*
 * MSVC shim — empty <regex.h>.
 *
 * ax25_pad.c includes POSIX <regex.h> but never references any regex API;
 * it's a leftover from upstream Direwolf code paths we don't compile.
 * MSVC has no native <regex.h> (only the C++ <regex>), so provide an
 * empty stub here.
 */
#ifndef WFWEB_MSVC_SHIM_REGEX_H
#define WFWEB_MSVC_SHIM_REGEX_H
#endif
