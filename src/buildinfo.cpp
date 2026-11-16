#include "buildinfo.h"
#include "wfweb_version.h"
#include "wfweb_build.h"   // generated in the build directory, see buildid.pri

// The only file that includes wfweb_build.h: a new commit recompiles this
// and nothing else.

const char *wfwebVersion()
{
    return sizeof(WFWEB_BUILD_ID) > 1 ? WFWEB_VERSION "+" WFWEB_BUILD_ID
                                      : WFWEB_VERSION;
}

const char *wfwebGitShort()
{
    return WFWEB_BUILD_GIT;
}
