Prepare a new release version.

Steps:
1. Check existing tags: `git tag -l | sort -V` to determine the next version number
2. Review commits since last tag: `git log $(git describe --tags --abbrev=0)..HEAD --oneline`
3. Update the CHANGELOG file:
   - Add a new section at the top with the version number and today's date
   - Categorize commits into user-facing sections (Features, Fixes, etc.)
   - Do NOT include internal/CI-only changes unless significant
4. Update the version in `include/wfweb_version.h` (look for `#define WFWEB_VERSION "X.Y.Z"`).
   This is the single source of truth — `tools/build-static.sh` and the GitHub
   Actions workflows grep this file for the version. Do NOT add a `WFWEB_VERSION`
   DEFINE to `wfweb.pro`: that path silently produced stale binaries in v0.7.1
   because Make doesn't track `-D` flag changes (see commit history).
5. Build to verify: `qmake wfweb.pro && make -j$(nproc)`. Then run
   `./wfweb --version` and confirm every reported version is the new one.
   Optional belt-and-suspenders: `make clean && qmake && make` — only needed
   if you suspect stale objects from before this header existed.
6. Show the user the CHANGELOG diff and version change for review before committing

CRITICAL: Always update CHANGELOG BEFORE bumping the version. Never skip the CHANGELOG.
