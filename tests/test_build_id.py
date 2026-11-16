"""tools/build-id.sh: what tells a development build from a release.

The script is copied with a version header into a scratch tree, so the test
does not depend on where this checkout's HEAD or tags are.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

pytestmark = pytest.mark.skipif(
    os.name == "nt" or not shutil.which("git"), reason="needs a POSIX shell and git"
)


def git(tree, *args):
    subprocess.run(
        ["git", "-C", str(tree), "-c", "user.name=t", "-c", "user.email=t@example.invalid",
         "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", *args],
        check=True, capture_output=True,
    )


def run(tree, *args, build_id=None):
    env = {k: v for k, v in os.environ.items() if k != "WFWEB_BUILD_ID"}
    if build_id is not None:
        env["WFWEB_BUILD_ID"] = build_id
    out = subprocess.run(
        [str(tree / "tools" / "build-id.sh"), *args],
        check=True, capture_output=True, text=True, env=env,
    )
    return out.stdout.rstrip("\n")


@pytest.fixture
def tree(tmp_path):
    t = tmp_path / "src"
    (t / "tools").mkdir(parents=True)
    (t / "include").mkdir()
    shutil.copy(ROOT / "tools" / "build-id.sh", t / "tools" / "build-id.sh")
    (t / "include" / "wfweb_version.h").write_text('#define WFWEB_VERSION "1.2.3"\n')
    return t


def commit(tree, msg="c"):
    git(tree, "add", "-A")
    git(tree, "commit", "-q", "--allow-empty", "-m", msg)
    return subprocess.run(
        ["git", "-C", str(tree), "rev-parse", "--short", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


def test_no_git_is_the_bare_version(tree):
    assert run(tree) == "1.2.3"
    assert run(tree, "--id") == ""


def test_untagged_commit_is_a_dev_build(tree):
    git(tree, "init", "-q")
    sha = commit(tree)
    assert run(tree) == f"1.2.3+dev.{sha}"
    assert run(tree, "--id") == f"dev.{sha}"


def test_the_tagged_commit_is_the_release_and_the_next_one_is_not(tree):
    git(tree, "init", "-q")
    commit(tree)
    git(tree, "tag", "v1.2.3")
    assert run(tree) == "1.2.3"

    sha = commit(tree, "after the release")
    assert run(tree) == f"1.2.3+dev.{sha}"


def test_another_tag_on_the_commit_does_not_make_it_a_release(tree):
    git(tree, "init", "-q")
    sha = commit(tree)
    git(tree, "tag", "v1.2.2")
    git(tree, "tag", "dev-dev")
    assert run(tree) == f"1.2.3+dev.{sha}"


def test_environment_overrides(tree):
    git(tree, "init", "-q")
    sha = commit(tree)
    assert run(tree, build_id="release") == "1.2.3"
    assert run(tree, build_id="dev.20261115.abcdef12") == "1.2.3+dev.20261115.abcdef12"
    assert run(tree, build_id="") == f"1.2.3+dev.{sha}", "empty means not set"


def test_a_tarball_inside_another_repository_ignores_that_repository(tmp_path, tree):
    git(tmp_path, "init", "-q")
    commit(tmp_path)
    assert run(tree) == "1.2.3"


def test_header_is_left_alone_when_nothing_changed(tree):
    git(tree, "init", "-q")
    sha = commit(tree)
    header = tree / "wfweb_build.h"

    run(tree, "--header", str(header))
    text = header.read_text()
    assert f'#define WFWEB_BUILD_ID "dev.{sha}"' in text
    assert f'#define WFWEB_BUILD_GIT "{sha}"' in text

    os.utime(header, (1_000_000_000, 1_000_000_000))
    run(tree, "--header", str(header))
    assert header.stat().st_mtime == 1_000_000_000, "an unchanged id must not touch the file"

    sha2 = commit(tree, "next")
    run(tree, "--header", str(header))
    assert f'"dev.{sha2}"' in header.read_text()
    assert header.stat().st_mtime != 1_000_000_000

    # Without git the commit for the --version banner comes from the CI id.
    shutil.rmtree(tree / ".git")
    run(tree, "--header", str(header), build_id="dev.20261115.abcdef12")
    assert '#define WFWEB_BUILD_GIT "abcdef12"' in header.read_text()
    run(tree, "--header", str(header), build_id="release")
    assert '#define WFWEB_BUILD_ID ""' in header.read_text()


def test_the_server_reports_one_version_everywhere(rest_url):
    """--version and the version the page footer shows come from one place."""
    import re
    import requests
    from conftest import WFWEB_BIN

    banner = subprocess.run([str(WFWEB_BIN), "--version"], capture_output=True, text=True)
    m = re.search(r"wfweb version: (\S+) \(Git:(\S*) on ", banner.stdout + banner.stderr)
    assert m, banner.stdout + banner.stderr
    shown, commit = m.groups()

    base = re.search(r'#define WFWEB_VERSION "([^"]+)"',
                     (ROOT / "include" / "wfweb_version.h").read_text()).group(1)
    assert shown == base or shown.startswith(base + "+dev."), shown
    if shown != base and commit:
        assert shown.endswith("." + commit), "a dev build is named after its commit"

    # CI builds and tests in one job with WFWEB_BUILD_ID set (build.yml): on a
    # v* tag this is what fails the release if its binary is not the plain
    # version, and on a dev build if it does not carry the package's id.
    given = os.environ.get("WFWEB_BUILD_ID", "")
    if given == "release":
        assert shown == base, "a release build shows the bare version"
    elif given:
        assert shown == f"{base}+{given}"

    info = requests.get(f"{rest_url}/info", timeout=5).json()
    info = info.get("info", info)
    assert info["version"] == shown
