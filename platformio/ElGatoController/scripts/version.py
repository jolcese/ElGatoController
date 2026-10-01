#
# PlatformIO pre-build script: inject the current short git commit hash
# into the firmware as the GIT_REV macro, so the running firmware can
# report exactly which commit it was built from.
#
import subprocess

Import("env")  # noqa: F821  (provided by the PlatformIO build system)


def git_short_rev():
    try:
        rev = (
            subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
            .strip()
            .decode()
        )
        # Mark the build as dirty if there are uncommitted changes.
        dirty = subprocess.call(
            ["git", "diff", "--quiet", "--ignore-submodules", "HEAD"]
        )
        if dirty != 0:
            rev += "-dirty"
        return rev
    except Exception:
        return "unknown"


rev = git_short_rev()
print("version.py - building from git rev: %s" % rev)
env.Append(CPPDEFINES=[("GIT_REV", env.StringifyMacro(rev))])  # noqa: F821
