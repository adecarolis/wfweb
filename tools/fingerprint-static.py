#!/usr/bin/env python3
"""Append ?v=<version> to local asset URLs in a built dist/ tree.

Forces browser cache invalidation when the bundle is redeployed without
relying on hosting-side cache headers (GitHub Pages serves everything with
Cache-Control: max-age=600). Rewrites in place across all .html / .js /
.mjs files under the given directory.

Patterns covered:
  - <script src="X">, <link href="X">, <img src="X">
  - ES static imports: from "X" / from 'X'
  - Dynamic imports:   import("X") / import('X')
  - Emscripten URL loaders: new URL("X", import.meta.url)
  - Emscripten literal:  wasmBinaryFile="X.wasm"

A URL is "local" when it doesn't start with a scheme (http:, https:, data:,
blob:, mailto:) or with `//`. Already-versioned URLs (those containing `?`)
are skipped, so the script is idempotent.

Usage: tools/fingerprint-static.py <dist-dir> <version>
"""

import os
import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: fingerprint-static.py <dist-dir> <version>", file=sys.stderr)
        return 2
    dist, version = sys.argv[1], sys.argv[2]
    if not os.path.isdir(dist):
        print(f"error: {dist} is not a directory", file=sys.stderr)
        return 2

    # A local URL: no scheme, no `//` prefix, ends in one of our asset
    # extensions, and doesn't already contain a query string.
    local_url = (
        r"(?![a-zA-Z][a-zA-Z0-9+.\-]*:|//)"   # not a scheme, not protocol-relative
        r"[^'\"\s?#)<>]+"                      # path body, no `?` (skip already-versioned)
        r"\.(?:js|mjs|wasm|css|onnx|png|html)" # known asset extensions
    )

    suffix = f"?v={version}"

    patterns = [
        # <tag src="X"> / <tag href="X">
        (re.compile(rf'(\b(?:src|href)=)(["\'])({local_url})\2'),
         lambda m: f'{m.group(1)}{m.group(2)}{m.group(3)}{suffix}{m.group(2)}'),
        # from "X" / from 'X'  (ES static imports)
        (re.compile(rf'(\bfrom\s+)(["\'])({local_url})\2'),
         lambda m: f'{m.group(1)}{m.group(2)}{m.group(3)}{suffix}{m.group(2)}'),
        # import("X") / import('X')  (dynamic imports)
        (re.compile(rf'(\bimport\(\s*)(["\'])({local_url})\2'),
         lambda m: f'{m.group(1)}{m.group(2)}{m.group(3)}{suffix}{m.group(2)}'),
        # new URL("X", import.meta.url)
        (re.compile(rf'(\bnew\s+URL\(\s*)(["\'])({local_url})\2(\s*,\s*import\.meta\.url)'),
         lambda m: f'{m.group(1)}{m.group(2)}{m.group(3)}{suffix}{m.group(2)}{m.group(4)}'),
        # wasmBinaryFile="X.wasm"  (emscripten loader literal)
        (re.compile(rf'(wasmBinaryFile\s*=\s*)(["\'])({local_url})\2'),
         lambda m: f'{m.group(1)}{m.group(2)}{m.group(3)}{suffix}{m.group(2)}'),
    ]

    # Marker substitution: HTML files can opt into a runtime version global
    # by including <!--WFWEB_VERSION_SLOT-->. Worker bootstraps and other
    # runtime-constructed URLs read window.__WFWEB_V__ / self.__WFWEB_V__ and
    # append `?v=…` themselves — the regex above can't see through Blob
    # strings or `new Worker(literal)` calls.
    version_slot = "<!--WFWEB_VERSION_SLOT-->"
    version_script = f'<script>window.__WFWEB_V__="{version}";</script>'

    exts = {".html", ".js", ".mjs"}
    changed_files = 0
    total_subs = 0

    for root, _dirs, files in os.walk(dist):
        for name in files:
            if os.path.splitext(name)[1] not in exts:
                continue
            path = os.path.join(root, name)
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            new_text = text
            file_subs = 0
            for pat, repl in patterns:
                new_text, n = pat.subn(repl, new_text)
                file_subs += n
            if version_slot in new_text:
                new_text = new_text.replace(version_slot, version_script)
                file_subs += 1
            if new_text != text:
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(new_text)
                changed_files += 1
                total_subs += file_subs

    print(f"fingerprinted v={version}: {total_subs} ref(s) across {changed_files} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
