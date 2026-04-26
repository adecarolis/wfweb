#!/usr/bin/env bash
#
# wfweb hook C: PostToolUse on Edit|MultiEdit. If wfweb.pro's VERSION line
# changed, remind to update CHANGELOG before committing (per /release docs).

set -uo pipefail

input="$(cat)"
file_path="$(printf '%s' "$input" | jq -r '.tool_input.file_path // .tool_response.filePath // empty')"
[[ -z "$file_path" ]] && exit 0

project_dir="${CLAUDE_PROJECT_DIR:-/home/alain/Devel/wfweb}"
case "$file_path" in
  /*) abs="$file_path" ;;
  *)  abs="$project_dir/$file_path" ;;
esac

[[ "$abs" != "$project_dir/wfweb.pro" ]] && exit 0

cd "$project_dir" || exit 0

# Look at staged + unstaged diff for WFWEB_VERSION (or plain VERSION) touches.
# wfweb.pro:18 reads `DEFINES += WFWEB_VERSION=\"X.Y.Z\"`; the qmake-idiomatic
# `VERSION = X.Y.Z` form is allowed too in case the project ever switches.
if (git diff -- wfweb.pro 2>/dev/null; git diff --cached -- wfweb.pro 2>/dev/null) \
    | grep -qE '^[+-].*(WFWEB_VERSION=|^[+-][[:space:]]*VERSION[[:space:]]*=)'; then
  jq -n '{
    systemMessage: "wfweb.pro VERSION changed — update CHANGELOG before committing.",
    hookSpecificOutput: {
      hookEventName: "PostToolUse",
      additionalContext: "Reminder: wfweb.pro VERSION was modified. Per the release process in CLAUDE.md, add a new dated section to CHANGELOG (categorized commits since the last tag) BEFORE committing the version bump."
    }
  }'
fi
exit 0
