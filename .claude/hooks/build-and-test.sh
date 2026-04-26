#!/usr/bin/env bash
#
# wfweb hooks A + B: PostToolUse on Edit|Write|MultiEdit.
#   A) Incremental `make` for changes under src/, include/, resources/direwolf/,
#      *.pro, or the top-level wfweb_*.{c,h} stubs.
#   B) After successful build, run `./wfweb --packet-self-test` if the change
#      touched a Direwolf-related file (CLAUDE.md: "Compile-clean is not enough").
#
# Exits 0 silently for irrelevant edits and successful builds. Exits 2 with a
# stderr explanation on failure so PostToolUse feeds the error back to Claude.

set -uo pipefail

input="$(cat)"
file_path="$(printf '%s' "$input" | jq -r '.tool_input.file_path // .tool_response.filePath // empty')"
[[ -z "$file_path" ]] && exit 0

project_dir="${CLAUDE_PROJECT_DIR:-/home/alain/Devel/wfweb}"
case "$file_path" in
  /*) abs="$file_path" ;;
  *)  abs="$project_dir/$file_path" ;;
esac

[[ "$abs" != "$project_dir"/* ]] && exit 0
rel="${abs#"$project_dir"/}"

build_relevant=0
[[ "$rel" =~ ^src/.*\.(cpp|h)$ ]] && build_relevant=1
[[ "$rel" =~ ^include/.*\.h$ ]] && build_relevant=1
[[ "$rel" =~ ^resources/direwolf/.*\.(c|h)$ ]] && build_relevant=1
[[ "$rel" =~ ^[^/]+\.pro$ ]] && build_relevant=1
[[ "$rel" =~ ^wfweb_(direwolf_stubs|dw_server_shim|tq)\.(c|h)$ ]] && build_relevant=1

[[ $build_relevant -eq 0 ]] && exit 0

cd "$project_dir" || exit 0
[[ ! -f Makefile ]] && exit 0

if ! make_output="$(make -j"$(nproc)" 2>&1)"; then
  printf 'Build failed after editing %s:\n\n%s\n' "$rel" "$make_output" >&2
  exit 2
fi

# B: run packet self-test if the change touched a Direwolf-related path.
self_test_relevant=0
[[ "$rel" == *direwolfprocessor* ]] && self_test_relevant=1
[[ "$rel" == resources/direwolf/* ]] && self_test_relevant=1
[[ "$rel" == wfweb_direwolf_stubs.c ]] && self_test_relevant=1

if [[ $self_test_relevant -eq 1 && -x ./wfweb ]]; then
  if ! test_output="$(./wfweb --packet-self-test 2>&1)"; then
    printf 'Packet self-test failed after editing %s:\n\n%s\n' "$rel" "$test_output" >&2
    exit 2
  fi
fi

exit 0
