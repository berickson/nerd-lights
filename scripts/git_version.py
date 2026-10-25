# Embeds the git commit (and branch) in the firmware as FIRMWARE_COMMIT, so a
# binary - and the controller running it - says what source it was built from.
# __DATE__ alone could not: on 2026-10-23 six-month-old code compiled that day
# looked newer than the build it replaced.
#
# Runs as a post: script and appends to projenv, which covers only src/. The
# framework and libraries don't see the define, so a new commit recompiles
# main.cpp instead of everything.

import subprocess

Import("env", "projenv")


def git(*args):
    try:
        return subprocess.check_output(
            ["git"] + list(args),
            cwd=env.subst("$PROJECT_DIR"),
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


commit = git("describe", "--always", "--dirty", "--abbrev=7") or "unknown"
branch = git("rev-parse", "--abbrev-ref", "HEAD")
if branch and branch != "HEAD":
    commit += " " + branch

print("FIRMWARE_COMMIT: " + commit)
projenv.Append(CPPDEFINES=[("FIRMWARE_COMMIT", env.StringifyMacro(commit))])
