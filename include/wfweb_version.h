#ifndef WFWEB_VERSION_H
#define WFWEB_VERSION_H

// Single source of truth for the wfweb version.
//
// Defined here as a header (not as a -D flag in wfweb.pro) so that Make's
// header-dependency tracking forces a rebuild of every file that embeds the
// version string when this file changes. With -D, qmake's Makefile only
// reflects the new flag in compile commands, but Make does not rerun those
// commands unless the .cpp/.h mtimes change — which leaves stale .o files
// with the old version baked in. See v0.7.1, where freedvreporter.o kept
// reporting "0.7.0" after a version bump that touched only wfweb.pro.
//
// tools/build-static.sh and .github/workflows/build.yml grep this file for
// the version, so keep the line format stable.

#define WFWEB_VERSION "0.8.0"

#endif // WFWEB_VERSION_H
