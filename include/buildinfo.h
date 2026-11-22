#ifndef BUILDINFO_H
#define BUILDINFO_H

// What this build calls itself: WFWEB_VERSION for a release ("0.9.5"), with
// a build id after it for a development build ("0.9.5+dev.e49e8c76" from a
// local tree, "0.9.5+dev.20261115.e49e8c76" from CI). Rules in
// tools/build-id.sh.
//
// Use this for anything a person reads (the page footer, --version, the
// console page, the reporters). WFWEB_VERSION stays the bare release number,
// for what must not change with every commit (the /models ETag) or must
// parse as a version (the WSJT-X heartbeat).
const char *wfwebVersion();

// Short hash of the commit this was built from, "" when built without git.
const char *wfwebGitShort();

#endif // BUILDINFO_H
