Prepare a new release version.

Steps:
1. Check existing tags: `git tag -l | sort -V` to determine the next version number
2. Review commits since last tag: `git log $(git describe --tags --abbrev=0)..HEAD --oneline`
3. Update the CHANGELOG file:
   - Add a new section at the top with the version number and today's date
   - Categorize commits into user-facing sections (Features, Fixes, etc.)
   - Do NOT include internal/CI-only changes unless significant
4. Update the version in `wfweb.pro` (look for `DEFINES += WFWEB_VERSION=\"X.Y.Z\"` on line ~18)
5. Build to verify: `qmake wfweb.pro && make -j$(nproc)`
6. Show the user the CHANGELOG diff and version change for review before committing

CRITICAL: Always update CHANGELOG BEFORE bumping the version. Never skip the CHANGELOG.
