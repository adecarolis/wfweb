#!/usr/bin/env bash
#
# wfweb hook E: PreToolUse on Bash. Block `git commit` if build is stale AND
# the commit includes build-relevant files. Doc-only commits proceed even on
# a stale tree. Uses `make -q`: 0 = up-to-date, 1 = needs rebuild, 2 = error.

set -uo pipefail

input="$(cat)"
cmd="$(printf '%s' "$input" | jq -r '.tool_input.command // empty')"

case "$cmd" in
  "git commit"|"git commit "*) ;;
  *) exit 0 ;;
esac

project_dir="${CLAUDE_PROJECT_DIR:-/home/alain/Devel/wfweb}"
cd "$project_dir" || exit 0
[[ ! -f Makefile ]] && exit 0

# What's being committed: staged files, plus working-tree changes if -a/--all.
files="$(git diff --cached --name-only 2>/dev/null)"
if [[ "$cmd" =~ (^|[[:space:]])(-a|--all)([[:space:]]|$) ]]; then
  files="$files
$(git diff --name-only 2>/dev/null)"
fi

relevant=0
while IFS= read -r f; do
  [[ -z "$f" ]] && continue
  case "$f" in
    src/*.cpp|src/*.h|include/*.h) relevant=1 ;;
    resources/direwolf/*.c|resources/direwolf/*.h) relevant=1 ;;
    resources/direwolf/*/*.c|resources/direwolf/*/*.h) relevant=1 ;;
    *.pro) relevant=1 ;;
    wfweb_direwolf_stubs.c|wfweb_dw_server_shim.*|wfweb_tq.*) relevant=1 ;;
  esac
  [[ $relevant -eq 1 ]] && break
done <<< "$files"

[[ $relevant -eq 0 ]] && exit 0

make -q >/dev/null 2>&1
status=$?

if [[ $status -eq 1 ]]; then
  jq -n '{
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "deny",
      permissionDecisionReason: "Build is stale: source files changed since the last successful make. Run `make -j$(nproc)` to rebuild before committing source changes."
    }
  }'
fi
exit 0
